# 2.9 Atomic 操作

前几章讨论了 RDMA WRITE 和 READ。它们都是单边操作：发起方写入或读取远端内存，远端 CPU 不参与数据搬运。

本章讨论另一种单边操作：**Atomic**。它允许发起方在远端内存上执行原子操作，远端 CPU 仍然不参与，但网卡会执行更复杂的操作——比较并交换、取数加。

在基础 Verbs 模型中，Atomic 通常只在可靠连接类传输上使用，例如 RC QP。它不消耗远端 Receive WR；远端需要提供的是带 `IBV_ACCESS_REMOTE_ATOMIC` 权限的 MR，以及 QP 上允许的 READ/Atomic 并发资源。

本章实验使用 `examples/programming_model/03_rc_loopback.c`。实验的第四段在设备支持 Atomic 时执行 Compare & Swap：先把 B 端某个 8 字节变量设为 7，再由 A 端投递 CAS；远端值仍为 7 时，网卡将其改为 11，并把旧值写回 A 端 result buffer。输出中的 `old=7 remote_now=11` 对应 CAS 的返回值和远端更新结果。设备不支持 Atomic 时，这一段实验会被跳过。

```bash
make -C examples/programming_model
./examples/programming_model/03_rc_loopback -d mlx5_0 -p 1 -g 0
```

## 2.9.1 Atomic 操作的使用场景

Atomic 操作用于远端状态的同步和协调。它不是普通 RDMA READ/WRITE 的替代品，而是在需要原子更新远端 64 位值时使用的特殊操作。

本章以分布式锁和计数器为例说明 CAS 与 Fetch & Add 的使用方式。至于更完整的分布式同步协议设计，需要结合一致性模型、失败恢复和上层协议讨论，本章不展开。

### 一个经典问题：分布式锁

以分布式锁为例，多个节点可能同时竞争同一个远端锁变量。

**尝试1：用 RDMA WRITE**

```c
// 发起方 A
lock_value = 1;
rdma_write(remote_lock_addr, &lock_value, sizeof(lock_value));

// 发起方 B（几乎同时）
lock_value = 1;
rdma_write(remote_lock_addr, &lock_value, sizeof(lock_value));
```

问题：两个节点都认为获得了锁，但实际上只有一个应该成功。

**尝试2：用 RDMA READ + WRITE**

```c
// 发起方 A
rdma_read(remote_lock_addr, &current_value);
if (current_value == 0) {
    lock_value = 1;
    rdma_write(remote_lock_addr, &lock_value, sizeof(lock_value));
}
```

问题：READ 和 WRITE 之间，另一个节点可能已经修改了锁。这是经典的**竞态条件**。

**一种常见做法：Atomic 操作**

```c
// 发起方 A
atomic_compare_and_swap(remote_lock_addr, 
                        expected=0,   // 期望是 0
                        new_value=1);  // 设置为 1
// 返回原始值，如果返回 0，说明成功获取锁
```

Atomic 操作保证读-修改-写这三个步骤是原子的——不会有其他节点插入其中。

### Atomic 操作的执行过程

```mermaid
sequenceDiagram
    participant Initiator as 发起方
    participant INIC as 发起方网卡
    participant TNIC as 远端网卡
    participant Target as 远端

    Note over Initiator: 1. 准备本地缓冲区（接收结果）
    Initiator->>Initiator: alloc & register buffer

    Note over Initiator: 2. 投递 Atomic WR
    Initiator->>INIC: ibv_post_send(Atomic)

    Note over INIC: 3. 发送原子请求
    INIC->>TNIC: Atomic 请求 (addr, rkey, compare/add)

    Note over TNIC: 4. 网卡执行原子操作
    TNIC->>TNIC: 读取远端内存<br/>执行比较/交换或加法<br/>写回结果

    Note over TNIC: 5. 返回结果
    TNIC->>INIC: 原始值/结果

    Note over INIC: 6. 写入本地缓冲区
    INIC->>Initiator: DMA 写入本地缓冲区

    Note over Initiator: 7. 轮询 CQ 确认
    Initiator->>INIC: ibv_poll_cq()
    INIC-->>Initiator: Atomic WC
```

图 2-13：Atomic 操作的完整流程。
{: .figure-caption }

!!! note "原子操作在网卡上执行"
    关键点是：读-修改-写由远端网卡按原子语义执行，而不是由发起方 CPU 分多步完成。多个发起方同时访问同一远端原子变量时，网卡会按照原子操作语义串行化这些更新。

    Atomic 的返回值会写入发起方 WR 中 `sg_list` 描述的本地缓冲区。这个缓冲区需要注册，并允许本地写入；发起方收到成功 WC 后，才能读取其中的原始值。

### Atomic 操作的类型

RDMA 支持两种原子操作：

| 操作 | 名称 | 功能 | 返回值 |
|------|------|------|--------|
| **CAS** | Compare & Swap | 比较并交换 | 返回原始值 |
| **FA** | Fetch & Add | 取数加 | 返回原始值 |

### Atomic 操作的典型应用场景

Atomic 操作适用于需要远程同步和协调的场景：

| 场景 | 说明 |
|------|-----------------|
| **分布式锁** | 原子地获取锁，避免竞态条件 |
| **无锁队列** | 原子地更新队列索引 |
| **引用计数** | 原子地增加/减少计数 |
| **序号生成** | 原子地分配唯一 ID |

### 扩展原子操作说明

!!! info "文献中的扩展原子操作"
    标准 RDMA verbs 中常用的远端原子操作是 CAS 和 FA。阅读论文或厂商示例时，可能会遇到扩展原子操作，如：

    Masked Compare-and-Swap 是带掩码的 CAS，只比较和修改 64 位值中的某些位；Masked Fetch-and-Add 是带掩码的 FA，只对 64 位值中的某些位进行加法。

    这些通常属于厂商扩展接口，不属于可移植的基础 Verbs 编程模型。如果论文或系统使用这些操作，如果论文中使用这些操作，可能需要：

    1. 使用厂商提供的特定 verbs 扩展（如 `ibv_exp_*` 函数）
    2. 使用特定版本的网卡和驱动
    3. 注意代码的可移植性

    对于大多数应用，标准的 CAS 和 FA 已经足够实现各种同步原语。

## 2.9.2 Compare & Swap（CAS）

### CAS 操作原理

CAS 是一种经典的原子操作。它会原子性地读取远端 64 位值，并将该值与 `compare` 值比较；如果二者相等，就把 `swap` 值写入远端，否则保持远端值不变。无论是否交换，操作都会把修改前的原始值返回给发起方。

```mermaid
flowchart LR
    A["读取远端值"] --> B{"与 compare 相等?"}
    B -->|是| C["写入 swap 值"]
    B -->|否| D["保持不变"]
    C --> E["返回原始值"]
    D --> E
```

图 2-14：CAS 操作的逻辑流程。
{: .figure-caption }

!!! note "CAS 的返回值"
    CAS 返回的是**原始值**，而不是“是否成功”的布尔值。应用需要比较返回值和期望值：
    如果返回值等于期望值，说明 CAS 成功；如果返回值不等于期望值，说明远端值已被其他节点修改，本次 CAS 没有完成交换。

### CAS 操作的参数

```c
struct ibv_send_wr wr = {
    .opcode = IBV_WR_ATOMIC_CMP_AND_SWP,
    .wr_id = 1001,
    .sg_list = &sge,                    // 本地接收缓冲区（接收原始值）
    .num_sge = 1,
    .send_flags = IBV_SEND_SIGNALED,
    .wr.atomic.remote_addr = remote_addr,  // 远端地址
    .wr.atomic.rkey = remote_rkey,         // 远端 key
    .wr.atomic.compare_add = compare_value, // 比较值（64 位）
    .wr.atomic.swap = swap_value,         // 交换值（64 位）
    .next = NULL
};
```

### CAS 示例：分布式锁

```c
#include <infiniband/verbs.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>

#define LOCK_FREE   0
#define LOCK_ACQUIRED 1

struct distributed_lock {
    struct ibv_qp *qp;
    struct ibv_cq *cq;
    struct ibv_mr *result_mr;
    uint64_t *result_buffer;  // 存储原始值
    uint64_t remote_lock_addr;
    uint32_t remote_lock_rkey;
};

int try_acquire_lock(struct distributed_lock *lock) {
    // 准备本地缓冲区接收原始值
    struct ibv_sge sge = {
        .addr = (uintptr_t)lock->result_buffer,
        .length = sizeof(uint64_t),
        .lkey = lock->result_mr->lkey
    };

    // 尝试获取锁：如果远端值是 LOCK_FREE，则设置为 LOCK_ACQUIRED
    struct ibv_send_wr wr = {
        .wr_id = 1001,
        .sg_list = &sge,
        .num_sge = 1,
        .opcode = IBV_WR_ATOMIC_CMP_AND_SWP,
        .send_flags = IBV_SEND_SIGNALED,
        .wr.atomic.remote_addr = lock->remote_lock_addr,
        .wr.atomic.rkey = lock->remote_lock_rkey,
        .wr.atomic.compare_add = LOCK_FREE,   // 期望是 FREE
        .wr.atomic.swap = LOCK_ACQUIRED,     // 设置为 ACQUIRED
        .next = NULL
    };

    struct ibv_send_wr *bad_wr;
    if (ibv_post_send(lock->qp, &wr, &bad_wr)) {
        fprintf(stderr, "Failed to post CAS WR\n");
        return -1;
    }

    // 等待完成
    struct ibv_wc wc;
    while (1) {
        int n = ibv_poll_cq(lock->cq, 1, &wc);
        if (n < 0) {
            fprintf(stderr, "Poll CQ failed\n");
            return -1;
        }
        if (n == 0) continue;

        if (wc.status != IBV_WC_SUCCESS) {
            fprintf(stderr, "CAS failed: %s\n", ibv_wc_status_str(wc.status));
            return -1;
        }

        if (wc.opcode == IBV_WC_COMP_SWAP) {
            // 原始值已在 result_buffer 中
            uint64_t original = *lock->result_buffer;

            if (original == LOCK_FREE) {
                printf("Lock acquired!\n");
                return 1;  // 成功获取锁
            } else {
                printf("Lock already held (original value: %lu)\n", original);
                return 0;  // 锁已被占用
            }
        }
    }
}
```

!!! note "CAS 的正确使用模式"
    CAS 不是“设置并返回成功与否”，而是“比较并返回原始值”。应用需要检查返回的原始值来判断 CAS 是否成功。

## 2.9.3 Fetch & Add（FA）

### FA 操作原理

Fetch & Add 是另一种原子操作。它会原子性地读取远端 64 位值，将该值与 `add` 值相加并写回远端，同时把加法之前的原始值返回给发起方。

```mermaid
flowchart LR
    A["读取远端值"] --> B["与 add 值相加"]
    B --> C["写回结果"]
    C --> D["返回原始值"]
```

图 2-15：Fetch & Add 操作的逻辑流程。
{: .figure-caption }

!!! note "FA 返回的是原始值，不是新值"
    FA 返回的是**加法之前的原始值**，不是加法后的新值。如果需要新值，应用可以自行计算：`new_value = original + add`。

### FA 操作的参数

```c
struct ibv_send_wr wr = {
    .opcode = IBV_WR_ATOMIC_FETCH_ADD,
    .wr_id = 1001,
    .sg_list = &sge,                    // 本地接收缓冲区（接收原始值）
    .num_sge = 1,
    .send_flags = IBV_SEND_SIGNALED,
    .wr.atomic.remote_addr = remote_addr,  // 远端地址
    .wr.atomic.rkey = remote_rkey,         // 远端 key
    .wr.atomic.compare_add = add_value,    // 要加的值（64 位）
    .next = NULL
};
```

### FA 示例：分布式计数器

```c
#include <infiniband/verbs.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>

struct distributed_counter {
    struct ibv_qp *qp;
    struct ibv_cq *cq;
    struct ibv_mr *result_mr;
    uint64_t *result_buffer;
    uint64_t remote_counter_addr;
    uint32_t remote_counter_rkey;
};

int increment_counter(struct distributed_counter *counter, int delta) {
    // 准备本地缓冲区接收原始值
    struct ibv_sge sge = {
        .addr = (uintptr_t)counter->result_buffer,
        .length = sizeof(uint64_t),
        .lkey = counter->result_mr->lkey
    };

    // 远端计数器增加 delta
    struct ibv_send_wr wr = {
        .wr_id = 1001,
        .sg_list = &sge,
        .num_sge = 1,
        .opcode = IBV_WR_ATOMIC_FETCH_ADD,
        .send_flags = IBV_SEND_SIGNALED,
        .wr.atomic.remote_addr = counter->remote_counter_addr,
        .wr.atomic.rkey = counter->remote_counter_rkey,
        .wr.atomic.compare_add = delta,  // 要加的值
        .next = NULL
    };

    struct ibv_send_wr *bad_wr;
    if (ibv_post_send(counter->qp, &wr, &bad_wr)) {
        fprintf(stderr, "Failed to post FA WR\n");
        return -1;
    }

    // 等待完成
    struct ibv_wc wc;
    while (1) {
        int n = ibv_poll_cq(counter->cq, 1, &wc);
        if (n < 0) {
            fprintf(stderr, "Poll CQ failed\n");
            return -1;
        }
        if (n == 0) continue;

        if (wc.status != IBV_WC_SUCCESS) {
            fprintf(stderr, "FA failed: %s\n", ibv_wc_status_str(wc.status));
            return -1;
        }

        if (wc.opcode == IBV_WC_FETCH_ADD) {
            // 原始值已在 result_buffer 中
            uint64_t original = *counter->result_buffer;
            uint64_t new_value = original + delta;

            printf("Counter incremented: %lu -> %lu (+%d)\n",
                   original, new_value, delta);
            return new_value;
        }
    }
}
```

## 2.9.4 Atomic 操作的限制

### 数据类型限制

RDMA Atomic 操作只支持 **64 位** 操作：

```c
// 只能操作 64 位值
uint64_t *remote_ptr = ...;  // 正确
uint32_t *remote_ptr = ...;  // 错误！

// 地址必须 64 位对齐
assert((uint64_t)remote_addr % sizeof(uint64_t) == 0);
```

!!! note "64 位操作粒度"
    RDMA 的 Atomic 操作设计要满足硬件实现的高效性。64 位是现代处理器和网卡最自然的原子操作粒度。支持任意大小会大大增加硬件复杂度。

### 并发限制

与 RDMA READ 一样，Atomic 操作也受并发限制：

`max_dest_rd_atomic` 表示远端作为响应方允许的最大并发 Atomic 操作数，`max_rd_atomic` 表示本地作为发起方允许的最大并发 Atomic 操作数。

这些字段在协议资源上与 RDMA READ 共享，很多程序会把 READ 和 Atomic 统一纳入同一个 outstanding 窗口管理。也就是说，即使每个 Atomic 操作只改 8 字节，过量并发仍可能耗尽远端 responder resources。

### 权限要求

远端 MR 必须设置正确的权限：

```c
int access = IBV_ACCESS_LOCAL_WRITE |     // 设置 REMOTE_ATOMIC 时必须同时设置
             IBV_ACCESS_REMOTE_ATOMIC;    // Atomic 操作必需

struct ibv_mr *mr = ibv_reg_mr(pd, buffer, size, access);
```

!!! note "REMOTE_ATOMIC 需要配合 LOCAL_WRITE"
    `ibv_reg_mr` 要求：设置 `IBV_ACCESS_REMOTE_WRITE` 或 `IBV_ACCESS_REMOTE_ATOMIC` 时，必须同时设置 `IBV_ACCESS_LOCAL_WRITE`。Atomic 操作本身不要求同时设置 `IBV_ACCESS_REMOTE_WRITE`，除非同一段 MR 还要允许远端 RDMA WRITE。

## 2.9.5 错误处理

### 常见错误类型

**错误1：远端权限错误**

```c
// WC status = IBV_WC_REM_ACCESS_ERR
// 原因：远端 MR 没有 REMOTE_ATOMIC 权限
// 解决：确保远端 MR 注册时设置了正确的权限

int access = IBV_ACCESS_LOCAL_WRITE |
             IBV_ACCESS_REMOTE_ATOMIC;  // 必须设置
```

**错误2：地址未对齐**

```c
// WC status = IBV_WC_REM_INV_REQ_ERR
// 原因：远端地址未 64 位对齐
// 解决：确保地址 8 字节对齐

assert(remote_addr % sizeof(uint64_t) == 0);
```

**错误3：并发超限**

```c
// WC status = IBV_WC_REM_OP_ERR
// 原因：超过了远端的 max_dest_rd_atomic 限制
// 解决：增加 max_dest_rd_atomic 或减少并发
```

### 错误处理示例

```c
struct ibv_wc wc;
int n = ibv_poll_cq(cq, 1, &wc);

if (n > 0 && wc.status != IBV_WC_SUCCESS) {
    fprintf(stderr, "Atomic operation failed:\n");
    fprintf(stderr, "  status: %s\n", ibv_wc_status_str(wc.status));

    switch (wc.status) {
        case IBV_WC_REM_ACCESS_ERR:
            fprintf(stderr, "  Check: Remote MR has REMOTE_ATOMIC permission\n");
            break;

        case IBV_WC_LOC_LEN_ERR:
            fprintf(stderr, "  Check: Local buffer size\n");
            break;

        case IBV_WC_REM_INV_REQ_ERR:
            fprintf(stderr, "  Check: Remote address alignment (must be 8-byte aligned)\n");
            break;

        case IBV_WC_REM_OP_ERR:
            fprintf(stderr, "  Check: Concurrent atomic operations limit\n");
            break;

        default:
            fprintf(stderr, "  Check: Connection and resource state\n");
            break;
    }
}
```

## 2.9.6 本章小结

Atomic 操作也是单边操作，但它访问的是远端的 64 位原子变量。CAS 用于“值仍等于期望值时才更新”的场景，Fetch & Add 用于计数和序号分配一类场景。二者都会把远端原始值写回发起方 `sg_list` 指向的本地缓冲区。

远端 MR 必须带有 `IBV_ACCESS_REMOTE_ATOMIC` 权限，目标地址也必须满足 8 字节对齐要求。Atomic 的执行受 `max_rd_atomic` 和 `max_dest_rd_atomic` 等资源限制影响，因此它适合小规模同步变量，不适合替代普通数据搬运。

!!! note "编程模型章节小结"
    到这里，第二篇已经说明了 RDMA 的核心资源对象和基本操作。下一章补上一块容易被忽略的内容：错误完成与异步事件。它们不改变数据操作的基本语义，却决定了程序如何感知请求失败、资源异常和端口状态变化。
