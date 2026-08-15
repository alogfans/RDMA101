# 3.5 RC 可靠传输机制

RC（Reliable Connected）QP 提供可靠连接语义。它保证同一连接上的请求按传输规则推进：包有序号，接收方确认，发送方在超时或 NAK 后重传，持续失败时把错误暴露给本端应用。RC 的可靠性接近“可靠搬运”，但不是应用层事务。RDMA WRITE 成功完成表示传输层已经完成该操作，不表示远端应用已经处理数据。

RC QP 只有在状态机进入 RTS（Ready to Send）后才能正常发送。建立连接时，双方需要交换 QP number、初始 PSN、GID/LID、MTU、访问参数等信息，并通过 `ibv_modify_qp` 完成 RESET 到 INIT、INIT 到 RTR、RTR 到 RTS 的状态转换。

```text
RESET -> INIT -> RTR -> RTS
   \                  /
    \                /
     ------ ERR ----
```

INIT 阶段配置本地端口、P_Key 和访问权限；RTR 阶段配置远端 QP number、路径、接收 PSN 和接收侧资源；RTS 阶段配置发送 PSN、timeout、retry、RNR retry 等传输参数。状态机的意义在于把“资源已经创建”和“连接可以传输”分开。QP 创建成功并不表示它已经能发送 RDMA 请求。

## 3.5.1 PSN 与按序传输

Packet Sequence Number（PSN）是 RC 可靠性的基础。RC 把一条 WR 切成一个或多个 packet，每个 packet 带有 PSN。接收方按 PSN 检查顺序，发送方根据确认和错误响应决定是否继续、重传或报错。

```text
一条 16 KiB 消息，path MTU 为 2 KiB

packet 0  PSN=N
packet 1  PSN=N+1
packet 2  PSN=N+2
...
packet 7  PSN=N+7
```

如果接收方期望 PSN=N+2，却先看到了 PSN=N+3，就能判断中间有包缺失。对于 RC，乱序和丢失不会直接交给应用处理；设备传输层通过 ACK、NAK 和重传机制修复，修复失败才把错误报告到 CQ 或异步事件。

PSN 是有限宽度字段，会循环使用。连接建立时的初始 PSN 由双方配置，并参与后续可靠传输判断。应用程序通常不直接管理每个 packet 的 PSN，但在调试重传、路径 MTU 和连接参数时，需要知道一条 WR 可能对应多个 PSN。

## 3.5.2 ACK、NAK 与重传

RC 的正常路径由 ACK 推进。发送方发出 packet，接收方按序接收后返回确认；发送方收到确认后释放相应传输状态，最终在本端生成 completion。ACK 可以是累积确认，即确认某个 PSN 之前的包都已经按序收到。

发生丢包、乱序或接收端错误时，接收方可以返回 NAK。发送方收到 NAK 后按协议重传相应 packet；如果没有收到确认，也会在 timeout 后触发重传。`retry_cnt` 控制普通重试的上限，`timeout` 控制等待响应的时间尺度。重试耗尽后，本端 WR 会以错误 completion 结束，QP 通常进入错误相关状态。

`timeout` 的编码不是直接填写微秒数。InfiniBand Verbs 中常见的解释是以 `4.096us * 2^timeout` 为基本形式。过小的 timeout 会把短暂拥塞或路径抖动误判为失败；过大的 timeout 会让真实故障暴露得很晚。生产程序应当根据网络规模、交换机队列、拥塞控制和业务容忍度设置，而不是机械套用示例值。

## 3.5.3 RNR

RNR（Receiver Not Ready）表示接收端没有准备好匹配的 Receive WQE。它主要影响 SEND/RECV 语义。RDMA WRITE 不消耗远端 Receive Queue，通常不会因为远端没有 Receive WQE 而产生 RNR；SEND 操作到达时若接收方 RQ 为空，则可能触发 RNR NAK。

收到 RNR NAK 后，发送方不会立即失败，而是等待一段时间再重试。等待时间与接收方配置的 `min_rnr_timer` 有关，重试次数由发送方的 `rnr_retry` 控制。若接收方长期不补充 Receive WQE，发送方最终可能得到 `IBV_WC_RNR_RETRY_EXC_ERR`。

RNR 常常不是网络坏了，而是应用接收队列管理出了问题。接收端 poller 太慢、预投递 Receive WR 太少、消息突发超过队列水位、或者连接建立后忘记先补 RQ，都可能造成 RNR。SEND/RECV 程序通常要维护接收队列低水位，一旦 CQ 消费掉若干 receive completion，就及时补充新的 Receive WQE。

## 3.5.4 MTU 与分片

Path MTU 决定一个 RDMA packet 的最大 payload 尺寸。Verbs WR 可以远大于 MTU，设备会在传输层把它分片成多个 packet。每个 packet 有自己的 PSN，可靠性机制按 packet 工作，而 completion 仍按 WR 返回。

MTU 配置不一致会导致性能下降或连接失败。InfiniBand 环境中，path MTU 与子网管理和端口能力有关；RoCE 环境中，还要受到 Ethernet MTU、VLAN、IP 路由和交换机配置影响。QP 配置的 path MTU 不应超过路径上实际可承载的大小。

MTU 越大，单位数据的包头开销越低，通常有利于吞吐；但大 MTU 对端到端配置一致性要求更高，丢包或重传时单个 packet 的代价也更大。低延迟小消息和大吞吐流量对 MTU 的偏好可能不同，不能只用一个数字概括。

## 3.5.5 错误如何返回

RC 的失败最终会落到本端可观察对象上。发送 WR 的错误通常以 `ibv_wc.status` 返回，例如本地访问错误、远端访问错误、重试耗尽、RNR 重试耗尽或响应超时。更严重的设备、端口或 QP 状态变化可能通过异步事件上报。

本地访问错误通常指本端 SGE、`lkey`、地址范围或权限不合法。远端访问错误通常指远端 `remote_addr`、`rkey` 或权限不匹配。重试耗尽和响应超时常指向网络、路径、远端 QP 状态或拥塞问题。RNR 重试耗尽则优先检查接收端是否为 SEND/RECV 准备了足够 Receive WQE。

错误 completion 不应被简单打印后忽略。许多错误会使 QP 进入 ERR 状态，后续 WR 即使成功投递也无法按预期完成。稳健的程序需要把错误 completion 与异步事件纳入连接生命周期：停止继续使用该 QP，清理未完成 WR，根据业务语义重建连接或向上层报告失败。

## 3.5.6 RC 与应用协议

RC 保证的是传输层语义。RDMA WRITE 成功完成后，发起方可以按规则复用本地源 buffer；远端内存已经处于这次 WRITE 的传输结果中。但远端 CPU 是否已经读到这段内存，远端应用是否知道有新数据，GPU kernel 是否能看到刚写入的显存，都不由 RC 自动保证。

应用层通常需要额外的提交协议。WRITE with Immediate 可以让 WRITE 携带 immediate data，并在远端接收 CQ 上产生可见事件；SEND 可以作为控制消息通知远端；共享 ring buffer 可以通过生产者/消费者索引表达数据可用。无论使用哪种方式，都应把“数据搬运完成”和“应用状态提交”分开设计。

RC 也不能替代幂等性设计。如果应用在上层自行重试某个操作，需要考虑旧操作是否可能已经在远端生效。传输层重传由设备处理，不向应用暴露重复 packet；应用层重试则是新的 WR，可能再次写同一地址或再次提交同一语义动作。

## 3.5.7 小结

RC 依靠 PSN、ACK/NAK、重传、RNR、timeout 和 MTU 管理可靠传输。成功 completion 表示传输层完成，失败 completion 或异步事件表示设备无法继续满足该 WR 或 QP 的可靠性要求。

RC 的价值在于把丢包、乱序和有限重传隐藏在设备传输层内；它的边界在于不表达远端应用已经消费数据，也不提供应用级事务。理解这个边界，是设计 RDMA 控制协议、错误恢复和性能参数的前提。
