# 第二篇：RDMA 编程模型

第一篇的程序已经把字符串写到了另一端。接下来沿着这份代码，解释内存怎样授权、请求怎样提交、完成结果又怎样回到应用。

2.1 先梳理整体流程，2.2—2.5 逐一介绍资源，2.6—2.9 比较操作，最后处理错误和设备事件。Atomic 可以选读；继续阅读第三篇之前，应能区分投递成功、传输完成和接收方消费结束。

## 阅读顺序

| 章节 | 从本章理解什么 |
|---|---|
| [2.1 RDMA WRITE 范例解读](01-first-rdma-program.md) | 沿 main 追踪准备、连接、写入、通知和清理。 |
| [2.2 Context 与 Protection Domain](02-context-and-pd.md) | 打开设备，并建立资源之间的保护关系。 |
| [2.3 Memory Region](03-mr.md) | 登记内存范围和权限，理解 lkey、rkey 与内存寿命。 |
| [2.4 Queue Pair](04-qp.md) | 连接两个队列对，提交一个或多个请求。 |
| [2.5 Completion Queue](05-cq.md) | 识别完成结果，判断何时可以复用缓冲区。 |
| [2.6 SEND 与 RECV](06-send-recv.md) | 预投递接收请求，观察双端完成与 RNR。 |
| [2.7 RDMA WRITE](07-rdma-write.md) | 使用远端授权地址，并为连续写入增加通知和空间归还。 |
| [2.8 RDMA READ](08-rdma-read.md) | 由消费方发起读取，保持源数据有效。 |
| [2.9 Atomic 操作（选读）](09-atomic.md) | 理解 CAS、FA 和返回旧值；本章选读。 |
| [2.10 错误完成与异步事件](10-error-and-events.md) | 分别处理投递失败、错误完成和异步事件。 |

表 2-14：RDMA 编程模型篇的阅读顺序。
{: .table-caption }

## 配套实验

在仓库根目录运行：

```bash
make -C examples/programming_model
./examples/programming_model/01_device_info
./examples/programming_model/02_resource_lifecycle
./examples/programming_model/03_rc_loopback
```

三个程序分别观察设备、资源的创建与销毁，以及同一进程内两个 RC QP 的通信。它们默认使用端口 1；设备信息与 RC 通信程序默认查询或使用 GID index 0。实际设备选择和参数说明见[环境配置](../01-introduction/02-environment-and-first-program.md)。单进程实验仍需要可用的 RDMA 设备或 RXE，不能用来代表跨机性能。

查找具体 API 或比较操作时，可直接使用[术语与问题索引](../reference.md)。完成本篇后，[第三篇](../03-internals/index.md)将把这些接口对应到驱动、网卡和网络。
