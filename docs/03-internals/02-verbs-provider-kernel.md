# 3.2 用户态 Verbs、mlx5 Provider 与内核驱动

## 机制入口

- 已安装的 mlx5 provider 与设备枚举
- `ibv_devinfo`
- `rdma link`
- `ldd examples/programming_model/03_rc_loopback`
- `linux-rdma/rdma-core` 中 `providers/mlx5/` 暴露出的实现线索

## 本章问题

- RDMA 为什么说绕过内核数据路径
- 哪些操作仍然需要内核参与
- `libibverbs`、mlx5 provider、`mlx5_ib`、`mlx5_core` 各自负责什么

## 提纲

### 3.2.1 Verbs API 在 mlx5 路径中的分层位置

- 应用程序
- `libibverbs`
- mlx5 provider
- kernel RDMA subsystem
- `mlx5_ib`
- `mlx5_core`

### 3.2.2 控制路径与数据路径

- 资源创建
- 内存注册
- QP 状态转换
- WR 投递
- CQ 轮询

### 3.2.3 mlx5 Provider 的作用

- 设备相关实现
- 用户态对象映射
- WQE 格式
- doorbell 机制

### 3.2.4 `mlx5_ib` 与 `mlx5_core` 的作用

- 设备管理
- 权限检查
- 资源分配
- 内存映射
- 异步事件

### 3.2.5 UAR、mmap 与用户态访问

- 用户态可写 doorbell 区域
- CQ/QP 相关映射
- 安全边界
- `mlx5-abi.h`
- `mlx5_user_ioctl_cmds.h`

### 3.2.6 实现证据与硬件行为推断

- `rdma-core/providers/mlx5/mlx5.c`
- `rdma-core/providers/mlx5/verbs.c`
- `rdma-core/providers/mlx5/mlx5.h`
- `rdma-core/providers/mlx5/mlx5-abi.h`
- `rdma-core/providers/mlx5/mlx5dv.h`

### 3.2.7 本章小结

- “绕过内核”的准确含义
- 语义保证与实现差异
