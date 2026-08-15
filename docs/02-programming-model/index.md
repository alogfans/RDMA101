# 第二篇：编程模型

第一篇跑通了一个最小 one-sided RDMA WRITE 程序。第二篇在这个基础上展开 RDMA Verbs 的编程模型：内存如何授权给网卡，QP/CQ 如何组织异步请求，远端地址和 `rkey` 如何进入数据路径，completion 又表达什么语义。

本篇按“程序框架 → 核心对象 → 核心操作 → 错误与事件”的顺序展开。阅读时可以把第一篇的样例放在旁边，对照每个对象在代码中的位置。

## 学习目标

完成本篇后，读者应当能够：

- 说出 Context、PD、MR、QP、CQ 五个核心对象的职责与依赖关系，并能按正确顺序创建与销毁；
- 解释 `lkey` 与 `rkey` 的用途差异，以及远端地址为什么不是访问权限；
- 描述 RC QP 从 RESET 到 RTS 的状态转换，以及 `ibv_modify_qp` 各阶段设置的参数；
- 区分 SEND/RECV、RDMA WRITE、RDMA READ、Atomic 的完成语义，包括 RNR、重试与错误完成；
- 区分错误完成（WC status）与异步事件（async event），并说明各自的处理责任。

本篇实验程序位于 `examples/programming_model/`。实验内容依次覆盖设备枚举、Context 打开、设备与端口能力查询、MR 注册、QP/CQ 创建，以及 SEND、WRITE、READ、Atomic 等操作的完成记录。阅读正文时，应把资源对象、API 参数和实验输出放在一起理解：API 参数说明调用形式，程序输出则显示资源状态和完成语义。

```bash
make -C examples/programming_model
./examples/programming_model/01_device_info
./examples/programming_model/02_resource_lifecycle
./examples/programming_model/03_rc_loopback
```

当前机器如果没有 RDMA 设备，`01_device_info` 会打印 `Found 0 RDMA device(s)`。这表示实验环境尚未准备好，需要先配置真实网卡或 Soft-RoCE/RXE。

## 程序框架

- [2.1 第一个 RDMA 程序](01-first-rdma-program.md)
  围绕第一篇的 `examples/one_sided_write/` 样例，从整体上理解 RDMA 程序的四个阶段：控制通道、资源创建、QP 连接、以及数据搬运。

## 核心对象详解

- [2.2 Context 与 PD](02-context-and-pd.md)
  设备上下文与保护域：RDMA 资源的入口与资源隔离边界。

- [2.3 Memory Region（MR）](03-mr.md)
  内存注册：把用户缓冲区交给网卡访问，并通过 `lkey`/`rkey` 控制访问。

- [2.4 Queue Pair（QP）](04-qp.md)
  队列对：状态机、SQ/RQ 交互、传输服务类型。

- [2.5 Completion Queue（CQ）](05-cq.md)
  完成队列：异步操作的完成语义、轮询与事件驱动、错误处理。

## 核心操作详解

- [2.6 SEND/RECV 操作](06-send-recv.md)
  双边操作：SEND/RECV、Immediate 数据、RNR 错误处理、Scatter-Gather。

- [2.7 RDMA WRITE 操作](07-rdma-write.md)
  单边写入：基本 RDMA WRITE、WRITE with Immediate、缓存一致性、性能优化。

- [2.8 RDMA READ 操作](08-rdma-read.md)
  单边读取：主动拉取数据、并发限制、Scatter Read、远程缓存。

- [2.9 Atomic 操作](09-atomic.md)
  原子操作：Compare & Swap、Fetch & Add、分布式锁、无锁数据结构。

## 错误与事件

- [2.10 错误完成与异步事件](10-error-and-events.md)
  编程模型中的异常通道：投递错误、WC error、async event 之间的区别，以及应用应承担的基本处理责任。
