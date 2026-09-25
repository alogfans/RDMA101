// Copyright 2026 Feng Ren
// Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <getopt.h>
#include <infiniband/verbs.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct options {
    const char *device;
    unsigned port, gid, depth, batch, size, iterations, timeout_ms;
};
struct endpoint {
    struct ibv_qp *qp;
    struct ibv_mr *mr;
    unsigned char *data;
    uint32_t psn;
};
struct slot {
    uint64_t sequence;
    int busy;
};

/* Failure is process-fatal: never deregister/free possibly in-flight memory.
 * This teaching program does not implement in-process recovery. */
static void fail(const char *message) {
    fprintf(stderr, "FAIL: %s\n", message);
    exit(EXIT_FAILURE);
}
static void check_rc(int rc, const char *operation) {
    if (rc) {
        fprintf(stderr, "FAIL: %s: %s (%d)\n", operation, strerror(rc), rc);
        exit(EXIT_FAILURE);
    }
}
static uint64_t now_ns(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) fail("clock_gettime");
    return (uint64_t)t.tv_sec * 1000000000 + (uint64_t)t.tv_nsec;
}
static unsigned number(const char *s, unsigned min, unsigned max) {
    char *end;
    errno = 0;
    unsigned long n = strtoul(s, &end, 10);
    if (errno || !*s || *end || *s == '-' || n < min || n > max)
        fail("numeric argument out of range");
    return (unsigned)n;
}
static struct options parse(int argc, char **argv) {
    struct options o = {.port=1, .depth=16, .batch=4, .size=4096,
                        .iterations=1000, .timeout_ms=5000};
    const struct option opts[] = {
        {"device", required_argument, NULL, 'd'},
        {"port", required_argument, NULL, 'p'},
        {"gid-index", required_argument, NULL, 'g'},
        {"depth", required_argument, NULL, 'q'},
        {"batch", required_argument, NULL, 'b'},
        {"size", required_argument, NULL, 's'},
        {"iterations", required_argument, NULL, 'n'},
        {"timeout-ms", required_argument, NULL, 't'},
        {"help", no_argument, NULL, 'h'}, {NULL, 0, NULL, 0}
    };
    int c;
    while ((c = getopt_long(argc, argv, "d:p:g:q:b:s:n:t:h", opts, NULL)) != -1) {
        switch (c) {
        case 'd': o.device = optarg; break;
        case 'p': o.port = number(optarg, 1, 255); break;
        case 'g': o.gid = number(optarg, 0, 255); break;
        case 'q': o.depth = number(optarg, 1, 4096); break;
        case 'b': o.batch = number(optarg, 1, 4096); break;
        case 's': o.size = number(optarg, 8, 1048576); break;
        case 'n': o.iterations = number(optarg, 1, 10000000); break;
        case 't': o.timeout_ms = number(optarg, 1, 600000); break;
        case 'h':
            puts("Usage: write_pipeline [-d device] [-p port] [-g gid-index]\n"
                 "  --depth N       outstanding slot limit (default 16)\n"
                 "  --batch N       WRs per post call, <= depth (default 4)\n"
                 "  --size N        bytes per WRITE, >= 8 (default 4096)\n"
                 "  --iterations N  total WRs (default 1000)\n"
                 "  --timeout-ms N  no-progress timeout (default 5000)\n"
                 "Uses two local RC QPs; verifies EVERY completed payload.\n"
                 "Exit 77 means no visible RDMA device. No simulated backend.");
            exit(0);
        default: fail("invalid option; use --help");
        }
    }
    if (optind != argc || o.batch > o.depth) fail("batch must be <= depth; no positional arguments");
    if ((uint64_t)o.size * o.depth > 64 * 1024 * 1024)
        fail("each registered pool must be <= 64 MiB");
    return o;
}
static struct ibv_context *open_context(const char *name) {
    int count;
    struct ibv_device **list = ibv_get_device_list(&count);
    if (!list) fail("ibv_get_device_list");
    if (!count) {
        ibv_free_device_list(list);
        puts("SKIP: no visible RDMA device; use hardware or configure RXE");
        exit(77);
    }
    struct ibv_context *ctx = NULL;
    for (int i=0; i<count; ++i) {
        if (!name || !strcmp(name, ibv_get_device_name(list[i]))) {
            printf("device=%s\n", ibv_get_device_name(list[i]));
            ctx = ibv_open_device(list[i]);
            break;
        }
    }
    ibv_free_device_list(list);
    if (!ctx) fail("requested device unavailable or could not be opened");
    return ctx;
}
static void create_endpoint(struct endpoint *e, struct ibv_pd *pd,
                            struct ibv_cq *cq, const struct options *o,
                            uint32_t psn) {
    size_t bytes = (size_t)o->depth * o->size;
    e->psn = psn;
    int rc = posix_memalign((void **)&e->data, 4096, bytes);
    check_rc(rc, "posix_memalign");
    memset(e->data, 0, bytes);
    e->mr = ibv_reg_mr(pd, e->data, bytes,
                      IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
    if (!e->mr) fail("ibv_reg_mr (check memlock and device resources)");
    struct ibv_qp_init_attr init = {
        .send_cq=cq, .recv_cq=cq, .qp_type=IBV_QPT_RC,
        .cap={.max_send_wr=o->depth, .max_recv_wr=1,
              .max_send_sge=1, .max_recv_sge=1},
    };
    e->qp = ibv_create_qp(pd, &init);
    if (!e->qp) fail("ibv_create_qp (try a smaller depth)");
    if (init.cap.max_send_wr < o->depth) fail("insufficient QP capacity");
    struct ibv_qp_attr a = {.qp_state=IBV_QPS_INIT, .port_num=o->port,
                           .pkey_index=0, .qp_access_flags=IBV_ACCESS_REMOTE_WRITE};
    check_rc(ibv_modify_qp(e->qp, &a, IBV_QP_STATE | IBV_QP_PORT |
             IBV_QP_PKEY_INDEX | IBV_QP_ACCESS_FLAGS), "QP INIT");
}
static void connect_endpoint(struct endpoint *a, const struct endpoint *b,
                             const struct options *o,
                             const struct ibv_port_attr *port, union ibv_gid gid) {
    struct ibv_qp_attr q = {
        .qp_state=IBV_QPS_RTR, .dest_qp_num=b->qp->qp_num, .rq_psn=b->psn,
        .path_mtu=port->active_mtu < IBV_MTU_1024 ? port->active_mtu : IBV_MTU_1024,
        .max_dest_rd_atomic=0, .min_rnr_timer=12,
        .ah_attr={.dlid=port->lid, .port_num=o->port},
    };
    if (port->link_layer == IBV_LINK_LAYER_ETHERNET) {
        q.ah_attr.is_global = 1;
        q.ah_attr.grh.dgid = gid;
        q.ah_attr.grh.sgid_index = o->gid;
        q.ah_attr.grh.hop_limit = 1;
    }
    check_rc(ibv_modify_qp(a->qp, &q, IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU |
             IBV_QP_DEST_QPN | IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC |
             IBV_QP_MIN_RNR_TIMER), "QP RTR");
    memset(&q, 0, sizeof(q));
    q.qp_state = IBV_QPS_RTS;
    q.sq_psn = a->psn;
    q.timeout = 14;
    q.retry_cnt = 3;
    q.rnr_retry = 3;
    q.max_rd_atomic = 0; /* Only WRITE, no READ or Atomic. */
    check_rc(ibv_modify_qp(a->qp, &q, IBV_QP_STATE | IBV_QP_SQ_PSN |
             IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY |
             IBV_QP_MAX_QP_RD_ATOMIC), "QP RTS");
}
/* Sequence and byte position both affect the pattern; repeated constants
 * would fail to detect a previous iteration's contents. */
static unsigned char pattern(uint64_t sequence, unsigned offset) {
    return (unsigned char)((sequence >> (8 * (offset % 8))) ^ (offset * 31u));
}
static void run(struct endpoint *src, struct endpoint *dst,
                struct ibv_cq *cq, const struct options *o) {
    struct slot *slots = calloc(o->depth, sizeof(*slots));
    struct ibv_sge *sges = calloc(o->batch, sizeof(*sges));
    struct ibv_send_wr *wrs = calloc(o->batch, sizeof(*wrs));
    if (!slots || !sges || !wrs) fail("allocate descriptors");
    uint64_t submitted=0, completed=0, calls=0;
    unsigned inflight=0, peak=0;
    uint64_t start=now_ns(), last_progress=start;
    while (completed < o->iterations) {
        unsigned count=0;
        while (count < o->batch && submitted + count < o->iterations &&
               inflight + count < o->depth) {
            uint64_t sequence = submitted + count;
            unsigned index = sequence % o->depth;
            if (slots[index].busy) break;
            unsigned char *source = src->data + (size_t)index * o->size;
            unsigned char *target = dst->data + (size_t)index * o->size;
            slots[index] = (struct slot){.sequence=sequence, .busy=1};
            for (unsigned j=0; j<o->size; ++j) {
                source[j] = pattern(sequence, j);
                target[j] = (unsigned char)~source[j];
            }
            sges[count] = (struct ibv_sge){.addr=(uintptr_t)source,
                .length=o->size, .lkey=src->mr->lkey};
            wrs[count] = (struct ibv_send_wr){.wr_id=sequence,
                .sg_list=&sges[count], .num_sge=1, .opcode=IBV_WR_RDMA_WRITE,
                .send_flags=IBV_SEND_SIGNALED,
                .wr.rdma={.remote_addr=(uintptr_t)target, .rkey=dst->mr->rkey}};
            if (count) wrs[count-1].next = &wrs[count];
            ++count;
        }
        if (count) {
            struct ibv_send_wr *bad = NULL;
            int rc = ibv_post_send(src->qp, wrs, &bad);
            if (rc) {
                fprintf(stderr, "partial post possible; first rejected wr_id=%" PRIu64 "\n",
                        bad ? bad->wr_id : UINT64_MAX);
                check_rc(rc, "ibv_post_send"); /* Do not replay or free buffers. */
            }
            submitted += count;
            inflight += count;
            ++calls;
            if (inflight > peak) peak = inflight;
        }
        struct ibv_wc wc[32];
        int n = ibv_poll_cq(cq, 32, wc);
        if (n < 0) fail("ibv_poll_cq");
        for (int i=0; i<n; ++i) {
            if (wc[i].status != IBV_WC_SUCCESS) {
                fprintf(stderr, "wr_id=%" PRIu64 " status=%s qp=%u vendor=%u\n",
                        wc[i].wr_id, ibv_wc_status_str(wc[i].status),
                        wc[i].qp_num, wc[i].vendor_err);
                fail("WRITE completion failed");
            }
            unsigned index = wc[i].wr_id % o->depth;
            if (wc[i].opcode != IBV_WC_RDMA_WRITE || !slots[index].busy ||
                slots[index].sequence != wc[i].wr_id) fail("unexpected or duplicate completion");
            unsigned char *target = dst->data + (size_t)index * o->size;
            for (unsigned j=0; j<o->size; ++j) {
                if (target[j] != pattern(wc[i].wr_id, j)) {
                    fprintf(stderr, "content mismatch: sequence=%" PRIu64 " byte=%u\n", wc[i].wr_id, j);
                    fail("payload verification");
                }
            }
            /* Same-process consumer has now finished. A remote consumer
             * would need to send an explicit acknowledgement/credit. */
            slots[index].busy = 0;
            --inflight;
            ++completed;
        }
        uint64_t now = now_ns();
        if (n) last_progress = now;
        if (now - last_progress > (uint64_t)o->timeout_ms * 1000000)
            fail("no progress; device work may still be outstanding (process exits)");
    }
    double seconds = (now_ns() - start) / 1e9;
    printf("PASS: submitted=%" PRIu64 " completed=%" PRIu64 " verified=%" PRIu64 "\n",
           submitted, completed, completed);
    printf("post_calls=%" PRIu64 " peak_inflight=%u depth=%u batch=%u\n", calls, peak, o->depth, o->batch);
    printf("local_loopback_seconds=%.6f verified_MiB_per_s=%.2f\n", seconds,
           (double)completed * o->size / (1024 * 1024) / seconds);
    puts("Timing includes CPU fill/check; this is NOT a cross-host network benchmark.");
    free(wrs); free(sges); free(slots);
}
int main(int argc, char **argv) {
    struct options o = parse(argc, argv);
    struct ibv_context *ctx = open_context(o.device);
    struct ibv_port_attr port;
    check_rc(ibv_query_port(ctx, o.port, &port), "ibv_query_port");
    if (port.state != IBV_PORT_ACTIVE) fail("selected port is not ACTIVE");
    union ibv_gid gid = {0};
    if (port.link_layer == IBV_LINK_LAYER_ETHERNET)
        check_rc(ibv_query_gid(ctx, o.port, o.gid, &gid), "ibv_query_gid");
    struct ibv_pd *pd = ibv_alloc_pd(ctx);
    if (!pd) fail("ibv_alloc_pd");
    struct ibv_cq *cq = ibv_create_cq(ctx, (int)o.depth, NULL, NULL, 0);
    if (!cq) fail("ibv_create_cq");
    struct endpoint a={0}, b={0};
    create_endpoint(&a, pd, cq, &o, 100);
    create_endpoint(&b, pd, cq, &o, 200);
    connect_endpoint(&a, &b, &o, &port, gid);
    connect_endpoint(&b, &a, &o, &port, gid);
    printf("RC loopback: port=%u gid_index=%u size=%u iterations=%u\n",
           o.port, o.gid, o.size, o.iterations);
    run(&a, &b, cq, &o);
    check_rc(ibv_destroy_qp(a.qp), "destroy QP A");
    check_rc(ibv_destroy_qp(b.qp), "destroy QP B");
    check_rc(ibv_dereg_mr(a.mr), "deregister MR A");
    check_rc(ibv_dereg_mr(b.mr), "deregister MR B");
    free(a.data); free(b.data);
    check_rc(ibv_destroy_cq(cq), "destroy CQ");
    check_rc(ibv_dealloc_pd(pd), "deallocate PD");
    check_rc(ibv_close_device(ctx), "close device");
    return 0;
}
