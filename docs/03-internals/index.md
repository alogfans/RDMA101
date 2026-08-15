# 第三篇：内部机制

第二篇说明了 RDMA Verbs 的编程模型。第三篇在这个基础上向系统内部推进一层：WR 如何变成设备可执行的 WQE，内存注册如何建立设备可访问的地址和权限，RC 如何提供可靠传输，RoCE 网络和主机拓扑又如何影响程序表现。

本篇以 Mellanox/NVIDIA mlx5 路径为主要参照。文中的 provider、WQE、CQE、MKey、UAR、doorbell、BlueFlame 等内容，用于解释 mlx5 设备在 Verbs 语义下暴露出的行为；跨设备可依赖的部分，仍以 Verbs 编程语义为边界。

读完本篇后，读者应能把一个 RDMA 程序中的 API 调用，同用户态 provider、内核驱动、设备队列、DMA、网络路径和完成记录联系起来。

## 执行路径

- [3.1 WRITE 路径](01-operation-path.md)
  沿一条 RDMA WRITE 请求观察 `ibv_post_send`、WQE、doorbell、DMA、远端 key 校验和 CQE 写回。

- [3.2 Verbs 分层](02-verbs-provider-kernel.md)
  说明 `libibverbs`、mlx5 provider、Linux RDMA 子系统、`mlx5_ib` 和 `mlx5_core` 的分工。

## 设备对象

- [3.3 内存注册](03-memory-registration-dma-iommu.md)
  解释 MR、MKey、`lkey`/`rkey`、页固定、ODP、DMA 映射和 IOMMU 的关系。

- [3.4 队列与门铃](04-qp-wqe-cqe-doorbell.md)
  展开 QP、WQE、CQE、doorbell record、UAR、BlueFlame 和 selective signaling。

## 传输与网络

- [3.5 RC 可靠性](05-rc-reliability.md)
  讨论 PSN、ACK/NAK、重传、RNR、timeout、MTU 和 RC 错误完成。

- [3.6 RoCE 路径](06-roce-network.md)
  说明 GID、GID index、RoCE v1/v2、IP 路由、VLAN、MTU、PFC、ECN 和 DCQCN。

## 拓扑与 GPU

- [3.7 主机拓扑](07-pcie-numa-cache.md)
  说明 PCIe、NUMA、DMA 可见性和 CPU cache 边界对 RDMA 的影响。

- [3.8 GPUDirect RDMA](08-gpudirect-rdma.md)
  讨论 GPU 显存注册、peer-memory/dma-buf、GPU-NIC 拓扑和 GPU 可见性同步。

## 排查方法

- [3.9 诊断](09-observability-diagnostics.md)
  从 WC status、异步事件、端口状态、GID、计数器、PCIe/NUMA/GPU 拓扑定位问题。
