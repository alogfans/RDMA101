# 6.1 Mooncake Transfer Engine 的设计与使用

前五篇已经介绍地址、注册、请求、完成、并发和 GPU 路径。如果每个应用都直接使用 Verbs，就要重复管理这些机制。Transfer Engine（TE）提供统一的数据搬运接口，让应用描述要移动哪些区间，再由引擎安排底层传输。

本章以 Mooncake 源码快照 `5772765c5070cd44db78629864db3c00e5239b70` 为阅读依据，先使用 classic Python 接口解释一次 CPU buffer 传输，再对照 C++ 核心模型。TENT 的接口和恢复机制另行区分，不把两套默认行为混用。

## Segment 描述可访问的地址空间

一个 RAM Segment 在逻辑上代表某个引擎实例的内存地址空间，其中实际可访问的部分来自已注册的 buffer。这些 buffer 可以位于不连续的地址，也可能属于不同内存类型。

应用通过名字找到远端 Segment，通过地址指定其中的区域。C++ 请求中的 `target_offset` 对 RAM Segment 表示远端虚拟地址；对文件型 Segment 则具有相应文件偏移含义。字段叫 offset，不意味着所有情况都从某个 buffer 起点相对计数。

权限仍按注册与后端能力检查。一个 Segment 名字不代表应用可以访问对端全部进程内存。[TE 接口说明](https://github.com/kvcache-ai/Mooncake/blob/5772765c5070cd44db78629864db3c00e5239b70/docs/source/api-reference/cpp/transfer-engine.md)给出具体类型及方向。

## 先做一次 CPU buffer 传输

这个小实验使用同一台机器的两个 Python 交互终端，以 TCP 后端学习接口，因此不需要 RDMA 网卡。需要安装与上述接口兼容、包含 TCP 的 Mooncake Python 包，并记录包版本；关闭可能改变后端选择的自定义配置，确认初始化日志中的实际路径。

在两个终端执行 `python3`。先在目标端运行以下代码，并保持解释器存活：

```python
import ctypes
from mooncake.engine import TransferEngine

def check(code):
    if code != 0:
        raise RuntimeError(f"TE operation failed: {code}")

engine = TransferEngine()
check(engine.initialize("127.0.0.1", "P2PHANDSHAKE", "tcp", ""))
buffer = ctypes.create_string_buffer(4096)
address = ctypes.addressof(buffer)
check(engine.register_memory(address, ctypes.sizeof(buffer)))
print("target:", f"127.0.0.1:{engine.get_rpc_port()}")
print("address:", hex(address))
```

目标地址和名称由实验者在终端间传递，相当于最小的控制信息交换。真实应用通常通过自己的 RPC 交换，并增加长度、身份和版本校验。

在发起端运行：

```python
import ctypes
from mooncake.engine import TransferEngine

def check(code):
    if code != 0:
        raise RuntimeError(f"TE operation failed: {code}")

engine = TransferEngine()
check(engine.initialize("127.0.0.1", "P2PHANDSHAKE", "tcp", ""))
payload = b"hello transfer engine"
buffer = ctypes.create_string_buffer(payload, 4096)
address = ctypes.addressof(buffer)
check(engine.register_memory(address, ctypes.sizeof(buffer)))
target = input("target（复制目标端输出）: ").strip()
remote_address = int(input("address（复制目标端输出）: ").strip(), 0)
check(engine.transfer_sync_write(target, address, remote_address, len(payload)))
print("WRITE completed")
```

确认发起端输出 `WRITE completed` 后，在目标端执行 `print(buffer.value)`，正常结果是 `b'hello transfer engine'`。此处人工确认完成后再读目标，保留了第二篇讲过的通知顺序。

两个终端都确认没有后续请求后，各自执行：

```python
check(engine.unregister_memory(address))
del buffer
```

随后退出解释器。若任一步失败，就停止后续步骤，保留引擎和缓冲区调查状态；不能直接把上述成功清理步骤放进无条件 `finally`，假定失败或超时已终止所有访问。服务恢复应使用第四篇的资源管理方法。

换成两台机器时，实例名要使用各自可达的地址；换成 RDMA 还需设备、注册和网络能力验证。不要在一次实验中同时改变网络、内存类型和后端。上游的 [Python 接口](https://github.com/kvcache-ai/Mooncake/blob/5772765c5070cd44db78629864db3c00e5239b70/docs/source/api-reference/python/transfer-engine.md)及[测试示例](https://github.com/kvcache-ai/Mooncake/blob/5772765c5070cd44db78629864db3c00e5239b70/mooncake-wheel/tests/transfer_engine_initiator_test.py)可继续对照。

## 自动完成双进程校验

`examples/transfer_engine/te_roundtrip.py` 将前面的手工步骤整理为完整程序：启动两个独立引擎进程，交换地址，执行 WRITE，再 READ 回读，最后请目标进程核对内容。每轮目标确认后才允许复用缓冲区，并为控制步骤设置时间限制。[TE 配套实验](../labs.md#te)说明命令、预期结果和失败时的退出方式。

## 同步接口后面的异步请求

Python 的同步调用内部仍使用底层请求和状态推进。C++ 核心流程可以概括为下面的伪代码，所有返回值都需要在真实实现中检查：

```text
注册本地内存，打开目标 Segment
分配可容纳本次请求数的 BatchID
提交 READ/WRITE 请求集合
持续推进并查询结果，分别处理成功、失败与结果未知
确认允许回收后释放 BatchID、关闭引用并注销内存
```

`TransferRequest::source` 总是本地缓冲区：WRITE 时是源，READ 时是结果位置。远端由 `target_id` 和 `target_offset` 指定。一个 batch 收集多个请求，某个请求还可能被拆成多个 slice。

应用只看到逻辑请求，不代表单个成功提交已经完成了全部数据。一次查询也可能仍返回等待状态；获取状态的接口成功与请求本身成功要分别判断。

## 从请求追踪到 Transport

TE 根据地址范围识别内存位置和可用路径，再选择 Transport。RDMA 后端管理连接、NIC 与切片；其他后端使用各自的复制或传输机制。拓扑选择因此依赖源和目标两端信息。

阅读源码可以按这个顺序：公共请求定义、Segment 解析、Transport 选择、请求拆分、实际投递、状态汇总、资源回收。每一步都能对应前文的一项知识，暂时不必把所有后端实现同时读完。

[TE 设计文档](https://github.com/kvcache-ai/Mooncake/blob/5772765c5070cd44db78629864db3c00e5239b70/docs/source/design/transfer-engine/index.md)解释这些抽象，[TENT 概览](https://github.com/kvcache-ai/Mooncake/blob/5772765c5070cd44db78629864db3c00e5239b70/docs/source/design/tent/overview.md)展示进一步的运行时路径选择与调度设计。能力和恢复保证需要按版本、编译选项与执行路径核对。

## 应用仍负责什么

TE 搬运字节，不自动知道这些字节属于哪个推理请求、哪一版权重或什么 Tensor 布局。目标分配、数据描述、消费依赖与业务发布仍要由接入层安排。

下一章将缓冲区替换成 KV blocks，观察 [PD 分离](02-pd-disaggregation.md)怎样组织这些工作。
