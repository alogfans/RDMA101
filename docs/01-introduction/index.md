# 第一篇：快速入门

第一篇建立 RDMA 入门所需的最小闭环：先从 TCP socket 理解网络编程的一般模式，再说明 RDMA 为什么改变数据路径，最后完成环境检查、基础测试和一个单边 RDMA WRITE 样例。

当前已经完成的章节如下：

- [1.1 从 TCP socket 到 RDMA Hello World](01-hello-world.md)
  说明网络编程的一般模式、TCP 数据路径的边界、RDMA 的关键特点、实验环境验证方法，以及一个可运行的 one-sided RDMA WRITE 样例。

完成第一篇后，应能判断一台机器是否具备运行 RDMA 程序的基本条件，并能运行一个最小的 one-sided RDMA 程序。
