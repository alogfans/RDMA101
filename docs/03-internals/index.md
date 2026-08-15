# 第三篇：机制探秘

RDMA 程序在 API 层由一组 Verbs 对象组成：设备、Context、PD、MR、CQ、QP、WR 和 WC。前两篇已经说明这些对象如何创建和使用。到这一层为止，一个基本程序可以完成连接、投递请求并取得完成结果；但许多现象仍不能只靠 API 形式解释，例如内存注册为什么昂贵，`ibv_post_send` 为什么不等于传输完成，CQ 为什么可能溢出，RoCE 网络配置为什么会改变程序表现，GPU 显存为什么能够进入 RDMA 数据路径。

第三篇把这些 API 对象向系统内部推进一层。Context 不只是一个句柄，它连接到用户态 provider 和内核 uverbs 设备；MR 不只是 `lkey` 和 `rkey`，它对应页固定、DMA 映射、IOMMU 转换和设备侧 memory key；QP 不只是发送队列和接收队列的抽象，它在设备侧有 QP context，在用户态有可映射的队列内存和 doorbell 区域；CQ 不只是一个轮询接口，它对应设备写回的 CQE ring。理解这些对象的内部位置，才能把程序现象同系统行为联系起来。

本篇的实现讨论以 Mellanox/NVIDIA mlx5 路径为主线。mlx5 provider 运行在用户态，负责把 Verbs WR 编码为 mlx5 设备可消费的 WQE，并通过 doorbell 通知设备；内核中的 `mlx5_ib` 把 mlx5 设备接入 Linux RDMA 子系统，管理 QP、CQ、MR 等 RDMA 对象；更底层的 `mlx5_core` 负责设备初始化、固件命令、PCIe 资源和事件处理。`rdma-core/providers/mlx5/` 与 Linux 内核 mlx5 驱动展示了这条软件路径的形状。它们不是应用程序需要依赖的接口，却能帮助解释 mlx5 设备在 Verbs 语义下暴露出来的行为。

需要区分三类边界。第一类是 Verbs 语义，例如 WR 投递、WC 返回、MR 权限校验和 RC 传输保证；这些内容构成跨设备编程模型。第二类是 Linux RDMA 栈的实现路径，例如 provider、uverbs、DMA mapping、mmap、异步事件和 sysfs 计数器；这些内容解释 Linux 上的资源管理和诊断入口。第三类是 mlx5 设备的实现特征，例如 WQE/CQE 格式、UAR、BlueFlame、doorbell record、MKey、RoCE 拥塞控制和 GPUDirect RDMA；这些内容帮助理解 ConnectX/BlueField 这类设备的实际表现，但不应直接推广为所有 RNIC 的硬件设计。

## 章节安排

### 3.1 一次 RDMA WRITE 的完整路径

从一条 RDMA WRITE 的 Send WR 开始，沿 `ibv_post_send`、mlx5 provider、WQE、doorbell、设备 DMA、远端 key 校验和 CQE 写回展开完整路径。后续章节都围绕这条路径中的某一段继续加深。

### 3.2 用户态 Verbs、Provider 与内核驱动

说明 `libibverbs`、mlx5 provider、kernel RDMA subsystem、`mlx5_ib` 和 `mlx5_core` 的分工。“绕过内核”在 RDMA 中只描述高频数据路径；资源创建、权限控制、内存注册、队列映射和事件上报仍由内核参与。

### 3.3 Memory Registration、MKey 与 DMA 地址

解释普通用户态内存为什么必须注册后才能交给设备 DMA。重点包括页固定与 ODP、DMA 地址与 IOMMU、设备侧 MKey、`lkey`/`rkey`、访问权限和注册成本。

### 3.4 QP、WQE、CQE 与 Doorbell

展开 QP 和 CQ 在 mlx5 路径中的软件可见结构。Send Queue、Receive Queue、doorbell record、UAR、BlueFlame、CQE owner bit 和 selective signaling 共同决定请求如何从用户态队列进入设备流水线，又如何以 completion 的形式返回。

### 3.5 RC 可靠传输机制

讨论 Reliable Connected QP 的可靠传输语义。PSN、ACK/NAK、重传、RNR、timeout 和 path MTU 决定了 RC 如何处理丢包、乱序、接收端未准备好和远端无响应等情况。

### 3.6 RoCE 网络路径

说明 RoCE 把 RDMA 放入 Ethernet/IP 网络后引入的地址和网络条件。GID、GID index、RoCE v1/v2、MTU、PFC、ECN、DCQCN、交换机队列和网卡计数器共同影响程序的成功率、延迟和吞吐。

### 3.7 PCIe、NUMA 与缓存可见性

把 RDMA 数据路径放回主机拓扑中观察。网卡、CPU、内存和 GPU 之间的 PCIe 层级、NUMA 归属、IOMMU/ATS 能力以及 DMA 与缓存一致性边界，会直接影响延迟、带宽和可见性。

### 3.8 GPUDirect RDMA

重点讨论 GPU 显存作为 RDMA buffer 时的机制变化。GPU memory 的注册、peer-memory 或 dma-buf 映射、GPU-NIC 拓扑、BAR/BAR1、CUDA stream 顺序和 RDMA completion 之间存在额外边界，这些边界是 GPU-RDMA 程序正确性的核心。

### 3.9 观察与诊断

把前面各章的机制反过来用于排查。错误 completion、异步事件、端口状态、GID 表、MTU、PFC/ECN 计数器、PCIe/NUMA/GPU 拓扑和系统日志，构成从程序现象回到系统原因的诊断路径。

## 主线

第三篇的难度高于编程模型篇。关键不是记住每一个术语，而是把它们放在同一条执行路径中：应用提交 WR，provider 把 WR 写成设备队列项，doorbell 使网卡看到新工作，网卡通过 DMA 和网络完成数据搬运，CQE 把完成结果交还给应用。3.1 建立总路径，3.2 到 3.4 解释 Linux 与 mlx5 设备的执行边界，3.5 和 3.6 说明可靠传输与 RoCE 网络，3.7 和 3.8 进入主机拓扑和 GPU 显存，3.9 把这些机制用于诊断。
