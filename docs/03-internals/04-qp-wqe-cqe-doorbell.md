# 3.4 队列与门铃

QP 和 CQ 是 Verbs 编程模型中最常见的对象。到设备执行层，它们不再只是 API 句柄，而是一组由用户态、内核和网卡共同维护的队列状态。应用把 WR 投递到 QP；mlx5 provider 把 WR 写成 WQE；doorbell 通知设备读取新 WQE；设备完成后向 CQ 写入 CQE；provider 再把 CQE 转换为应用看到的 WC。

QP 创建参数中的队列深度和 SGE 上限，会在设备侧变成真实的资源约束。创建完成以后，provider 不能任意扩大发送队列，也不能让一条 WQE 引用超过上限的 SGE。队列容量、WQE 格式和 completion 策略因此直接影响程序能投递多快、能积压多少未完成请求，以及 CQ 是否会被完成记录压满。

## 3.4.1 QP 的队列形态

一个 RC QP 至少包含发送方向和接收方向两组队列状态。Send Queue 保存发送 WQE，Receive Queue 保存接收 WQE。对于 RDMA WRITE 和 RDMA READ，发送方主要使用 SQ；对于 SEND/RECV，接收方必须提前在 RQ 上投递 Receive WQE，否则发送来的 SEND 无法匹配。

```text
QP

  Send Queue
    WQE #0
    WQE #1
    WQE #2
    ...

  Receive Queue
    Recv WQE #0
    Recv WQE #1
    ...

  Doorbell Record
    SQ producer index
    RQ producer index

  Device QP Context
    QPN, state, PSN, path, retry, queue pointers, UAR relation
```

SQ、RQ 和 doorbell record 通常位于主机内存，并通过 mmap 暴露给用户态 provider。设备可以通过 DMA 读取 WQE 和 doorbell record；软件可以在用户态写入新 WQE 并推进生产者位置。QP context 则是设备侧状态，创建和状态转换时由内核驱动配置。

这种布局解释了 QP 的两个边界。第一，投递 WR 只是写入用户态映射的队列，并通知设备；它不是同步传输。第二，队列容量是真实资源。SQ 被填满而 completion 没有及时回收时，后续 `ibv_post_send` 会失败；RQ 没有足够 Receive WQE 时，SEND/RECV 语义会出现 RNR 或错误。

## 3.4.2 WQE 的段结构

mlx5 WQE 由若干个 16 字节 segment 组成。不同 opcode 使用不同的 segment 组合。RDMA WRITE 的核心结构可以简化为三部分：控制段说明这是一条什么请求，远端地址段说明写到哪里，数据段说明从本地哪里取数据。

```text
RDMA WRITE WQE

+-------------------------------+
| Control Segment               |
| opcode, WQE index, QPN, flags |
+-------------------------------+
| Remote Address Segment        |
| remote address, rkey          |
+-------------------------------+
| Data Segment 0                |
| local address, lkey, length   |
+-------------------------------+
| Data Segment 1 ...            |
+-------------------------------+
```

mlx5 provider 中的定义可按普通 RDMA WRITE 需要整理为如下简化形式。实际结构还包含保留字段、字节序要求和其他 opcode 使用的扩展段。

```c
struct mlx5_wqe_ctrl_seg {
    uint32_t opmod_idx_opcode;  /* opmod | WQE index | opcode */
    uint32_t qpn_ds;            /* QP number | descriptor size */
    uint8_t  signature;
    uint16_t dci_stream_channel_id;
    uint8_t  fm_ce_se;          /* fence, completion, solicited event */
    uint32_t imm;
};

struct mlx5_wqe_raddr_seg {
    uint64_t raddr;
    uint32_t rkey;
    uint32_t reserved;
};

struct mlx5_wqe_data_seg {
    uint32_t byte_count;
    uint32_t lkey;
    uint64_t addr;
};
```

在 mlx5 的发送路径中，`IBV_WR_RDMA_WRITE` 会填入远端地址段和数据段。`remote_addr` 与 `rkey` 进入 `mlx5_wqe_raddr_seg`；每个有效 SGE 进入一个 `mlx5_wqe_data_seg`，其中包含本地虚拟地址、`lkey` 和长度。控制段最后发布，包含 opcode、QP number、WQE index、WQE 大小和 completion 标志。控制段最后写入这一点很重要：它减少设备看到半成品 WQE 的机会。

WQE 中的地址仍以 Verbs 语义中的虚拟地址出现。设备并不是直接相信这个地址，而是用 `lkey` 或 `rkey` 查找设备侧 MKey，再完成权限校验和地址转换。地址与 key 分离，是 RDMA 能够让远端访问内存而不失去保护边界的基础。

## 3.4.3 Doorbell Record、UAR 与 BlueFlame

WQE 写入 SQ 后，设备还不知道有新工作。mlx5 发送路径用两步发布队列状态：先写 doorbell record，再写 UAR/BF register。

doorbell record 是主机内存中的队列进度记录。软件把新的 SQ producer index 写入这里，设备可以通过 DMA 读取它。UAR 是映射到用户态的设备访问区域；BF register 是 UAR 中用于低延迟投递的寄存器窗口。写 UAR/BF register 是 MMIO 写，会在 PCIe 上到达设备，起到“敲门”的作用。

```text
软件投递顺序

1. 填写 Send Queue 中的 WQE
2. 执行内存屏障，使 WQE 内容先对设备可见
3. 写 doorbell record，发布新的 producer index
4. 写 UAR/BF register，通知设备读取队列
```

这个顺序不能随意交换。若设备先看到 doorbell，却还看不到完整 WQE，就可能按错误内容执行请求。provider 因此在 doorbell 之前使用面向设备的内存屏障。屏障保证普通内存中的 WQE 和 doorbell record 先于 MMIO 门铃被设备观察到。

BlueFlame 是 mlx5 的低延迟投递优化。普通 doorbell 主要通知设备去主机内存读取 WQE；BlueFlame 路径可以把 WQE 开头部分随 MMIO 写一并推给设备。小 WQE 因此可能少一次主机内存读取，延迟更低。BlueFlame 依赖 UAR、write-combining 和设备能力，属于 mlx5 实现特征，而不是 Verbs 语义要求。

## 3.4.4 CQE 与 WC

Completion Queue 是设备写回完成结果的队列。应用创建 CQ 后，设备获得写 CQE 的队列状态，provider 获得用户态可读的 CQE ring。发送 WR 若设置 `IBV_SEND_SIGNALED`，完成后会在发送 CQ 中产生 CQE；接收 WQE 匹配到 SEND 后，会在接收 CQ 中产生 CQE。

CQE 是设备格式，WC 是 Verbs 格式。mlx5 CQE 中包含 opcode、owner bit、WQE counter、syndrome、byte count、immediate data 等信息；provider 轮询 CQ 时检查 CQE 是否归软件所有，再把设备字段转换成 `struct ibv_wc`。应用最终看到的是 `wr_id`、`status`、`opcode`、`byte_len`、`imm_data`、`vendor_err` 等字段。

CQE ring 是循环队列，需要区分“这个槽位还属于设备”还是“这个槽位已经有新 completion”。mlx5 使用 owner bit 处理这个问题。设备每写完一圈后，owner bit 的期望值发生翻转；provider 根据 consumer index 和 owner bit 判断某个 CQE 是否有效。

```text
CQ ring

index:       0   1   2   3   0   1   2   3
round:       0   0   0   0   1   1   1   1
owner bit:   A   A   A   A   B   B   B   B
```

CQ 容量也是真实资源。设备写 CQE 的速度超过应用轮询速度时，CQ 可能溢出。CQ overrun 不是单条 WR 的普通失败，而是完成队列无法再可靠记录结果，通常会导致 CQ 或相关 QP 进入错误状态。高吞吐程序必须控制 signaled WR 的比例，或保证 poller 能及时消费 CQE。

## 3.4.5 Selective Signaling

并非每条 Send WR 都必须产生 CQE。`IBV_SEND_SIGNALED` 控制该 WR 完成后是否写发送 CQ。若每条 WR 都产生 CQE，程序容易获得简单的生命周期边界，但 CQ 压力和 PCIe 写回开销会增加。若只对部分 WR 使用 signaled，吞吐通常更好，但应用必须自己保证未 signaling 的 WR 不会无限堆积，并通过后续 signaled WR 回收队列进度。

Selective signaling 的常见方式是每隔若干条 WR 设置一次 `IBV_SEND_SIGNALED`。当这条 signaled WR 成功完成时，同一 QP 上在它之前的发送 WR 按队列顺序已经完成到相应语义边界。这个规则让应用既能降低 CQE 数量，又能周期性取得资源回收点。

这个策略只适用于理解了 QP 顺序和错误语义的程序。若某条未 signaling WR 失败，错误可能在后续 signaled WR 或异步事件中体现；错误发生后，QP 可能进入 ERR 状态。程序不能因为某条 WR 没有请求 CQE，就认为它的失败可以被忽略。

## 3.4.6 小结

QP、WQE、CQE 和 doorbell 把 Verbs API 与设备执行连接起来。QP 提供队列和状态，WQE 是设备读取的请求格式，doorbell record 和 UAR/BF register 发布新工作，CQE 是设备写回的完成记录，provider 把 CQE 转换为应用可见的 WC。

`ibv_post_send` 的返回点位于投递路径，不位于传输完成点。只有设备执行请求并写回 CQE 后，`ibv_poll_cq` 才能取得 completion。WQE 发布顺序、doorbell、CQ 容量和 selective signaling 都直接影响性能与正确性，也是后续可靠传输、RoCE 网络和诊断章节的基础。
