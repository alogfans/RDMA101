# 第五篇：实例研究

本篇回到真实系统，分析 RDMA 在工程系统中的使用方式。前四篇分别建立运行方法、编程模型、内部机制和优化方法；第五篇会把这些概念放回 Mooncake TE 以及其他 RDMA 系统的数据路径和控制路径中。

## 学习目标

完成本篇后，读者应当能够：

- 说明一个真实 RDMA 系统如何组织 endpoint、metadata 与 transport 等抽象；
- 分析 RDMA READ/WRITE 在 AI 推理、KVCache 迁移和分布式缓存中的适用条件；
- 描述系统如何处理连接失败、QP error、重试与 failover，并与 RC 错误语义对应；
- 区分论文系统与工程系统在 RDMA 使用方式上的差异，识别可复现与不可复现的结论。

## 计划内容

本篇后续章节将围绕以下主题展开：

- Mooncake TE 如何组织 endpoint、metadata 和 transport；
- RDMA READ/WRITE 如何服务 AI 推理、KVCache 迁移和分布式缓存；
- 系统如何处理连接失败、QP error、重试和 failover；
- 性能调优如何与真实 workload 结合；
- 论文系统与工程系统在 RDMA 使用方式上的差异。

!!! note "章节状态"
    本篇目前是主题预览。详细案例章节正在编写中；读者可先对照 Mooncake TE 的源码与文档阅读，并尝试用前四篇的概念分析其 transport 设计。
