# 第四篇：优化技巧

本篇讨论 RDMA 程序和系统中的常见优化技术。优化不是简单地调大参数，也不是追求单次 benchmark 的最高数字；它需要同时理解 workload、队列深度、内存注册、CPU polling、NUMA、网卡能力和网络配置。

## 学习目标

完成本篇后，读者应当能够：

- 解释 batching、signaled/unsignaled WR 与 completion moderation 对吞吐和延迟的影响；
- 判断 inline data、消息大小与延迟之间的权衡，以及何时适合使用 inline；
- 说明 queue depth、多 QP、多 CQ 与线程模型如何决定并发窗口和 CPU 利用率；
- 理解 MR cache、内存池与注册开销的关系，并设计避免热路径注册的方案；
- 比较 polling、interrupt 与混合模式在 CPU 占用和延迟上的取舍，并考虑 CPU 亲和性与 NUMA；
- 设计可复现的基准测试，识别测量误差来源，并给出有边界的性能解释。

## 计划内容

本篇后续章节将围绕以下主题展开：

- batching、signaled/unsignaled WR 和 completion moderation；
- inline data、message size 和延迟；
- queue depth、多 QP、多 CQ 和线程模型；
- MR cache、内存池和注册开销；
- polling、interrupt、CPU 亲和性和 NUMA；
- 基准测试中的可复现性和误差来源。

!!! note "章节状态"
    本篇目前是主题预览。各主题的详细章节正在编写中；读者可以先结合第二篇的编程模型和第三篇的机制，自行用 `perftest` 与示例程序验证其中的基本结论。
