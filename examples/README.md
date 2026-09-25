# 教程配套实验

示例按教程顺序从一次 WRITE 扩展到流水线、Tensor 布局、GPU 同步和 TE 传输。完整的运行条件、预期结果、代码解释和实验变体见[配套实验与测试](../docs/labs.md)。

在仓库根目录一次编译并检查：

```bash
make -C examples check
```

C 程序需要 Linux、C 编译器、Make、libibverbs 头文件和链接库。标准库布局实验无需 GPU 或第三方 Python 包；其余实验根据设备与依赖运行，缺少时明确打印 `SKIP` 并返回 77。测试结果分别统计通过、跳过和失败。

- [one_sided_write](one_sided_write/one_sided_write.c)：已有的双端单次 WRITE。
- [programming_model](programming_model/README.md)：已有的设备查询、资源生命周期和 RC 操作比较。
- [pipeline/write_pipeline.c](pipeline/write_pipeline.c)：新增的连续 WRITE、链表批量提交和逐条内容校验。
- [ai_data/tensor_layout.py](ai_data/tensor_layout.py)：新增的转置、切片与打包实验，可选 `--torch` 对照真实 Tensor。
- [ai_data/gpu_streams.py](ai_data/gpu_streams.py)：新增的 CUDA stream/event 与 pinned host 拷贝实验。
- [transfer_engine/te_roundtrip.py](transfer_engine/te_roundtrip.py)：新增的双进程 TE WRITE、READ 回读和目标端确认。

硬件机器可以指定参数并要求实验必须执行：

```bash
make -C examples check CHECK_ARGS="--device mlx5_0 --gid-index 3 --require rdma"
make -C examples check CHECK_ARGS="--require torch --require gpu --require te"
```

`--require` 将对应项目的跳过视为失败，便于在具备环境的机器上验收。设备与 GID 取实际值。统一测试入口会限制单项运行时间，超时时终止该实验的进程组；它没有自动创建 RXE 或修改网络配置。

`tests/test_examples.py` 的 CPU 检查验证布局边界、控制消息的轮次与超时，以及 C 程序参数。它们不模拟 RDMA 成功；Verbs 的通过结果只能来自实际设备执行与内容校验。

清理本次编译生成的程序：

```bash
make -C examples clean
```
