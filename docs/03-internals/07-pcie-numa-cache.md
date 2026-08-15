# 3.7 主机拓扑

RDMA 请求由网卡执行，但数据仍要穿过主机。网卡通过 PCIe 访问内存，CPU 通过 NUMA 互联访问本地或远端内存。API 层的 `ibv_post_send` 看不见这些拓扑差异，延迟和带宽却会清楚地反映出来。

```text
CPU Socket 0                  CPU Socket 1
  Memory Node 0                 Memory Node 1
      |                             |
  Root Complex                 Root Complex
      |                             |
   PCIe Switch                  PCIe Switch
      |                             |
   mlx5_0                         mlx5_1
```

同一条 RDMA WRITE，在拓扑良好的机器上可能只是 NIC 从本地 NUMA 内存读数据并发包；在拓扑不佳的机器上，NIC 可能跨 socket 读远端内存。机制相同，代价不同。

## 3.7.1 PCIe 路径

mlx5 网卡通过 PCIe 与主机通信。WQE 读取、CQE 写回、host memory DMA、doorbell MMIO 都会经过 PCIe 或与 PCIe 相关的路径。PCIe 代际和 lane 宽度决定理论上限，拓扑层级决定访问需要经过哪些 switch、root complex 或 socket 间互联。

PCIe Gen4 x16 的理论双向能力高于 Gen3 x16，Gen5 x16 又高于 Gen4 x16；但 RDMA 程序看到的是有效吞吐，不是规格表数字。有效吞吐受 payload size、DMA 方向、IOMMU、NUMA、设备调度、PCIe switch 和同时竞争的设备影响。诊断性能时，`lspci -vv` 中的 negotiated speed 和 width 比设备宣传能力更重要，因为设备可能插在低速槽位，或被 BIOS/主板限制到较低宽度。

```bash
lspci -tv
lspci -vv -s 87:00.0 | grep -E 'LnkCap|LnkSta|Speed|Width'
```

Doorbell 写入也是 PCIe 事务。普通内存写可以被 CPU cache 和内存系统吸收，MMIO 写则面向设备寄存器窗口，延迟和排序规则不同。mlx5 的 BlueFlame 优化正是围绕小 WQE 的 MMIO 投递成本设计的。

## 3.7.2 NUMA locality

NUMA 系统中，每个 CPU socket 通常连接一组本地内存和一部分 PCIe 设备。CPU 访问本地内存延迟低、带宽高；访问另一个 socket 的内存需要经过 socket 间互联。RNIC DMA 访问内存也会受到这种拓扑影响。

如果 `mlx5_0` 属于 NUMA node 1，而应用线程运行在 node 0，buffer 又分配在 node 0，那么一次发送可能包含三段不理想路径：线程跨节点写控制数据，NIC 跨节点读 payload，completion 又跨节点被 poller 读取。单次延迟可能只是增加一小段，但在高 QPS 或大带宽场景中会累积成明显差距。

```bash
cat /sys/class/infiniband/mlx5_0/device/numa_node
numactl -H
```

优化原则是让 poller 线程、发送线程、RDMA buffer 和 RNIC 尽量位于同一 NUMA 节点。常见做法包括线程绑核、按 NIC 所在节点分配内存、每个 NUMA 节点使用独立 QP/CQ、避免多个 socket 共享同一个高频 CQ。这样的优化不改变 Verbs 语义，却常常决定实际性能上限。

## 3.7.3 DMA 与 CPU 缓存

在主流服务器平台上，普通 host memory 的 DMA 与 CPU cache 通常由平台一致性机制和内核 DMA API 共同保证。应用不需要在每次 RDMA 操作前手工刷新 cache，也不应把 `clflush` 当作通用 RDMA 编程步骤。更重要的边界是完成顺序和内存可见性：什么时候可以改写发送 buffer，什么时候可以读取接收 buffer，什么时候远端应用可以消费新数据。

发送方向上，CPU 在投递 WR 之前写好本地 buffer；provider 在发布 WQE 和 doorbell 前使用必要的内存屏障，保证设备不会先看到门铃却读不到完整 WQE。发送 completion 返回后，本地源 buffer 可以按该 WR 的生命周期规则复用。对于未 signaled WR，则需要通过后续 signaled WR 或其他队列进度判断资源是否可回收。

接收方向上，设备 DMA 写入本地 MR 后，completion 是应用读取该数据的重要边界。应用在看到相应 WC 之前，不应假设目标 buffer 已经包含完整新数据。若平台、内存类型或设备不具备一致 DMA 语义，例如某些非一致架构、特殊映射内存或 GPU 显存，还需要使用对应平台提供的同步机制。

## 3.7.4 小结

PCIe、NUMA 和缓存可见性构成 RDMA 的主机侧基础。Verbs API 隐藏了这些细节，但无法消除它们的代价。正确性主要依赖 MR 生命周期、设备 DMA 语义和 completion 边界；性能则取决于队列所在内存、线程所在 CPU、RNIC 所在 NUMA 节点和 PCIe 链路能力。

IOMMU 与地址转换成本属于 MR 机制，见 3.3；GPU-NIC 拓扑和显存可见性属于 GPUDirect RDMA 路径，见 3.8。
