# 3.9 观察与诊断

## 机制入口

- RDMA 环境报告的组成
- 设备、端口、GID、NUMA、PCIe、GPU-NIC 拓扑、网络接口、计数器

## 本章问题

- 一个 RDMA 问题应从哪一层开始定位
- 哪些工具对应 Verbs、内核、设备、网络、GPU 和拓扑
- 如何把错误完成、异步事件和系统计数器联系起来

## 提纲

### 3.9.1 设备与端口

- `ibv_devices`
- `ibv_devinfo`
- `rdma link`
- `rdma dev`

### 3.9.2 GID 与网络接口

- GID table
- netdev 关联
- IP 地址
- VLAN

### 3.9.3 PCIe、NUMA 与 GPU-NIC 拓扑

- `lspci`
- `numactl`
- `nvidia-smi topo -m`
- sysfs
- CPU affinity

### 3.9.4 网卡、GPU 与交换机计数器

- `ethtool -S`
- `perfquery`
- provider-specific counters
- `nvidia-smi`
- pause / ECN / retransmit

### 3.9.5 GPUDirect RDMA 能力检查

- CUDA device attributes
- NVIDIA driver / CUDA version
- peer memory / dma-buf 路径
- container 权限
- 拓扑约束

### 3.9.6 错误完成与异步事件

- WC status
- vendor error
- async event
- QP / CQ / port 关联

### 3.9.7 分层排查路径

- 程序参数
- Verbs 资源
- QP 状态
- 内存注册
- GPU memory 注册
- 网络路径
- 拓扑与性能

### 3.9.8 本篇小结

- 从机制理解到优化实践
- 第四篇入口
