# 3.2 Verbs 分层

RDMA 常被概括为“绕过内核”。这句话只适合描述数据路径中的高频部分：应用投递 WR、轮询 CQ、数据由网卡 DMA 搬运，这些动作不再经过传统内核网络协议栈，也不需要每条消息都进入内核处理。若把这句话理解为 RDMA 程序不需要内核，就会误解整个系统。设备打开、资源创建、内存注册、权限检查、队列映射、异步事件和错误恢复都依赖内核。

一段普通 Verbs 程序大致从以下对象开始：

```c
struct ibv_context *ctx = ibv_open_device(device);
struct ibv_pd *pd = ibv_alloc_pd(ctx);
struct ibv_mr *mr = ibv_reg_mr(pd, addr, size, access);
struct ibv_cq *cq = ibv_create_cq(ctx, cqe_cnt, NULL, NULL, 0);
struct ibv_qp *qp = ibv_create_qp(pd, &qp_init_attr);

int ret = ibv_post_send(qp, &wr, &bad_wr);
int n = ibv_poll_cq(cq, 1, &wc);
```

这些函数都以 `ibv_` 开头，但执行路径并不相同。`ibv_reg_mr`、`ibv_create_cq`、`ibv_create_qp` 需要进入内核创建或配置设备资源；`ibv_post_send` 和 `ibv_poll_cq` 在资源建立以后通常走用户态快速路径。RDMA 的性能来自这种分工：低频控制路径交给内核保持安全和一致性，高频数据路径留在用户态减少系统调用和拷贝。

## 3.2.1 Verbs API 的分派

Linux RDMA 用户态库主要维护在 `linux-rdma/rdma-core` 项目中。公共库 `libibverbs` 提供统一 API，设备厂商的 provider 负责把这些 API 适配到具体硬件。应用链接的是 libibverbs，真正执行设备相关逻辑的是 provider。

在 `libibverbs` 中，`struct ibv_context` 持有一组操作函数指针。应用调用 `ibv_post_send(qp, wr, bad_wr)` 时，公共入口最终通过 `qp->context->ops.post_send` 跳转到当前设备的 provider。对于 mlx5 设备，这个函数由 mlx5 provider 提供；在 rdma-core 的 mlx5 路径中，操作表把 `.post_send` 绑定到 `mlx5_post_send`，随后进入 `providers/mlx5/qp.c` 中的发送投递逻辑。

这种结构类似一个统一接口下的设备后端。Verbs 规定应用应当看到什么语义，例如 WR 如何投递、WC 如何返回、MR 权限如何检查；provider 决定这些语义如何编码为设备能够消费的格式。对 mlx5 来说，provider 要把 `ibv_send_wr` 转换成 mlx5 WQE，把 completion queue 中的 mlx5 CQE 转换为 `ibv_wc`。

```text
应用程序
  |
  | ibv_post_send / ibv_poll_cq / ibv_reg_mr
  v
libibverbs
  |
  | context->ops 分派
  v
mlx5 provider
  |
  | 用户态快速路径：WQE、CQE、doorbell
  | 控制路径：通过 uverbs 进入内核
  v
mlx5_ib / mlx5_core
  |
  v
mlx5 HCA
```

## 3.2.2 控制路径

控制路径负责建立设备可以信任的状态。`ibv_open_device` 打开 uverbs 设备文件，并建立与内核 RDMA 子系统的上下文关系。`ibv_alloc_pd` 创建 Protection Domain，用于把 QP、MR 等对象放在同一个保护边界内。`ibv_reg_mr` 需要检查用户地址、固定页或建立按需分页机制、建立 DMA 映射，并在设备侧创建 memory key。`ibv_create_cq` 和 `ibv_create_qp` 需要分配队列内存、创建设备对象、设置初始状态，并把必要区域映射回用户态。

这些动作都不能只靠用户态完成。用户进程不能自行固定任意物理页，不能直接给设备分配 QP number，不能绕过权限检查创建可被远端访问的 key，也不能随意映射设备 MMIO 区域。内核 RDMA 子系统承担公共检查和对象生命周期管理，mlx5_ib 把这些请求转换为 mlx5 设备命令，mlx5_core 负责更底层的设备、固件和 PCIe 管理。

在 mlx5 QP 创建过程中，内核侧会建立 QP context，记录 QP number、队列规模、状态机参数、UAR page 等信息；MR 创建过程中，内核侧会建立 MKey，记录地址范围、访问权限、页表或 translation table；CQ 创建过程中，设备获得可写回完成记录的队列状态。控制路径完成以后，用户态 provider 才拥有后续快速路径所需的映射和句柄。

## 3.2.3 数据路径

数据路径从已经创建好的对象出发。发送方向上，provider 在用户态 Send Queue 中填写 WQE，更新 doorbell record，然后向 UAR/BF register 做一次 MMIO 写，通知设备有新工作。完成方向上，设备把 CQE 写入 Completion Queue，provider 在用户态轮询 CQE ring，并把设备格式转换为 `struct ibv_wc`。

这里没有传统 socket 发送路径中的内核协议栈处理，也没有把应用数据复制到内核网络缓冲区的步骤。网卡按 WQE 中的 `lkey`、地址和长度访问本地 MR，再按操作类型生成 RDMA 报文；远端网卡按 `rkey`、远端地址和权限状态校验访问，并把数据 DMA 到目标内存。

用户态快速路径仍然受内核建立的状态约束。QP 必须处于允许发送的状态，SQ 不能溢出，MR 的 key 必须有效，UAR 映射必须属于当前进程，CQ 必须有足够空间接收 completion。provider 可以直接写队列和 doorbell，不等于可以绕过这些检查；很多检查在资源创建、状态转换或设备执行时已经固化在 QP context、MKey 和 CQ context 中。

## 3.2.4 Provider 与内核驱动的关系

mlx5 provider 和 mlx5_ib 不是同一个组件。mlx5 provider 是用户态库的一部分，随应用进程运行；mlx5_ib 是内核驱动，运行在内核空间。两者通过 uverbs ABI 协作。创建对象时，provider 把参数交给内核；内核返回对象句柄、队列布局、mmap 偏移和设备能力；随后 provider 使用这些信息在用户态执行投递和轮询。

`mlx5_core` 位于更底层，它服务的不只是 RDMA。Ethernet、RoCE、设备初始化、固件命令、PCIe 资源、中断和事件管理都可能经过 mlx5_core。mlx5_ib 把 RDMA 语义翻译为 mlx5_core 能够提交给设备的命令，mlx5 provider 则把用户态 Verbs 语义翻译为 mlx5 队列格式。三者分别处在用户态库、内核 RDMA 驱动和设备核心驱动的位置。

这种分层说明了可移植性边界。应用依赖 `ibv_post_send`、`ibv_poll_cq`、`ibv_reg_mr` 等 Verbs 语义，通常可以在不同 RNIC 之间迁移；应用若依赖 mlx5 direct verbs、BlueFlame 行为、特定 CQE 字段或 vendor error，则已经进入 mlx5 实现边界。后一类信息对性能调优和诊断很有价值，但不能当作所有 RDMA 设备的共同规则。

## 3.2.5 UAR 与 mmap

UAR 是 User Access Region，即用户态可映射的设备访问区域。mlx5 provider 之所以能够在用户态敲 doorbell，是因为内核在 context 和队列创建阶段已经分配并映射了相应 UAR 页面。应用进程看到的是一段可写地址；从系统角度看，这段地址对应设备 MMIO 窗口，而不是普通 DRAM。

UAR 映射解释了用户态快速路径为何能够成立，也解释了它的边界。用户进程不能任意写设备寄存器；它只能写内核分配给该进程、并与设备对象匹配的 UAR。驱动在创建对象和建立映射时完成权限控制，设备在执行时仍会用 QP context、MKey 和 UAR 关联关系检查请求是否有效。doorbell record、BlueFlame 和发布顺序属于队列执行细节，放在 3.4 统一讨论。

## 3.2.6 小结

RDMA 的“绕过内核”是一个受限命题。WR 投递、CQ 轮询和数据搬运构成用户态快速路径，避免了传统内核网络协议栈和内核缓冲区拷贝；资源创建、内存注册、队列映射、权限控制、状态转换和事件上报仍属于控制路径，需要内核参与。

`libibverbs` 给出统一 API，provider 把 API 转换为设备格式，mlx5_ib 管理 RDMA 对象，mlx5_core 管理更底层的 mlx5 设备。理解这个分工以后，许多现象就有了明确位置：`ibv_post_send` 快，是因为它使用已映射的队列和 UAR；`ibv_reg_mr` 慢，是因为它必须建立设备可验证的内存对象；错误 completion 能返回到用户态，是因为设备执行结果最终写入 CQE，再由 provider 转换为 WC。

## 延伸阅读

- [rdma-core 源码结构](https://github.com/linux-rdma/rdma-core)：`libibverbs/`（公共库）、`providers/mlx5/`（mlx5 用户态 provider）、`kernel-headers/`（uverbs ABI 定义）。
- Linux 内核源码 `drivers/infiniband/core/uverbs_*.c` 与 `drivers/infiniband/hw/mlx5/`：uverbs 处理与 `mlx5_ib` 驱动实现。
- [Linux 内核文档：RDMA subsystem](https://docs.kernel.org/infiniband/)：用户态与内核态 RDMA 接口的总体说明。
- [RDMA Aware Programming 指南](https://github.com/linux-rdma/rdma-core/tree/master/Documentation)（rdma-core 文档）：介绍 Verbs 编程模型与常见错误。
