# 第一篇：快速入门

> 本篇正文待补充。

<!--
原大纲（暂不显示）：

本篇目标是把 RDMA Hello World 跑起来。读者不需要在这一篇完全掌握 RDMA 模型，但应该能够搭建环境、运行工具、读懂最小示例的输入输出，并对 RDMA 为什么存在形成基本直觉。

## 本篇目标

- 理解数据从一个进程移动到另一台机器上的基本路径；
- 区分控制路径与数据路径；
- 理解 RDMA 希望减少的系统开销；
- 认识 SEND/RECV、RDMA READ、RDMA WRITE 和 Atomic 的基本差异；
- 能够搭建实验环境并跑通 RDMA Hello World；
- 建立基本性能测量意识。

## 第 1 章：数据如何跨机器移动

- Buffer、地址与长度；
- `memcpy`、CPU load/store、cache 与内存带宽；
- NUMA 的初步概念；
- pipe、共享内存与 socket；
- 用户态、内核态与系统调用；
- CPU copy、DMA、packet、message 与 byte stream。

实验：

- 测量不同大小的 `memcpy`；
- 实现 TCP ping-pong；
- 实现 TCP streaming；
- 绘制消息大小与延迟、吞吐量的关系。

## 第 2 章：传统网络数据路径

- TCP socket 的基本生命周期；
- send/recv buffer；
- 数据复制、协议处理和上下文切换；
- blocking、non-blocking、polling 与 event notification；
- 控制路径与数据路径；
- 数据传输成本是否一定由复制主导。

## 第 3 章：为什么需要 RDMA

- Kernel bypass；
- Zero copy；
- CPU offload；
- 预注册内存；
- 异步队列；
- one-sided communication；
- RDMA 能解决什么、不能解决什么。

重点辨析：

- RDMA 不等于完全不经过内核；
- zero-copy 不等于系统里没有数据移动；
- one-sided 不等于不需要控制面；
- 可靠传输不等于应用逻辑正确；
- RDMA 不一定适合所有消息大小和工作负载。

## 第 4 章：RDMA 操作模型

- SEND/RECV：two-sided communication、预投递 receive、消息边界与流控；
- RDMA WRITE：远端地址、rkey、push 模型与远端通知；
- RDMA READ：pull 模型、请求响应、远端资源消耗；
- Atomic：Compare-and-Swap、Fetch-and-Add、远端同步能力边界；
- completion 到底保证了什么。

## 第 5 章：搭建实验环境

- 软件 RDMA 环境与真实硬件环境；
- RDMA device、port、GID 与 netdev；
- InfiniBand、RoCEv1、RoCEv2 与 Soft-RoCE/RXE；
- `rdma-core`、`libibverbs`、RDMA CM；
- `rdma link`、`ibv_devices`、`ibv_devinfo`；
- `ib_write_bw`、`ib_read_bw`、`ib_send_bw`；
- MTU、路由和基本连通性；
- Soft-RoCE 可以验证什么、不能验证什么。

## 本篇项目

跑通 RDMA Hello World，并完成一份最小实验记录：

- 确认 RDMA 设备、端口、GID 和 netdev；
- 运行 `ibv_devinfo` 与至少一个 `perftest` 工具；
- 编译并运行一个最小 SEND/RECV 或 WRITE 示例；
- 记录两端命令、环境信息、输出结果和遇到的问题；
- 用一段话解释这个程序中数据是如何从本地 buffer 到达远端的。

-->
