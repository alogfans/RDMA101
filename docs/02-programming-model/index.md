# 第二篇：编程模型

第一篇跑通了一个最小 one-sided RDMA WRITE 程序。第二篇在这个基础上展开 RDMA Verbs 的编程模型：内存如何授权给网卡，QP/CQ 如何组织异步请求，远端地址和 `rkey` 如何进入数据路径，completion 又表达什么语义。

本篇按“程序框架 → 核心对象 → 核心操作”的顺序展开。阅读时可以把第一篇的样例放在旁边，对照每个对象在代码中的位置。

为了避免把 API 说明写成零散手册，本篇只把最常用、也最容易误解的 Verbs 语义放入正文：资源创建时的实际能力可能与请求值不同，设备能力查询给出的是上限而不是剩余配额，CQ 中的非成功 WC 不能按成功路径解释，以及 WRITE with Immediate 会消耗远端预投递的 Receive WR。读者如果需要逐个函数查阅参数和返回值，可以配合 RDMAmojo 的 libibverbs 文章阅读。

## 程序框架

- [2.1 第一个 RDMA 程序](01-first-rdma-program.md)
  围绕第一篇的 `examples/one_sided_write/` 样例，从整体上理解 RDMA 程序的四个阶段：控制通道、资源创建、QP 连接、以及数据搬运。

## 核心对象详解

- [2.2 Context 与 PD](02-context-and-pd.md)
  设备上下文与保护域：RDMA 资源的入口与资源隔离边界。

- [2.3 Memory Region（MR）](03-mr.md)
  内存注册：把用户缓冲区交给网卡访问，并通过 `lkey`/`rkey` 控制访问。

- [2.4 Queue Pair（QP）](04-qp.md)
  队列对：状态机、SQ/RQ 交互、传输服务类型。

- [2.5 Completion Queue（CQ）](05-cq.md)
  完成队列：异步操作的完成语义、轮询与事件驱动、错误处理。

## 核心操作详解

- [2.6 SEND/RECV 操作](06-send-recv.md)
  双边操作：SEND/RECV、Immediate 数据、RNR 错误处理、Scatter-Gather。

- [2.7 RDMA WRITE 操作](07-rdma-write.md)
  单边写入：基本 RDMA WRITE、WRITE with Immediate、缓存一致性、性能优化。

- [2.8 RDMA READ 操作](08-rdma-read.md)
  单边读取：主动拉取数据、并发限制、Scatter Read、远程缓存。

- [2.9 Atomic 操作](09-atomic.md)
  原子操作：Compare & Swap、Fetch & Add、分布式锁、无锁数据结构。
