# 3.7 PCIe、NUMA 与主机拓扑

请求离开 CPU 以后，还要经过主机内部互联，才能到达网卡。即使网络端口很快，跨 NUMA 访问或共享 PCIe 上游链路仍可能限制吞吐。因此一次传输的路径要从源内存开始看，直到目标内存结束。

## 设备位置决定数据经过哪里

PCIe 将 CPU、网卡、GPU 等设备连接起来。设备可能挂在同一交换机下，也可能通过不同根端口访问。多个设备共享上游链路时，它们的端口速率不能简单相加。

```bash
lspci -t
readlink -f /sys/class/infiniband/mlx5_0/device
cat /sys/class/infiniband/mlx5_0/device/numa_node
```

第一条帮助观察 PCIe 树，后两条把 RDMA 设备对应到 PCI 地址与 NUMA 节点。`numa_node` 为 `-1` 表示没有提供有效亲和信息，不能直接当作节点 0。

## 线程位置与内存位置是两件事

NUMA 机器包含多个内存节点。CPU 访问本地节点和其他节点的路径不同，网卡 DMA 到不同内存节点也可能经过额外互联。

仅把 worker 线程绑到网卡附近，还不足以保证缓冲区在相同节点。内存分配、首次实际触页以及后续策略都会影响页面位置。观察时应同时记录线程亲和性与内存分布：

```bash
numactl --hardware
numastat -p 12345
```

`12345` 替换为测试进程 PID。实验可以用 `numactl --cpunodebind=0 --membind=0` 启动测试程序，再与另一内存节点比较；节点编号必须先从本机拓扑取得。绑定行为和权限受系统配置影响，应检查命令结果。

## CPU 缓存与 DMA

网卡通过 DMA 写入内存，CPU 仍需要按平台和编程接口的规则观察这些数据。常见服务器提供主机内存 DMA 一致性支持，但应用线程间的发布、设备完成与 GPU 消费是不同层面的同步。

例如一个线程处理 CQ 后将缓冲区交给另一个线程，仍需要 C/C++ 中正确的线程同步。对普通标志变量使用无同步的忙读，可能产生语言层面的数据竞争。`volatile` 不提供线程所有权转移或 GPU 执行依赖。

第五篇会在这条路径中加入显存和 CUDA stream。此处先保持主机内存实验，避免同时改变内存位置和执行模型。

## 做一组有解释力的对照

固定设备、消息大小、QP 数量和测试时长，先让线程与内存在网卡所在节点，再只改变内存位置。比较有效带宽、CPU 使用量和延迟；随后再单独改变线程位置。

如果一次把线程、内存、多网卡数量和消息大小都改变，即使带宽提高，也难以知道原因。拓扑图只是形成假设的依据，测量才说明当前负载是否真正受这条路径限制。

记录多网卡结果时还应观察：瓶颈是否转移到共享 PCIe 链路或内存带宽。单网卡已经接近某个共同上限时，增加第二张卡可能不会带来相同比例的提升。

第三篇至此把一次 WRITE 串过用户态、设备、网络和主机内存。下一篇从[性能测量](../04-optimization/01-measurement.md)开始，用这些知识解释优化结果。

## 参考资料

[Linux NUMA 内存策略](https://docs.kernel.org/admin-guide/mm/numa_memory_policy.html)解释分配策略；[Linux DMA API](https://docs.kernel.org/core-api/dma-api.html)解释设备访问规则。GPU 设备路径可继续阅读 [GPUDirect RDMA 官方文档](https://docs.nvidia.com/cuda/gpudirect-rdma/index.html)。
