# 第五篇：GPU 与 AI 数据传输

把主机缓冲区换成显存，程序多了两类需要协调的设备：网卡负责传输，GPU 负责生产和消费数据。一个有效指针既不能描述完整 Tensor 布局，也不能说明 GPU 已经完成计算。

本篇接着内存注册、异步完成和主机拓扑，补充理解 GPU 数据传输所需的基础。无需先学习模型计算原理；重点是数据在哪里、怎样组织、何时可以访问。

## 阅读顺序

| 章节 | 从本章理解什么 |
|---|---|
| [5.1 GPU 内存与 GPUDirect RDMA](01-gpudirect.md) | 比较主机中转与显存直传，理解注册条件。 |
| [5.2 Tensor 布局与缓冲区管理](02-tensor-memory.md) | 从 shape、dtype、stride 计算实际传输区间，管理底层 allocation。 |
| [5.3 GPU 执行顺序与通信同步](03-synchronization.md) | 用 stream 和 event 建立生产、传输与消费之间的依赖。 |
| [5.4 多 GPU、多网卡与传输路径](04-topology-transports.md) | 观察 GPU/NIC 拓扑和实际后端，比较不同负载的传输路径。 |

表 5-3：GPU 与 AI 数据传输篇的阅读顺序。
{: .table-caption }

Tensor 布局示例可以在安装 PyTorch 的 CPU 环境运行。完整 GPUDirect 实验需要兼容的 GPU、网卡、驱动与传输实现；文中的时序和片段不等同于已经验证的平台配置。随后进入[第六篇](../06-case-studies/index.md)，把这些条件放到传输引擎和 AI 系统中。
