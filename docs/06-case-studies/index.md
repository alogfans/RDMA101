# 第六篇：传输引擎与 AI 系统案例

一个推理请求需要的 KV、一组待更新的权重或一段流水线输出，最终都要映射成可以传输的内存区域。传输引擎管理搬运，上层系统仍负责数据含义、目标空间和使用时机。

先用 6.1 的 CPU buffer 实验认识 Mooncake TE，再按应用方向选择后续章节。案例使用明确标注的源码和资料版本，区分 classic TE、TENT、Store 以及应用框架承担的工作。

## 阅读顺序

| 章节 | 从本章理解什么 |
|---|---|
| [6.1 Mooncake Transfer Engine 的设计与使用](01-transfer-engine.md) | 完成一次 CPU buffer 传输，再认识 Segment、Batch、Transport 与请求生命周期。 |
| [6.2 PD 分离中的 KV Cache 传输](02-pd-disaggregation.md) | 沿 Prefill 到 Decode 跟踪 KV block 的分配、映射、传输和消费。 |
| [6.3 分层 KV Cache 与共享缓存池](03-kv-cache-store.md) | 理解缓存查找、预取、发布和淘汰，区分 Store 与 TE。 |
| [6.4 模型权重的加载与同步](04-weight-transfer.md) | 比较权重加载与运行期更新，协调分片和版本切换。 |
| [6.5 多模态与训练流水线](05-pipeline-transfer.md) | 为 embedding、Tensor 和结构化训练数据设计描述与传输协议。 |

表 6-3：传输引擎与 AI 系统案例篇的阅读顺序。
{: .table-caption }

这些案例说明怎样组合前文知识。具体部署仍需对照所选框架版本；文中的设计练习不承诺某个现成后端已提供完整业务协议。若需要定位实现中的失败，可以返回[第四篇诊断](../04-optimization/06-diagnostics.md)。
