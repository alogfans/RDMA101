# 第二篇：编程模型

> 本篇正文待补充。

<!--
原大纲（暂不显示）：

本篇目标是搞明白 RDMA 模型。第一篇已经让程序跑起来，本篇要回答“为什么必须这样写”：RDMA 如何描述本地资源、远端资源、访问权限、异步提交、完成通知和连接元数据。

## 本篇目标

- 理解 Verbs 核心对象之间的依赖关系；
- 掌握资源创建、使用和销毁的生命周期；
- 理解权限、异步队列和 completion 模型；
- 理解 RDMA 中本地对象、远端地址、key、队列和 completion 如何共同构成传输语义；
- 能够实现 SEND、READ 和 WRITE；
- 能将原生 RDMA 对象映射到 Mooncake TE 抽象。

## 第 6 章：对象关系总览

- Device Context；
- Port；
- Protection Domain（PD）；
- Memory Region（MR）；
- Completion Queue（CQ）；
- Queue Pair（QP）；
- Work Request（WR）；
- Work Queue Element（WQE）；
- Scatter/Gather Entry（SGE）；
- Address Handle（AH）；
- Shared Receive Queue（SRQ）。

每个对象统一回答：

- 它为什么存在？
- 谁创建并拥有它？
- 它依赖哪些资源？
- 硬件中是否存在对应结构？
- 它是否位于性能关键路径？
- 失败时会表现为什么？
- Mooncake TE 如何封装或管理它？

## 第 7 章：Device、Context 与 Port

- RDMA device 与普通 netdev 的关系；
- device context；
- physical port、port state；
- LID、GID 与 GID index；
- InfiniBand 与 RoCE 的寻址差异；
- device capability；
- 多端口与多网卡。

实验：

- 编写 `rdma-info`；
- 输出设备、端口、GID 和能力；
- 找出 RDMA device 对应的 netdev；
- 分析设备存在但无法通信的情况。

## 第 8 章：Protection Domain 与 Memory Region

- PD 的资源隔离作用；
- MR 的 address、length、lkey、rkey；
- local/remote access permission；
- memory pinning 与 DMA mapping；
- MR 生命周期；
- 越界访问和失效 key；
- 注册与注销成本；
- hugepage、MR cache 和注册内存池；
- DRAM、persistent memory 与 GPU memory。

Mooncake TE 关联：

- Segment 与 Buffer；
- `registerLocalMemory`；
- Buffer metadata；
- remote-accessible Buffer；
- unregister 与未完成请求。

实验：

- 测量不同大小 MR 的注册成本；
- 使用错误 rkey；
- 制造越界访问；
- 在请求完成前释放或注销 Buffer。

## 第 9 章：Completion Queue

- CQ、CQE 与 Work Completion；
- `wr_id`；
- polling 与 event notification；
- CQ capacity；
- CQ overrun；
- success、protection error 与 flush error；
- 一个 CQ 服务多个 QP；
- completion 的语义边界。

## 第 10 章：Queue Pair

- Send Queue 与 Receive Queue；
- RC、UC 与 UD；
- RESET、INIT、RTR、RTS、ERR；
- QP number 与 PSN；
- QP capability；
- outstanding WR；
- ordering 与 retry；
- 一个 QP 的并发访问；
- QP-per-connection 的扩展性问题。

Mooncake TE 关联：

- Endpoint 管理；
- 连接复用；
- 多 QP；
- QP 错误与 TransferStatus。

## 第 11 章：WR、WQE 与 SGE

- opcode；
- scatter/gather；
- inline data；
- immediate data；
- signaled/unsignaled；
- fence；
- chained WR；
- `wr_id` 与应用上下文；
- 从应用请求到硬件队列项。

## 第 12 章：连接与元数据交换

- 手工交换 QP/MR 信息；
- TCP 控制通道；
- RDMA CM；
- GID、QP number、PSN、address 与 rkey；
- connection establishment；
- disconnect 与 reconnect；
- metadata freshness；
- 数据面与控制面分离。

## 第 13 章：从 Verbs 到 Mooncake TE

| RDMA 概念 | Mooncake TE 抽象 |
| separator | separator |
| MR | 注册后的 Buffer |
| 远端地址和 rkey | Segment metadata |
| WR | TransferRequest |
| 多个 WR | BatchTransfer |
| QP | Endpoint 内部资源 |
| CQ/CQE | 后台 completion 处理 |
| WC 状态 | TransferStatus |
| 连接信息 | metadata 与 endpoint management |
| NIC 选择 | topology-aware path selection |

## 本篇项目

实现同时支持 SEND、READ 和 WRITE 的 `rdma-playground`。项目重点不是追求性能，而是用对象图、时序图和故障实验说明 RDMA 模型：

- 每个 Verbs 对象何时创建、由谁持有、何时销毁；
- QP、MR、CQ、WR 和 SGE 如何协作完成一次传输；
- 本地地址、远端地址、lkey、rkey 和访问权限分别约束什么；
- completion 表示什么、不表示什么；
- 连接信息和内存 metadata 过期时会发生什么。

-->
