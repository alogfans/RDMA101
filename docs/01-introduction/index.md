# 第一篇：快速入门

第一篇建立 RDMA 入门所需的最小闭环：先从 TCP socket 理解网络编程的一般模式，再说明 RDMA 为什么改变数据路径，最后完成环境检查、基础测试和一个单边 RDMA WRITE 样例。

读完本篇后，读者应能判断实验环境是否可用，并执行第一个程序。

## 学习目标

完成本篇后，读者应当能够：

- 说明 TCP socket 数据路径的主要开销来源，以及 RDMA 如何重新组织数据路径；
- 列举 RDMA 与 TCP socket 在内存模型、队列模型和完成语义上的核心区别；
- 检查实验环境（设备、端口、链路状态），并使用 `perftest` 完成一次基础带宽测试；
- 运行 `examples/one_sided_write/` 下的最小单边 RDMA WRITE 程序，并解释其控制面与数据面的分工。

## 理论基础

- [1.1 从 TCP 到 RDMA](01-tcp-to-rdma.md)
  说明网络编程的一般模式、TCP 数据路径的边界、RDMA 的关键特点。

## 动手实践

- [1.2 环境配置与第一个 RDMA 程序](02-environment-and-first-program.md)
  说明实验环境验证方法，以及一个可运行的 one-sided RDMA WRITE 样例。
