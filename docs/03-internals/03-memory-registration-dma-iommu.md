# 3.3 Memory Registration、DMA 与 IOMMU

## 机制入口

- `ibv_reg_mr` 返回的 `addr`、`length`、`lkey`、`rkey`
- 对比普通用户态地址与已注册 MR

## 本章问题

- 普通内存为什么不能直接用于 RDMA 数据路径
- 注册内存为什么比普通函数调用更重
- `lkey`、`rkey` 与 DMA 访问校验有什么关系

## 提纲

### 3.3.1 用户态虚拟地址与设备 DMA

- CPU 虚拟地址
- 物理页
- DMA 地址
- 地址转换边界

### 3.3.2 注册内存时发生什么

- 页固定或按需分页机制
- DMA 映射
- 权限记录
- key 生成

### 3.3.3 IOMMU 与设备访问保护

- IOMMU 的位置
- DMA remapping
- 访问隔离
- 性能代价

### 3.3.4 MTT、MR cache 与设备侧缓存

- memory translation table
- 设备缓存
- 注册成本
- cache miss 对性能的影响

### 3.3.5 ODP 与传统注册的差异

- On-Demand Paging
- page fault 路径
- 适用场景
- 限制与代价

### 3.3.6 本章小结

- MR 的机制含义
- 对程序设计的影响
