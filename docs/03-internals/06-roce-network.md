# 3.6 RoCE 网络路径

## 机制入口

- `rdma link`
- `ibv_devinfo`
- `show_gids` 或等价 GID 查询
- `ip link`

## 本章问题

- RoCE 与 InfiniBand 在地址和网络路径上有什么差异
- GID index 为什么会影响连接
- PFC、ECN 和拥塞控制为什么会影响 RDMA 稳定性

## 提纲

### 3.6.1 InfiniBand 与 RoCE 的路径差异

- InfiniBand fabric
- RoCE Ethernet
- RoCE v1
- RoCE v2

### 3.6.2 LID、GID 与地址选择

- LID
- GID
- GID table
- GID index
- IPv4/IPv6 映射

### 3.6.3 MTU 与链路配置

- active MTU
- Ethernet MTU
- path MTU
- 配置不一致的表现

### 3.6.4 Lossless Ethernet 的要求

- PFC
- ECN
- DCQCN
- 拥塞传播

### 3.6.5 交换机与网卡计数器

- 丢包
- pause frame
- ECN mark
- retransmit
- timeout

### 3.6.6 本章小结

- RoCE 配置对程序语义的影响
- 与性能诊断的关系
