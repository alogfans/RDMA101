# 术语与问题索引

这页供阅读和调试时回查。首次学习可以从[第一篇](01-introduction/index.md)开始；遇到陌生缩写、API 或错误码，再回到这里找对应章节。

## 资源与请求

| 名称 | 含义 | 详细解释 |
|---|---|---|
| Context / `ibv_context` | 程序打开 RDMA 设备后得到的上下文 | [2.2 Context 与 PD](02-programming-model/02-context-and-pd.md) |
| PD / Protection Domain | 限定本地资源能否配合使用的保护域 | [2.2 Context 与 PD](02-programming-model/02-context-and-pd.md) |
| MR / Memory Region | 已登记地址、长度和权限的内存区域 | [2.3 MR](02-programming-model/03-mr.md) |
| `lkey` / `rkey` | 分别用于本地设备访问和远端访问的 key | [2.3 MR](02-programming-model/03-mr.md) |
| QP / Queue Pair | 承载请求的队列对，连接型 QP 还保存连接状态 | [2.4 QP](02-programming-model/04-qp.md) |
| SQ / RQ | 发送队列 / 接收队列 | [2.4 QP](02-programming-model/04-qp.md) |
| WR / Work Request | 应用向队列提交的工作请求 | [投递请求](02-programming-model/04-qp.md#posting) |
| SGE / Scatter-Gather Element | 一段本地内存的地址、长度和 key | [示例中的请求](02-programming-model/01-first-rdma-program.md) |
| CQ / Completion Queue | 保存请求完成结果的队列 | [2.5 CQ](02-programming-model/05-cq.md) |
| WC / Work Completion | `ibv_poll_cq` 返回给应用的完成结构 | [轮询与字段解释](02-programming-model/05-cq.md#polling) |
| `wr_id` | 应用登记在 WR 中、完成时返回的请求标识 | [完成处理](02-programming-model/05-cq.md#polling) |
| RC / Reliable Connected | 提供可靠传输的连接类型 | [QP 类型](02-programming-model/04-qp.md)、[重传机制](03-internals/05-rc-reliability.md) |
| Immediate / `imm_data` | SEND 或 WRITE with Immediate 携带的 32 位通知数据 | [SEND](02-programming-model/06-send-recv.md)、[WRITE 通知](02-programming-model/07-rdma-write.md#notification) |
| Inline / `IBV_SEND_INLINE` | 投递时把小块数据复制进工作项 | [发送标志](02-programming-model/04-qp.md#posting) |

表 0-3：资源与请求。
{: .table-caption }

## 几种操作的对照 {#operations}

下表以 RC、普通主机内存、已授权目标，以及请求了成功完成通知为前提。成功完成均不代表远端业务已处理数据。

| 操作 | 本地 SGE 的作用 | 是否需要远端 RECV | 远端是否得到完成 | 入口 |
|---|---|---|---|---|
| SEND | 数据源 | 需要 | RECV 完成 | [2.6](02-programming-model/06-send-recv.md) |
| WRITE | 数据源 | 不需要 | 无 | [2.7](02-programming-model/07-rdma-write.md) |
| WRITE with Immediate | 数据源 | 需要，用于通知 | 带立即数的接收完成 | [2.7 通知](02-programming-model/07-rdma-write.md#notification) |
| READ | 读回数据的目标 | 不需要 | 无 | [2.8](02-programming-model/08-rdma-read.md) |
| CAS / FA | 接收远端旧值 | 不需要 | 无 | [2.9](02-programming-model/09-atomic.md) |

表 0-4：几种操作的对照。
{: .table-caption }

## 按 API 查找

| 接口 | 查阅位置 |
|---|---|
| `ibv_get_device_list`、`ibv_open_device`、`ibv_query_device`、`ibv_query_port`、`ibv_alloc_pd` | [设备与 PD](02-programming-model/02-context-and-pd.md) |
| `ibv_reg_mr`、`ibv_dereg_mr` | [内存注册](02-programming-model/03-mr.md) |
| `ibv_create_qp`、`ibv_modify_qp` | [QP 创建与状态转换](02-programming-model/04-qp.md#connect) |
| `ibv_query_qp`、`ibv_destroy_qp` | [QP 查询与释放](02-programming-model/04-qp.md) |
| `ibv_post_send`、`ibv_post_recv` | [投递与部分失败](02-programming-model/04-qp.md#posting)、[预投递接收](02-programming-model/06-send-recv.md#receive) |
| `ibv_create_cq`、`ibv_poll_cq`、`ibv_destroy_cq` | [完成队列](02-programming-model/05-cq.md) |
| `ibv_req_notify_cq`、`ibv_get_cq_event`、`ibv_ack_cq_events` | [完成事件通知](02-programming-model/05-cq.md#events) |
| `ibv_get_async_event`、`ibv_ack_async_event` | [设备与资源事件](02-programming-model/10-error-and-events.md) |

表 0-5：按 API 查找。
{: .table-caption }

## 网卡、内存与网络术语

| 名称 | 含义与查阅位置 |
|---|---|
| Provider / `libibverbs` / uverbs | 用户态设备实现、公共库与内核接口，见 [3.2](03-internals/02-verbs-provider-kernel.md) |
| WQE / CQE | 网卡使用的工作项和完成条目，见 [3.4](03-internals/04-qp-wqe-cqe-doorbell.md) |
| Doorbell / UAR / BlueFlame | 发布队列进度及 mlx5 门铃机制，见 [3.4](03-internals/04-qp-wqe-cqe-doorbell.md) |
| Selective signaling | 只让部分发送请求产生成功完成，见 [3.4](03-internals/04-qp-wqe-cqe-doorbell.md#signaling) |
| DMA / IOMMU / MKey | 设备内存访问、地址转换与 mlx5 内存权限对象，见 [3.3](03-internals/03-memory-registration-dma-iommu.md) |
| ODP / On-Demand Paging | 访问时按需准备页面的注册方式，见 [3.3](03-internals/03-memory-registration-dma-iommu.md) |
| PSN / ACK / NAK | 包序列号、确认和否定确认，见 [3.5](03-internals/05-rc-reliability.md) |
| GID / GID index | 端点标识及本机地址表的条目编号，见 [3.6](03-internals/06-roce-network.md#gid-config) |
| MTU / PFC / ECN / DCQCN | 包大小、链路暂停和拥塞控制，见 [3.6](03-internals/06-roce-network.md) |
| PCIe / NUMA | 主机设备互联与内存拓扑，见 [3.7](03-internals/07-pcie-numa-cache.md) |
| GPUDirect RDMA | 网卡直接访问 GPU 显存，见 [5.1](05-gpu-data/01-gpudirect.md) |

表 0-6：网卡、内存与网络术语。
{: .table-caption }

## 性能、并发与恢复

| 名称或问题 | 查阅位置 |
|---|---|
| 延迟、p99、有效字节带宽、在途窗口 | [4.1 性能测量](04-optimization/01-measurement.md) |
| Batch、流水线、注册与连接复用 | [4.2 批处理与复用](04-optimization/02-batching-pipeline.md) |
| Worker、背压、准入、公平性、请求引用 | [4.3 异步传输](04-optimization/03-async-backpressure.md) |
| Segment 缓存、实例代次、元数据版本 | [4.4 连接与元数据](04-optimization/04-connections-metadata.md) |
| 超时、取消、结果未知、迟到完成、重放 | [4.5 故障恢复](04-optimization/05-failure-recovery.md) |
| 日志、线程状态、网络计数器的相互验证 | [4.6 诊断](04-optimization/06-diagnostics.md) |

表 0-7：性能、并发与恢复。
{: .table-caption }

## 错误码与现象

错误状态指出某类失败，不一定直接说明根因。先保存最早的错误及请求信息，再依据执行位置缩小范围。

| 状态或现象 | 从哪里开始 |
|---|---|
| `ibv_post_send` 返回非零 | [bad_wr 与部分投递](02-programming-model/04-qp.md#posting) |
| `IBV_WC_LOC_LEN_ERR` | [接收长度](02-programming-model/06-send-recv.md)、[错误分类](02-programming-model/10-error-and-events.md) |
| `IBV_WC_LOC_PROT_ERR` | [本地范围、权限与 lkey](02-programming-model/03-mr.md) |
| `IBV_WC_REM_ACCESS_ERR` | [远端授权](02-programming-model/03-mr.md)、[诊断](04-optimization/06-diagnostics.md#symptoms) |
| `IBV_WC_RNR_RETRY_EXC_ERR` | [RNR 与预投递接收](02-programming-model/06-send-recv.md#rnr) |
| `IBV_WC_RETRY_EXC_ERR` | [RC 重试](03-internals/05-rc-reliability.md)、[路径诊断](04-optimization/06-diagnostics.md#symptoms) |
| `IBV_WC_WR_FLUSH_ERR` | [保留最早错误](02-programming-model/10-error-and-events.md) |
| 没有错误，但请求不再推进 | [软件队列的循环等待](04-optimization/03-async-backpressure.md)、[分层观察](04-optimization/06-diagnostics.md) |
| 超时后能否立即注销 MR | [资源回收条件](04-optimization/05-failure-recovery.md) |

表 0-8：错误码与现象。
{: .table-caption }

## GPU 与应用数据

| 名称或接口 | 含义与查阅位置 |
|---|---|
| Host staging、peer-memory、dma-buf | 主机中转与显存注册方式，见 [5.1](05-gpu-data/01-gpudirect.md) |
| Shape、dtype、stride、storage offset | Tensor 的逻辑布局和实际区间，见 [5.2](05-gpu-data/02-tensor-memory.md) |
| CUDA VMM、allocation、注册缓存 | 底层映射与缓冲区寿命，见 [5.2](05-gpu-data/02-tensor-memory.md) |
| Stream、event、`cudaEventSynchronize` | GPU 工作之间的依赖，见 [5.3](05-gpu-data/03-synchronization.md) |
| `cuFlushGPUDirectRDMAWrites` | 按设备能力和作用范围处理写入可见性，见 [5.3](05-gpu-data/03-synchronization.md#gpu-sync) |
| NVLink、GPU/NIC 亲和性、多 rail | 设备之间的实际传输路径，见 [5.4](05-gpu-data/04-topology-transports.md) |
| TE Segment、Batch、Transport、`transfer_sync_write` | 传输引擎的接口和使用实验，见 [6.1](06-case-studies/01-transfer-engine.md) |
| Prefill、Decode、KV block、TTFT | 两阶段推理中的数据交接，见 [6.2](06-case-studies/02-pd-disaggregation.md) |
| TE 与 Store、缓存发布与持久化 | 地址搬运和对象管理的职责，见 [6.3](06-case-studies/03-kv-cache-store.md) |
| 权重版本、分片、reshard、checkpoint-engine | 权重加载与更新，见 [6.4](06-case-studies/04-weight-transfer.md) |
| Embedding、DataProto、PG/EP | 流水线与结构化数据，见 [6.5](06-case-studies/05-pipeline-transfer.md) |

表 0-9：GPU 与应用数据。
{: .table-caption }

## 常见问题

- **没有 RDMA 网卡，能否学习？** 可以用 [RXE 环境](01-introduction/02-environment-and-first-program.md#environment)运行基础实验；它不能代表硬件性能。
- **投递成功就可以改写 buffer 吗？** 查看[完成后的缓冲区使用规则](02-programming-model/05-cq.md#completion)，inline 的例外见[请求投递](02-programming-model/04-qp.md#posting)。
- **WRITE 完成后，server 为什么没收到消息？** 普通 WRITE 不生成远端完成，见[通知方式](02-programming-model/07-rdma-write.md#notification)。
- **出现 RNR 或一批 flush 错误怎么办？** 从[按现象排查](04-optimization/06-diagnostics.md#symptoms)进入；优先保留最早的错误。
- **带宽低，但没有任何失败完成？** 检查[主机拓扑](03-internals/07-pcie-numa-cache.md)与[网络计数器](03-internals/06-roce-network.md)。
- **RDMA 已成功，GPU 为什么仍读到旧数据？** 查看[GPU 生产和消费的同步顺序](05-gpu-data/03-synchronization.md#gpu-sync)。

## 实验入口

可按现有环境选择[配套实验](labs.md#choose)：[连续 WRITE](labs.md#pipeline)、[Tensor 布局](labs.md#layout)、[GPU 同步](labs.md#gpu)和 [TE 双进程传输](labs.md#te)。指南同时解释预期输出、检查范围和跳过状态。
