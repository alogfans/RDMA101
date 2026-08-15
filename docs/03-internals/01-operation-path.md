# 3.1 WRITE 路径

RDMA WRITE 的程序入口是一条 Send WR。应用在 WR 中写明操作类型，在 SGE 中写明本地数据，在 `wr.rdma` 中写明远端地址和 `rkey`，然后调用 `ibv_post_send`。从 API 的形式看，这只是一次函数调用；从设备的角度看，这次调用向 QP 的 Send Queue 增加了一条设备可执行的工作项。

RDMA WRITE 的最小投递形式可以写成：

```c
struct ibv_sge sge = {
    .addr = (uintptr_t)local_buffer,
    .length = length,
    .lkey = local_mr->lkey,
};

struct ibv_send_wr wr = {
    .wr_id = WRID_WRITE,
    .sg_list = &sge,
    .num_sge = 1,
    .opcode = IBV_WR_RDMA_WRITE,
    .send_flags = IBV_SEND_SIGNALED,
};

wr.wr.rdma.remote_addr = remote_addr;
wr.wr.rdma.rkey = remote_rkey;

struct ibv_send_wr *bad_wr = NULL;
int ret = ibv_post_send(qp, &wr, &bad_wr);
```

RDMA WRITE 请求由几项基本信息组成。`opcode` 说明操作是 WRITE；`addr`、`length` 和 `lkey` 说明发起方网卡应当从哪段本地内存读取数据；`remote_addr` 和 `rkey` 说明远端网卡应当把数据写到哪里；`IBV_SEND_SIGNALED` 要求设备在完成后向发送 CQ 写入一条 CQE。`ibv_post_send` 的工作不是传输数据，而是把这些信息交给 provider，由 provider 写成网卡能够读取的队列格式。

## 3.1.1 从 Verbs API 到 mlx5 provider

`ibv_post_send` 是 libibverbs 提供的统一入口。公共库不直接规定设备队列的具体格式，而是通过当前 context 中的 provider 操作表分派到设备实现。对于 mlx5 设备，发送投递进入 mlx5 provider；provider 根据 WR opcode、SGE 和 QP 状态，把应用结构写成 mlx5 HCA 可以消费的 WQE。

这一步仍然发生在用户态。资源创建和映射已经在此前由内核完成，provider 使用这些已建立的队列、doorbell 和 key 状态执行快速投递。libibverbs、provider、内核驱动之间的完整分工见 3.2；在这条 WRITE 路径中，关键事实是：`ibv_post_send` 返回以前，请求已经从 Verbs WR 变成发送队列中的设备工作项。

`_mlx5_post_send` 首先处理软件层面的投递条件。它取得 QP 发送队列锁，检查 WR opcode 是否受支持，检查 Send Queue 是否还有空间，检查 `num_sge` 是否超过 QP 当前状态允许的最大 SGE 数。任一条件不满足，函数返回错误，并把 `bad_wr` 指向链表中第一条失败的 WR。此时失败发生在投递阶段，请求还没有成为设备可执行的 WQE。

通过检查以后，provider 根据 `qp->sq.cur_post` 找到下一条 Send WQE 的位置。Send Queue 在用户态映射中表现为一段环形队列内存；`cur_post` 是软件生产者位置，`wqe_cnt` 是队列可容纳的 WQE 数量。mlx5 代码用 `mlx5_get_send_wqe(qp, idx)` 得到本次要填写的 WQE 地址，其中 `idx = qp->sq.cur_post & (qp->sq.wqe_cnt - 1)`。

## 3.1.2 WQE 的基本 layout

WR 是应用结构，WQE 是设备结构。二者表达同一条请求，但字段布局并不相同。mlx5 的 WQE 由若干个 16 字节段组成，RDMA WRITE 的常见形式是控制段、远端地址段和一个或多个数据段。

```text
Send WQE for RDMA WRITE

+-------------------------------+
| Control Segment               |
| opcode, QP number, WQE index, |
| completion flags, immediate   |
+-------------------------------+
| Remote Address Segment        |
| raddr, rkey                   |
+-------------------------------+
| Data Segment 0                |
| local addr, lkey, byte_count  |
+-------------------------------+
| Data Segment 1 ...            |
+-------------------------------+
```

控制段说明 WQE 的类型和归属，远端地址段说明远端访问目标，数据段说明本地 DMA 数据源。mlx5 provider 会按设备要求写入字节序、索引、QP number、长度和 completion 标志。字段级结构见 3.4。

对于 RC QP 上的 `IBV_WR_RDMA_WRITE`，`_mlx5_post_send` 在控制段之后调用 `set_raddr_seg(seg, wr->wr.rdma.remote_addr, wr->wr.rdma.rkey)`，把远端地址和远端 key 写入远端地址段。随后，函数遍历 `wr->sg_list`，对每个长度非零的 SGE 调用 `set_data_ptr_seg`，把本地地址、`lkey` 和字节数写入数据段。控制段最后写入设备 opcode、WQE index、QP number 和 WQE 大小，`qp->sq.wrid[idx]` 保存应用给出的 `wr_id`，用于完成时把 CQE 对回原来的 WR。

因此，`ibv_post_send` 返回 0 时，可以确定 WR 已经被 provider 接受并写入发送队列；不能据此认为本地数据已经被 DMA 读取，也不能认为远端内存已经发生变化。数据搬运发生在 doorbell 之后，由网卡异步完成。

## 3.1.3 队列、doorbell 与硬件可见性

mlx5 的发送路径涉及三类位置：主机内存中的队列和 doorbell record，用户态映射的 UAR 页面，以及设备内部的 QP/MKey/CQ 上下文。创建 QP、CQ 和 MR 时，内核驱动建立这些对象，provider 在用户态数据路径中使用已经映射好的资源。

在这条路径中，HCA（Host Channel Adapter）表示 RDMA 网卡的设备侧执行单元。QP context 是设备保存的 QP 状态，包含 QP number、队列位置、状态机和传输参数等信息。MKey 是设备侧的内存访问对象，`lkey` 和 `rkey` 最终都要由设备侧的 key 机制校验。

Send Queue 采用生产者、消费者模型。producer index 表示发送队列中已经由软件填好的新 WQE 到了哪里。MMIO 是 CPU 对设备寄存器窗口的内存映射写入，不等同于写普通 DRAM。UAR 和 BlueFlame 的细节在 3.4 中讨论；在完整路径中，它们共同承担“通知设备读取新 WQE”的角色。

```text
用户进程

  ibv_send_wr / ibv_sge
          |
          v
  mlx5 provider
          |
          v
  设备可访问的主机内存
  +----------------------+       +----------------------+
  | Send Queue buffer    |       | Doorbell Record      |
  | WQE ring             |       | producer index       |
  +----------------------+       +----------------------+
          |                              |
          | 设备可通过 DMA 读取          | 设备可通过 DMA 读取
          v                              v
  +-----------------------------------------------------+
  | mlx5 HCA                                            |
  | QP context | MKey table | scheduler | packet engine |
  +-----------------------------------------------------+
          ^
          |
  映射到用户态的 UAR / BF register
  MMIO 写入触发设备门铃

完成路径

  +----------------------+       +----------------------+
  | Completion Queue     |<------| HCA writes CQE       |
  | CQE ring             |       +----------------------+
  +----------------------+
          |
          v
  ibv_poll_cq 返回 ibv_wc
```

WQE 写入 Send Queue 以后，provider 必须按顺序发布这条请求。它先保证 WQE 内容对设备可见，再发布新的队列生产者位置，最后通过设备门铃通知网卡。门铃完成后，CPU 侧的投递动作结束；请求的推进由网卡继续完成。

## 3.1.4 网卡如何执行 RDMA WRITE

RDMA WRITE 的设备执行依赖三类已经建立的状态。QP 创建阶段，内核驱动为设备创建 QP context，其中包含 QP number、队列规模、UAR page、传输状态和路径参数等信息。MR 注册阶段，驱动为内存区域创建 MKey，`lkey` 和 `rkey` 都是设备校验这类内存对象时使用的 key。WR 投递阶段，provider 把 Verbs WR 编码成 WQE，并把新的 SQ producer index 和门铃通知发布给设备。

发起方网卡收到 doorbell 后，按照 QP context 中的队列状态消费已经发布的 WQE。控制段告诉设备这是一条 RDMA WRITE，属于哪个 QP，WQE 有多大，是否需要产生 completion；远端地址段给出目标虚拟地址和 `rkey`；数据段给出本地虚拟地址、`lkey` 和长度。设备内部还可能包含调度、缓存、预取和流水线等实现细节；对 Verbs 程序而言，稳定可见的边界是 WQE 被消费、本地内存被 DMA 读取、远端访问被 key 和权限校验、完成结果被写入 CQ。

本地 `lkey` 对应本端 MR。MR 注册时，内核和设备已经建立了设备访问这段内存所需的信息，包括页固定或按需分页、DMA 映射、IOMMU 相关状态以及设备侧的 key。执行 WRITE 时，发起方网卡用数据段中的 `lkey` 校验本地访问权限，并把本地虚拟地址转换到可 DMA 的地址范围，然后通过 PCIe 从主机内存读取数据。

远端 `rkey` 对应远端 MR。WRITE 请求到达远端网卡后，远端网卡用包中的 `remote_addr`、长度和 `rkey` 查询远端设备侧的 memory key 状态。校验通过后，远端网卡把 payload DMA 写入远端 MR 覆盖的内存范围。地址本身不是权限；`rkey`、访问权限、地址范围和 PD 等边界共同决定这次远端写是否被允许。

对于 RC QP，RDMA WRITE 还处在可靠传输协议之内。请求会受到 QP 状态、PSN、MTU、ACK/NAK、重传、timeout 和 outstanding 资源的约束。成功完成时，发起方最终收到传输层确认；失败时，错误会以本端 completion status 或异步事件的形式暴露出来。

## 3.1.5 Completion 表示什么

`IBV_SEND_SIGNALED` 使这条 WR 在完成后生成 CQE。设备执行完成后，把 CQE 写入发送 CQ 的 CQE ring。应用调用 `ibv_poll_cq` 时，provider 从 CQE ring 取出设备完成记录，并填充 `struct ibv_wc`。

```c
struct ibv_wc wc;
int n = ibv_poll_cq(cq, 1, &wc);
```

`wc.wr_id` 来自投递时保存的 `wr_id`，用于识别是哪条 WR 完成。`wc.status` 是 Verbs 层看到的结果；成功时为 `IBV_WC_SUCCESS`，失败时可能是本地访问错误、远端访问错误、重试耗尽、RNR 重试耗尽等状态。mlx5 CQE 中还有更底层的 syndrome，provider 和驱动把这些设备错误转换成 Verbs 层的 WC status。

RDMA WRITE 的成功 completion 有严格边界。它表示发起方这条 WR 在 RDMA 传输语义下已经完成，本地 SGE 对应的源 buffer 可以按照生命周期规则复用。它不表示远端应用已经被通知，也不表示远端 CPU 或 GPU kernel 已经开始消费数据。基本 RDMA WRITE 不需要远端应用提前投递 Receive WR，也不会自动在远端 CQ 中产生完成记录。

因此，远端可消费性必须由应用协议建立。常见做法包括 WRITE with Immediate、额外的 SEND、控制通道消息，或者在共享内存区域中维护生产者和消费者索引。涉及 GPU 显存时，还需要额外处理 CUDA stream、GPU cache 和设备间同步边界。RDMA completion 只完成传输层承诺，不替代应用层提交协议。

## 3.1.6 一条 WRITE 的时间顺序

一次 RDMA WRITE 可以按时间顺序分为八步。

第一，应用填写 SGE 和 Send WR。SGE 描述本地数据源，WR 描述操作类型、远端地址、`rkey` 和 completion 策略。

第二，`ibv_post_send` 进入 mlx5 provider。provider 检查 opcode、SQ 空间和 SGE 数量，失败时返回错误并设置 `bad_wr`。

第三，provider 在 Send Queue 中构造 WQE。RDMA WRITE WQE 通常由控制段、远端地址段和数据段组成；控制段携带 opcode、QP number、WQE index 和 completion 标志；远端地址段携带 `remote_addr` 与 `rkey`；数据段携带本地 `addr`、`lkey` 和长度。

第四，provider 发布这条请求。它先用内存屏障保证 WQE 内容对设备可见，再更新发送 doorbell record，发布新的 SQ producer index，最后通过 UAR/BF register 进行 MMIO 写。前两步写的是主机内存中的队列状态，最后一步才是对设备的门铃通知。

第五，发起方网卡读取 WQE。设备根据 QP context 解释 WQE，并根据本地 `lkey` 访问本地 MR。

第六，发起方网卡通过 PCIe DMA 读取本地数据，并按照 RC 传输语义生成 RDMA WRITE 请求包。

第七，远端网卡校验 `remote_addr`、长度和 `rkey`，通过后把数据 DMA 写入远端 MR。

第八，发起方网卡在发送 CQ 中写入 CQE。应用通过 `ibv_poll_cq` 得到 WC，判断这条 WR 的完成状态。

这条路径揭示了 RDMA 编程中最重要的分界：`ibv_post_send` 完成的是投递，doorbell 之后才进入设备执行，`ibv_poll_cq` 取得的是传输完成结果，远端应用是否消费数据则由更高层协议决定。

## 延伸阅读

- [rdma-core 源码 `providers/mlx5/qp.c`](https://github.com/linux-rdma/rdma-core/blob/master/providers/mlx5/qp.c)：`mlx5_post_send` 的实现，本章引用的 `set_raddr_seg`、`set_data_ptr_seg` 与队列生产者索引都在其中。
- [rdma-core 源码 `providers/mlx5/cq.c`](https://github.com/linux-rdma/rdma-core/blob/master/providers/mlx5/cq.c)：CQE 轮询与 `ibv_wc` 转换的实现。
- [InfiniBand Architecture Specification Volume 1](https://www.infinibandta.org/)：RC 传输协议中 WQE 执行、PSN 与确认的规范描述。
- 注意：本章涉及 mlx5 队列格式的细节属于厂商实现，只用于解释行为；应用代码不应依赖这些内部结构。
