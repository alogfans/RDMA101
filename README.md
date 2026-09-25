# RDMA101

RDMA101 是一套中文 RDMA 教程，从运行一个远端内存写入程序开始，逐步解释资源、请求和完成处理，再追踪数据经过内存、网卡和网络的过程。后续章节把程序扩展到异步服务、GPU 数据传输与 AI 系统。项目源于 Mooncake Transfer Engine 的开发实践。

[在线阅读](https://renfeng.org/rdma101/) · [术语与问题索引](docs/reference.md) · [示例程序与测试](examples/README.md)

阅读需要基本的 C/C++ 和 Linux 使用经验。没有 RDMA 网卡时，可以用 Soft-RoCE/RXE 学习接口和通信流程；硬件性能和 GPUDirect 实验需要相应设备。

## 从哪里开始

六篇共 34 章。初学者顺序阅读前三篇，再根据工作需要进入后续内容；已有经验的读者可以按术语、API 或错误现象查找。

| 内容 | 主要问题 |
|---|---|
| [第一篇：快速入门](docs/01-introduction/index.md) | RDMA 和 socket 有什么不同？如何跑通第一个程序？ |
| [第二篇：编程模型](docs/02-programming-model/index.md) | 如何注册内存、连接队列、提交请求和处理完成？ |
| [第三篇：内部机制](docs/03-internals/index.md) | 网卡怎样执行请求？网络和主机拓扑怎样影响结果？ |
| [第四篇：性能优化与工程实践](docs/04-optimization/index.md) | 怎样测量、增加并发，并管理连接与故障？ |
| [第五篇：GPU 与 AI 数据传输](docs/05-gpu-data/index.md) | 怎样描述 Tensor、建立 GPU 依赖并验证传输路径？ |
| [第六篇：传输引擎与 AI 系统案例](docs/06-case-studies/index.md) | TE 怎样接入 KV 缓存、权重同步和数据流水线？ |

## 运行示例与构建文档

环境检查和双端启动步骤见 [1.2 环境配置与第一个 RDMA 程序](docs/01-introduction/02-environment-and-first-program.md)。第二篇的三个实验依次观察设备信息、资源生命周期和单进程内两个 QP 的通信：

```bash
make -C examples/programming_model
./examples/programming_model/01_device_info
./examples/programming_model/02_resource_lifecycle
./examples/programming_model/03_rc_loopback
```

后续章节的局部代码和伪代码会注明适用范围。GPU 示例需要对应的 CUDA 或 PyTorch 环境；TE 示例需要安装兼容的 Mooncake Python 包，并确认所用后端。

本地预览文档：

```bash
python3 -m pip install -r requirements.txt
mkdocs serve
```

配套实验新增了连续 WRITE、Tensor 布局、CUDA 同步和 TE 双进程校验。运行 `make -C examples check` 可以统一编译和测试；缺少依赖的项目会明确跳过。逐步说明与预期结果见[实验指南](docs/labs.md)。

文档修改遵循[写作准则](SKILLS.md)，提交前运行 `mkdocs build --strict` 和 `git diff --check`。内容划分与案例选取依据见[大纲与编写说明](OUTLINE.md)。

## 贡献

欢迎提交文档修正、概念解释、实验代码、排障记录、系统案例和论文解读。尤其欢迎真实环境中的 RDMA 或 Mooncake TE 排障记录，包括问题现象、软硬件环境、排查过程和最终原因。

提交文档贡献前，请注意本项目的出版权安排：贡献者保留自己贡献内容的版权；同时，贡献者一旦向本项目提交文档、示例解释、图示、实验记录或案例分析，即表示同意项目发起人可以在 RDMA101 以及基于 RDMA101 的后续文章、讲义、书籍、课程或其他独立出版物中使用、整理、改写、重组和发布这些贡献内容，包括以独立作者身份出版的作品。除非另有书面约定，此类使用不需要再次取得贡献者许可，也不产生稿酬、版税或其他报酬义务。

RDMA101 的公开文档版本会持续保留。上述出版权安排并不意味着项目作者可以撤回已经公开发布的 RDMA101 文档，也不影响读者和贡献者在 CC BY-NC-SA 4.0 条款下继续访问、分享和改编公开版本。

请通过 issue 或 pull request 参与。

## License

本仓库中的公开文档材料采用 Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License（CC BY-NC-SA 4.0）发布。

这个许可主要适用于教程、文章、图示、解释性示例等文档内容，尤其是 `docs/` 目录下的材料。仓库中的源码、可执行示例或软件组件如有单独许可，应以对应文件中的说明为准。

你可以在遵守许可要求的前提下分享和改编这些文档材料：

- 署名：需要注明 RDMA101 和原始贡献者；
- 非商业使用：未经相关版权持有人许可，不得用于商业用途；
- 相同方式共享：如果改编这些材料，需要以相同许可发布改编内容。

每位贡献者保留自己贡献内容的版权。与此同时，项目发起人保留基于 RDMA101 全部材料，包括社区贡献内容，创作、改写、整理并以独立作者身份出版文章、讲义、书籍、课程或其他作品的权利。

RDMA101 的公开文档版本会持续保留，并继续按照 CC BY-NC-SA 4.0 条款提供给公众访问、分享和改编。第三方商业使用 RDMA101 文档材料仍需提前获得相关版权持有人的书面许可。
