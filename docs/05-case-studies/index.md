# 第五篇：实例研究

本篇回到真实系统，分析 RDMA 在工程系统中的使用方式。前四篇分别建立运行方法、编程模型、内部机制和优化方法；第五篇将把这些概念放回 Mooncake TE 以及其他 RDMA 系统的数据路径和控制路径中。

后续章节将围绕以下问题展开：

- Mooncake TE 如何组织 endpoint、metadata 和 transport；
- RDMA READ/WRITE 如何服务 AI 推理、KVCache 迁移和分布式缓存；
- 系统如何处理连接失败、QP error、重试和 failover；
- 性能调优如何与真实 workload 结合；
- 论文系统与工程系统在 RDMA 使用方式上的差异。
