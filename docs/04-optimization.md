# 第四篇：优化技巧

> 本篇正文待补充。

<!--
原大纲（暂不显示）：

本篇强调“机制到证据”：每个优化都需要说明适用条件、收益、副作用、反例和可复现数据。

## 本篇目标

- 掌握可复现的性能实验方法；
- 能够从数据路径和资源模型提出瓶颈假设；
- 理解常见优化的机制、收益和副作用；
- 能够评价 Mooncake TE 中的优化设计；
- 避免只报告峰值数字或机械套用优化参数。

## 第 22 章：建立正确的 Benchmark

- 明确研究问题与假设；
- 自变量、因变量与控制变量；
- ping-pong 与 streaming；
- 单向与双向带宽；
- warm-up；
- 测试时长；
- 重复实验与置信区间；
- p50、p95 与 p99；
- 稳态和突发负载；
- 消息大小分布；
- CPU、NIC、PCIe 和网络指标；
- 微基准与应用基准；
- 结果的可复现性和外部有效性。

## 第 23 章：提交与完成优化

- Batch Posting；
- Doorbell Batching；
- Unsignaled Completion；
- 周期性 signaled WR；
- CQ 写入和 polling 压力；
- SQ 被填满和资源无法回收的风险；
- error propagation。

## 第 24 章：小消息与队列优化

- Inline Data；
- payload 直接进入 WQE；
- inline size capability；
- 小消息收益；
- 更大 WQE 对队列容量和 cache 的影响；
- Queue Depth；
- bandwidth-delay product；
- outstanding request；
- pipeline；
- 吞吐、内存占用和尾延迟。

## 第 25 章：内存注册与 Buffer 优化

- MR reuse；
- MR cache；
- registered memory pool；
- batch registration；
- hugepage；
- on-demand registration；
- 注册粒度；
- 内存碎片；
- pinned memory 压力；
- 预注册 Buffer pool；
- scatter/gather；
- 数据分片；
- ownership 与生命周期。

## 第 26 章：Polling、线程与拓扑优化

- busy polling；
- event-driven completion；
- adaptive polling；
- dedicated poller；
- application thread polling；
- shared CQ；
- CPU affinity；
- cache locality 与 false sharing；
- NIC-local memory；
- application thread placement；
- poller placement；
- GPU/NIC affinity。

## 第 27 章：多 QP、多 NIC 与操作选择

- 多 QP 并行；
- connection sharding；
- request slicing；
- multi-rail；
- path selection；
- bandwidth aggregation；
- dynamic load balancing；
- out-of-order completion；
- failover；
- READ、WRITE 与 SEND 的选择；
- push 与 pull；
- WRITE with immediate；
- 根据请求大小和工作负载动态选择操作。

Mooncake TE 关联：

- request slicing 阈值；
- topology matrix；
- preferred/secondary NIC；
- 多 NIC 带宽聚合；
- retry 与 alternative path；
- 拓扑、实时负载与故障状态的联合决策。

## 本篇项目

选择一个真实瓶颈，完成“现象—假设—机制—实现—实验—反例—边界”的完整分析。项目可以基于 `rdma-playground` 或 Mooncake TE，必须同时提交代码、正确性测试和可复现实验报告。

-->
