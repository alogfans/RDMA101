# 2.2 Context 与 Protection Domain

范例在注册内存、创建队列前，先打开 RDMA 设备并分配 PD。本章沿这两步展开。它们把程序后续创建的资源放到正确的设备中，并限定哪些资源可以配合使用。

## 选择设备并打开 Context

Linux 中一台机器可能有多张 RDMA 网卡。`ibv_get_device_list` 返回当前进程可以使用的设备列表，应用从中选择一个，再用 `ibv_open_device` 得到 `ibv_context`。

下面是初始化片段，`device_name` 为已选定的名称，例如 `mlx5_0`，调用方负责处理错误返回：

```c
int count = 0;
struct ibv_device **devices = ibv_get_device_list(&count);
if (!devices) return -1;
struct ibv_context *ctx = NULL;
for (int i = 0; i < count; ++i) {
    if (strcmp(ibv_get_device_name(devices[i]), device_name) == 0) {
        ctx = ibv_open_device(devices[i]);
        break;
    }
}
ibv_free_device_list(devices);
if (!ctx) return -1;
```

设备列表用于发现和选择设备，Context 则要保留到依赖它的资源全部释放之后。打开设备成功后可以释放列表。没有设备与设备打开失败是不同情况，完整程序应分别记录，避免把容器映射、权限和驱动问题混在一起。

`mlx5_0` 是 Verbs 设备名，`eth0` 是网络接口名，两者不能互换。RoCE 环境下可以通过 `rdma link show` 查看设备端口与网络接口的对应关系。

## 查询能力，再选择配置

程序不能假定所有网卡都支持相同队列深度、SGE 数量和原子操作。`ibv_query_device` 查询设备能力；`ibv_query_port` 查询指定端口的当前状态。

```c
struct ibv_device_attr dev;
struct ibv_port_attr port;
int rc = ibv_query_device(ctx, &dev);
if (rc != 0) return -1;
rc = ibv_query_port(ctx, 1, &port);
if (rc != 0) return -1;
printf("ports=%u max_qp=%d max_cqe=%d\n",
       dev.phys_port_cnt, dev.max_qp, dev.max_cqe);
```

这些是局部片段，失败时的完整释放逻辑见配套程序。设备支持的上限只说明能力，不保证在任意资源占用下都能申请到对应数量。真正创建 QP 后，还要看创建接口返回的实际容量。

| 查询对象 | 初学时重点关注的字段 | 用途 |
|---|---|---|
| 设备 | `phys_port_cnt`、`max_qp_wr`、`max_sge`、`max_cqe` | 判断端口和队列能力 |
| 设备 | `atomic_cap` | 决定是否运行 Atomic 实验 |
| 端口 | `state`、`link_layer`、`active_mtu` | 确认端口可用和路径配置基础 |
| 端口地址 | LID、GID 表 | 建立到对端的地址路径 |

表 2-3：查询能力，再选择配置。
{: .table-caption }

端口状态会变化。启动时为 ACTIVE，不代表运行期间永远可用。第二篇末尾会介绍异步事件，第三篇再解释 IB 与 RoCE 地址的区别。

## PD 约束的是本地资源关系

PD 是 Protection Domain，通常译为保护域。应用调用 `ibv_alloc_pd(ctx)` 创建它，然后把同一个 `pd` 传给 QP 创建和 MR 注册。

```c
struct ibv_pd *pd = ibv_alloc_pd(ctx);
if (!pd) {
    ibv_close_device(ctx);
    return -1;
}
```

当 QP 使用某个本地 MR 的 `lkey` 时，设备会检查相关保护关系。这样可以发现把不属于这个资源组的内存描述用于请求的错误。

PD 是本地设备资源，不需要与另一台机器使用相同编号。两端各自管理自己的 PD，再通过 QP 连接和远端访问凭证协作。一个 QP 可以访问本 PD 中符合权限的多块 MR，一个 MR 也可以被本 PD 中多个 QP 使用。

最小程序采用一个 PD 已经足够。按模块拆成多个 PD 可以限制资源误用，但同进程代码仍共享进程地址空间，不能把这种分组当成进程或租户安全隔离的完整替代。

## 把创建和释放放在一起看

资源关系决定了释放顺序。只使用 Context 和 PD 时，先 `ibv_dealloc_pd`，再 `ibv_close_device`。加入 QP、MR、CQ 后，要先停止相关通信，消除在途引用，再释放这些资源；QP 引用的 CQ 和 PD 必须保留到 QP 销毁之后。

创建过程中途失败时，清理已经成功创建的资源即可。把句柄初始值设为 `NULL`，并在成功创建后更新，可以让清理函数知道哪些对象实际存在。释放接口也可能失败，不能无条件认定底层内存已经可复用。

配套实验将查询和生命周期拆为两份短程序：

```bash
make -C examples/programming_model
./examples/programming_model/01_device_info -d mlx5_0 -p 1
./examples/programming_model/02_resource_lifecycle -d mlx5_0 -p 1
```

第一份输出设备能力，第二份创建并销毁 PD、CQ、QP 和 MR，不执行传输。对照输出，先区分“设备允许的上限”和“本次创建返回的容量”。下一章再解释第二份程序中那块已注册内存的含义。

## 参考资料

[ibv_open_device](https://github.com/linux-rdma/rdma-core/blob/master/libibverbs/man/ibv_open_device.3)、[ibv_alloc_pd](https://github.com/linux-rdma/rdma-core/blob/master/libibverbs/man/ibv_alloc_pd.3)和[ibv_query_device](https://github.com/linux-rdma/rdma-core/blob/master/libibverbs/man/ibv_query_device.3)手册给出接口及能力字段定义。
