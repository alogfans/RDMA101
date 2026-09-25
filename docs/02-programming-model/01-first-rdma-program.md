# 2.1 RDMA WRITE 范例解读

第一篇已经看到 server 打印 client 写来的字符串。接下来打开 [one_sided_write.c](https://github.com/alogfans/RDMA101/blob/main/examples/one_sided_write/one_sided_write.c)，沿程序执行顺序理解这件事。真正搬运数据只需一条 WRITE 请求，前面的准备代码则决定它从哪里读、向哪里写，以及是否有权访问。

本章先建立整体关系。Context、PD、MR、QP 和 CQ 会在后续章节各自展开，首次阅读不必同时记住所有字段。

## 从 main 看到的流程

`main` 先建立 TCP 控制连接，再调用 `init_rdma` 准备本地资源。双方各自生成 `peer_info`，通过 `exchange_info` 交换，然后调用 `connect_qp` 配置通信关系。

```text
建立 TCP 连接
  → 创建本地 RDMA 资源
  → 交换连接与内存信息
  → 配置双方 QP
  → client 写入并等待完成
  → TCP 通知 server
  → server 打印，双方清理
```

两端都要创建资源，差别在于最后由谁发起操作。下面摘录 client 的业务部分：

```c
size_t len = strlen(opt.message) + 1;
if (len > BUFFER_SIZE) diex("message too long");
memcpy(state.buffer, opt.message, len);
post_write(&state, &remote, len);
if (send_all(sock, "D", 1) != 0) die("send done");
```

这里的 `post_write` 是示例自己的辅助函数，内部包含投递和等待。库函数 `ibv_post_send` 本身只负责投递。区分这两个函数，才能正确解释最后一条 TCP 消息为什么在传输完成之后才发出。

## 本地资源怎样配合 {#resources}

打开网卡得到 Context，程序在这个设备上下文中创建其他资源。PD（Protection Domain，保护域）把可以配合使用的本地资源归在一起。注册缓冲区得到 MR；创建 QP 时指定 PD 和完成所用的 CQ。

| 对象 | 保存或代表什么 | 依赖 |
|---|---|---|
| Context | 当前进程访问设备的上下文 | 选定的 RDMA 设备 |
| PD | 本地资源的保护关系 | Context |
| MR | 地址范围、长度和内存访问权限 | PD 与底层内存 |
| CQ | 请求执行后的完成记录 | Context |
| QP | 待执行工作及连接状态 | PD、发送 CQ、接收 CQ |

表 2-1：本地资源怎样配合。
{: .table-caption }

因此 Context 要先建立，QP 使用的 CQ 和 PD 也必须已经存在。MR 与 QP 之间没有唯一的创建先后要求，但投递引用这块内存的请求前，MR 必须准备好。

示例的 `rdma_state` 把这些句柄与缓冲区放在一起，便于函数传参。这只是 C 结构体的组织方式，资源实际依赖仍由 API 规定。一个长期运行的应用可能用一个 PD 管理许多 MR 和 QP，也可能让多个 QP 共享 CQ。

## 连接信息与内存信息

TCP 交换的 `peer_info` 包含两组用途不同的信息：

| 信息 | 用途 |
|---|---|
| QP 编号、初始 PSN、LID/GID | 让本端 QP 知道如何与对端通信 |
| 缓冲区地址、`rkey` | 指定远端内存位置及其访问凭证 |

表 2-2：连接信息与内存信息。
{: .table-caption }

QP 编号类似于要联系的设备对象编号，不等于内存授权。即使连接正确，错误的地址或 `rkey` 仍会导致访问失败。反过来，有内存地址也不足以决定该通过哪个连接去访问。

本例直接通过 TCP 交换结构体，适合相同 ABI 和字节序的实验环境。跨平台协议应明确编码各字段，并携带长度、版本等信息；本例的固定大小缓冲区约定不能直接当成通用协议。

`connect_qp` 将 QP 从 RESET 依次改为 INIT、RTR、RTS：先指定本端端口与权限，再设置对端与路径，最后准备发送。具体代码在 [2.4](04-qp.md#connect)解释。

## 把一次搬运写成请求

WRITE 的本地源由 SGE（Scatter/Gather Element，内存片段描述）表示。工作请求 WR（Work Request）引用 SGE，并补上操作、远端地址与 `rkey`。以下是 `post_write` 内的核心字段，省略检查与等待部分：

```c
struct ibv_sge sge = {
    .addr = (uintptr_t)s->buffer,
    .length = len,
    .lkey = s->mr->lkey,
};
struct ibv_send_wr wr = {
    .wr_id = 1,
    .sg_list = &sge,
    .num_sge = 1,
    .opcode = IBV_WR_RDMA_WRITE,
    .send_flags = IBV_SEND_SIGNALED,
    .wr.rdma.remote_addr = remote->addr,
    .wr.rdma.rkey = remote->rkey,
};
```

两个地址属于不同进程。`sge.addr` 是 client 的源，`remote_addr` 是 server 的目标；本地网卡使用 `lkey`，远端网卡使用 `rkey` 检查访问。`IBV_SEND_SIGNALED` 要求本次操作产生可轮询的成功完成。

WR 和 SGE 是交给投递函数的描述，底层缓冲区是之后由设备访问的数据。投递调用返回以后，可以重新构造描述，但普通非 inline 请求使用的源缓冲区要保留到操作完成。

## 从 CQ 取回结果

示例持续调用 `ibv_poll_cq`。返回 0 表示目前没有结果；返回正数后，再检查 `wc.status` 是否成功。成功完成以后，`post_write` 返回，client 才发送 `D`。

server 收到 `D` 才访问缓冲区，所以本例把数据搬运和消费顺序连接起来。这里假设普通主机内存及对应平台的 DMA 一致性支持；GPU 消费需要第五篇额外介绍的同步。

程序正常结束时，已经没有新的传输，随后注销内存并释放资源。完整服务还需要处理初始化只成功一半、设备仍持有请求和对端继续访问等情况。第二篇先学习各资源的释放条件，第四篇再组合成退出流程。

回到代码，可以找出三个不同的动作：`memcpy` 准备本地源，`ibv_post_send` 提交搬运，`send_all` 告知对端。这三个动作服务不同阶段；把 TCP 通知提前到完成之前，会破坏本例的消费顺序。

下一章从[设备上下文和保护域](02-context-and-pd.md)开始逐个展开资源。
