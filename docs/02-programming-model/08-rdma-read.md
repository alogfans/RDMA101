# 2.8 RDMA READ

WRITE 由生产方推送。若数据已经存在远端，使用者希望在需要时取回，可以发起 READ。远端先注册并授权数据源，之后每次取数由发起方提交，远端 CPU 无需逐次调用发送接口。

例如缓存服务保存了一块不变的数据，客户端从控制通道得到地址、长度和 `rkey`，即可把它读入自己的缓冲区。

## 本地缓冲区成为结果位置

READ 仍投递到发送队列，因为它主动发起操作。但 SGE 的作用与 WRITE 相反：它描述读回数据要放到本地哪里。

| 字段或权限 | READ 中的含义 |
|---|---|
| 本地 SGE 地址、长度、`lkey` | 接收结果的本地范围 |
| `remote_addr`、`rkey` | 远端数据源及授权 |
| 本地 MR 的 `LOCAL_WRITE` | 允许本端网卡写回结果 |
| 远端 MR/QP 的 `REMOTE_READ` | 允许读取远端数据 |

表 2-9：本地缓冲区成为结果位置。
{: .table-caption }

一个常见错误是沿用 WRITE 的只读源 MR 来接收 READ 结果。两个请求都通过 `ibv_post_send` 提交，但设备访问本地内存的方向已经改变。

## 从 WRITE 请求改成 READ

在前一章请求结构的基础上，保留已连接 QP，使用本地可写结果 MR：

```c
struct ibv_sge sge = {
    .addr = (uintptr_t)result_buffer,
    .length = read_size,
    .lkey = result_mr->lkey,
};
struct ibv_send_wr wr = {
    .wr_id = 1002,
    .sg_list = &sge,
    .num_sge = 1,
    .opcode = IBV_WR_RDMA_READ,
    .send_flags = IBV_SEND_SIGNALED,
    .wr.rdma.remote_addr = remote_addr,
    .wr.rdma.rkey = remote_rkey,
};
struct ibv_send_wr *bad = NULL;
int rc = ibv_post_send(qp, &wr, &bad);
if (rc != 0) return -1;
```

范围和权限检查完成后才能执行这段片段。等待对应成功 WC，再读取结果；不能把失败 READ 当成文件接口中正常的短读。普通 READ 不需要远端 RECV，也不会通知远端业务线程“某个客户端刚刚读完”。

## 读取期间谁保持数据稳定

传输成功只说明设备完成了访问。如果远端 CPU 同时修改这块对象，客户端可能读到不同更新时刻的数据。例如长度字段已更新，正文尚未改完，得到的组合就不是一个完整版本。

最初的实验让 B 先写好字符串，再启动 A 的 READ。真实服务可以用不可变对象加版本发布，或由业务协议协调读写。版本校验也要规定更新顺序与重试条件，不能简单认为多读一次就能得到一致快照。

远端还必须知道何时可以回收数据源。没有远端完成通知意味着不能仅从 B 的 CQ 判断所有读取已经结束。地址与 `rkey` 的持有期，应与缓存对象或租约的生命周期协调。

## READ 与 Atomic 的并发窗口 {#read-window}

READ 发出请求后，要等待远端返回数据，两端设备需保存未完成操作的状态。除了 SQ 深度，它还受到 READ/Atomic 资源窗口约束。

| 字段 | 角色 |
|---|---|
| RTR 的 `max_dest_rd_atomic` | 本端作为响应方可承担的资源数 |
| RTS 的 `max_rd_atomic` | 本端允许发起的未完成操作数 |

表 2-10：READ 与 Atomic 的并发窗口。
{: .table-caption }

发起窗口应在本端能力和对端提供的资源内配置。设备字段的精确定义见 [ibv_query_device](https://github.com/linux-rdma/rdma-core/blob/master/libibverbs/man/ibv_query_device.3)。

排队的 READ 可以多于同一时刻实际进行的 READ，网卡按窗口推进。因此，“提交了第五条 READ”本身不能证明超过四个响应资源会立刻产生访问错误。要区分软件已排队、设备已发出和已经完成的数量。

## 对照推送与拉取

`03_rc_loopback` 的 `[3] RDMA READ` 先在 B 准备字符串，再由 A 取回。输出应从 A 的缓冲区读取。与 WRITE 阶段对照，连接不变，变化的是谁生产数据、谁决定启动搬运，以及谁知道它已经完成。

逐条读、逐条等会把往返等待串起来。多个独立读取可以使用不同结果区域并发执行；性能实验还应考虑窗口、消息大小和双方内存位置。WRITE 同样涉及 RC 确认，不能仅按“单向与往返”推断两者具有固定倍数的延迟差。

下一章选读 [Atomic](09-atomic.md)，它同样需要响应资源，但把远端读与更新结合成一次操作。

## 参考资料

[ibv_post_send](https://github.com/linux-rdma/rdma-core/blob/master/libibverbs/man/ibv_post_send.3)定义 READ 请求；[ibv_modify_qp](https://github.com/linux-rdma/rdma-core/blob/master/libibverbs/man/ibv_modify_qp.3)定义两端资源窗口。
