# 2.10 错误完成与异步事件

到这里已经看到一次请求从投递走到成功完成。异常也沿这条过程出现：描述不合法时可能投递失败，设备执行时可能报告访问错误，端口掉线还会产生资源事件。程序需要分别读取这些信息，才能知道请求停在哪一步。

## 先判断请求有没有被接受

`ibv_post_send` 和 `ibv_post_recv` 的返回值说明投递结果。链表投递可能接受前面一部分，再在某一条失败，`bad_wr` 指向失败位置。已经被接受的请求仍需保留上下文并等待结果。

这意味着不能把“提交函数返回错误”统一处理成“整批什么也没做”。[QP 章节](04-qp.md#posting)的 A→B→C 例子中，B 失败时 A 可能已开始执行。错误恢复要先分清已接受与未接受的部分。

## 从 WC 读取执行失败

已接受的请求由 WC 报告执行结果。轮询返回正数后，先检查 `status`，失败时记录：

```c
fprintf(stderr, "wr_id=%llu status=%s qp=%u vendor=%u\n",
        (unsigned long long)wc.wr_id,
        ibv_wc_status_str(wc.status), wc.qp_num, wc.vendor_err);
```

这是错误日志片段，外层轮询及返回值检查见 [2.5](05-cq.md#polling)。错误 WC 的 `opcode` 等字段不能按成功路径解释；请求类型、地址与长度应从应用保存的上下文取得。

| 状态 | 从哪里开始检查 |
|---|---|
| `IBV_WC_LOC_PROT_ERR` | 本地 key、范围及保护关系 |
| `IBV_WC_LOC_LEN_ERR` | 接收空间或请求长度是否满足操作要求 |
| `IBV_WC_REM_ACCESS_ERR` | 远端地址、key、MR/QP 权限与寿命 |
| `IBV_WC_RETRY_EXC_ERR` | 对端 QP、路径与可靠传输重试 |
| `IBV_WC_RNR_RETRY_EXC_ERR` | 接收请求是否及时准备或补充 |
| `IBV_WC_WR_FLUSH_ERR` | 导致 QP 停止正常执行的更早错误 |

表 2-12：从 WC 读取执行失败。
{: .table-caption }

一个状态提供的是检查方向，不能独自证明根因。比如重试耗尽既可能涉及网络，也可能是对端没有处于正确状态。连续出现大量 flush 时，优先保留最早的错误，后续 flush 往往是同一故障的结果。

失败也不保证目标完全未被修改。对多包 WRITE，部分数据可能已经到达。只有对应上层协议确认后，才能把结果发布给消费者或决定是否重新执行。

## 资源变化通过另一条事件通道报告

有些异常影响整个 CQ、QP 或端口，未必只对应某一条请求。应用通过 Context 的异步事件接口读取：

```c
struct ibv_async_event event;
if (ibv_get_async_event(ctx, &event) == 0) {
    fprintf(stderr, "event=%s\n", ibv_event_type_str(event.event_type));
    /* 依据类型读取 event.element，更新对应资源状态。 */
    ibv_ack_async_event(&event);
}
```

默认调用会阻塞，通常放在独立线程，或结合非阻塞 fd 与事件循环。取得事件后应确认；如果处理过程中需要把资源交给其他线程，必须保证引用寿命，避免确认和销毁之后继续使用旧指针。

| 事件 | 影响 |
|---|---|
| `IBV_EVENT_CQ_ERR` | CQ 已不能按正常路径继续使用 |
| `IBV_EVENT_QP_FATAL` | QP 出现严重故障，需要停止正常提交 |
| `IBV_EVENT_PORT_ERR` / `PORT_ACTIVE` | 端口可用性发生变化 |
| `IBV_EVENT_DEVICE_FATAL` | 设备级异常，影响可能超出单连接 |

表 2-13：资源变化通过另一条事件通道报告。
{: .table-caption }

这里的异步事件与 completion channel 的 CQ 通知不同。CQ 通知提醒应用去轮询 WC；异步事件报告资源状态。二者各有自己的读取与确认接口。

## 先收住请求，再决定恢复

一个线程观察到故障后，应让提交线程知道该连接暂时不可用。否则新的请求仍不断进入，既使恢复更复杂，也会用大量后续错误覆盖最早的线索。

接下来追踪已接受的请求，处理后续完成或资源终止，再根据设备及 API 保证回收它们引用的内存。应用等待超时只是本地计时结果，并没有自动取消设备工作。软件超时如何与设备排空协调，在 [4.5](../04-optimization/05-failure-recovery.md)详细展开。

端口重新 ACTIVE 也只说明链路状态改善。旧 QP、对端实例和内存授权仍需重新确认，不能把端口事件直接解释成所有业务请求已恢复。

本篇至此完成了从创建资源、投递操作到处理结果的基本过程。配套环回程序主要展示成功路径；故障实验应在独立环境中增加。继续阅读[一次 WRITE 的内部过程](../03-internals/01-operation-path.md)，可以理解这些完成和事件为什么由设备产生。

## 参考资料

[ibv_get_async_event](https://github.com/linux-rdma/rdma-core/blob/master/libibverbs/man/ibv_get_async_event.3)、[ibv_poll_cq](https://github.com/linux-rdma/rdma-core/blob/master/libibverbs/man/ibv_poll_cq.3)以及 [verbs.h](https://github.com/linux-rdma/rdma-core/blob/master/libibverbs/verbs.h)给出事件与状态的完整定义。
