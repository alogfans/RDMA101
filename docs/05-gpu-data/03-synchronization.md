# 5.3 GPU 执行顺序与通信同步

得到正确 GPU 指针以后，还要知道数据何时已经生成。CUDA kernel 的提交通常是异步的，CPU 从启动调用返回时，GPU 可能尚未执行。此时让网卡读取同一块显存，就可能发送旧数据。

先把生产、传输和消费三段串行验证，再逐步重叠它们，更容易发现依赖缺口。

## stream 与 event 描述 GPU 工作的先后

stream 可以理解为 GPU 工作的一条有序提交序列。同一个 stream 中的工作按其规则执行，不同 stream 之间则要显式建立所需依赖。event 可以标记某条 stream 中已经提交到某个位置的工作。

以下 CUDA 片段只示意源端同步；`producer_stream`、`ready` 和生产 kernel 由调用方创建，实际代码必须检查 CUDA 返回值：

```cpp
produce<<<grid, block, 0, producer_stream>>>(gpu_buffer);
cudaEventRecord(ready, producer_stream);
cudaEventSynchronize(ready);
// 现在再按所用 GDR 注册和同步契约提交网卡读取。
```

`cudaEventSynchronize` 让 CPU 等待这个位置。它容易验证，但会阻塞主机。优化时可以让另一个 CUDA stream 使用 `cudaStreamWaitEvent` 建立 GPU 内部依赖；这不会自动让独立的网卡提交线程等待同一事件，网卡路径仍需由传输实现协调。[CUDA event 文档](https://docs.nvidia.com/cuda/cuda-runtime-api/cuda_runtime_api/group__CUDART__EVENT.html)说明事件记录与等待的范围。

## 源端就绪与目标端可见 {#gpu-sync}

传输涉及两条依赖链：

```text
源端：生产 kernel 完成 → 源数据可供网卡读取 → 提交传输
目标：传输写入完成 → 接收端获知并建立可见性 → 启动消费 kernel
```

源端必须确保生产工作已经完成，并满足所用注册机制对 CUDA 操作的同步要求。目标端必须确认数据已到，再按平台和接口保证安排 GPU 消费。发起方某处出现了成功完成，并不会让另一端已经运行的 kernel 自动获得正确的数据顺序。

对于 GPUDirect RDMA 写入，具体可见性要求依设备能力和作用范围而定。支持时可查询原生写入顺序能力与 flush 能力，按需要使用 `cuFlushGPUDirectRDMAWrites` 等接口；不能给所有平台无条件套用同一个调用。该接口处理写入可见性，也不代替上层判断整次传输是否已结束。[CUDA 设备管理文档](https://docs.nvidia.com/cuda/cuda-driver-api/cuda_driver_api/group__CUDA__DEVICE.html)定义其范围。

## 先验证一次 GPU 到主机的拷贝

`examples/ai_data/gpu_streams.py` 用两个 stream 和两个 event 实现“生产—拷贝—CPU 校验”。每轮写入不同序号，等待拷贝 event 后才检查内容。它验证 CUDA 依赖，尚不包含网卡访问；完整运行条件与解释见[GPU 同步实验](../labs.md#gpu)。

## 引擎内部 stream 也需要参与依赖

假设框架在 stream A 生产 Tensor，传输库在 stream B 执行异步拷贝。仅因为 CPU 先调用生产、再调用传输，并不能推出 A 与 B 有所需的执行顺序。

接入接口应说明接收哪个 stream、是否记录或等待 event，以及完成何时允许调用方复用内存。默认 stream 的行为与配置有关，不能默认为它总会替所有线程和 stream 建立依赖。

Mooncake 的 [PR #3570](https://github.com/kvcache-ai/Mooncake/pull/3570)处理调用方 per-thread stream 与内部拷贝 stream 的关系。学习这个案例时，先画出两个 stream，再找缺少的依赖边。

## 逐步加入计算通信重叠

准备两个独立槽位，可以在槽位 0 传输时，让 GPU 在槽位 1 产生下一块数据。每个槽位分别保存生产完成、传输完成和消费结束状态。

如果不同槽位实际是同一 storage 的重叠视图，就不能按两个独立缓冲区处理。第 5.2 章的布局检查因而是重叠实验的前提。

实验先使用明确同步，确认接收内容，再增加双缓冲，比较总耗时与各阶段时间。每轮填入不同序号或数据模式，能够发现读到上一轮内容的问题；只反复传同一个常量，可能掩盖时序错误。

## 退出时仍要等待设备引用

业务停止以后，GPU 工作、网卡请求和完成处理仍可能未结束。退出流程要先阻止新提交，再处理在途工作，确认相关 GPU 访问结束，最后注销和释放 allocation。

一次全局设备同步有时能帮助定位问题，但不是所有生产代码都应依赖的最终方案。清楚记录每个缓冲区由哪些 stream 和传输请求使用，才能逐步缩小同步范围。

## 参考资料

[NVIDIA 同步与内存顺序说明](https://docs.nvidia.com/cuda/gpudirect-rdma/index.html#synchronization-and-memory-ordering)解释外部 DMA 与 GPU 观察的约束；本章展示同步思路，完整 GDR 实验需按硬件和驱动能力验证。
