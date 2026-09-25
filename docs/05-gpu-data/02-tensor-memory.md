# 5.2 Tensor 布局与缓冲区管理

Tensor（张量）是框架中表示多维数组的对象。传输引擎看到的是地址区间，框架看到的是 Tensor。将两者连接起来，需要先判断一个逻辑 Tensor 的元素实际放在哪里。形状相同，并不保证内存排列相同。

## 从二维数组理解 stride

shape 表示各维度的大小，dtype 表示每个元素的类型，例如 64 位整数或半精度浮点数。stride 描述沿某个维度前进一步，需要在存储中跨过多少个元素。

假设一个连续的 2×3 整数 Tensor 按行存放：

```text
逻辑内容：       存储顺序：
0 1 2          0 1 2 3 4 5
3 4 5
```

访问下一行跨过 3 个元素，访问下一列跨过 1 个元素，stride 因而是 `(3, 1)`。转置后形状变成 3×2，stride 变成 `(1, 3)`，但底层存储可以完全不移动。

所以把转置 Tensor 的地址和 `numel * element_size` 交给字节传输，得到的仍可能是原始存储顺序，不能自动变成转置后的逻辑元素序列。

下面的 Python 实验只需要 CPU 版 PyTorch，观察布局不依赖 RDMA：

```python
import torch

base = torch.arange(6, dtype=torch.int64).reshape(2, 3)
view = base.t()
packed = view.contiguous()
for name, value in [("base", base), ("view", view), ("packed", packed)]:
    print(name, tuple(value.shape), value.stride(), value.is_contiguous())
    print(value)
```

先预测三个对象的 shape、stride 和连续性，再运行比较。`contiguous()` 在需要时生成连续排列，逻辑值相同不意味着没有发生复制。[PyTorch Tensor Views 文档](https://docs.pytorch.org/docs/2.14/tensor_view.html)说明相关行为。

## 地址、偏移和范围

Tensor 视图可能只覆盖底层 storage（存储空间）的一部分。`storage_offset` 描述首元素相对 storage 起点的元素偏移，`data_ptr()` 返回的首元素地址已经包含这个偏移，不应再重复加一次。注册可覆盖更大的 allocation，传输则应只访问本次允许的区间。

描述层需要保存 dtype、shape、stride 或约定的连续布局，数据层负责搬运相应字节。若双方约定传输连续表示，发送前可以打包，接收后按描述重建；若要避免打包，则需把有效数据转换为多个可传输区间。

| 方式 | 代价与约束 |
|---|---|
| 先打包连续数据 | 多一次复制，传输请求更简单 |
| 按连续区间分段 | 请求数增加，需要区间映射和批量管理 |
| 约定一致底层布局 | 搬运简单，但要求两端分配和解释规则兼容 |

表 5-1：Tensor 布局与字节传输的衔接方式。
{: .table-caption }

不能把 Tensor 元素数与地址跨度混为一谈；有间隔的视图可能跨过更大的存储范围，也可能与其他视图共享元素。

完整实验位于 `examples/ai_data/tensor_layout.py`。标准库模式即可观察转置与带间隔切片的字节差异，加上 `--torch` 则与真实 Tensor 对照；命令和预期输出见[布局实验](../labs.md#layout)。

## 分配器会改变注册条件

普通设备分配、内存池和 CUDA Virtual Memory Management（VMM）可能产生不同的底层映射方式。VMM 将虚拟地址预留、实际内存分配和映射分开，连续虚拟地址可以由多段映射组成。

导出的 dma-buf 范围必须覆盖实际要注册的地址区间。不能只查询指针所在的一段映射，就推断它覆盖整个 Tensor。Mooncake 的 [PR #3538](https://github.com/kvcache-ai/Mooncake/pull/3538)修复了这类范围问题，可在理解布局后对照阅读。

批量注册还需控制并发。每块缓冲区、每张 NIC 都创建注册工作时，初始化会消耗 CPU、线程和设备资源。优化应分别测启动成本与稳态带宽，不能只按“注册得越多，路径选择越自由”判断收益。

## Tensor、storage 与传输请求的寿命

多个 Tensor 视图可以共享 storage，传输需要保持真正的底层分配有效。Python 变量离开作用域、内存池重用、C++ guard 析构和设备工作结束，不一定发生在同一时刻。

因此接入层应建立这样的持有关系：请求引用 Tensor/storage 的所有者，注册引用有效 allocation，引擎在需要清理注册和 batch 时仍然存在。仅保存 `data_ptr()` 不能完成这些持有。

[PR #4087](https://github.com/kvcache-ai/Mooncake/pull/4087)中的 Python guard 对引擎引用，是观察跨语言生命周期的一个具体例子。它与 Tensor 布局一起说明，接入 TE 的工作不仅是把一个整数地址传进去。

下一章在已经找到正确内存的基础上，解释[何时可以读取和写入它](03-synchronization.md)。
