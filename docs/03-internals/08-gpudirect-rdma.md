# 3.8 GPUDirect RDMA

## 机制入口

- `nvidia-smi topo -m`
- `nvidia-smi -q`
- `ibv_devinfo`
- `rdma link`
- 检查 CUDA device attribute 中的 GPUDirect RDMA 支持能力
- CUDA device memory 作为 RDMA buffer 时的注册路径

## 本章问题

- GPUDirect RDMA 与普通 host memory RDMA 有什么区别
- NIC 如何直接访问 GPU memory
- 哪些能力来自 CUDA、GPU、NIC、驱动和平台拓扑
- 为什么 GPUDirect RDMA 程序还需要处理可见性和同步

## 提纲

### 3.8.1 从 Host Memory RDMA 到 GPU Memory RDMA

- host memory 数据路径
- GPU memory 数据路径
- staging copy
- direct peer access
- CPU 参与程度

### 3.8.2 GPUDirect、GPUDirect RDMA 与 GPUDirect Storage 的边界

- GPUDirect 概念族
- GPUDirect RDMA
- GPUDirect Storage
- NCCL 与通信库中的使用方式
- 本教程讨论范围

### 3.8.3 GPU 显存如何暴露给 PCIe Peer Device

- GPU BAR / BAR1
- peer memory mapping
- page table / mapping 生命周期
- NIC 作为 PCIe peer device
- 平台和 BIOS 约束

### 3.8.4 CUDA 内存、注册与 Verbs MR

- `cudaMalloc`
- CUDA device pointer
- `ibv_reg_mr` 对 GPU memory 的特殊路径
- `lkey` / `rkey`
- memory handle / dma-buf / peer-memory 机制
- 注册缓存

### 3.8.5 数据方向与完成语义

- NIC 写入 GPU memory
- NIC 从 GPU memory 读取
- RDMA WRITE to GPU
- RDMA READ from GPU
- SEND/RECV 与 GPU buffer
- CQE 与 CUDA stream 的边界

### 3.8.6 可见性、同步与 Flush

- CPU 可见性
- GPU kernel 可见性
- CUDA stream ordering
- GPUDirect RDMA writes visibility
- flush API
- completion 不等于 GPU kernel 已消费

### 3.8.7 拓扑与性能

- GPU-NIC 同 PCIe switch
- 跨 socket 路径
- PCIe generation / lane width
- BAR1 aperture
- IOMMU / ATS
- NUMA binding

### 3.8.8 能力检查与环境依赖

- GPU capability
- NIC capability
- CUDA / driver version
- kernel module
- container / Kubernetes 环境
- MIG / vGPU / 虚拟化限制

### 3.8.9 常见失败模式

- GPU memory 注册失败
- 路径退化为 host staging
- 拓扑导致带宽不足
- 远端写入后 GPU 读到旧数据
- 错误完成与 vendor error
- 资源释放顺序错误

### 3.8.10 本章小结

- GPUDirect RDMA 的机制位置
- 与训练、推理、存储和通信库的关系
- 与第四篇性能优化的衔接
