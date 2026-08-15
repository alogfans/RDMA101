# 3.9 诊断

RDMA 故障通常不会直接告诉应用“问题在第几层”。程序看到的可能只是 `ibv_post_send` 返回错误、`ibv_poll_cq` 得到非成功 WC、吞吐低于预期、尾延迟抖动，或者进程收到了异步事件。诊断的任务，是把这些现象放回前面几章建立的路径：API 参数、Verbs 对象、设备队列、内存注册、网络、PCIe/NUMA、GPU 和硬件计数器。

最有效的排查顺序，是先确认基础对象存在，再看 completion 状态，然后沿数据路径向下展开。不要一开始就猜交换机或驱动；也不要只盯着程序日志而忽略端口、GID、MTU 和计数器。

## 3.9.1 设备、端口与 GID

第一层检查是设备是否存在、端口是否 ACTIVE、RoCE GID 是否符合预期。设备不存在通常指向驱动、固件、PCIe 枚举或权限问题；端口不是 ACTIVE 通常指向线缆、光模块、交换机端口或链路配置；GID 错误则常见于 RoCE 连接失败和 timeout。

```bash
ibv_devices
ibv_devinfo -v
rdma link show
ip addr show
ip route get <remote-ip>
```

RoCE 下必须把 GID index、netdev、IP 地址和路由放在一起看。某个 GID 条目存在，不表示它就是当前连接应使用的地址；某个 netdev 有 IP，也不表示 QP 选择了对应 GID。连接建立代码中使用的 `sgid_index` 应与实际网络出口一致，远端 GID 应与对端接口地址和 RoCE 类型匹配。

MTU 也属于这一层的基本检查。QP path MTU、netdev MTU 和交换机端口 MTU 需要端到端一致。MTU 不一致时，程序可能表现为连接超时、重试增加或吞吐异常，而不是返回一个直接写着 “MTU mismatch” 的错误。

## 3.9.2 Completion Status

WC status 是程序层最重要的诊断入口。成功 completion 说明某条 WR 达到 Verbs 定义的完成边界；非成功 completion 则要根据状态判断可能层次。

```c
struct ibv_wc wc;
int n = ibv_poll_cq(cq, 1, &wc);

if (n > 0 && wc.status != IBV_WC_SUCCESS) {
    fprintf(stderr, "status=%s vendor_err=0x%x wr_id=%llu\n",
            ibv_wc_status_str(wc.status),
            wc.vendor_err,
            (unsigned long long)wc.wr_id);
}
```

本地长度或保护错误优先检查 SGE、`lkey`、地址范围、MR 权限和 QP 状态。远端访问错误优先检查对端交换来的 `remote_addr`、`rkey`、MR access flags 和 MR 生命周期。RNR retry exhausted 优先检查 SEND/RECV 接收端是否预投递足够 Receive WQE。Retry exceeded 或 response timeout 则通常要进入网络、远端 QP 状态、路径 MTU、端口状态和拥塞计数器检查。

`vendor_err` 不具备跨厂商可移植语义，但在 mlx5 诊断中很有价值。它通常需要结合设备手册、驱动日志或厂商工具解释。程序日志至少应记录 `wr_id`、opcode、QP 标识、WC status 和 vendor error，否则故障发生后很难把 completion 对回原始请求。

## 3.9.3 异步事件

Completion 记录单条或一组 WR 的结果，异步事件记录对象或设备状态变化。QP fatal、CQ error、port error、device fatal 等事件往往意味着普通轮询路径之外发生了状态变化。稳健程序应当创建并处理 async event channel，至少要记录事件类型和相关对象。

```c
struct ibv_async_event event;

if (ibv_get_async_event(ctx, &event) == 0) {
    fprintf(stderr, "async event: %s\n",
            ibv_event_type_str(event.event_type));
    ibv_ack_async_event(&event);
}
```

收到 QP fatal 后，继续把该 QP 当作正常连接使用通常没有意义。收到 CQ error 后，相关 CQ 上的 completion 可靠性可能已经受损。收到 port error 后，应检查链路、交换机、光模块和 RoCE netdev。异步事件的处理策略属于连接生命周期的一部分，而不是附加日志。

## 3.9.4 计数器与系统日志

计数器把设备和网络层的状态暴露出来。主机侧常用入口包括 sysfs RDMA counters、`ethtool -S`、`rdma` 命令和 `dmesg`。交换机侧需要查看端口丢包、PFC pause、ECN mark、buffer 水位、QoS 队列和错误帧。

```bash
cat /sys/class/infiniband/mlx5_0/ports/1/counters/*
ethtool -S ens3f0 | grep -E 'pause|ecn|drop|discard|error|crc'
dmesg | grep -iE 'mlx5|rdma|infiniband'
```

pause frame 增长说明 PFC 正在介入；ECN mark 增长说明交换机队列达到拥塞标记阈值；CRC 或 symbol error 指向物理层；discard/drop 需要结合交换机策略和队列水位判断。若 completion 显示 retry exceeded，而端口计数器同时出现丢包或链路错误，问题多半不在 WR 参数本身。若计数器干净但 RNR 错误频繁出现，则更可能是接收端队列管理不足。

## 3.9.5 PCIe、NUMA 与 GPU 路径

性能问题需要检查主机拓扑。功能正确但吞吐低，常见原因包括 PCIe negotiated width/speed 低于预期、RNIC 与 buffer 所在 NUMA 节点不一致、poller 线程跨 socket、GPU 与 NIC 路径过远、或者 IOMMU 配置带来额外转换成本。

```bash
lspci -tv
lspci -vv -s <pci-bdf> | grep -E 'LnkCap|LnkSta|Speed|Width'
cat /sys/class/infiniband/mlx5_0/device/numa_node
numactl -H
nvidia-smi topo -m
```

GPU-RDMA 场景还要确认 direct path 是否真的可用。GPU memory 注册失败、容器权限不足、peer-memory 模块缺失、dma-buf 路径不匹配、GPU-NIC 跨 socket，都可能让程序退化为 host staging 或表现出异常低带宽。若数据正确性异常，则回到同步边界：GPU 写后 RNIC 读、RNIC 写后 GPU 读，都需要相应的 CUDA 或驱动同步。

## 3.9.6 分层定位

一个可操作的诊断路径可以按以下顺序展开。先在程序层确认 WR 参数、`wr_id`、SGE、opcode 和 completion 处理；再在 Verbs 层确认 MR 权限、QP 状态、CQ 容量和 RQ 水位；随后检查 RoCE 的 GID、路由、MTU、PFC、ECN 和端口计数器；性能问题继续检查 PCIe、NUMA、IOMMU 和 GPU-NIC 拓扑；仍无法解释时，再结合 `dmesg`、固件版本、vendor error 和厂商工具进入硬件层。

这个顺序不是固定仪式，而是为了避免遗漏。RDMA 的现象常常跨层传播：接收端 RQ 没补足会在发送端表现为 RNR；交换机拥塞会在应用层表现为 retry exhausted；NUMA 错配不会造成错误 completion，却会让吞吐达不到预期；GPU 同步缺失不会改变 RDMA completion 成功状态，却可能让 GPU kernel 读到旧数据。

## 3.9.7 小结

诊断 RDMA 程序要从 completion 和对象状态出发，逐层回到设备、内存、网络和拓扑。WC status 告诉程序哪类 WR 失败，异步事件告诉对象或端口发生了什么变化，计数器和系统日志说明设备与网络是否出现拥塞、丢包或硬件错误，PCIe/NUMA/GPU 拓扑解释功能正确但性能不佳的情况。

第三篇建立的机制模型最终服务于这一点：把看似孤立的错误码、队列状态、网络计数器和拓扑信息放在同一条 RDMA 数据路径中理解。只有知道请求从哪里进入设备、在哪里访问内存、在哪里穿过网络、在哪里完成写回，才能把问题定位到真正的层次。

## 延伸阅读

- [rdma-core 工具](https://github.com/linux-rdma/rdma-core)：`ibv_devinfo`、`rdma link`、`ibv_*` 系列命令的用法。
- [NVIDIA 网卡诊断工具与文档](https://docs.nvidia.com/networking/)：`mft`、`mlxlink`、`mlxconfig` 等厂商工具的用法（用于固件、链路与计数器诊断）。
- Linux 内核文档与 [ethtool 手册](https://man7.org/linux/man-pages/man8/ethtool.8.html)：网卡计数器（pause、ECN、drop、CRC）的解释。
- 关于拥塞与性能诊断，可参考 DCQCN 论文（SIGCOMM 2015）与各厂商的 RoCE 运维指南。
