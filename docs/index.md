# RDMA101

假设要把一台机器上的字符串写进另一台机器的内存。使用 TCP 时，发送方把数据交给 socket，接收方再读出来。使用 RDMA WRITE 时，接收方先准备并授权一块内存，发送方就可以让网卡把数据写到那里。

这减少了数据搬运时的 CPU 参与，也把一些工作交给了应用：内存何时可用，谁有权访问，什么时候传输完成。RDMA101 从这个小程序开始，逐步走到并发传输、GPU 数据和 AI 系统。

[开始阅读](01-introduction/01-tcp-to-rdma.md){ .md-button .md-button--primary }
[按术语或问题查找](reference.md){ .md-button }

## 阅读顺序

教程需要基本的 C/C++ 和 Linux 使用经验。六篇共 34 章，前三篇建立编程和原理基础，后面三篇将程序扩展到持续运行的传输服务及 AI 应用。

| 篇 | 内容 |
|---|---|
| [一：快速入门](01-introduction/index.md) | 从 TCP 认识 RDMA，检查环境，运行一次远端写入。 |
| [二：编程模型](02-programming-model/index.md) | 对照示例学习资源、请求和完成，再比较 SEND、WRITE、READ 与 Atomic。 |
| [三：内部机制](03-internals/index.md) | 追踪驱动与网卡如何执行请求，理解注册、队列、RC、RoCE 和主机拓扑。 |
| [四：性能优化与工程实践](04-optimization/index.md) | 从测量到批处理和异步并发，继续处理连接、超时、恢复与诊断。 |
| [五：GPU 与 AI 数据传输](05-gpu-data/index.md) | 补充显存注册、Tensor 布局、GPU 同步和多设备路径所需的知识。 |
| [六：传输引擎与 AI 系统案例](06-case-studies/index.md) | 使用 Mooncake TE，再讨论 KV 缓存、权重和流水线中的传输协议。 |

表 0-1：全书六篇的阅读顺序。
{: .table-caption }

初次接触 RDMA，可以顺序阅读，第二篇的 Atomic 暂作选读。已有 Verbs 经验的读者可从第四篇开始，按需回查原理。面向 AI Infra 的读者在理解注册、异步完成与主机拓扑后，再进入第五、六篇。

## 实验与查阅

基础实验使用仓库中的 C 程序。没有 RDMA 网卡时，可用 RXE 学习接口和通信流程；性能结论需要在实际硬件上测量。GPU 章节包含布局实验和同步片段，完整直传实验需要相应设备与驱动。TE 的首次实验采用 TCP 后端，便于先理解接口。

| 当前问题 | 阅读位置 |
|---|---|
| 系统是否具备运行 RDMA 的条件？ | [设备检查与首次测试](01-introduction/02-environment-and-first-program.md#environment) |
| MR、QP、CQ 分别做什么？ | [示例中的资源关系](02-programming-model/01-first-rdma-program.md#resources) |
| 投递成功后能否立即改写 buffer？ | [完成处理与缓冲区复用](02-programming-model/05-cq.md#completion) |
| WRITE 怎样通知远端应用？ | [WRITE 与远端通知](02-programming-model/07-rdma-write.md#notification) |
| GID index 应该填多少？ | [GID 与网卡的对应关系](03-internals/06-roce-network.md#gid-config) |
| 带宽低、请求停滞或超时怎样查？ | [测量方法](04-optimization/01-measurement.md)、[按现象排查](04-optimization/06-diagnostics.md#symptoms) |
| RDMA 成功了，GPU 为什么仍读到旧数据？ | [GPU 同步](05-gpu-data/03-synchronization.md#gpu-sync) |
| TE 帮应用做什么，上层还要做什么？ | [TE 设计与使用](06-case-studies/01-transfer-engine.md) |

表 0-2：实验与查阅。
{: .table-caption }

完整程序的命令、预期输出及检查方法见[配套实验与测试](labs.md)。也可以在仓库根目录执行 `make -C examples check`，分别查看通过、跳过和失败的项目。

项目源于 Mooncake Transfer Engine 的开发实践。文档和示例源码位于 [GitHub 仓库](https://github.com/alogfans/RDMA101)，示例命令默认从仓库根目录执行。

## 贡献

欢迎提交文档修正、概念解释、实验代码、排障记录、系统案例和论文解读。尤其欢迎真实环境中的 RDMA 或 Mooncake TE 排障记录，包括问题现象、软硬件环境、排查过程和最终原因。

提交文档贡献前，请注意本项目的出版权安排：贡献者保留自己贡献内容的版权；同时，贡献者一旦向本项目提交文档、示例解释、图示、实验记录或案例分析，即表示同意项目发起人可以在 RDMA101 以及基于 RDMA101 的后续文章、讲义、书籍、课程或其他独立出版物中使用、整理、改写、重组和发布这些贡献内容，包括以独立作者身份出版的作品。除非另有书面约定，此类使用不需要再次取得贡献者许可，也不产生稿酬、版税或其他报酬义务。

RDMA101 的公开文档版本会持续保留。上述出版权安排并不意味着项目作者可以撤回已经公开发布的 RDMA101 文档，也不影响读者和贡献者在 CC BY-NC-SA 4.0 条款下继续访问、分享和改编公开版本。

请通过 issue 或 pull request 参与。

## License

本仓库中的公开文档材料采用 Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License（CC BY-NC-SA 4.0）发布。

这个许可主要适用于教程、文章、图示、解释性示例等文档内容，尤其是 `docs/` 目录下的材料。仓库中的源码、可执行示例或软件组件如有单独许可，应以对应文件中的说明为准。

在遵守许可要求的前提下，可以分享和改编这些文档材料：

- 署名：需要注明 RDMA101 和原始贡献者；
- 非商业使用：未经相关版权持有人许可，不得用于商业用途；
- 相同方式共享：如果改编这些材料，需要以相同许可发布改编内容。

每位贡献者保留自己贡献内容的版权。与此同时，项目发起人保留基于 RDMA101 全部材料，包括社区贡献内容，创作、改写、整理并以独立作者身份出版文章、讲义、书籍、课程或其他作品的权利。

RDMA101 的公开文档版本会持续保留，并继续按照 CC BY-NC-SA 4.0 条款提供给公众访问、分享和改编。第三方商业使用 RDMA101 文档材料仍需提前获得相关版权持有人的书面许可。
