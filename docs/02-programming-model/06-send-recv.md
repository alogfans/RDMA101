# 2.6 SEND/RECV 操作

前几章讨论了 RDMA 的资源模型：Context、PD、MR、QP、CQ。本章开始讨论具体操作。

RDMA 提供两类操作：**one-sided**（RDMA READ/WRITE/Atomic）和 **two-sided**（SEND/RECV）。SEND/RECV 是最接近传统 TCP socket 语义的 RDMA 操作，理解它是掌握 RDMA 双边通信的基础。

## 2.6.1 SEND/RECV 的使用场景

RDMA 已经提供 READ、WRITE 和 Atomic 等单边操作，但许多通信仍然需要接收方获得明确的消息完成事件。

控制消息、RPC 请求、完成通知和元数据交换都属于这类场景。SEND/RECV 的价值不在于替代 one-sided 操作，而在于提供接收方参与的消息语义。

### 双边与单边

双边操作（SEND/RECV）要求发送方和接收方都参与，接收方必须提前投递 RECV WR，因此双方都能通过 completion 感知操作发生。单边操作（RDMA READ/WRITE）则只有发起方投递 WR，远端 CPU 不参与数据搬运，也不会因为基本 READ/WRITE 自动得到完成事件。

### SEND/RECV 的执行过程

```mermaid
sequenceDiagram
    participant Sender
    participant SenderNIC
    participant ReceiverNIC
    participant Receiver

    Note over Receiver: 1. 提前投递 RECV WR
    Receiver->>ReceiverNIC: ibv_post_recv(RECV WR)

    Note over Sender: 2. 发送数据
    Sender->>SenderNIC: ibv_post_send(SEND WR)

    SenderNIC->>ReceiverNIC: 传输数据
    ReceiverNIC->>Receiver: 写入接收缓冲区

    Note over Sender: 3. 轮询 CQ
    Sender->>SenderNIC: ibv_poll_cq()
    SenderNIC-->>Sender: SEND WC

    Note over Receiver: 4. 轮询 CQ
    Receiver->>ReceiverNIC: ibv_poll_cq()
    ReceiverNIC-->>Receiver: RECV WC
```

图 2-9：SEND/RECV 操作的完整流程。
{: .figure-caption }

### SEND/RECV 与 TCP socket 的类比

SEND/RECV 与 TCP socket 可以作如下对照：

| TCP socket | RDMA SEND/RECV | 差异 |
|-----------|----------------|------|
| `send()` | `ibv_post_send(SEND)` | TCP 会拷贝数据，RDMA 直接 DMA |
| `recv()` | 提前 `ibv_post_recv()` | TCP 可以随时调用，RDMA 必须提前准备 |
| 内核缓冲区 | 用户注册内存 | TCP 管理缓冲区，RDMA 由应用管理 |
| 阻塞返回 | 轮询 CQ | TCP 同步等待，RDMA 异步轮询 |

!!! note "最关键的区别：接收方必须提前准备"
    TCP 可以在没有数据时阻塞等待或返回 `EAGAIN`。RDMA RECV 必须在消息到达前投递。对于 RC QP，如果没有可用 RECV WR，接收方会返回 RNR NAK，发送方按 `rnr_retry` 配置重试；重试耗尽后，发送方会得到错误完成。

    会消耗 Receive WR 的远端操作主要是 `SEND`、`SEND_WITH_IMM` 和 `RDMA_WRITE_WITH_IMM`。基本 RDMA WRITE、RDMA READ 和 Atomic 不消耗远端 Receive WR。

### SEND/RECV 的典型应用场景

SEND/RECV 适用于需要双方明确参与的场景：

| 场景 | 说明 |
|------|-------------------|
| **控制消息传递** | 需要双方确认，协商参数 |
| **RPC 请求/响应** | 客户端发送请求，服务端返回响应 |
| **RDMA WRITE 完成通知** | RDMA WRITE 是单边的，需要额外通知机制 |
| **元数据交换** | 交换地址、rkey 等信息 |

## 2.6.2 SEND 操作详解

### 投递 SEND WR

SEND WR 的结构与普通 Send WR 相同，主要区别在于 `opcode`：

```c
struct ibv_send_wr wr = {
    .wr_id = 1001,                    // 用户定义的 ID
    .sg_list = &sge,                  // 本地数据描述
    .num_sge = 1,
    .opcode = IBV_WR_SEND,            // SEND 操作
    .send_flags = IBV_SEND_SIGNALED,  // 生成 CQE
    .next = NULL
};

struct ibv_send_wr *bad_wr;
if (ibv_post_send(qp, &wr, &bad_wr)) {
    fprintf(stderr, "Failed to post SEND WR\n");
    return -1;
}
```

### SEND 操作的完成语义

**WC 生成的时间点**：以本教程主线的 RC QP 为背景，远端网卡已经接收并确认了这个消息。

发送方 CQ 会收到一个 SEND WC，`opcode` 为 `IBV_WC_SEND`，`status` 为 `IBV_WC_SUCCESS` 才表示成功。发送方 SEND WC 中的 `byte_len` 不作为发送长度使用。接收方 CQ 会收到一个 RECV WC，`opcode` 为 `IBV_WC_RECV`，`byte_len` 表示接收的字节数，数据已经在接收缓冲区中。

!!! note "SEND 完成时数据已在远端"
    当发送方获得 SEND WC 时，本地发送 WR 已完成，发送 buffer 可以复用。远端应用是否已经处理消息，取决于远端是否轮询并处理对应的 RECV WC。

### 示例：发送方

```c
#include <infiniband/verbs.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

int send_message(struct ibv_qp *qp, struct ibv_cq *cq,
                 struct ibv_mr *mr, const char *message) {
    // 准备数据
    size_t msg_len = strlen(message) + 1;
    memcpy(mr->addr, message, msg_len);

    // 构造 SGE
    struct ibv_sge sge = {
        .addr = (uintptr_t)mr->addr,
        .length = msg_len,
        .lkey = mr->lkey
    };

    // 构造 SEND WR
    struct ibv_send_wr wr = {
        .wr_id = 1001,
        .sg_list = &sge,
        .num_sge = 1,
        .opcode = IBV_WR_SEND,
        .send_flags = IBV_SEND_SIGNALED,
        .next = NULL
    };

    // 投递 WR
    struct ibv_send_wr *bad_wr;
    if (ibv_post_send(qp, &wr, &bad_wr)) {
        fprintf(stderr, "Failed to post SEND: %s\n", strerror(errno));
        return -1;
    }

    printf("SEND WR posted, wr_id=%lu\n", wr.wr_id);

    // 轮询 CQ 等待完成
    struct ibv_wc wc;
    while (1) {
        int n = ibv_poll_cq(cq, 1, &wc);
        if (n < 0) {
            fprintf(stderr, "Poll CQ failed\n");
            return -1;
        }
        if (n == 0) {
            continue;  // 还没完成，继续轮询
        }

        // 有 WC 了
        if (wc.status != IBV_WC_SUCCESS) {
            fprintf(stderr, "SEND failed: %s\n", ibv_wc_status_str(wc.status));
            return -1;
        }

        if (wc.opcode == IBV_WC_SEND) {
            printf("SEND completed, wr_id=%lu\n", wc.wr_id);
            return 0;
        } else {
            printf("Got unexpected opcode: %d\n", wc.opcode);
        }
    }
}
```

## 2.6.3 RECV 操作详解

### RECV 的核心要求：提前投递

这是 SEND/RECV 与传统 socket 最关键的区别：

**传统 TCP**：可以随时调用 `recv()`，如果没有数据，调用会阻塞或返回 EAGAIN。

**RDMA RECV**：必须在消息到达**之前**投递 RECV WR。对于 RC QP，没有预投递 RECV WR 会触发 RNR 重试，重试耗尽后发送方得到错误完成。

!!! note "提前投递的原因"
    RDMA 数据路径没有 socket buffer 这类内核接收队列。网卡需要把到达的数据写入应用预先注册并投递的缓冲区；没有可用 RECV WR 时，RC 连接会进入 RNR 重试路径。

### 投递 RECV WR

```c
struct ibv_recv_wr wr = {
    .wr_id = 2001,              // 用户定义的 ID
    .sg_list = &sge,            // 接收缓冲区描述
    .num_sge = 1,               // SGE 数量
    .next = NULL                // 链表下一个（NULL 表示单个）
};

struct ibv_recv_wr *bad_wr;
if (ibv_post_recv(qp, &wr, &bad_wr)) {
    fprintf(stderr, "Failed to post RECV WR\n");
    return -1;
}
```

### RECV WR 的生命周期

```
投递 RECV WR → 接收队列中等待 → 消息到达 → 数据写入缓冲区 → 生成 RECV WC
```

这里要区分几个时间点：投递成功不表示已有消息到达，消息到达也不表示应用已经取到了 WC；只有对应 RECV WC 生成并被应用轮询到之后，应用才可以按协议处理这段接收缓冲区。

!!! note "WC 生成时数据已可用"
    当 RECV WC 生成时，数据已经在接收缓冲区中，可以直接访问。应用仍需按照自己的消息格式解析 `byte_len` 和缓冲区内容。

    `ibv_post_recv` 返回后，`struct ibv_recv_wr` 和 `struct ibv_sge` 这类描述符可以复用；真正不能提前复用的是 SGE 指向的接收缓冲区。该缓冲区要等到对应 RECV WC 返回后，才适合交给应用处理或再次投递。

### 示例：接收方

```c
#include <infiniband/verbs.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

// 接收缓冲区（必须预先注册）
char recv_buffer[4096];
struct ibv_mr *recv_mr;

int init_recv_resources(struct ibv_pd *pd) {
    // 注册接收缓冲区
    int access = IBV_ACCESS_LOCAL_WRITE;
    recv_mr = ibv_reg_mr(pd, recv_buffer, sizeof(recv_buffer), access);
    if (!recv_mr) {
        perror("Failed to register recv MR");
        return -1;
    }

    // 投递初始 RECV WR
    struct ibv_sge sge = {
        .addr = (uintptr_t)recv_buffer,
        .length = sizeof(recv_buffer),
        .lkey = recv_mr->lkey
    };

    struct ibv_recv_wr wr = {
        .wr_id = 2001,
        .sg_list = &sge,
        .num_sge = 1,
        .next = NULL
    };

    struct ibv_recv_wr *bad_wr;
    if (ibv_post_recv(qp, &wr, &bad_wr)) {
        fprintf(stderr, "Failed to post RECV WR\n");
        return -1;
    }

    printf("RECV WR posted, waiting for message...\n");
    return 0;
}

int receive_message(struct ibv_qp *qp, struct ibv_cq *cq) {
    struct ibv_wc wc;

    while (1) {
        int n = ibv_poll_cq(cq, 1, &wc);
        if (n < 0) {
            fprintf(stderr, "Poll CQ failed\n");
            return -1;
        }
        if (n == 0) {
            continue;  // 还没完成，继续轮询
        }

        // 有 WC 了
        if (wc.status != IBV_WC_SUCCESS) {
            fprintf(stderr, "RECV failed: %s\n", ibv_wc_status_str(wc.status));
            return -1;
        }

        if (wc.opcode == IBV_WC_RECV) {
            printf("RECV completed, %u bytes received\n", wc.byte_len);
            printf("Message: \"%s\"\n", recv_buffer);
            printf("wr_id: %lu\n", wc.wr_id);

            // 重新投递 RECV WR，为下一次接收做准备
            repost_recv_wr(qp);
            return 0;
        } else {
            printf("Got unexpected opcode: %d\n", wc.opcode);
        }
    }
}

void repost_recv_wr(struct ibv_qp *qp) {
    struct ibv_sge sge = {
        .addr = (uintptr_t)recv_buffer,
        .length = sizeof(recv_buffer),
        .lkey = recv_mr->lkey
    };

    struct ibv_recv_wr wr = {
        .wr_id = 2002,  // 使用新的 wr_id
        .sg_list = &sge,
        .num_sge = 1,
        .next = NULL
    };

    struct ibv_recv_wr *bad_wr;
    if (ibv_post_recv(qp, &wr, &bad_wr)) {
        fprintf(stderr, "Failed to repost RECV WR\n");
    }
}
```

## 2.6.4 RNR（Receiver Not Ready）错误

### 什么是 RNR

RNR 是 SEND/RECV 操作中最常见的错误之一。当发送方发送消息时，如果接收方没有预先投递足够的 RECV WR，就会产生 RNR 错误。

```mermaid
flowchart LR
    S["发送方<br/>投递 SEND WR"]
    R["接收方<br/>没有 RECV WR"]
    NIC["RDMA 网卡"]

    S -->|"发送数据"| NIC
    NIC -->|"检查接收队列"| R
    R -->|"队列为空"| NIC
    NIC -->|"返回 RNR NAK"| S

    style R fill:#ffebee
    style NIC fill:#fff3e0
```

图 2-10：RNR 错误的产生过程。
{: .figure-caption }

### RNR 的后果

RNR 首先会触发发送方按 `rnr_retry` 配置重试。若重试耗尽，发送方会收到错误 WC，常见状态是 `IBV_WC_RNR_RETRY_EXC_ERR`，也可能表现为重试超限错误。进入错误路径后，RC QP 可能需要重建，应用应把它当作协议层接收队列管理失败来处理。

!!! note "RNR 是 SEND/RECV 相关错误"
    基本 RDMA WRITE、RDMA READ 和 Atomic 不会消耗远端 Receive WR，因此不会因为缺少远端接收缓冲区而走 RNR 路径。`RDMA_WRITE_WITH_IMM` 是例外：它会在远端生成 Receive WC，因此也需要远端提前投递 RECV WR。

### 如何避免 RNR

**策略1：保持足够的 RECV WR**

```c
#define RECV_WR_HIGH_WATERMARK 8

void maintain_recv_queue(struct ibv_qp *qp, int *posted_count) {
    while (*posted_count < RECV_WR_HIGH_WATERMARK) {
        struct ibv_recv_wr wr = {
            .wr_id = 2000 + *posted_count,
            .sg_list = &sge,
            .num_sge = 1,
            .next = NULL
        };

        struct ibv_recv_wr *bad_wr;
        if (ibv_post_recv(qp, &wr, &bad_wr) == 0) {
            (*posted_count)++;
        } else {
            break;
        }
    }
}

// 每次收到 RECV WC 后
int recv_completion_handler(struct ibv_wc *wc) {
    static int posted_count = 16;

    // 处理接收到的数据
    process_message(wc->byte_len);

    // 减少计数
    posted_count--;

    // 重新投递到高水线
    maintain_recv_queue(qp, &posted_count);

    return 0;
}
```

**策略2：设置合适的 RNR 重试参数**

```c
// 在修改 QP 到 RTR 状态时设置
struct ibv_qp_attr attr = {
    .qp_state = IBV_QPS_RTR,
    .min_rnr_timer = 12,  // RNR NAK 定时器（约 0.34ms）
    // ...
};

int attr_mask = IBV_QP_STATE | IBV_QP_MIN_RNR_TIMER;
ibv_modify_qp(qp, &attr, attr_mask);

// 在修改 QP 到 RTS 状态时设置
attr.qp_state = IBV_QPS_RTS;
attr.rnr_retry = 7;  // RNR 重试次数（7 次）

attr_mask = IBV_QP_STATE | IBV_QP_RNR_RETRY;
ibv_modify_qp(qp, &attr, attr_mask);
```

!!! note "RNR 重试参数的选择"
    `min_rnr_timer` 控制多快重试，太小会增加网络负载，太大会降低响应速度。`rnr_retry` 控制重试多少次后放弃，太大会导致故障发现变慢，太小又会降低短暂拥塞下的容错性。

## 2.6.5 SEND with Immediate

### 什么是 Immediate 数据

**SEND with Immediate** 允许发送方在消息携带一个 32 位 `imm_data`，这个数据会直接写入接收方的 WC 中，而不需要额外的存储空间。

### Immediate 数据的特点

| 特性 | 说明 |
|------|------|
| **大小** | 固定 32 位 |
| **传递方式** | 不占用接收缓冲区，直接写入 WC |
| **原子性** | 与消息一起原子传递 |
| **用途** | 小元数据传递、通知机制 |

!!! note "Immediate 数据的实际用途"
    典型用途是通知机制。例如 RDMA WRITE 是单边操作，远端不会自动得到完成事件。使用 RDMA WRITE with IMM 时，发起方写入数据并携带一个 32 位通知，远端通过 RECV WC 获得通知。

### 投递 SEND with Immediate

```c
struct ibv_send_wr wr = {
    .wr_id = 1001,
    .sg_list = &sge,
    .num_sge = 1,
    .opcode = IBV_WR_SEND_WITH_IMM,  // 注意：使用 SEND_WITH_IMM
    .send_flags = IBV_SEND_SIGNALED,
    .imm_data = htonl(0x12345678),    // 32 位立即数据（注意字节序）
    .next = NULL
};
```

!!! note "字节序转换"
    `imm_data` 在网络传输时使用网络字节序（大端）。使用 `htonl()` 转换，接收方用 `ntohl()` 转换回来。

### 接收 Immediate 数据

接收方不需要做特殊处理，`imm_data` 会直接出现在 WC 中：

```c
struct ibv_wc wc;
ibv_poll_cq(cq, 1, &wc);

if (wc.opcode == IBV_WC_RECV) {
    printf("Received %u bytes\n", wc.byte_len);

    // 检查是否有 immediate 数据
    if (wc.wc_flags & IBV_WC_WITH_IMM) {
        uint32_t imm_data = ntohl(wc.imm_data);  // 注意字节序转换
        printf("Immediate data: 0x%x\n", imm_data);
    }
}
```

### Immediate 数据的应用场景

**场景：RDMA WRITE 完成通知**

```c
// 发起 RDMA WRITE 的同时发送通知
struct ibv_send_wr wr = {
    .opcode = IBV_WR_RDMA_WRITE_WITH_IMM,
    .wr.rdma.remote_addr = remote_addr,
    .wr.rdma.rkey = remote_rkey,
    .imm_data = htonl(WRITE_COMPLETED),  // 通知类型
    // ...
};
```

## 2.6.6 SEND/RECV 的 Scatter-Gather

### Scatter-Gather 的概念

Scatter-Gather 允许一次 SEND/RECV 操作涉及多个不连续的内存区域。

**Scatter（接收）**：将收到的数据分散存储到多个缓冲区
**Gather（发送）**：从多个缓冲区收集数据一次性发送

### 发送 Gather 示例

```c
// 假设有三个不连续的数据块
struct {
    char header[64];
    char body[1024];
    char trailer[32];
} message_parts;

// 构造三个 SGE
struct ibv_sge sg_list[3] = {
    {
        .addr = (uintptr_t)message_parts.header,
        .length = sizeof(message_parts.header),
        .lkey = mr->lkey
    },
    {
        .addr = (uintptr_t)message_parts.body,
        .length = sizeof(message_parts.body),
        .lkey = mr->lkey
    },
    {
        .addr = (uintptr_t)message_parts.trailer,
        .length = sizeof(message_parts.trailer),
        .lkey = mr->lkey
    }
};

// 投递 Gather SEND
struct ibv_send_wr wr = {
    .opcode = IBV_WR_SEND,
    .sg_list = sg_list,
    .num_sge = 3,  // 三个 SGE
    .send_flags = IBV_SEND_SIGNALED,
    .wr_id = 1001
};

ibv_post_send(qp, &wr, &bad_wr);

// 接收方会收到连续的 1120 字节（64 + 1024 + 32）
```

!!! note "Scatter-Gather 的限制"
    `max_send_sge` 和 `max_recv_sge` 限制了单个 WR 可以使用的 SGE 数量。如果到达消息大于接收端所有 SGE 的总长度，操作会以长度错误完成。每个额外的 SGE 都会消耗硬件资源；同一个 WR 内也不要让多个 SGE 覆盖同一段内存，因为设备访问 SGE 的内部顺序不应被应用依赖。

!!! note "UD QP 的接收缓冲区"
    如果使用 UD QP，接收缓冲区通常要额外预留 40 字节空间给 GRH。即使某些消息没有实际 GRH，按这个约定预留空间也能避免从 RC/UC 迁移到 UD 时出现接收长度和数据偏移错误。本教程主线以 RC 为背景，UD 细节会在高级主题中再展开。

## 2.6.7 关键要点回顾

| 概念 | 要点 |
|------|------|
| **双边操作** | 双方都参与，不同于 one-sided 操作 |
| **必须提前投递 RECV WR** | 否则 RC 连接会进入 RNR 重试路径，重试耗尽后产生错误完成 |
| **RNR 错误** | 接收方没有足够的 RECV WR，发送方会收到错误 |
| **保持 RECV WR 水位线** | 避免耗尽，通常维持 8-16 个未完成的 RECV WR |
| **Immediate 数据** | 32 位元数据，不占用缓冲区，原子传递 |
| **Scatter-Gather** | 支持多缓冲区操作，减少拷贝 |
| **完成语义** | SEND WC 表示远端已确认，RECV WC 表示数据已可访问 |

!!! note "后续章节"
    SEND/RECV 是 RDMA 双边通信的基础，适合控制消息传递和需要接收方获得完成事件的场景。后续章节将转向单边写入、单边读取和远端原子操作。
