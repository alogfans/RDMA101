# 第三篇：机制探秘

RDMA 程序的表面结构由一组明确的 Verbs 对象组成：程序打开设备，创建 PD、MR、QP、CQ，把请求投递到 QP，再从 CQ 取得完成结果。到这一层为止，一个基本 RDMA 程序已经可以运行。但仅停留在 API 层面，很多现象仍然难以解释：内存注册为什么开销很高，`ibv_post_send` 为什么不表示传输完成，CQ 为什么会溢出，RoCE 网络配置为什么会影响用户态程序，GPU 显存为什么可以直接成为 RDMA 数据路径的一部分。

第三篇讨论这些问题。本篇不是驱动开发手册，也不是网卡硬件手册；它面向 RDMA 程序员，目标是把 API 对象向下翻译一层。Context、PD、MR、QP、CQ 在程序中是 API 对象，在系统内部则分别关联到用户态 provider、内核驱动、设备资源、DMA 映射、PCIe 拓扑和网络路径。理解这些联系，是后续讨论性能优化、故障诊断和真实系统设计的基础。

本篇区分三类内容。第一类是 Verbs 编程语义，例如 WR 投递、WC 返回和 MR 权限校验；第二类是 Linux RDMA 栈中的实现路径，例如 provider、内核 uverbs、DMA 映射和异步事件；第三类是 Mellanox/NVIDIA mlx5 设备上的实现细节，例如 WQE/CQE 格式、UAR、BF register、doorbell、ODP、RoCE 拥塞控制和 GPUDirect RDMA。第一类可以作为编程模型依赖，后两类则需要结合实际设备、驱动版本和平台配置判断。

本篇的实现讨论以 mlx5 为主线。文中出现的“provider”“doorbell”“WQE”“CQE”等实现对象，优先按照 Mellanox/NVIDIA ConnectX 系列及其 `mlx5` 驱动解释；其他 RNIC 的设计可作类比，但不默认等同。[linux-rdma/rdma-core](https://github.com/linux-rdma/rdma-core) 中的 `providers/mlx5/` 目录提供了用户态 provider 的实现证据，可用于确认 Verbs API 在 mlx5 数据路径上如何转换为 WQE、doorbell、CQE 等设备操作。

## 章节安排

### 3.1 一次 RDMA 操作的完整路径

从 RDMA WRITE 开始，说明一条 WR 从应用进入 provider，再变成 WQE、通知网卡、触发 DMA、穿过网络并最终生成 CQE 的过程。本章建立第三篇的总路线。

### 3.2 用户态 Verbs、Provider 与内核驱动

说明 `libibverbs`、mlx5 provider、kernel RDMA subsystem 和 `mlx5_ib`/`mlx5_core` 的分工。重点讨论“绕过内核”在 RDMA 中的准确含义：数据路径尽量避免传统内核协议栈，但资源创建、内存注册、映射和事件处理仍离不开内核。

### 3.3 Memory Registration、DMA 与 IOMMU

解释普通用户态内存为什么需要注册后才能被网卡访问。内容包括页固定或按需分页、DMA 地址映射、IOMMU、设备侧地址转换缓存、`lkey`/`rkey` 与访问校验。

### 3.4 QP、WQE、CQE 与 Doorbell

把第二篇的 QP/CQ 模型进一步展开到 mlx5 设备执行层。讨论 SQ/RQ、WQE、CQE、UAR、BF register、doorbell、doorbell record、MMIO、selective signaling，以及这些机制为什么会影响吞吐和尾延迟。

### 3.5 RC 可靠传输机制

围绕 RC QP 讨论可靠连接如何成立。内容包括 PSN、ACK/NAK、重传、RNR、timeout、path MTU，以及错误完成为什么最终回到本端 CQ。

### 3.6 RoCE 网络路径

讨论 RoCE 把 RDMA 放在 Ethernet/IP 网络中以后引入的问题。重点包括 GID、GID index、RoCE v1/v2、MTU、PFC、ECN、DCQCN、交换机和网卡计数器。本章为后续网络侧诊断打基础。

### 3.7 PCIe、NUMA、GPU 拓扑与缓存一致性

解释网卡、CPU、内存和 GPU 在物理拓扑中的位置关系。内容包括 PCIe root complex、NUMA locality、CPU cache 与 DMA 可见性、IOMMU/ATS，以及 GPU-NIC 拓扑对 GPUDirect RDMA 的影响。

### 3.8 GPUDirect RDMA

重点讨论 GPU 显存进入 mlx5 RDMA 数据路径后的机制变化。内容包括 host staging 与 direct peer access 的差别，GPU BAR/BAR1、CUDA device memory、GPU memory 注册、`lkey`/`rkey`、CUDA stream 与 RDMA completion 的同步边界，以及容器、MIG、虚拟化和拓扑限制。

### 3.9 观察与诊断

把本篇机制收束到排查方法。通过设备、端口、GID、PCIe、NUMA、GPU-NIC 拓扑、网卡计数器、错误完成和异步事件，建立一条从程序现象回到系统层次的诊断路径。

## 主线

第三篇的难度高于编程模型篇。关键不在于一次记住所有术语，而在于把它们放在同一条路径里：应用提交 WR，provider 和设备把它变成可执行请求，网卡通过 DMA 和网络完成数据搬运，CQE 把结果交还给应用。3.1 建立完整路径，3.3 和 3.4 展开 MR、QP、CQ 背后的设备机制。RoCE 环境对应 3.6 的网络路径；AI 训练、推理或 GPU 存储场景中的 RDMA，则对应 3.8 的 GPUDirect RDMA。
