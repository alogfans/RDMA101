# 配套实验与测试

正文里的代码片段用来解释一个概念，`examples/` 中的完整程序则负责把它变成可观察的结果。实验从单次 WRITE 开始，逐步增加在途请求、内存布局和设备之间的依赖。运行后既看完成状态，也检查接收内容。

所有命令从仓库根目录执行。C 示例需要 Linux、C 编译器、Make 和 libibverbs 开发文件；Python 示例使用 Python 3.9 或更新版本。PyTorch 和 Mooncake 是按实验选择的依赖，不随文档构建自动安装。

## 选择实验 {#choose}

| 实验 | 需要的环境 | 观察内容 | 对应正文 |
|---|---|---|---|
| 已有的单次 WRITE 与资源实验 | RDMA 设备或 RXE | 创建资源、连接、投递与完成 | [第一篇](01-introduction/02-environment-and-first-program.md)、[第二篇](02-programming-model/index.md) |
| [连续 WRITE 与批处理](#pipeline) | libibverbs，以及 RDMA 设备或 RXE | 槽位复用、在途上限、WR 链表、逐条数据校验 | [4.1](04-optimization/01-measurement.md)、[4.2](04-optimization/02-batching-pipeline.md) |
| [Tensor 布局](#layout) | Python 标准库；可选 CPU PyTorch | 转置、带间隔视图、打包与地址跨度 | [5.2](05-gpu-data/02-tensor-memory.md) |
| [GPU 执行依赖](#gpu) | CUDA 版 PyTorch、兼容 GPU 与驱动 | 生产 stream、拷贝 stream、event 与 CPU 消费 | [5.3](05-gpu-data/03-synchronization.md) |
| [TE 双进程传输](#te) | 包含 classic Python 接口与 TCP 后端的 Mooncake 包 | 注册、WRITE、READ、消费确认与注销 | [6.1](06-case-studies/01-transfer-engine.md) |

表 0-10：实验条件与正文的对应关系。
{: .table-caption }

GPU 实验验证 CUDA 拷贝依赖，完整 GPUDirect RDMA 的注册和可见性仍需另行验证。两个 RC QP 和两个 TE 进程的实验都在本机运行，跨机路径由第一篇的双端示例继续验证。

## 一次编译并检查

```bash
make -C examples check
```

该命令编译五个 C 程序，然后运行 CPU 断言、布局实验，以及按依赖可用性执行的 PyTorch、CUDA、TE 和 RC 传输检查。缺少设备或软件包时，相关实验打印 `SKIP` 并返回 77。数据不符、已选设备不可用、接口失败或超时则是失败，不能当作跳过。

最后一行分别统计 `passed`、`skipped` 和 `failed`。例如没有 RDMA 设备，也没有 PyTorch 与 Mooncake 时，结果是：

```text
RESULT: passed=2 skipped=5 failed=0
```

其中一项通过的是 CPU 断言组，另一项是标准库布局实验。这不表示五项跳过的传输测试已经通过。CPU 断言还会验证越界视图、过期轮次确认、控制消息超时和无效命令参数；它们不调用模拟网卡来代替真实传输。

在配置好的机器上，可以明确要求某类实验必须运行。例如：

```bash
make -C examples check CHECK_ARGS="--device mlx5_0 --gid-index 3 --require rdma"
make -C examples check CHECK_ARGS="--require torch --require gpu --require te"
```

`--require` 可重复使用。被要求的能力如果仍然跳过，测试入口会返回失败。设备名与 GID index 应根据[环境检查](01-introduction/02-environment-and-first-program.md#environment)选取，不能照抄另一台机器的编号。测试入口为子进程设置时间限制，并在超时时终止该实验的进程组。

## 连续 WRITE 与批处理 {#pipeline}

源码：[`examples/pipeline/write_pipeline.c`](https://github.com/alogfans/RDMA101/blob/main/examples/pipeline/write_pipeline.c)。程序在一个进程中建立两个 RC QP，分别注册源和目标内存池。每条 WRITE 仍由实际 RDMA 设备执行，没有 CPU 复制后端。

先使用单槽位、每次投递一条请求：

```bash
make -C examples/pipeline
./examples/pipeline/write_pipeline -d mlx5_0 -g 3 \
  --depth 1 --batch 1 --size 4096 --iterations 37
```

再增加槽位，但保持每次调用只投递一条：

```bash
./examples/pipeline/write_pipeline -d mlx5_0 -g 3 \
  --depth 7 --batch 1 --size 4096 --iterations 37
```

最后保持槽位数不变，将 WR 连成链表：

```bash
./examples/pipeline/write_pipeline -d mlx5_0 -g 3 \
  --depth 7 --batch 3 --size 4096 --iterations 37
```

这里故意选择 37 条请求，既会多次复用槽位，也会遇到最后不足一个 batch 的情况。三个参数各管一件事：`depth` 限制在途槽位，`batch` 限制一次调用提交的 WR 数量，`iterations` 决定总请求数。`size` 为每条 WRITE 的字节数，允许 8 字节到 1 MiB；每个注册池最多 64 MiB。

### 沿 run 函数理解一个槽位

每个槽位记录请求序号和是否正在使用。程序先按序号及字节位置生成数据，并把目标初始化成不同内容；随后填写 SGE 和 WR，将多个 WR 通过 `next` 连接，一次调用 `ibv_post_send`。

这里为每条 WRITE 设置 `IBV_SEND_SIGNALED`。轮询取得成功 WC 后，程序用 `wr_id` 找到槽位，逐字节检查目标，再将槽位归还。目标一旦校验完，这个单进程中的消费者就结束了；跨进程程序应使用通知与消费确认表达同样的条件。

成功时，固定的校验汇总应为：

```text
PASS: submitted=37 completed=37 verified=37
```

接下来的 `post_calls` 是投递调用次数，`peak_inflight` 是软件观察到的最大在途量，应不超过 `depth`。实际 batch 可能受空闲槽位限制而小于上限，所以不能简单用总请求数除以 batch 推算调用次数。

### 怎样解读时间与失败

计时区间包含填充、投递、完成处理和逐字节校验，输出名为 `verified_MiB_per_s`。它适合观察这份程序的总工作量，不代表网卡跨机带宽。真实链路测量仍使用[第四篇的测量方法](04-optimization/01-measurement.md)。

若投递只接受一部分，程序打印第一个被拒绝请求的标识，然后失败退出，不重放整个链表。若 CQ 长时间没有进展，`--timeout-ms` 触发失败退出。这个小程序没有实现进程内恢复，因此不会在超时后继续手工注销可能仍被设备访问的内存；它通过结束实验进程收尾。服务中的恢复需要[4.5](04-optimization/05-failure-recovery.md)介绍的额外机制。

可以先预测，再尝试 `--batch 8 --depth 7`：它应在打开设备前被拒绝。这个检查说明批量提交仍受到在途容量约束。

## Tensor 布局与打包 {#layout}

源码：[`examples/ai_data/tensor_layout.py`](https://github.com/alogfans/RDMA101/blob/main/examples/ai_data/tensor_layout.py)。先运行只依赖标准库的版本：

```bash
python3 examples/ai_data/tensor_layout.py
```

实验把 `[0, 1, 2, 3, 4, 5]` 看成连续的 2×3 数组，再按转置后的 shape 和 stride 读取。实际输出包括：

```text
raw storage:      [0, 1, 2, 3, 4, 5]
transpose packed: [0, 3, 1, 4, 2, 5]
slice offsets:    [1, 3, 5]
slice payload_bytes=24 address_span_bytes=40
```

`element_offsets` 根据 shape、stride 和 storage offset 找到逻辑元素的位置；`pack_view` 只把这些位置的值打包。为了明确字节表示，实验统一使用小端有符号 64 位整数。

切片 `[1, 3, 5]` 有三个元素，有效负载是 24 字节；从第一个元素地址到最后一个元素末尾，却跨过 40 字节。若只按有效负载长度复制首地址后的一段连续内存，就会夹入不需要的元素并漏掉需要的元素。

安装 CPU 版 PyTorch 后，可以检查真实 Tensor 是否与这个计算一致：

```bash
python3 examples/ai_data/tensor_layout.py --torch
```

附加检查比较转置视图、`contiguous()` 后的顺序，以及切片首地址和 `storage_offset()` 的关系。标准库函数只实现二维、非负 stride、非空视图，用来解释原理；它不是框架通用的 Tensor 序列化库。

## GPU 生产与拷贝之间的依赖 {#gpu}

源码：[`examples/ai_data/gpu_streams.py`](https://github.com/alogfans/RDMA101/blob/main/examples/ai_data/gpu_streams.py)。配置 CUDA 版 PyTorch 后运行：

```bash
python3 examples/ai_data/gpu_streams.py --iterations 10 --elements 4096
```

程序保留一个 GPU Tensor 和一个 pinned host Tensor。生产 stream 每轮将 GPU 数据填成新的序号，然后记录 `ready` event。拷贝 stream 等待这个 event，再异步复制到主机缓冲区并记录 `copied`。CPU 等待 `copied`，才检查主机内容。

```text
生产 stream：填充 GPU Tensor → ready
                                 ↓ 等待
拷贝 stream：                 GPU→Host → copied
                                           ↓ 等待
CPU：                                  校验，再开始下一轮
```

代码启动前还通过 `wait_stream` 接上默认 stream 的已有工作；Tensor 在全部使用结束前一直保留引用。这样既处理执行先后，也处理内存分配的寿命。[PyTorch CUDA 文档](https://docs.pytorch.org/docs/2.14/notes/cuda.html#cuda-streams)解释这些要求。

成功时应看到 `PASS: 10 GPU->pinned-host copies verified after event synchronization`。每轮等待是为了先建立明确的正确流程，没有声称已实现双缓冲或计算通信重叠。进一步优化时，可以增加独立槽位，但每个槽位仍要分别维护生产、搬运和消费结束条件。

这个实验没有网卡，不能用它证明外部 RDMA 写入已对 GPU 可见。两者的衔接见[5.3 源端就绪与目标端可见](05-gpu-data/03-synchronization.md#gpu-sync)。

## TE 双进程 WRITE 与 READ {#te}

源码：[`examples/transfer_engine/te_roundtrip.py`](https://github.com/alogfans/RDMA101/blob/main/examples/transfer_engine/te_roundtrip.py)。接口依据与 [6.1](06-case-studies/01-transfer-engine.md)相同。需要安装包含 `mooncake.engine.TransferEngine`、同步 WRITE/READ 和 TCP 后端的兼容 Mooncake 包，并记录所用版本。

```bash
python3 examples/transfer_engine/te_roundtrip.py --protocol tcp --iterations 3
```

脚本自动启动两个独立进程，各自创建引擎和 ctypes 缓冲区。目标进程把实际 RPC 端口与注册地址通过本机控制管道交给主进程，再由主进程交给发送进程；无须手工复制地址。

一轮操作依次是：发送端准备带轮次标识的数据，同步 WRITE 到目标，再同步 READ 回另一个本地缓冲区并校验。主进程收到成功消息以后，请目标进程检查它自己的内存。只有目标也确认后，才开始下一轮。

成功时应依次输出各轮的 `WRITE + READ-back + target verification OK`，最后得到：

```text
PASS: 3 rounds verified in two independent TE processes
```

这个顺序同时验证了 READ 本地缓冲区的方向和消费确认。目标没有因为发送端“提交过请求”就提前消费，也没有在校验完前把地址交给下一轮复用。

正常退出先确认没有后续访问，再注销。任何阶段失败时，整个隔离实验退出，不把超时当作已取消并立即释放 native 请求可能引用的内存。控制消息等待有时限；主进程负责终止未退出的子进程。这里展示可解释的实验收尾，不实现线上重连、去重或重放协议。

实验默认使用动态本地端口，不支持 `MC_LEGACY_RPC_PORT_BINDING`。若设置过该变量，应在这个实验的终端中取消设置。`requested_protocol` 仅打印请求的后端；实际传输方式仍需查看 TE 初始化日志和配置。

TCP 跑通后，具备本机 RDMA 条件时再单独尝试：

```bash
python3 examples/transfer_engine/te_roundtrip.py --protocol rdma --device mlx5_0
```

这个命令仍是本机双进程验证，不测试跨机路由。不要把 TE 的设备过滤参数与 Verbs 示例的 GID 参数混用。

## 保留实验记录

保留实际命令、软件版本、设备信息、全部完成与错误输出。RDMA 测试记录设备、端口、GID；GPU 测试记录 PyTorch、CUDA 和可见设备；TE 测试记录包版本及初始化日志中的后端。跳过的项目也保留在记录中。

本轮编写环境中，五个 C 程序编译通过，CPU 断言与标准库布局实验实跑通过；没有可见 RDMA 设备，也没有可导入的 PyTorch、Mooncake 包，因此其余运行项尚未验证。具备对应环境后，用 `--require` 明确补做这些检查。

清理编译产物：

```bash
make -C examples clean
```
