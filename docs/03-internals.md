# 第三篇：内部原理

> 本篇正文待补充。

<!--
原大纲（暂不显示）：

本篇把 Verbs API 与内核、驱动、NIC、PCIe、NUMA 和网络行为连接起来，帮助读者从“会调用 API”走向“能解释系统行为”。

## 本篇目标

- 将 Verbs API 与内核、驱动、NIC 和网络行为连接起来；
- 理解控制路径与用户态 fast path；
- 理解内存注册、WQE 提交、DMA 和 completion 的实现；
- 能够从多个系统层次定位 RDMA 故障；
- 理解 Mooncake TE 一次请求的完整执行路径。

## 第 14 章：RDMA 软件与硬件栈

- Application；
- `libibverbs`；
- provider；
- uverbs；
- kernel RDMA subsystem；
- device driver；
- NIC firmware；
- switch 与 network；
- 资源创建路径与数据 fast path。

## 第 15 章：资源创建与内存注册

- 应用调用 Verbs；
- provider 的设备相关处理；
- 内核权限校验与资源创建；
- 队列内存映射；
- 用户态句柄；
- 虚拟地址与物理页；
- page pinning；
- DMA mapping；
- IOMMU；
- NIC 地址转换与保护；
- on-demand paging；
- 注册成本的组成；
- hugepage 与 MR cache 的作用；
- 长期 pinning 对系统的影响。

## 第 16 章：从 WR 到 WQE 和 Doorbell

- WR 转换为设备 WQE；
- WQE ring；
- producer index；
- memory barrier；
- doorbell 与 MMIO；
- NIC 获取新任务；
- 一次提交多个 WR；
- inline data；
- 错误内存顺序可能造成的后果。

## 第 17 章：RDMA READ、WRITE 与 Atomic 的数据路径

- WRITE 从本地应用到远端内存的完整路径；
- READ request 与 response；
- round trip；
- 远端 NIC responder resource；
- outstanding READ 限制；
- READ 与 WRITE 的性能特征；
- Atomic 的远端序列化；
- Atomic 的扩展性边界。

## 第 18 章：Completion 的实现

- CQ ring；
- producer/consumer index；
- owner bit；
- CQ moderation；
- signaled 与 unsignaled；
- error CQE；
- flush completion；
- polling 与 CPU cache；
- CQ 容量不足的后果。

## 第 19 章：RC 可靠性与 RoCE 网络

- Packet Sequence Number；
- ACK/NAK；
- timeout；
- transport retry；
- RNR retry；
- ordering；
- QP 进入 error 的条件；
- RoCEv1 与 RoCEv2；
- GID 与路由；
- MTU；
- ECN、PFC 与拥塞控制；
- incast 与 head-of-line blocking；
- 网络问题如何表现为 Verbs 或应用错误。

## 第 20 章：PCIe、NUMA、GPU 与 NIC

- PCIe hierarchy；
- CPU socket 与 NUMA memory；
- PCIe switch；
- peer-to-peer DMA；
- GPUDirect RDMA；
- 跨 NUMA 和跨 root complex；
- topology-aware device selection；
- 多 NIC 聚合与本地性。

## 第 21 章：Mooncake TE 请求路径

```text
TransferEngine API
    -> request validation
    -> Segment/Buffer lookup
    -> Transport selection
    -> request slicing
    -> RdmaTransport
    -> Endpoint/QP
    -> post WR
    -> CQ polling
    -> completion aggregation
    -> TransferStatus
```

围绕该路径分析对象所有权、线程模型、metadata 生命周期、错误传播、retry 与 failover、teardown 与未完成请求、日志、指标和可观测性。

## 本篇项目

分别跟踪一次原生 RDMA 请求和一次 Mooncake TE 请求，画出软件调用路径、硬件数据路径、线程交互与错误传播路径，并通过日志或工具验证关键步骤。

-->
