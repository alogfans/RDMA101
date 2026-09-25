# 2.9 Atomic 操作（选读）

如果两个客户端都想把远端计数器加一，各自执行 READ、计算、WRITE，可能读到相同旧值，最后只增加一次。RDMA Atomic 让远端网卡在规定的原子性范围内完成读取和更新，解决单个值的并发修改问题。

本章介绍基础 Verbs 的 64 位 Compare & Swap（CAS，比较并交换）和 Fetch & Add（FA，取旧值并加）。只需要学习数据搬运的读者，可以先继续到[错误与事件](10-error-and-events.md)。

## CAS 的传输结果与业务结果

CAS 携带比较值和替换值。远端值等于比较值时才替换，无论是否替换，都把远端旧值返回本地。

| 旧值 | 比较值 | 替换值 | 新值 | 本地收到 |
|---|---|---|---|---|
| 7 | 7 | 11 | 11 | 7 |
| 9 | 7 | 11 | 9 | 9 |

表 2-11：CAS 的传输结果与业务结果。
{: .table-caption }

两种情况都可能得到成功 WC。完成状态说明请求执行成功，返回的旧值才告诉应用是否发生了交换。它们分别回答不同问题。

若用 0 表示空闲，多个客户端用 CAS 把 0 改成各自编号，只有观察到旧值 0 的操作取得了这个状态。这还不是完整的分布式锁，持有者退出、释放和超时后的恢复都要另外设计。

## 配置能力、权限与地址

设备应报告支持所需 Atomic，远端目标为 8 字节对齐的 64 位空间。目标 MR 允许 `LOCAL_WRITE | REMOTE_ATOMIC`，远端 QP 也允许 Atomic；本地结果 MR 则需要本地写权限。READ 和 Atomic 共用的窗口见 [2.8](08-rdma-read.md#read-window)。

`atomic_cap` 还限定原子性范围，不能直接推断与远端 CPU 写入、其他设备访问都互相原子。设计混合访问协议前，需要确认平台保证。

下面片段假设这些条件已经满足，`result` 指向至少 8 字节的本地结果区：

```c
struct ibv_sge sge = {
    .addr = (uintptr_t)result,
    .length = sizeof(uint64_t),
    .lkey = result_mr->lkey,
};
struct ibv_send_wr wr = {
    .wr_id = 1003,
    .sg_list = &sge,
    .num_sge = 1,
    .opcode = IBV_WR_ATOMIC_CMP_AND_SWP,
    .send_flags = IBV_SEND_SIGNALED,
    .wr.atomic.remote_addr = remote_addr,
    .wr.atomic.rkey = remote_rkey,
    .wr.atomic.compare_add = 7,
    .wr.atomic.swap = 11,
};
```

使用 `ibv_post_send` 提交并检查返回值，等待该请求成功完成后再读取 `*result`。不能在投递后立即判断它是否等于 7，因为结果可能还没有写回。

## Fetch & Add 与取号

FA 返回旧值，同时按给定增量修改远端值。旧值 100、增量 3，则本次将远端改成 103，并返回 100。它可以表示取得编号 100、101、102，但其他请求可能马上继续更新，因此 103 不是一个可长期依赖的“当前值”。

沿用前面的结构，修改：

```c
wr.opcode = IBV_WR_ATOMIC_FETCH_AND_ADD;
wr.wr.atomic.compare_add = 3;
wr.wr.atomic.swap = 0;
```

计数器仍需要考虑 64 位回绕。许多节点竞争同一个值时，操作也会在这个位置产生串行压力，8 字节请求并不自动意味着可无限扩展。

## 实验与恢复限制

`03_rc_loopback` 的最后一段执行 CAS，将 B 的 7 改为 11，A 收到旧值 7。设备报告不支持 Atomic 时，程序跳过这一段。可在独立实验副本中把比较值改成 8，预测成功 WC、本地旧值和远端新值应分别是什么。

恢复时尤其要区分网卡协议重传和重新发起业务操作。应用再提交一次 FA 就是新的一次加法。如果前一次已经生效，只是调用方不知道结果，再提交会多加一次。[4.5](../04-optimization/05-failure-recovery.md)会把“结果未知”与普通失败分开处理。

## 参考资料

[ibv_query_device](https://github.com/linux-rdma/rdma-core/blob/master/libibverbs/man/ibv_query_device.3)定义 Atomic 能力；[ibv_post_send](https://github.com/linux-rdma/rdma-core/blob/master/libibverbs/man/ibv_post_send.3)定义两种基础 Atomic 请求。masked 等扩展需另行确认设备与接口支持。
