# 3.6 RoCE 网络路径

RoCE（RDMA over Converged Ethernet）把 RDMA 传输放到 Ethernet 网络中。Verbs 程序仍然使用 QP、MR、WR 和 CQ，不需要改成 socket API；但数据包离开网卡以后，要经过 Ethernet、IP、交换机队列、MTU、优先级、拥塞控制和路由配置。程序看到的一个 `IBV_WC_RETRY_EXC_ERR`，底层原因可能是 GID 选错、MTU 不一致、PFC 配置不当、交换机丢包或远端 QP 根本不可达。

InfiniBand 使用专门的 IB 网络和 LID 地址；RoCE 使用以太网承载 RDMA。RoCE v1 封装在 Ethernet Layer 2，不能跨三层路由；RoCE v2 封装在 UDP/IP 上，使用 UDP 4791 端口，能够通过三层网络转发。现代数据中心中更常见的是 RoCE v2。

```text
RoCE v2 packet

Ethernet header
IP header
UDP header
IB transport headers
RDMA payload
```

这个封装方式使 RDMA 与普通 Ethernet 流量共享链路，也使网络配置进入 RDMA 程序的可靠性和性能模型。

## 3.6.1 GID 与 GID Index

RoCE 使用 GID（Global Identifier）标识端点地址。GID 是 128 位地址；在 RoCE v2 中，它通常与 IPv6 地址或 IPv4-mapped IPv6 地址相关。一个 RDMA 端口可以有多个 GID 条目，对应不同 IP、VLAN、RoCE 类型或网络命名空间。

Linux 上的 RoCE GID 表不是由应用直接写入的配置表。它由 RDMA 端口、关联的 Ethernet netdev、IP 地址、VLAN 设备和 RoCE 类型共同形成。给某个 RoCE netdev 配置 IPv6 地址时，驱动会在对应 RDMA 端口的 GID 表中生成相应 GID；给 netdev 配置 IPv4 地址时，RoCE v2 通常以 IPv4-mapped IPv6 的形式表示，例如文档保留地址 `192.0.2.11` 对应的 GID 可表示为 `::ffff:192.0.2.11`。如果在同一物理口上创建 VLAN 子接口，并给 VLAN netdev 配置 IP，GID 表中还会出现与该 VLAN 相关的条目。

同一个 IP 地址在 GID 表中可能出现不止一次。mlx5 设备常见的输出形式是：同一个 link-local 地址分别有 RoCE v1 和 RoCE v2 条目，同一个 IPv4 地址也分别有 v1 和 v2 条目。IPv4 地址在 GID 中以十六进制形式出现，例如 `192.0.2.11` 对应 `0000:0000:0000:0000:0000:ffff:c000:020b`，其中 `c000:020b` 正是 IPv4 四个字节的十六进制表示。

```text
DEV     PORT  INDEX  GID                                      IPv4          VER  DEV
mlx5_0  1     0      fe80:0000:0000:0000:0000:00ff:fe00:0011                v1   rdma0
mlx5_0  1     1      fe80:0000:0000:0000:0000:00ff:fe00:0011                v2   rdma0
mlx5_0  1     2      0000:0000:0000:0000:0000:ffff:c000:020b  192.0.2.11   v1   rdma0
mlx5_0  1     3      0000:0000:0000:0000:0000:ffff:c000:020b  192.0.2.11   v2   rdma0
```

GID index 是 GID 表中的条目编号。QP 建立连接时需要指定本端 GID index，并取得远端 GID。这个编号不能只按经验填写，因为它决定了 RDMA packet 使用哪个源地址、哪个 netdev、哪种 RoCE 类型以及可能的 VLAN 关系。现代 RoCE v2 网络中，程序通常应选择与目标 IP 同一网络、与实际出接口一致、类型为 RoCE v2 的 GID 条目。若本端使用的 GID 条目与远端地址族、VLAN、路由或网卡端口不匹配，QP 即使状态转换成功，后续传输也可能超时。

```c
attr.ah_attr.is_global = 1;
attr.ah_attr.grh.dgid = remote_gid;
attr.ah_attr.grh.sgid_index = local_gid_index;
attr.ah_attr.grh.hop_limit = 255;
```

RoCE 程序中的“交换地址信息”因此不只是交换 QP number 和 rkey，还包括选择正确的 GID。使用 RDMA CM 建立连接时，内核通常会根据 IP 路由选择合适的 netdev 和 GID；使用 raw Verbs 手动修改 QP 状态时，程序需要自己填入 `dgid` 和 `sgid_index`。程序启动时可以用 `ibv_query_gid` 或 `ibv_query_gid_ex` 查询候选 GID，也可以通过 sysfs 查看每个 GID index 关联的 netdev 和类型。

## 3.6.2 配置与验证 GID

配置 GID 的正确方式，是配置对应的 Ethernet netdev，而不是直接修改 RDMA GID 表。物理口直连或普通二层网络中，先确认 RDMA 设备与 netdev 的对应关系，再给 netdev 配置 IP 地址。

```bash
rdma link show
ip link set rdma0 up
ip addr add 192.0.2.11/24 dev rdma0
ip route get 192.0.2.12
```

`rdma link show` 用于确认 `mlx5_0/1` 这类 RDMA 端口对应哪个 netdev。`ip addr add` 建立 IP 地址后，mlx5 驱动会更新该端口的 GID 表。`ip route get <remote-ip>` 用于确认访问远端 IP 时实际选择的出口和源地址；这个出口应当与程序准备使用的 GID index 关联。

完成 IP 配置后，可以用 `show_gids` 直接查看设备、端口、GID index、RoCE 版本和 netdev 的对应关系。没有该工具时，可用 sysfs 读取相同信息。

```bash
show_gids

cat /sys/class/infiniband/mlx5_0/ports/1/gids/3
cat /sys/class/infiniband/mlx5_0/ports/1/gid_attrs/types/3
cat /sys/class/infiniband/mlx5_0/ports/1/gid_attrs/ndevs/3
```

这三项应当同时匹配：`gids/3` 是程序要使用的本端 GID，`types/3` 应符合连接使用的 RoCE 类型，`ndevs/3` 应是路由远端地址时使用的 netdev。若使用 perftest 或示例程序指定 `--gid-index 3`，这个 `3` 就应来自上述检查，而不是来自另一台机器或另一个端口的经验值。

VLAN 场景下，应在 VLAN netdev 上配置地址。这样生成的 GID 才带有正确的 VLAN 关系，RDMA packet 才会进入期望的二层网络和 QoS 队列。

```bash
ip link add link rdma0 name rdma0.100 type vlan id 100
ip link set rdma0.100 up
ip addr add 198.51.100.11/24 dev rdma0.100
ip route get 198.51.100.12
```

RDMA CM 应用还涉及默认 RoCE mode。`cma_roce_mode` 可以查看或设置某个 RDMA 设备端口上 RDMA CM 应用使用的默认 RoCE v1/v2 模式。

```bash
cma_roce_mode -d mlx5_0 -p 1
cma_roce_mode -d mlx5_0 -p 1 -m 2
```

该命令在部分系统上需要 root 权限。这个设置作用于 RDMA CM 的地址解析和默认模式选择。raw Verbs 程序直接填写 `ah_attr.grh.sgid_index` 时，仍然以所选 GID index 的实际类型为准。因此，调试 raw Verbs 连接时，不能只看 `cma_roce_mode`，还要检查程序传入的 `sgid_index` 是否对应正确条目。

GID index 可能随着 IP 地址、VLAN、netdev 状态、网络命名空间或驱动重新加载而变化。生产程序不宜把某个 index 当成跨机器固定常量；更稳妥的方式是以配置中的本端 IP、netdev 或远端路由为依据，在启动时查询并确认对应 GID index。连接两端还应使用兼容的地址族和 RoCE 类型，例如一端选择 IPv4-mapped RoCE v2 GID，另一端也应按同一 IP 网络和 RoCE v2 路径建立连接。

## 3.6.3 MTU

RDMA WR 可以很大，但网络 packet 必须受 MTU 限制。QP 的 path MTU 决定 RDMA 传输层的分片大小；RoCE 下实际可用 MTU 还受 Ethernet MTU、VLAN tag、交换机端口和路径中最小链路影响。

如果 QP 配置的 path MTU 超出网络路径可承载范围，结果可能表现为丢包、重传、吞吐下降或连接失败。若 MTU 过小，包头开销和 PSN 数量增加；若 MTU 过大，端到端配置要求更严格。RoCE 网络中常见做法是为 RDMA 流量配置一致的 jumbo frame，但这必须在主机和交换机两端同时成立。

Linux 主机侧配置的是 Ethernet netdev 的 MTU，交换机端口也必须使用兼容配置。QP 的 path MTU 不能大于端到端 Ethernet 路径实际可承载的 RDMA payload。使用 raw Verbs 时，程序在 RTR 阶段设置 `attr.path_mtu`；使用 RDMA CM 时，路径参数通常由地址解析和路由过程决定，但底层仍受 netdev 与交换机 MTU 约束。

```bash
ip link set rdma0 mtu 9000
ip link show rdma0
ibv_devinfo -v | grep -A 8 'port: 1'
```

MTU 问题的难点在于 Verbs API 本身不会直接告诉应用“交换机某一跳 MTU 不一致”。应用看到的往往只是 completion 超时或性能异常。部署阶段验证端到端 MTU，比在程序运行后追查 retry 更可靠。

## 3.6.4 PFC、ECN 与 DCQCN

RoCE 运行在 Ethernet 上，而传统 Ethernet 在拥塞时可能丢包。RC 可以重传丢失 packet，但频繁丢包会显著增加延迟，严重时导致 retry timeout。数据中心部署 RoCE 时，通常会结合 PFC、ECN 和 DCQCN 减少拥塞丢包并控制发送速率。

PFC（Priority Flow Control）按优先级暂停流量。当交换机或接收端某个优先级队列接近耗尽时，可以发送 PAUSE 帧让上游暂停该优先级。PFC 的价值是减少丢包；风险是配置不当时可能造成暂停传播、队头阻塞甚至拥塞扩散。因此 PFC 不是“打开即可”的开关，而是 QoS、buffer、水线和优先级规划的一部分。

ECN（Explicit Congestion Notification）在交换机队列拥塞时标记 packet，而不是直接丢弃。接收端或网卡根据 ECN 标记产生拥塞反馈，发送端再降低速率。DCQCN 是 RoCE 常用的基于 ECN 的拥塞控制机制，用于在高吞吐 RDMA 流量中控制队列长度和避免持续拥塞。

PFC 和 ECN 的角色不同。PFC 是链路级的暂停机制，目标是避免队列溢出丢包；ECN/DCQCN 是端到端或接近端到端的速率调节机制，目标是在拥塞变严重之前降低注入速率。只依赖 PFC 容易把拥塞推向上游，只依赖 ECN 又可能在短突发中来不及阻止丢包。实际部署通常需要两者配合，并用交换机与网卡计数器验证效果。

## 3.6.5 路由、VLAN 与 QoS

RoCE v2 进入 IP 网络后，普通网络配置会直接影响 RDMA。源 IP、目的 IP、路由表、ECMP 哈希、VLAN、DSCP、Priority Code Point、交换机 QoS 队列都可能改变流量路径和服务等级。两个进程交换了正确的 QPN 和 rkey，并不意味着网络一定把 packet 送到正确端口。

路由首先决定 packet 从哪个 netdev 发出。对于依赖 IP 路由的 RoCE v2，`ip route get <remote-ip>` 是最基本的确认方式；输出中的 `dev` 和 `src` 应与所选 GID index 对应。多网卡主机上，如果控制连接走一张网卡，而 RDMA GID index 选到另一张网卡，连接参数可能看起来完整，数据路径却无法到达。

VLAN 与 QoS 则决定 packet 进入哪类二层队列。RDMA 流量常被放入专门优先级，以便交换机应用 PFC、ECN 或更高优先级调度。主机侧的 DSCP、VLAN PCP、traffic class 与交换机队列之间如何映射，取决于操作系统、驱动和交换机配置。对 Verbs 程序而言，直接相关的是 `ah_attr.grh.traffic_class`、GID 对应的 netdev/VLAN，以及部署环境中的 QoS 映射是否把这类 packet 放入预期队列。

ECMP 也会影响 RDMA 流量。RoCE v2 使用 UDP 封装，网络可以根据五元组做负载均衡。路径不一致时，不同 flow 的延迟和拥塞状态可能不同；某些网络中还需要固定 UDP source port 或调整哈希策略，使 RDMA 流量按预期分布。

## 3.6.6 计数器

RoCE 问题很少只靠程序日志定位。端口状态和计数器能够暴露网络层状态。主机侧常用入口包括 `rdma link show`、`ibv_devinfo -v`、`ethtool -S <netdev>`、`/sys/class/infiniband/<dev>/ports/<port>/counters/`。交换机侧需要查看端口丢包、pause、ECN mark、buffer 水位和 QoS 队列计数。

```bash
rdma link show
ibv_devinfo -v
ethtool -S rdma0 | grep -E 'pause|ecn|drop|error'
cat /sys/class/infiniband/mlx5_0/ports/1/counters/*
```

pause 计数持续升高，说明 PFC 正在频繁介入；ECN mark 增多，说明交换机队列进入拥塞标记区域；CRC 或 symbol error 指向物理链路问题；discard/drop 计数增加则需要区分是拥塞丢包、buffer 不足还是策略丢弃。RDMA completion 中的 retry exhausted 只说明可靠传输最终失败，不能单独判断是哪一层网络原因。

## 3.6.7 小结

RoCE 让 RDMA 使用以太网承载，带来了 IP 地址、GID index、MTU、QoS、PFC、ECN、DCQCN、路由和交换机队列等因素。Verbs API 把这些差异隐藏在统一编程模型之后，但无法消除它们对延迟、吞吐和错误完成的影响。

设计 RoCE 程序时，连接参数与网络配置必须一同考虑。诊断 RoCE 问题时，应从 GID、路由和 MTU 开始，再看 PFC/ECN 与端口计数器，最后结合 RC completion 判断传输层如何失败。
