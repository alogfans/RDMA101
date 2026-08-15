// Copyright 2026 Feng Ren
//
// Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#define _POSIX_C_SOURCE 200112L

#include <errno.h>
#include <getopt.h>
#include <infiniband/verbs.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum {
    DEFAULT_BUFFER_SIZE = 4096,
    DEFAULT_CQE = 16,
    DEFAULT_WR = 16,
    DEFAULT_SGE = 1,
    DEFAULT_INLINE = 64,
};

struct options {
    const char *device;
    int port;
    size_t buffer_size;
    int cqe;
    int wr;
    int sge;
    int inline_data;
};

static void die(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

static void diex(const char *msg) {
    fprintf(stderr, "%s\n", msg);
    exit(EXIT_FAILURE);
}

static void usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [options]\n\n"
            "Options:\n"
            "  -d, --device <name>      RDMA device, e.g. mlx5_0 or rxe0\n"
            "  -p, --port <port>        RDMA port (default: 1)\n"
            "  -s, --size <bytes>       MR buffer size (default: 4096)\n"
            "  -c, --cqe <count>        Requested CQE count (default: 16)\n"
            "  -w, --wr <count>         Requested send/recv WR count (default: 16)\n"
            "  -e, --sge <count>        Requested send/recv SGE count (default: 1)\n"
            "  -i, --inline <bytes>     Requested inline data size (default: 64)\n",
            prog);
}

static struct options parse_options(int argc, char **argv) {
    struct options opt;
    memset(&opt, 0, sizeof(opt));
    opt.port = 1;
    opt.buffer_size = DEFAULT_BUFFER_SIZE;
    opt.cqe = DEFAULT_CQE;
    opt.wr = DEFAULT_WR;
    opt.sge = DEFAULT_SGE;
    opt.inline_data = DEFAULT_INLINE;

    static const struct option long_opts[] = {
        {"device", required_argument, NULL, 'd'},
        {"port", required_argument, NULL, 'p'},
        {"size", required_argument, NULL, 's'},
        {"cqe", required_argument, NULL, 'c'},
        {"wr", required_argument, NULL, 'w'},
        {"sge", required_argument, NULL, 'e'},
        {"inline", required_argument, NULL, 'i'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0},
    };

    for (;;) {
        int c = getopt_long(argc, argv, "d:p:s:c:w:e:i:h", long_opts, NULL);
        if (c == -1) break;
        switch (c) {
            case 'd':
                opt.device = optarg;
                break;
            case 'p':
                opt.port = atoi(optarg);
                break;
            case 's':
                opt.buffer_size = (size_t)strtoull(optarg, NULL, 0);
                break;
            case 'c':
                opt.cqe = atoi(optarg);
                break;
            case 'w':
                opt.wr = atoi(optarg);
                break;
            case 'e':
                opt.sge = atoi(optarg);
                break;
            case 'i':
                opt.inline_data = atoi(optarg);
                break;
            case 'h':
                usage(argv[0]);
                exit(EXIT_SUCCESS);
            default:
                usage(argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if (opt.port <= 0 || opt.buffer_size == 0 || opt.cqe <= 0 ||
        opt.wr <= 0 || opt.sge <= 0 || opt.inline_data < 0) {
        diex("invalid option value");
    }
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

static const char *qp_state_name(enum ibv_qp_state state) {
    switch (state) {
        case IBV_QPS_RESET:
            return "RESET";
        case IBV_QPS_INIT:
            return "INIT";
        case IBV_QPS_RTR:
            return "RTR";
        case IBV_QPS_RTS:
            return "RTS";
        case IBV_QPS_SQD:
            return "SQD";
        case IBV_QPS_SQE:
            return "SQE";
        case IBV_QPS_ERR:
            return "ERR";
        default:
            return "UNKNOWN";
    }
}

static void query_qp_state(struct ibv_qp *qp, const char *label) {
    struct ibv_qp_attr attr;
    struct ibv_qp_init_attr init_attr;
    memset(&attr, 0, sizeof(attr));
    memset(&init_attr, 0, sizeof(init_attr));
    if (ibv_query_qp(qp, &attr, IBV_QP_STATE, &init_attr) != 0)
        die("ibv_query_qp");
    printf("%s qp_state=%s\n", label, qp_state_name(attr.qp_state));
}

int main(int argc, char **argv) {
    struct options opt = parse_options(argc, argv);

    struct ibv_context *ctx = open_device(opt.device);

    struct ibv_device_attr dev_attr;
    if (ibv_query_device(ctx, &dev_attr) != 0) die("ibv_query_device");
    printf("device limits: max_qp_wr=%d max_sge=%d max_cqe=%d\n",
           dev_attr.max_qp_wr, dev_attr.max_sge, dev_attr.max_cqe);

    struct ibv_port_attr port_attr;
    if (ibv_query_port(ctx, (uint8_t)opt.port, &port_attr) != 0)
        die("ibv_query_port");
    printf("port %d state=%s active_mtu=%d\n", opt.port,
           ibv_port_state_str(port_attr.state), 1 << (port_attr.active_mtu + 7));

    struct ibv_pd *pd = ibv_alloc_pd(ctx);
    if (!pd) die("ibv_alloc_pd");
    printf("allocated PD %p\n", (void *)pd);

    struct ibv_cq *cq = ibv_create_cq(ctx, opt.cqe, NULL, NULL, 0);
    if (!cq) die("ibv_create_cq");
    printf("created CQ request_cqe=%d\n", opt.cqe);

    struct ibv_qp_init_attr qp_init;
    memset(&qp_init, 0, sizeof(qp_init));
    qp_init.send_cq = cq;
    qp_init.recv_cq = cq;
    qp_init.qp_type = IBV_QPT_RC;
    qp_init.cap.max_send_wr = (uint32_t)opt.wr;
    qp_init.cap.max_recv_wr = (uint32_t)opt.wr;
    qp_init.cap.max_send_sge = (uint32_t)opt.sge;
    qp_init.cap.max_recv_sge = (uint32_t)opt.sge;
    qp_init.cap.max_inline_data = (uint32_t)opt.inline_data;

    printf("create QP requested: wr=%d sge=%d inline=%d\n", opt.wr, opt.sge,
           opt.inline_data);
    struct ibv_qp *qp = ibv_create_qp(pd, &qp_init);
    if (!qp) die("ibv_create_qp");
    printf("created QP qpn=%u actual_send_wr=%u actual_recv_wr=%u "
           "actual_send_sge=%u actual_recv_sge=%u actual_inline=%u\n",
           qp->qp_num, qp_init.cap.max_send_wr, qp_init.cap.max_recv_wr,
           qp_init.cap.max_send_sge, qp_init.cap.max_recv_sge,
           qp_init.cap.max_inline_data);
    query_qp_state(qp, "after create");

    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) page_size = 4096;

    void *buffer = NULL;
    if (posix_memalign(&buffer, (size_t)page_size, opt.buffer_size) != 0)
        diex("posix_memalign failed");
    memset(buffer, 0x5a, opt.buffer_size);

    int access = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE |
                 IBV_ACCESS_REMOTE_READ;
    struct ibv_mr *mr = ibv_reg_mr(pd, buffer, opt.buffer_size, access);
    if (!mr) die("ibv_reg_mr");
    printf("registered MR addr=%p length=%zu lkey=0x%x rkey=0x%x\n", mr->addr,
           mr->length, mr->lkey, mr->rkey);

    struct ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_INIT;
    attr.pkey_index = 0;
    attr.port_num = (uint8_t)opt.port;
    attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;
    int flags = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT |
                IBV_QP_ACCESS_FLAGS;
    if (ibv_modify_qp(qp, &attr, flags) != 0) die("ibv_modify_qp INIT");
    query_qp_state(qp, "after modify to INIT");

    if (ibv_dereg_mr(mr) != 0) die("ibv_dereg_mr");
    printf("deregistered MR\n");
    free(buffer);

    if (ibv_destroy_qp(qp) != 0) die("ibv_destroy_qp");
    printf("destroyed QP\n");
    if (ibv_destroy_cq(cq) != 0) die("ibv_destroy_cq");
    printf("destroyed CQ\n");
    if (ibv_dealloc_pd(pd) != 0) die("ibv_dealloc_pd");
    printf("deallocated PD\n");
    if (ibv_close_device(ctx) != 0) die("ibv_close_device");
    printf("closed device\n");
    return EXIT_SUCCESS;
}
