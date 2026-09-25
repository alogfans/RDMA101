# 2.6 SEND 与 RECV

QP 和 CQ 准备好后，可以用 SEND/RECV 完成一次消息交换。发送方说明数据在哪里，接收方事先提供一个缓冲区，网卡将消息放入这个缓冲区，然后通知双方各自的完成。

它与 socket 收发一样，需要两端程序参与。使用 RECV 时，接收空间要在数据到达前作为请求交给网卡。一次 RECV 被使用后，不会自动重新入队。

## 先准备接收缓冲区 {#receive}

假设 A、B 的 RC QP 已连接，B 注册了一块允许设备写入的缓冲区。B 先投递 RECV，下面是局部代码：

```c
struct ibv_sge recv_sge = {
    .addr = (uintptr_t)recv_buffer,
    .length = recv_capacity,
    .lkey = recv_mr->lkey,
};
struct ibv_recv_wr recv_wr = {
    .wr_id = 2001,
    .sg_list = &recv_sge,
    .num_sge = 1,
};
struct ibv_recv_wr *bad_recv = NULL;
int rc = ibv_post_recv(b_qp, &recv_wr, &bad_recv);
if (rc != 0) return -1;
```

这里的长度是可接收容量，不是已收到的字节数。收到完成以后，才从对应字段确定消息的有效长度。接收缓冲区需要容纳整条消息；普通 RC SEND/RECV 保留消息边界，过大的消息不能按 TCP 短读的方式分几次 `recv`。

## 发送一条消息

A 构造相应的源 SGE，并通过 SQ 发起 SEND：

```c
struct ibv_sge send_sge = {
    .addr = (uintptr_t)message,
    .length = message_size,
    .lkey = send_mr->lkey,
};
struct ibv_send_wr send_wr = {
    .wr_id = 1001,
    .sg_list = &send_sge,
    .num_sge = 1,
    .opcode = IBV_WR_SEND,
    .send_flags = IBV_SEND_SIGNALED,
};
struct ibv_send_wr *bad_send = NULL;
int rc = ibv_post_send(a_qp, &send_wr, &bad_send);
if (rc != 0) return -1;
```

SEND 请求没有远端地址和 `rkey`。接收位置由 B 的 RECV 决定，发送方只需要符合双方约定的消息长度与格式。

A 等待发送成功完成后可以复用源数据；B 取得接收成功完成后可以处理消息。A 的发送完成不表示 B 的业务线程已经执行，所以一次业务往返仍可能需要 B 再发回复。

## 接收队列需要不断补充 {#rnr}

假设 B 只投递一次 RECV，却要接收十条消息。第一条消耗了这个接收请求，下一条到达时没有位置可放，RC 可能收到 RNR（Receiver Not Ready）反馈并重试。接收资源一直不补充，就会等待或耗尽 RNR 重试次数。

因此，连续消息程序常维护一组接收缓冲区。完成处理取走一条消息后，将处理完的区域重新投递，或立即换入另一块空闲区域。尚被业务线程读取的区域不能重新交给网卡覆盖。

```text
空闲区域 → 投递 RECV → 消息写入 → 取得接收完成
    ↑                                  ↓
    └────────── 业务处理结束 ────────────┘
```

这条循环解释了接收队列深度与业务消费速度的关系。加深 RQ 能吸收短时突发，但若业务长期处理不过来，最终仍需应用背压。

## 立即数和多个内存片段

`IBV_WR_SEND_WITH_IMM` 可以附带 32 位立即数，适合携带小标记。发送端按要求使用网络字节序，接收端先检查 `IBV_WC_WITH_IMM` 再解释 `imm_data`。立即数不能代替任意长度的消息头。

多个 SGE 可以把本地几个内存区间组成一条消息，避免应用先拼接它们。接收方也可以使用多个接收 SGE，但总容量仍要足够，片段数量还受 QP 能力约束。SGE 列表描述的是本地内存，不能直接表达多个任意远端目标。

运行 `03_rc_loopback` 的 `[1] SEND/RECV`，先观察 B 投递接收、A 投递发送，再看两个完成与 B 的缓冲区内容。作为练习，可以找出程序中哪一行决定 B 的接收地址，哪一行决定 A 的消息长度。

下一章把接收位置交给发起方，使用 [RDMA WRITE](07-rdma-write.md)直接写入远端授权空间。

## 参考资料

[ibv_post_recv](https://github.com/linux-rdma/rdma-core/blob/master/libibverbs/man/ibv_post_recv.3)解释接收队列接口；[ibv_post_send](https://github.com/linux-rdma/rdma-core/blob/master/libibverbs/man/ibv_post_send.3)列出 SEND 与立即数字段。
