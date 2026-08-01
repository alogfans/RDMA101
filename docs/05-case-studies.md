# 第五篇：实例研究

> 本篇正文待补充。

<!--
原大纲（暂不显示）：

本篇进入真实系统，关注不同工作负载为什么选择不同 RDMA 操作、抽象、内存管理方式、控制面和故障模型。

## 本篇目标

- 理解不同工作负载为什么选择不同的 RDMA 操作和抽象；
- 分析控制面、数据面、内存管理、流控和故障恢复；
- 比较原生 Verbs 与上层系统的设计取舍；
- 综合理解 Mooncake TE；
- 从真实系统限制中识别值得继续研究的问题。

## 案例分析模板

每个案例统一回答：

1. 工作负载和数据对象是什么？
2. 为什么使用 RDMA？
3. 谁决定数据传输？
4. 采用 push 还是 pull？
5. 使用 READ、WRITE 还是 SEND？
6. 控制面如何交换 metadata？
7. 注册内存如何管理？
8. completion 在该系统中代表什么？
9. 如何实现流控和背压？
10. 节点、连接或网络失败后如何恢复？
11. 瓶颈从 CPU 转移到了哪里？
12. 系统设计还存在哪些局限和开放问题？

## 第 28 章：RDMA RPC

- SEND/RECV RPC；
- WRITE-based RPC；
- pre-posted receive；
- request ID；
- Buffer pool；
- credit-based flow control；
- small-message optimization；
- polling；
- timeout、duplicate request 与 retry。

## 第 29 章：RDMA Key-Value Store

- key lookup 与 value transfer；
- one-sided READ；
- server-driven WRITE；
- metadata cache；
- version validation；
- concurrent update；
- remote atomic；
- variable-length value；
- cache invalidation；
- hotspot 与 NIC contention。

## 第 30 章：分布式存储与 NVMe-oF

- primary/replica；
- log replication；
- data replication；
- durability；
- 网络完成与持久化完成；
- remote memory 与 remote storage；
- NVMe-oF；
- recovery；
- replication 与 erasure coding。

## 第 31 章：MPI、HPC 与分布式训练

- point-to-point communication；
- collective communication；
- eager 与 rendezvous；
- registered memory cache；
- large-message transfer；
- 通信与计算重叠；
- all-reduce；
- checkpoint 分发；
- GPU、NIC 与 NUMA topology；
- 多租户干扰与大规模 collective 拥塞。

## 第 32 章：远程内存与内存解耦

- remote memory；
- remote paging；
- page 与 object granularity；
- READ-based access；
- caching；
- consistency；
- latency hiding；
- remote node failure；
- 远端内存、网络和本地存储的层次关系。

## 第 33 章：KVCache 与 Mooncake Transfer Engine

- LLM 推理中的 KVCache；
- Prefill/Decode 分离；
- DRAM、VRAM 与 NVMe 之间的数据移动；
- Segment；
- registered Buffer；
- BatchTransfer；
- metadata service；
- Transport abstraction；
- RDMA Endpoint；
- asynchronous completion；
- request slicing；
- 多 NIC 聚合；
- topology-aware path selection；
- retry 与 failover；
- 异构内存和传输机制；
- 通用抽象与底层控制之间的权衡。

## 第 34 章：跨系统比较

比较原生 Verbs、RDMA CM、UCX、MPI、NCCL、Mooncake TE 和典型 RDMA 存储系统：

| 维度 | 核心问题 |
| separator | separator |
| 抽象层级 | 用户面对 QP/MR、message，还是 Segment/Transfer？ |
| 内存管理 | 谁注册、缓存和释放 MR？ |
| 建连 | 谁交换 QP、地址和访问凭证？ |
| 完成模型 | CQ、future、callback 还是 batch status？ |
| 故障处理 | transport、框架还是应用负责？ |
| 拓扑 | 是否感知 NUMA、GPU 和多 NIC？ |
| 可移植性 | 是否绑定特定硬件和网络？ |
| 性能控制 | 用户能够控制多少底层参数？ |
| 可观测性 | 如何解释性能与错误？ |

## 本篇项目

任选一个真实系统方向，完成设计、实现与评价：

- RDMA RPC；
- RDMA Key-Value Store；
- 远程内存服务；
- 文件或对象传输引擎；
- GPU Direct 数据传输；
- Mooncake TE 分析、优化或扩展。

最终交付包括问题定义、背景与相关设计、系统语义、prototype 或代码修改、正确性与故障测试、实验方法和性能结果、结果解释与适用边界、尚未解决的问题。

-->
