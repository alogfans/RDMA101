# 第四篇：优化技巧

本篇讨论 RDMA 程序和系统中的常见优化技术。优化不是简单地调大参数，也不是追求单次 benchmark 的最高数字；它需要同时理解 workload、队列深度、内存注册、CPU polling、NUMA、网卡能力和网络配置。

后续章节将围绕以下问题展开：

- batching、signaled/unsignaled WR 和 completion moderation；
- inline data、message size 和延迟；
- queue depth、多 QP、多 CQ 和线程模型；
- MR cache、内存池和注册开销；
- polling、interrupt、CPU 亲和性和 NUMA；
- 严肃基准测试中的可复现性和误差来源。
