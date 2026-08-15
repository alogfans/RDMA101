# 3.3 内存注册

普通用户态内存不能直接交给 RDMA 网卡使用。CPU 访问内存时使用虚拟地址，由 MMU 和页表完成地址转换；网卡执行 DMA 时需要设备可见的地址和稳定的访问权限。`ibv_reg_mr` 的作用，就是把一段应用内存转换为设备可以验证、可以寻址、可以 DMA 访问的 memory region。

```c
void *buf = malloc(size);

struct ibv_mr *mr = ibv_reg_mr(pd, buf, size,
    IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);

struct ibv_sge sge = {
    .addr = (uintptr_t)buf,
    .length = size,
    .lkey = mr->lkey,
};
```

代码表面上只是把 `buf` 注册成 MR，并取得 `lkey` 和 `rkey`。在系统内部，内核和设备需要解决三个问题：这段虚拟地址背后的物理页是否稳定，设备应当用什么地址访问这些页，哪些 QP 可以按什么权限访问它们。

## 3.3.1 CPU 地址与设备地址

应用中的 `buf` 是进程虚拟地址。它可以来自 heap、stack、`mmap` 或共享内存。CPU 使用它时，页表把虚拟地址翻译为物理页；操作系统仍然可以换页、复制页、迁移页或改变映射。设备 DMA 不能依赖这种随时可能变化的状态。

RDMA 设备需要的是设备地址空间中的可访问范围。在没有 IOMMU 的系统上，这个地址空间可能接近物理地址；在启用 IOMMU 的系统上，设备看到的是 DMA 地址，IOMMU 再把 DMA 地址翻译到主机物理地址。无论是哪种形式，设备执行 DMA 时都必须保证目标页存在、权限正确，并且在 MR 生命周期内不会被普通内存管理破坏。

```text
应用虚拟地址
    |
    | CPU 页表
    v
主机物理页
    |
    | DMA mapping / IOMMU
    v
设备可见 DMA 地址
    |
    | MKey 权限和地址转换
    v
RNIC DMA 访问
```

内存注册把这几层关系固定下来。对应用来说，注册后的地址仍是原来的虚拟地址；对设备来说，它获得了一份可校验的地址转换和权限描述。`lkey` 与 `rkey` 正是访问这份描述时使用的 key。

## 3.3.2 注册过程

传统 MR 注册通常包含页固定、DMA 映射和设备侧 MKey 创建。页固定防止底层物理页在设备 DMA 期间被换出或重新分配；DMA mapping API 建立设备可见地址；mlx5 驱动再通过设备命令创建 MKey，把起始地址、长度、页大小、访问权限、PD 关联和 translation table 等状态交给设备。

```text
ibv_reg_mr(pd, addr, length, access)
        |
        v
内核验证地址、权限和 PD
        |
        v
固定页或建立 ODP fault 路径
        |
        v
建立 DMA / IOMMU 映射
        |
        v
创建设备侧 MKey
        |
        v
返回包含 lkey/rkey 的 ibv_mr
```

在 mlx5 内核路径中，MKey 创建最终对应设备命令。用户态看到的 `mr->lkey` 和 `mr->rkey` 不是随机标签，而是设备侧 memory key 的用户态句柄。设备执行 WQE 时，会用 WQE 数据段中的 `lkey` 校验本地内存访问；远端 RDMA READ 或 WRITE 到达时，会用报文中的 `rkey` 校验远端访问。

访问权限在注册时写入 MR。没有 `IBV_ACCESS_REMOTE_WRITE` 的 MR 不能作为 RDMA WRITE 的远端目标；没有本地写权限的缓冲区不能用于需要设备写入本地内存的操作。地址、长度、key 和权限必须同时匹配，远端只知道一个 `rkey` 并不足以越界访问任意内存。

## 3.3.3 MKey、lkey 与 rkey

MKey 是 mlx5 设备侧的内存访问对象。可以把它理解为设备能够查询的一条内存描述记录：这段内存属于哪个 PD，覆盖哪个地址范围，允许哪些访问，地址转换表在哪里，当前 key 是否有效。

```c
struct mlx5_mkey_context_simplified {
    uint32_t pd;
    uint64_t start_addr;
    uint64_t length;
    uint32_t access_flags;
    uint32_t page_size;
    uint64_t translation_table;
};
```

`lkey` 用在本端设备访问本地 MR 的场景。例如 RDMA WRITE 的发起方需要从本地 SGE 读取数据，数据段中的 `lkey` 用于校验本地读访问；RDMA READ 的发起方需要把远端数据写入本地缓冲区，本地 SGE 的 `lkey` 用于校验本地写访问。`rkey` 用在远端设备访问该 MR 的场景。远端执行 RDMA WRITE 时，报文中的 `rkey` 与 `remote_addr` 一起交给目标设备校验。

PD 提供保护边界。QP 和 MR 属于 PD；设备校验时不仅看 key，也看对象之间是否处在允许的保护关系中。正因为有 PD 和 key，远端地址才不是裸地址。RDMA 程序需要交换 `remote_addr` 和 `rkey`，但这两个值只对相应 MR、权限和生命周期有效。

## 3.3.4 IOMMU 与地址转换成本

IOMMU 位于设备和主机内存之间，为 DMA 提供地址转换和隔离。启用 IOMMU 后，RNIC 发出的 DMA 地址先经过 IOMMU，再到达主机物理页。虚拟化、安全隔离和设备直通通常依赖 IOMMU；高性能场景则需要关注它带来的地址转换开销。

IOMMU 的代价主要来自转换缓存。设备或 IOMMU 可能缓存近期转换结果；缓存未命中时，需要查找转换表。注册大量小 MR、使用高度分散的页、或者工作集超过 IOTLB 容量时，转换开销可能进入数据路径。大页、较少的注册区域和合理的 MR cache 往往能改善这种情况。

ATS（Address Translation Services）允许支持该能力的 PCIe 设备参与地址转换缓存，减少部分 IOMMU 查找开销。ATS 是否可用取决于 CPU 平台、IOMMU、PCIe 拓扑、设备和内核配置。应用程序通常不直接操作 ATS，但在分析高端平台 RDMA 性能时，需要知道它可能改变 DMA 地址转换的成本。

## 3.3.5 ODP 与传统 MR

传统 MR 在注册时尽量把所需页准备好：页被固定，DMA 映射建立，设备侧 translation table 可用于后续访问。这种方式执行时稳定，但注册成本高，注册很大的地址范围会占用大量固定页并增加系统内存压力。

ODP（On-Demand Paging）把部分成本推迟到访问时。ODP MR 注册时建立的是按需分页关系；设备首次访问尚未就绪的页时触发 fault，内核取页并补齐映射，然后设备继续执行。ODP 适合地址范围很大但访问稀疏的场景，也常与高级内存管理和 GPU 场景一起讨论。

ODP 不是免费的优化。首次访问可能出现 fault 延迟，访问模式不稳定时尾延迟会变差；设备、驱动和内核都必须支持相应能力。传统 MR 与 ODP 的选择，应当由内存规模、访问密度、延迟要求和平台能力决定。

## 3.3.6 注册成本与生命周期

内存注册是相对重的操作。它涉及内核页管理、DMA mapping、设备命令和 key 状态维护。高性能 RDMA 程序通常不会为每条消息临时注册内存，而是预注册大块缓冲区，再在其中做自己的内存分配；或者维护 MR cache，复用近期已经注册过的区域。

MR cache 的难点在于生命周期和权限。缓存项必须覆盖正确的地址范围，权限必须满足后续操作，底层内存不能在 MR 仍有效时释放或重用为不兼容用途。注销 MR 以后，相关 `lkey` 和 `rkey` 失效；远端仍持有旧 `rkey` 时，再访问该地址应被视为协议错误。

MR 的生命周期还影响资源释放顺序。常规 host memory 路径中，应先确保所有使用该 MR 的 WR 已完成，再注销 MR，最后释放底层内存。GPU memory 路径中还要考虑 CUDA allocation 的生命周期，不能在 RNIC 仍可能访问该显存时调用 `cudaFree`。

## 3.3.7 小结

`ibv_reg_mr` 的本质是为设备建立一个可 DMA 访问、可权限校验、可在 MR 生命周期内保持稳定的内存对象。`lkey` 用于本端设备访问本地 MR，`rkey` 用于远端设备访问该 MR；二者背后都指向设备侧 memory key 状态。

页固定、DMA 映射、IOMMU 转换和 MKey 创建共同构成注册成本。传统 MR 把成本放在注册阶段，ODP 把部分成本推迟到首次访问阶段。理解这些机制以后，注册慢、频繁注册性能差、`rkey` 越界访问报错、注销顺序错误导致异常等现象，都可以放回同一个模型中解释。

## 延伸阅读

- Linux 内核文档 [DMA API](https://docs.kernel.org/core-api/dma-api.html) 与 [DMA attributes](https://docs.kernel.org/core-api/dma-attributes.html)：DMA mapping 的语义与约束。
- Linux 内核源码 `drivers/infiniband/core/umem.c`（`ib_umem_get`）与 `drivers/infiniband/hw/mlx5/mr.c`：页固定与 mlx5 MKey 创建的实现。
- [rdma-core 手册页：ibv_reg_mr(3)](https://man.archlinux.org/man/extra/rdma-core/)：访问标志的合法组合。
- 关于 ODP 的能力与限制，见 Linux 内核文档 [On-Demand Paging](https://docs.kernel.org/infiniband/opd.html) 及所用网卡驱动手册。
