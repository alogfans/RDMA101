// Copyright 2026 Feng Ren
//
// Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <getopt.h>
#include <infiniband/verbs.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum {
    BUFFER_SIZE = 4096,
    CQ_DEPTH = 32,
    MAX_WR = 16,
    MAX_SGE = 1,
    ATOMIC_OFFSET = 128,
    ATOMIC_RESULT_OFFSET = 256,
    TIMEOUT_MS = 5000,
};

enum wr_ids {
    WRID_SEND = 1,
    WRID_RECV = 2,
    WRID_WRITE = 3,
    WRID_READ = 4,
    WRID_CAS = 5,
};

struct options {
    const char *device;
    int port;
    int gid_index;
};

struct endpoint {
    const char *name;
    struct ibv_qp *qp;
    struct ibv_mr *mr;
    char *buffer;
    uint32_t psn;
};

static void die(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

static void diex(const char *msg) {
    fprintf(stderr, "%s\n", msg);
    exit(EXIT_FAILURE);
}

static uint64_t now_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) die("clock_gettime");
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static void usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [options]\n\n"
            "Options:\n"
            "  -d, --device <name>      RDMA device, e.g. mlx5_0 or rxe0\n"
            "  -p, --port <port>        RDMA port (default: 1)\n"
            "  -g, --gid-index <index>  GID index (default: 0)\n",
            prog);
}

static struct options parse_options(int argc, char **argv) {
    struct options opt;
    memset(&opt, 0, sizeof(opt));
    opt.port = 1;
    opt.gid_index = 0;

    static const struct option long_opts[] = {
        {"device", required_argument, NULL, 'd'},
        {"port", required_argument, NULL, 'p'},
        {"gid-index", required_argument, NULL, 'g'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0},
    };

    for (;;) {
        int c = getopt_long(argc, argv, "d:p:g:h", long_opts, NULL);
        if (c == -1) break;
        switch (c) {
            case 'd':
                opt.device = optarg;
                break;
            case 'p':
                opt.port = atoi(optarg);
                break;
            case 'g':
                opt.gid_index = atoi(optarg);
                break;
            case 'h':
                usage(argv[0]);
                exit(EXIT_SUCCESS);
            default:
                usage(argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if (opt.port <= 0 || opt.gid_index < 0) diex("invalid option value");
    return opt;
}

static struct ibv_context *open_device(const char *name) {
    int num_devices = 0;
    struct ibv_device **devices = ibv_get_device_list(&num_devices);
    if (!devices) die("ibv_get_device_list");
    if (num_devices == 0) diex("no RDMA device found");

    struct ibv_device *chosen = NULL;
    for (int i = 0; i < num_devices; ++i) {
        const char *dev_name = ibv_get_device_name(devices[i]);
        if (!name || strcmp(name, dev_name) == 0) {
            chosen = devices[i];
            break;
        }
    }
    if (!chosen) diex("requested RDMA device not found");

    printf("open device: %s\n", ibv_get_device_name(chosen));
    struct ibv_context *ctx = ibv_open_device(chosen);
    ibv_free_device_list(devices);
    if (!ctx) die("ibv_open_device");
    return ctx;
}

static int gid_is_zero(union ibv_gid gid) {
    static const union ibv_gid zero;
    return memcmp(&gid, &zero, sizeof(gid)) == 0;
}

static enum ibv_mtu choose_mtu(enum ibv_mtu active_mtu) {
    return active_mtu < IBV_MTU_1024 ? active_mtu : IBV_MTU_1024;
}

static uint32_t make_psn(unsigned seed) {
    return (0x101000u + seed * 0x12345u) & 0xffffffu;
}

static void create_endpoint(struct endpoint *ep, const char *name,
                            struct ibv_pd *pd, struct ibv_cq *cq,
                            int mr_access) {
    memset(ep, 0, sizeof(*ep));
    ep->name = name;
    ep->psn = make_psn(name[0]);

    if (posix_memalign((void **)&ep->buffer, 4096, BUFFER_SIZE) != 0)
        diex("posix_memalign failed");
    memset(ep->buffer, 0, BUFFER_SIZE);

    ep->mr = ibv_reg_mr(pd, ep->buffer, BUFFER_SIZE, mr_access);
    if (!ep->mr) die("ibv_reg_mr");

    struct ibv_qp_init_attr init;
    memset(&init, 0, sizeof(init));
    init.send_cq = cq;
    init.recv_cq = cq;
    init.qp_type = IBV_QPT_RC;
    init.cap.max_send_wr = MAX_WR;
    init.cap.max_recv_wr = MAX_WR;
    init.cap.max_send_sge = MAX_SGE;
    init.cap.max_recv_sge = MAX_SGE;
    init.cap.max_inline_data = 64;

    ep->qp = ibv_create_qp(pd, &init);
    if (!ep->qp) die("ibv_create_qp");

    printf("%s: qpn=%u psn=%u lkey=0x%x rkey=0x%x inline=%u\n", ep->name,
           ep->qp->qp_num, ep->psn, ep->mr->lkey, ep->mr->rkey,
           init.cap.max_inline_data);
}

static void modify_to_init(struct endpoint *ep, int port, int qp_access) {
    struct ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_INIT;
    attr.pkey_index = 0;
    attr.port_num = (uint8_t)port;
    attr.qp_access_flags = qp_access;

    int flags = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT |
                IBV_QP_ACCESS_FLAGS;
    if (ibv_modify_qp(ep->qp, &attr, flags) != 0) die("ibv_modify_qp INIT");
}

static void connect_qp_pair(struct endpoint *local, const struct endpoint *remote,
                            const struct ibv_port_attr *port_attr,
                            union ibv_gid remote_gid, int port,
                            int gid_index) {
    struct ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTR;
    attr.path_mtu = choose_mtu(port_attr->active_mtu);
    attr.dest_qp_num = remote->qp->qp_num;
    attr.rq_psn = remote->psn;
    attr.max_dest_rd_atomic = 4;
    attr.min_rnr_timer = 12;
    attr.ah_attr.dlid = port_attr->lid;
    attr.ah_attr.sl = 0;
    attr.ah_attr.src_path_bits = 0;
    attr.ah_attr.port_num = (uint8_t)port;

    if (port_attr->link_layer == IBV_LINK_LAYER_ETHERNET ||
        !gid_is_zero(remote_gid)) {
        attr.ah_attr.is_global = 1;
        attr.ah_attr.grh.dgid = remote_gid;
        attr.ah_attr.grh.sgid_index = gid_index;
        attr.ah_attr.grh.hop_limit = 1;
    }

    int flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU |
                IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
                IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
    if (ibv_modify_qp(local->qp, &attr, flags) != 0)
        die("ibv_modify_qp RTR");

    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTS;
    attr.timeout = 14;
    attr.retry_cnt = 7;
    attr.rnr_retry = 7;
    attr.sq_psn = local->psn;
    attr.max_rd_atomic = 4;

    flags = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
            IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;
    if (ibv_modify_qp(local->qp, &attr, flags) != 0)
        die("ibv_modify_qp RTS");
}

static const char *wc_opcode_name(enum ibv_wc_opcode opcode) {
    switch (opcode) {
        case IBV_WC_SEND:
            return "SEND";
        case IBV_WC_RECV:
            return "RECV";
        case IBV_WC_RDMA_WRITE:
            return "RDMA_WRITE";
        case IBV_WC_RDMA_READ:
            return "RDMA_READ";
        case IBV_WC_COMP_SWAP:
            return "COMP_SWAP";
        case IBV_WC_FETCH_ADD:
            return "FETCH_ADD";
        case IBV_WC_RECV_RDMA_WITH_IMM:
            return "RECV_RDMA_WITH_IMM";
        default:
            return "OTHER";
    }
}

static void poll_success(struct ibv_cq *cq, int expected, const char *label) {
    int seen = 0;
    uint64_t deadline = now_ms() + TIMEOUT_MS;

    while (seen < expected) {
        struct ibv_wc wc;
        int n = ibv_poll_cq(cq, 1, &wc);
        if (n < 0) diex("ibv_poll_cq failed");
        if (n == 0) {
            if (now_ms() > deadline) diex("timed out polling CQ");
            continue;
        }
        if (wc.status != IBV_WC_SUCCESS) {
            fprintf(stderr, "%s failed: wr_id=%lu status=%s vendor_err=%u\n",
                    label, wc.wr_id, ibv_wc_status_str(wc.status),
                    wc.vendor_err);
            exit(EXIT_FAILURE);
        }
        printf("  CQE %-12s wr_id=%lu opcode=%s byte_len=%u\n", label,
               wc.wr_id, wc_opcode_name(wc.opcode), wc.byte_len);
        ++seen;
    }
}

static void run_send_recv(struct endpoint *a, struct endpoint *b,
                          struct ibv_cq *cq) {
    printf("\n[1] SEND/RECV\n");
    strcpy(a->buffer, "hello from SEND");
    memset(b->buffer, 0, BUFFER_SIZE);

    struct ibv_sge recv_sge;
    memset(&recv_sge, 0, sizeof(recv_sge));
    recv_sge.addr = (uintptr_t)b->buffer;
    recv_sge.length = BUFFER_SIZE;
    recv_sge.lkey = b->mr->lkey;

    struct ibv_recv_wr recv_wr;
    memset(&recv_wr, 0, sizeof(recv_wr));
    recv_wr.wr_id = WRID_RECV;
    recv_wr.sg_list = &recv_sge;
    recv_wr.num_sge = 1;

    struct ibv_recv_wr *bad_recv = NULL;
    if (ibv_post_recv(b->qp, &recv_wr, &bad_recv) != 0)
        die("ibv_post_recv");

    struct ibv_sge send_sge;
    memset(&send_sge, 0, sizeof(send_sge));
    send_sge.addr = (uintptr_t)a->buffer;
    send_sge.length = (uint32_t)(strlen(a->buffer) + 1);
    send_sge.lkey = a->mr->lkey;

    struct ibv_send_wr send_wr;
    memset(&send_wr, 0, sizeof(send_wr));
    send_wr.wr_id = WRID_SEND;
    send_wr.sg_list = &send_sge;
    send_wr.num_sge = 1;
    send_wr.opcode = IBV_WR_SEND;
    send_wr.send_flags = IBV_SEND_SIGNALED;

    struct ibv_send_wr *bad_send = NULL;
    if (ibv_post_send(a->qp, &send_wr, &bad_send) != 0)
        die("ibv_post_send SEND");

    poll_success(cq, 2, "SEND/RECV");
    printf("  B buffer: \"%s\"\n", b->buffer);
}

static void run_write(struct endpoint *a, struct endpoint *b, struct ibv_cq *cq) {
    printf("\n[2] RDMA WRITE\n");
    strcpy(a->buffer, "hello from RDMA WRITE");
    memset(b->buffer, 0, BUFFER_SIZE);

    struct ibv_sge sge;
    memset(&sge, 0, sizeof(sge));
    sge.addr = (uintptr_t)a->buffer;
    sge.length = (uint32_t)(strlen(a->buffer) + 1);
    sge.lkey = a->mr->lkey;

    struct ibv_send_wr wr;
    memset(&wr, 0, sizeof(wr));
    wr.wr_id = WRID_WRITE;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_RDMA_WRITE;
    wr.send_flags = IBV_SEND_SIGNALED;
    wr.wr.rdma.remote_addr = (uintptr_t)b->buffer;
    wr.wr.rdma.rkey = b->mr->rkey;

    struct ibv_send_wr *bad = NULL;
    if (ibv_post_send(a->qp, &wr, &bad) != 0)
        die("ibv_post_send RDMA_WRITE");

    poll_success(cq, 1, "WRITE");
    printf("  B buffer after WRITE: \"%s\"\n", b->buffer);
}

static void run_read(struct endpoint *a, struct endpoint *b, struct ibv_cq *cq) {
    printf("\n[3] RDMA READ\n");
    strcpy(b->buffer, "hello from RDMA READ source");
    memset(a->buffer, 0, BUFFER_SIZE);

    struct ibv_sge sge;
    memset(&sge, 0, sizeof(sge));
    sge.addr = (uintptr_t)a->buffer;
    sge.length = (uint32_t)(strlen(b->buffer) + 1);
    sge.lkey = a->mr->lkey;

    struct ibv_send_wr wr;
    memset(&wr, 0, sizeof(wr));
    wr.wr_id = WRID_READ;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_RDMA_READ;
    wr.send_flags = IBV_SEND_SIGNALED;
    wr.wr.rdma.remote_addr = (uintptr_t)b->buffer;
    wr.wr.rdma.rkey = b->mr->rkey;

    struct ibv_send_wr *bad = NULL;
    if (ibv_post_send(a->qp, &wr, &bad) != 0)
        die("ibv_post_send RDMA_READ");

    poll_success(cq, 1, "READ");
    printf("  A buffer after READ: \"%s\"\n", a->buffer);
}

static void run_atomic_cas(struct endpoint *a, struct endpoint *b,
                           struct ibv_cq *cq) {
    printf("\n[4] Atomic Compare & Swap\n");
    uint64_t *remote_value = (uint64_t *)(void *)(b->buffer + ATOMIC_OFFSET);
    uint64_t *result = (uint64_t *)(void *)(a->buffer + ATOMIC_RESULT_OFFSET);
    *remote_value = 7;
    *result = 0;

    struct ibv_sge sge;
    memset(&sge, 0, sizeof(sge));
    sge.addr = (uintptr_t)result;
    sge.length = sizeof(*result);
    sge.lkey = a->mr->lkey;

    struct ibv_send_wr wr;
    memset(&wr, 0, sizeof(wr));
    wr.wr_id = WRID_CAS;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_ATOMIC_CMP_AND_SWP;
    wr.send_flags = IBV_SEND_SIGNALED;
    wr.wr.atomic.remote_addr = (uintptr_t)remote_value;
    wr.wr.atomic.rkey = b->mr->rkey;
    wr.wr.atomic.compare_add = 7;
    wr.wr.atomic.swap = 11;

    struct ibv_send_wr *bad = NULL;
    if (ibv_post_send(a->qp, &wr, &bad) != 0)
        die("ibv_post_send ATOMIC_CMP_AND_SWP");

    poll_success(cq, 1, "ATOMIC");
    printf("  CAS returned old=%lu remote_now=%lu\n", *result, *remote_value);
}

static void destroy_endpoint(struct endpoint *ep) {
    if (ep->qp) ibv_destroy_qp(ep->qp);
    if (ep->mr) ibv_dereg_mr(ep->mr);
    free(ep->buffer);
}

int main(int argc, char **argv) {
    struct options opt = parse_options(argc, argv);

    struct ibv_context *ctx = open_device(opt.device);

    struct ibv_device_attr dev_attr;
    if (ibv_query_device(ctx, &dev_attr) != 0) die("ibv_query_device");

    struct ibv_port_attr port_attr;
    if (ibv_query_port(ctx, (uint8_t)opt.port, &port_attr) != 0)
        die("ibv_query_port");
    if (port_attr.state != IBV_PORT_ACTIVE)
        diex("selected port is not ACTIVE");

    union ibv_gid gid;
    memset(&gid, 0, sizeof(gid));
    if (ibv_query_gid(ctx, (uint8_t)opt.port, opt.gid_index, &gid) != 0)
        fprintf(stderr, "warning: ibv_query_gid failed; continuing with zero GID\n");

    int atomic_enabled = dev_attr.atomic_cap != IBV_ATOMIC_NONE;
    printf("port %d active, atomic_cap=%s\n", opt.port,
           atomic_enabled ? "supported" : "not supported");

    struct ibv_pd *pd = ibv_alloc_pd(ctx);
    if (!pd) die("ibv_alloc_pd");

    struct ibv_cq *cq = ibv_create_cq(ctx, CQ_DEPTH, NULL, NULL, 0);
    if (!cq) die("ibv_create_cq");

    int access = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE |
                 IBV_ACCESS_REMOTE_READ;
    int qp_access = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;
    if (atomic_enabled) {
        access |= IBV_ACCESS_REMOTE_ATOMIC;
        qp_access |= IBV_ACCESS_REMOTE_ATOMIC;
    }

    struct endpoint a, b;
    create_endpoint(&a, "A", pd, cq, access);
    create_endpoint(&b, "B", pd, cq, access);

    modify_to_init(&a, opt.port, qp_access);
    modify_to_init(&b, opt.port, qp_access);
    connect_qp_pair(&a, &b, &port_attr, gid, opt.port, opt.gid_index);
    connect_qp_pair(&b, &a, &port_attr, gid, opt.port, opt.gid_index);
    printf("connected A <-> B as RC loopback QPs\n");

    run_send_recv(&a, &b, cq);
    run_write(&a, &b, cq);
    run_read(&a, &b, cq);
    if (atomic_enabled) {
        run_atomic_cas(&a, &b, cq);
    } else {
        printf("\n[4] Atomic Compare & Swap\n");
        printf("  skipped: device reports no atomic support\n");
    }

    destroy_endpoint(&a);
    destroy_endpoint(&b);
    if (ibv_destroy_cq(cq) != 0) die("ibv_destroy_cq");
    if (ibv_dealloc_pd(pd) != 0) die("ibv_dealloc_pd");
    if (ibv_close_device(ctx) != 0) die("ibv_close_device");
    return EXIT_SUCCESS;
}
