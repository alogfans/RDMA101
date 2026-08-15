# 3.7 PCIe、NUMA、GPU 拓扑与缓存一致性

## 机制入口

- `lspci -tv`
- `numactl -H`
- `/sys/class/infiniband/*/device/numa_node`
- `nvidia-smi topo -m`
- CPU 绑定、内存位置与设备拓扑的对应关系

## 本章问题

- 网卡、CPU、内存的物理位置为什么重要
- GPU、NIC 和 CPU socket 的相对位置为什么影响 GPUDirect RDMA
- RDMA DMA 与 CPU cache 之间如何建立可见性边界
- NUMA 错配为什么会增加延迟或降低带宽

## 提纲

### 3.7.1 PCIe 路径

- PCIe device
- root complex
- switch
- DMA read / write

### 3.7.2 NUMA 拓扑

- CPU socket
- local memory
- remote memory
- NIC locality

### 3.7.3 GPU 与 NIC 拓扑

- GPU 所在 PCIe 层级
- NIC 所在 PCIe 层级
- PCIe switch
- cross-socket path
- `nvidia-smi topo -m`

### 3.7.4 DMA 与 CPU cache

- cache coherence
- registered buffer
- CPU 读写时机
- completion 作为可见性边界

### 3.7.5 内存分配与线程绑定

- `numactl`
- CPU affinity
- memory policy
- buffer pool

### 3.7.6 IOMMU、ATS 与性能影响

- IOMMU translation
- IOTLB
- Address Translation Service
- 设备差异

### 3.7.7 本章小结

- 拓扑对性能的影响
- 与 GPUDirect RDMA 的衔接
