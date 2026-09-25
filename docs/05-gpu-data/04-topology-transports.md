# 5.4 多 GPU、多网卡与传输路径

一台服务器可能有多个 GPU 和多张网卡。同一块数据既可能发给本机另一张 GPU，也可能发往远端主机。选择哪条路径，取决于两端内存的位置、设备能力和运行时状态。

先画出实际数据路径，再比较后端和参数，比单纯把 NIC 数量加倍更容易解释结果。

## 把 GPU 放进主机拓扑

第三篇已经介绍 PCIe 与 NUMA。GPU 加入以后，还可能通过 NVLink 互联，使受支持的 GPU 之间直接交换数据。可以用以下命令把 GPU、网卡与主机拓扑放在一起观察：

```bash
nvidia-smi topo -m
rdma link show
lspci -t
```

拓扑表中的 PIX、PHB、SYS 等标记帮助判断设备之间经过哪些主机结构，NVLink 标记则表示对应 GPU 互联。它们提供路径信息，不能直接当作实测带宽或所有 peer 访问能力的证明。

还需区分进程可见 GPU 编号与物理设备身份。设置 `CUDA_VISIBLE_DEVICES` 后，进程中的 GPU 0 可能对应另一块物理设备。记录 PCI bus ID 等稳定标识，有助于把框架分配与拓扑图对应起来。

## 统一接口下的不同路径

| 两端位置 | 可以考虑的路径 | 仍需确认 |
|---|---|---|
| 同机主机内存 | 本地复制、共享内存 | 是否共享地址空间或建立映射 |
| 同机 GPU | 支持的 GPU peer copy、NVLink/PCIe | peer 能力、映射与 stream 依赖 |
| 跨机主机内存 | RDMA、TCP 等 | 网络可达、注册及后端配置 |
| 跨机 GPU | GDR 或经主机 staging | 两端能力、注册、同步与路径 |

表 5-2：统一接口下的不同路径。
{: .table-caption }

这张表用于组织检查，并不承诺任何编译版本都支持所有组合。后端编进程序、运行时发现它、选择它执行，是三个不同步骤。

自动 fallback 可以帮助功能运行，但性能实验必须记录实际路径。如果 GPU 数据最后经过主机 staging，就应把拷贝时间、主机内存占用和对应带宽一起统计。

## 选择两端 NIC

源端 GPU 附近的 NIC 只是路径的一半，远端 NIC 到目标内存同样重要。选择还要考虑网络可达性与两端注册资源。某张卡不能访问目标 allocation，就不能仅凭源端亲和性将它加入候选。

这里把一条可独立使用的网卡及其网络路径称为 rail。把同一缓冲区注册到更多 NIC，可以增加选择余地，也增加注册时间和设备资源。按 GPU 的合适 rail 注册较少设备，则可能缩短初始化，但会减少故障或调度选择。需要分别测量启动、稳态与恢复，才能评价取舍。

[Issue #3217](https://github.com/kvcache-ai/Mooncake/issues/3217)讨论 EFA 的多 NIC 注册成本，属于特定环境的报告，不能直接把其中倍数外推到所有平台。

## 大块切片与小块请求

大块权重可以拆成 slice 分给多条 rail。批次需要等所有必要 slice 完成，一条慢路径可能拉长整个请求尾部。按观察到的速度或队列动态分配工作，可以减少慢路径影响，但也增加调度复杂度。

KV Cache 是模型生成过程保存的中间状态，实际请求可能由许多 block 组成。它更容易受到每块调度、注册查找和通知成本影响，不能仅用一个超大连续 buffer 的带宽代表其表现。

先对单 GPU、单 NIC 建立基线，再逐步增加 rail。保持数据量与计算同步方式一致，同时观察有效吞吐、P99、CPU 和内存占用。增加 NIC 后带宽不再增长时，回查共享 PCIe、内存带宽或软件瓶颈。

## 为案例阅读准备一张路径图

在图中标出源 Tensor、源 GPU、候选 NIC、远端 NIC、目标 GPU，再标出控制通知和生产/消费依赖。每个箭头都能说明使用什么传输方式，以及何时认为完成。

接下来阅读 [Mooncake TE](../06-case-studies/01-transfer-engine.md)，就能理解 Segment、Transport 和拓扑选择各自省去了应用的哪些工作。

## 参考资料

[GPUDirect RDMA 文档](https://docs.nvidia.com/cuda/gpudirect-rdma/index.html)说明平台条件；[Mooncake TE 设计](https://github.com/kvcache-ai/Mooncake/blob/5772765c5070cd44db78629864db3c00e5239b70/docs/source/design/transfer-engine/index.md)展示拓扑与多 NIC 的组织方式，具体支持能力以该版本及编译配置为准。
