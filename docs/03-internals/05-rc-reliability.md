# 3.5 RC 可靠传输机制

## 机制入口

- QP 转到 RTS 前后的行为边界
- 关联第二篇中的 retry、RNR、timeout 参数

## 本章问题

- RC 的可靠性由哪些机制支撑
- PSN、ACK、重传和 RNR 如何配合
- 错误完成为什么会出现在本端 CQ

## 提纲

### 3.5.1 RC QP 的连接状态

- RESET
- INIT
- RTR
- RTS
- ERR

### 3.5.2 Packet Sequence Number

- PSN 的作用
- 初始 PSN
- 顺序保证
- 重传定位

### 3.5.3 ACK、NAK 与重传

- ACK
- retry
- timeout
- retry exceeded

### 3.5.4 RNR 机制

- Receive Not Ready
- RNR NAK
- `rnr_retry`
- 接收队列水位

### 3.5.5 MTU 与分片

- path MTU
- message 与 packet 的边界
- 大消息传输

### 3.5.6 可靠性与应用协议的边界

- 传输可靠
- 应用级提交
- 幂等性
- 故障恢复

### 3.5.7 本章小结

- RC 保证的内容
- RC 不保证的内容
