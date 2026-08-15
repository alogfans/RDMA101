# 3.4 mlx5 QP、WQE、CQE 与 Doorbell

## 机制入口

- SEND、WRITE、READ、Atomic 的 WC 差异
- 对照每类操作的 WR 字段
- 根据 mlx5 provider 的 WQE 与 doorbell 实现推断设备行为

## 本章问题

- mlx5 QP 为什么不是普通用户态队列
- WQE 和 CQE 分别由谁写入、谁读取
- doorbell 如何把新请求通知给网卡

## 提纲

### 3.4.1 mlx5 QP 的设备侧含义

- SQ
- RQ
- QP context
- QP state

### 3.4.2 WQE 的组织方式

- opcode
- control segment
- SGE
- remote address
- immediate data
- inline data
- WQE basic block

### 3.4.3 Doorbell 机制

- doorbell write
- doorbell record
- UAR
- BF register
- MMIO
- BlueFlame 类机制

### 3.4.4 网卡如何消费 WQE

- SQ producer / consumer
- 读取 WQE
- 读取本地数据
- 执行网络操作

### 3.4.5 CQE 如何写回

- CQ producer / consumer
- WC 字段来源
- CQE syndrome
- CQ overrun
- completion channel

### 3.4.6 实现证据与设备 layout

- `rdma-core/providers/mlx5/qp.c`
- `rdma-core/providers/mlx5/cq.c`
- `rdma-core/providers/mlx5/wqe.h`
- `rdma-core/providers/mlx5/mlx5dv.h`

### 3.4.7 Selective Signaling 的机制背景

- signaled WR
- unsignaled WR
- CQE 数量
- outstanding 资源回收

### 3.4.8 本章小结

- 从队列抽象到设备执行
- 与性能优化的关系
