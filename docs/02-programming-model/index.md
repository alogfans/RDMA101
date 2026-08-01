# 第二篇：编程模型

第一篇完成了 RDMA 环境检查和一个最小 one-sided RDMA WRITE 程序。第二篇在这个基础上展开 RDMA Verbs 的编程模型：内存如何授权给网卡，QP/CQ 如何组织异步请求，远端地址和 `rkey` 如何进入数据路径，completion 又表达什么语义。

本篇会逐章讨论 RDMA 程序中的核心对象和执行逻辑。当前已经完成的章节如下：

- [2.1 RDMA WRITE 范例解读](01-one-sided-write.md)  
  围绕第一篇的 `examples/one_sided_write/` 样例，按程序执行顺序解释控制通道、资源创建、内存注册、QP 连接、WR 投递和 completion 检查。

后续章节将继续补充 QP 状态机、SEND/RECV、RDMA READ、内存注册策略、错误语义和资源生命周期。
