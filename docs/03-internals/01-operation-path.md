# 3.1 一次 RDMA WRITE 的完整路径

RDMA WRITE 的程序入口是一条 Send WR。应用在 WR 中写明操作类型，在 SGE 中写明本地数据，在 `wr.rdma` 中写明远端地址和 `rkey`，然后调用 `ibv_post_send`。从 API 的形式看，这只是一次函数调用；从设备的角度看，这次调用向 QP 的 Send Queue 增加了一条设备可执行的工作项。

典型代码如下：

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

Linux RDMA 的用户态部分主要维护在 `linux-rdma/rdma-core` 项目中。这个项目包含公共 Verbs 库，也包含不同设备的用户态 provider。`libibverbs` 提供 `ibv_post_send`、`ibv_poll_cq`、`ibv_reg_mr` 等统一 API；provider 则负责把这些统一 API 落到具体设备格式上。mlx5 provider 对应 Mellanox/NVIDIA ConnectX 和 BlueField 系列设备的用户态实现，`rdma-core` 中的 `providers/mlx5/` 定义了 Verbs 操作到 mlx5 队列格式之间的转换。

provider 不是内核驱动。内核中的 `mlx5_ib` 负责把 mlx5 设备接入 Linux RDMA 子系统，`mlx5_core` 负责更底层的设备管理；用户态 mlx5 provider 则运行在进程地址空间内，使用内核已经创建并映射好的 QP、CQ、MR、UAR 等资源。在资源创建、内存注册、QP 状态转换等控制路径上，provider 需要通过 uverbs 与内核协作；在 WR 投递和 CQ 轮询这类高频数据路径上，provider 尽量直接读写用户态映射的队列和 doorbell 区域。

从 API 到设备队列的转换分为三层。`libibverbs/verbs.h` 中的 `ibv_post_send(qp, wr, bad_wr)` 调用 `qp->context->ops.post_send(qp, wr, bad_wr)`，公共 API 本身不规定设备队列的具体格式。`providers/mlx5/mlx5.c` 的 verbs 操作表把 `.post_send` 指向 `mlx5_post_send`，mlx5 设备的发送投递因此进入专门的 provider 路径。`providers/mlx5/qp.c` 中的 `_mlx5_post_send` 负责 WQE 构造和 doorbell 写入，一次 RDMA WRITE 在返回应用以前，已经从 Verbs WR 转换为 mlx5 HCA 可以消费的队列项。

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

mlx5 provider 中的结构定义可按普通 RDMA WRITE 的需要整理为以下形式。真实定义还包含对齐、保留字段和不同操作类型使用的扩展段；下列字段已经覆盖本节所需的核心信息。

```c
struct mlx5_wqe_ctrl_seg {
    uint32_t opmod_idx_opcode;  /* opmod | WQE index | opcode */
    uint32_t qpn_ds;            /* QP number | WQE size in 16-byte units */
    uint8_t  signature;
    uint16_t dci_stream_channel_id;
    uint8_t  fm_ce_se;          /* fence, completion, solicited event flags */
    uint32_t imm;
};

struct mlx5_wqe_raddr_seg {
    uint64_t raddr;             /* remote virtual address */
    uint32_t rkey;              /* remote memory key */
    uint32_t reserved;
};

struct mlx5_wqe_data_seg {
    uint32_t byte_count;
    uint32_t lkey;              /* local memory key */
    uint64_t addr;              /* local virtual address */
};
```

控制段说明这条 WQE 的类型和归属。`opmod_idx_opcode` 中包含设备 opcode 和 WQE index，`qpn_ds` 中包含 QP number 和 WQE 大小，`fm_ce_se` 中包含 fence、completion、solicited event 等标志。远端地址段说明远端访问目标，数据段说明本地 DMA 数据源。mlx5 代码在写入这些字段时使用 big-endian 转换函数，因为这些字段最终由设备按硬件格式解释。

对于 RC QP 上的 `IBV_WR_RDMA_WRITE`，`_mlx5_post_send` 在控制段之后调用 `set_raddr_seg(seg, wr->wr.rdma.remote_addr, wr->wr.rdma.rkey)`，把远端地址和远端 key 写入远端地址段。随后，函数遍历 `wr->sg_list`，对每个长度非零的 SGE 调用 `set_data_ptr_seg`，把本地地址、`lkey` 和字节数写入数据段。控制段最后写入设备 opcode、WQE index、QP number 和 WQE 大小，`qp->sq.wrid[idx]` 保存应用给出的 `wr_id`，用于完成时把 CQE 对回原来的 WR。

因此，`ibv_post_send` 返回 0 时，可以确定 WR 已经被 provider 接受并写入发送队列；不能据此认为本地数据已经被 DMA 读取，也不能认为远端内存已经发生变化。数据搬运发生在 doorbell 之后，由网卡异步完成。

## 3.1.3 队列、doorbell 与硬件可见性

mlx5 的发送路径涉及三类位置：主机内存中的队列和 doorbell record，用户态映射的 UAR 页面，以及设备内部的 QP/MKey/CQ 上下文。创建 QP、CQ 和 MR 时，内核驱动建立这些对象，provider 在用户态数据路径中使用已经映射好的资源。

在这条路径中，HCA（Host Channel Adapter）表示 RDMA 网卡的设备侧执行单元。QP context 是设备保存的 QP 状态，包含 QP number、队列位置、状态机和传输参数等信息。MKey 是设备侧的内存访问对象，`lkey` 和 `rkey` 最终都要由设备侧的 key 机制校验。

Send Queue 采用生产者、消费者模型。producer index 是软件生产者位置，表示发送队列中已经由软件填好的新 WQE 到了哪里。MMIO 是 CPU 对设备寄存器窗口的内存映射写入，不等同于写普通 DRAM。UAR（User Access Region）是用户态可写的设备门铃区域，BF register 是 UAR 中用于 BlueFlame 低延迟投递的寄存器窗口。

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

沿发送方向看，CPU 与网卡之间至少经过四个对象。Send Queue buffer 保存 WQE 内容，doorbell record 保存队列生产者位置，UAR/BF register 承担设备通知，Completion Queue buffer 保存设备写回的 CQE。

```text
WQE buffer        请求内容：opcode、QPN、remote_addr、rkey、SGE
Doorbell record   队列进度：SQ producer index
UAR / BF register 设备通知：一次 MMIO 写触发网卡取请求
CQE buffer        完成结果：wr_id、status、opcode、vendor error
```

UAR 是 User Access Region。创建 context 和 QP 的过程中，内核驱动为进程建立必要的设备映射；provider 随后可以在用户态写 UAR 中的 doorbell/BF register。这个写入不是普通内存写，而是到设备 MMIO 区域的写入，最终在 PCIe 上表现为面向设备寄存器窗口的事务。正因为 UAR 已经映射到进程地址空间，高频发送路径不必为了每条 WR 再通过系统调用进入内核敲门。

WQE 写入 Send Queue 以后，provider 必须按顺序发布这条请求。`_mlx5_post_send` 在完成一批 WR 的 WQE 构造后调用 `post_send_db`。该函数先增加软件侧的 `qp->sq.head`，再执行面向设备的内存屏障。这个屏障保证前面写入的 WQE 内容先对设备可见，然后才发布新的队列位置和门铃通知。若顺序被破坏，设备可能先看到 doorbell，却从 Send Queue 中读到尚未完整写好的 WQE。

屏障之后，provider 把 `qp->sq.cur_post & 0xffff` 写入 `qp->db[MLX5_SND_DBR]`。这块 doorbell record 位于设备可见的主机内存中，含义是发送队列的生产者位置已经前移。doorbell record 提供队列状态，但它本身通常还不足以让设备立即处理新请求；网卡还需要一次门铃写入来得到通知。

最后一步是写 UAR/BF register。`post_send_db` 在满足 BlueFlame 条件时使用 `mlx5_bf_copy`，否则使用 `mmio_write64_be`。普通 doorbell 写主要告诉设备到 Send Queue 中读取 WQE；BlueFlame 路径则把 WQE 开头部分随 MMIO 写一并推给设备，使小请求可以更快进入设备流水线。现代 CPU 可能把连续的 MMIO 写暂存在 write-combining 缓冲中再合并发出，随后执行的 `mmio_flush_writes` 用于刷新这类缓冲，避免不同 CPU 上的门铃写被设备以错误顺序观察到。

这一步之后，CPU 侧的投递动作结束。WQE 已在设备可见的队列中，doorbell record 记录了新的生产者位置，UAR/BF register 的 MMIO 写触发设备开始取工作项。此后，请求的推进由网卡完成。

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
