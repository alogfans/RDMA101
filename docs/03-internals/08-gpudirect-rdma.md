# 3.8 GPUDirect RDMA

GPUDirect RDMA 使 RNIC 能够直接访问 GPU 显存。没有这项能力时，GPU 数据通常需要先复制到 host memory，再由 RNIC 从 host buffer 发出；接收方向也常常先写入 host buffer，再复制到 GPU。GPUDirect RDMA 去掉这段 staging 拷贝，使 GPU memory 可以像普通 RDMA buffer 一样出现在 SGE 或远端地址中。

```text
Host staging

GPU memory -> cudaMemcpy -> host MR -> RNIC -> network

GPUDirect RDMA

GPU memory <------------------------> RNIC -> network
              PCIe peer / DMA path
```

API 形式上的相似性容易掩盖机制差异。`ibv_reg_mr(pd, gpu_ptr, size, access)` 仍然返回 MR，WQE 中仍然填写地址、长度和 key；但 GPU memory 的页管理、DMA 映射、设备间可见性和同步边界与普通 host memory 不同。高性能 GPU-RDMA 程序的难点不在于多写几个 Verbs API，而在于正确处理这些边界。

## 3.8.1 Host Staging 与 Direct Path

Host staging 路径清楚、兼容性好，但多一次拷贝。发送方向上，GPU kernel 产生的数据先通过 `cudaMemcpy` 或异步 copy 到 host buffer；host buffer 注册为 MR 后，RNIC 再从其中 DMA 读取。接收方向上，RNIC 先写 host MR，CPU 或 CUDA runtime 再把数据复制到 GPU memory。这个路径消耗 host 内存带宽和 PCIe 带宽，也增加延迟。

Direct path 让 RNIC 直接访问 GPU memory。发送时，RNIC 从 GPU memory 读取数据；接收时，RNIC 把数据直接写入 GPU memory。对于大模型训练、参数同步、GPU 存储和高吞吐推理服务，这条路径可以减少 CPU 参与和中间拷贝。

Direct path 的收益依赖拓扑。GPU 与 RNIC 在同一 PCIe switch 或同一 NUMA 局部性内时，peer access 路径较短；跨 socket 或跨复杂 PCIe fabric 时，收益可能下降。即使功能可用，拓扑不佳也可能让性能低于预期。

## 3.8.2 GPU 显存如何成为 MR

GPU 显存由 GPU 驱动和 GPU MMU 管理，不是普通可固定的主机页。GPUDirect RDMA 需要 GPU 驱动、内核 RDMA 驱动和 RNIC 协作，把 GPU allocation 暴露为 RNIC 可以 DMA 访问的内存对象。不同内核、CUDA、NVIDIA 驱动和 rdma-core 版本可能使用不同机制，常见路径包括 NVIDIA peer memory 模块和 dma-buf 相关接口。

```text
cudaMalloc 得到 GPU virtual address
        |
        v
GPU 驱动确认 allocation 与访问属性
        |
        v
peer-memory / dma-buf 建立设备间映射
        |
        v
mlx5_ib 建立 DMA 映射并创建设备侧 MKey
        |
        v
ibv_reg_mr 返回 lkey/rkey
```

注册完成后，GPU pointer 可以出现在 SGE 中，也可以作为远端地址通过连接信息发给对端。WQE 中的地址仍是应用可见地址，设备执行时通过 MKey 和 peer mapping 找到实际可访问的 GPU memory 范围。`lkey` 用于本端 RNIC 访问本地 GPU buffer；`rkey` 用于远端 RNIC 访问该 GPU buffer。

BAR/BAR1 常被用于解释 GPU memory 的 PCIe 可见窗口。它不是应用直接操作的 Verbs 对象，而是 GPU 通过 PCIe 暴露资源的一部分。实际系统可能通过动态映射、IOMMU、peer-memory 回调和设备页表共同完成访问。文档或工具中看到的 BAR1 大小，可以帮助判断某些映射限制，但不应简单等同于“可注册显存总量”。

## 3.8.3 数据方向

GPU memory 可以参与多种 RDMA 操作。发送方向的 RDMA WRITE 可以让本端 RNIC 从本地 GPU buffer 读取数据并写到远端 MR；接收方向的 RDMA WRITE 可以让远端 RNIC 直接把数据写入本端 GPU buffer；RDMA READ 可以让 RNIC 从远端 GPU buffer 读取数据；SEND/RECV 也可以在支持的平台上使用 GPU buffer 作为 SGE。

```c
cudaMalloc(&gpu_buf, size);

struct ibv_mr *mr = ibv_reg_mr(pd, gpu_buf, size,
    IBV_ACCESS_LOCAL_WRITE |
    IBV_ACCESS_REMOTE_READ |
    IBV_ACCESS_REMOTE_WRITE);

struct ibv_sge sge = {
    .addr = (uintptr_t)gpu_buf,
    .length = size,
    .lkey = mr->lkey,
};
```

关键不在语法，而在生命周期。GPU allocation 必须在 MR 有效期间保持存在；所有可能访问该 MR 的 WR 完成或失败以后，才能注销 MR；注销后再释放 GPU memory。远端持有的 `rkey` 也要按协议失效，避免继续访问已经注销或重用的显存。

## 3.8.4 Completion 与 GPU 可见性

GPUDirect RDMA 最容易出错的地方是 completion 语义。RDMA completion 表示 RNIC 的传输操作完成；它不自动等价于 GPU kernel 已经能够按 CUDA stream 顺序看到这些数据，也不表示 GPU 写入的数据已经对 RNIC 可见。RNIC、GPU copy engine、GPU SM、CPU 和内存系统之间存在多个顺序域。

GPU 写、RNIC 读的方向上，GPU kernel 或 CUDA copy 必须先完成到足以让外部设备读取的边界。通常需要使用 CUDA stream 同步、事件或平台提供的 flush 机制，再投递 RDMA READ 或 SEND/WRITE。否则 RNIC 可能读取到 GPU 还没有写完或尚未对 peer 设备可见的数据。

RNIC 写、GPU 读的方向上，发送端看到 RDMA WRITE completion 只能说明远端 RNIC 已完成接收侧写入的传输语义。远端 GPU kernel 何时可以消费这段显存，还取决于 GPU 驱动和 CUDA 提供的同步机制。NVIDIA 平台为 GPUDirect RDMA 写入可见性提供过专门的 flush 或同步接口；具体 API 和要求随 CUDA、驱动和平台能力变化，应按当前 NVIDIA 文档和运行环境确认。

因此，GPU-RDMA 程序通常要显式建立两个提交点：一个是 RDMA completion，说明 RNIC 侧搬运完成；另一个是 GPU stream 或驱动同步点，说明 GPU 执行单元可以安全生产或消费这段 memory。把这两个提交点混为一个，是许多偶发数据错误的来源。

## 3.8.5 拓扑与能力

GPUDirect RDMA 需要多层能力同时成立。GPU 必须支持相关 peer access 能力，RNIC 和驱动必须支持注册 GPU memory，内核必须加载相应模块或启用 dma-buf 路径，IOMMU 和 PCIe 拓扑不能阻断设备间访问，容器环境还需要暴露 `/dev/infiniband`、GPU 设备和必要权限。

```bash
nvidia-smi topo -m
lsmod | grep -E 'nvidia_peermem|nv_peer_mem'
ls -l /dev/infiniband /dev/nvidia*
```

能力检查不能只看一个开关。某台机器可能 GPU 支持 GPUDirect，RNIC 也支持 mlx5 RDMA，但容器没有设备节点；也可能裸机可用，虚拟化或 IOMMU 配置改变后不可用；还可能功能可用但 GPU-NIC 跨 socket，导致带宽显著低于同 switch 配对。

多 GPU、多 NIC 系统中，GPU 与 NIC 的配对是一项实际设计。训练框架、通信库或应用调度层应尽量让每个 GPU 使用拓扑上更近的 RNIC。若某个 GPU 必须跨 socket 使用远端 RNIC，吞吐和尾延迟都应按较差路径估算。

## 3.8.6 常见失败形态

GPU memory 注册失败通常指向能力或权限边界：peer-memory 模块缺失，dma-buf 路径不匹配，容器未暴露设备，IOMMU/虚拟化配置不允许 peer mapping，或驱动版本组合不支持当前路径。此时 `ibv_reg_mr` 可能直接失败，也可能由上层库退回 host staging。

性能明显偏低时，首先检查路径是否真的走 direct。若程序或通信库静默退回 staging，GPU 与 host 之间会出现额外 copy，CPU 利用率、PCIe throughput 和延迟形态都会不同。若确认 direct path 可用，再检查 `nvidia-smi topo -m`、PCIe link speed/width、NUMA node 和并发流量。

数据偶发错误或旧数据问题，通常应回到可见性边界检查。GPU 写后 RNIC 读，需要 GPU 写完成并对 peer 设备可见；RNIC 写后 GPU 读，需要 RDMA completion 与 GPU 侧同步共同成立。仅等待其中一个完成点，都可能不足以构成完整的生产者/消费者协议。

## 3.8.7 小结

GPUDirect RDMA 的目标是让 RNIC 直接访问 GPU 显存，减少 host staging 拷贝。它在 Verbs API 上仍表现为 MR、SGE、WR 和 CQE，但内部涉及 GPU 驱动、peer-memory 或 dma-buf、MKey、PCIe/IOMMU 拓扑和 CUDA 同步。

正确使用 GPUDirect RDMA 的核心是三点：GPU memory 注册后才可作为 RDMA buffer；GPU allocation、MR 和远端 `rkey` 必须有清晰生命周期；RDMA completion 与 GPU 可见性不是同一个边界。性能分析则必须把 GPU-NIC 拓扑放在第一层，而不是只看程序是否成功完成。
