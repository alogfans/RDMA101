# GPUDirect RDMA：新的阅读位置

原 3.8 章已扩充为[第五篇：GPU 与 AI 数据传输](../05-gpu-data/index.md)。阅读时可沿用第三篇的注册与主机拓扑知识，依次学习以下内容：

1. [GPU 内存与 GPUDirect RDMA](../05-gpu-data/01-gpudirect.md)：显存直传、主机中转与注册条件。
2. [Tensor 布局与缓冲区管理](../05-gpu-data/02-tensor-memory.md)：地址区间、分配器与内存寿命。
3. [GPU 执行顺序与通信同步](../05-gpu-data/03-synchronization.md)：生产、传输与消费之间的依赖。
4. [多 GPU、多网卡与传输路径](../05-gpu-data/04-topology-transports.md)：拓扑、路径选择与验证。

## 同步问题 {#gpu-sync}

原来的 GPU 同步内容见[源端就绪与目标端可见](../05-gpu-data/03-synchronization.md#gpu-sync)。本页保留旧链接入口。
