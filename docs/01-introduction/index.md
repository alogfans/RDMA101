# 第一篇：快速入门

第一篇建立 RDMA 入门所需的最小闭环：先从 TCP socket 理解网络编程的一般模式，再说明 RDMA 为什么改变数据路径，最后完成环境检查、基础测试和一个单边 RDMA WRITE 样例。

本篇讨论 RDMA 的基础概念、环境配置和第一个可运行的程序。

## 理论基础

- [1.1 从 TCP 到 RDMA](01-tcp-to-rdma.md)
  说明网络编程的一般模式、TCP 数据路径的边界、RDMA 的关键特点。

## 动手实践

- [1.2 环境配置与第一个 RDMA 程序](02-environment-and-first-program.md)
  说明实验环境验证方法，以及一个可运行的 one-sided RDMA WRITE 样例。
