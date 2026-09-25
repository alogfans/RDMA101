# 第三篇：RDMA 内部机制

前面的程序用一条工作请求描述写入，再从 CQ 取得结果。两次调用之间，用户态库、内核、网卡和网络各自完成了什么工作？本篇沿一次 WRITE 展开，再分别解释内存、队列、可靠传输和主机拓扑。

阅读时保留第二篇的代码作为参照。遇到硬件名称，先判断它服务于哪一步，再看厂商实现细节。

## 阅读顺序

| 章节 | 从本章理解什么 |
|---|---|
| [3.1 一次 WRITE 的执行过程](01-operation-path.md) | 把应用、Provider、两端网卡与内存连接起来。 |
| [3.2 Verbs、Provider 与内核](02-verbs-provider-kernel.md) | 区分资源建立和数据传输期间各层的职责。 |
| [3.3 内存注册、DMA 与地址转换](03-memory-registration-dma-iommu.md) | 理解设备怎样访问内存，以及注册为什么有成本。 |
| [3.4 WQE、CQE 与 Doorbell](04-qp-wqe-cqe-doorbell.md) | 将 WR、WC 对应到硬件工作项、完成条目和门铃。 |
| [3.5 RC 可靠传输](05-rc-reliability.md) | 理解包序列、确认、重传与 RNR。 |
| [3.6 RoCE 网络基础](06-roce-network.md) | 认识 GID、MTU、路由与拥塞控制，学会读取计数器。 |
| [3.7 PCIe、NUMA 与主机拓扑](07-pcie-numa-cache.md) | 观察线程、内存与网卡的位置关系，设计 NUMA 对照实验。 |

表 3-3：RDMA 内部机制篇的阅读顺序。
{: .table-caption }

到这里，可以用执行过程解释注册成本、队列深度和网络错误。[第四篇](../04-optimization/index.md)开始测量和改进程序；GPU 的注册与同步放在[第五篇](../05-gpu-data/index.md)，先修知识也来自本篇。
