// Copyright 2026 Feng Ren
//
// Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#define _POSIX_C_SOURCE 200112L

#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>
#include <infiniband/verbs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

enum { BUFFER_SIZE = 4096, DEFAULT_TCP_PORT = 18515 };

struct options {
    const char *device;
    const char *server;
    int is_client;
    int tcp_port;
    int ib_port;
    int gid_index;
    const char *message;
};

struct peer_info {
    uint16_t lid;
    uint32_t qpn;
    uint32_t psn;
    uint32_t rkey;
    uint64_t addr;
    union ibv_gid gid;
};

struct rdma_state {
    struct ibv_context *context;
    struct ibv_pd *pd;
    struct ibv_cq *cq;
    struct ibv_qp *qp;
    struct ibv_mr *mr;
    struct ibv_port_attr port_attr;
    union ibv_gid gid;
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

static int send_all(int fd, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    while (len > 0) {
        ssize_t n = send(fd, p, len, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

static int recv_all(int fd, void *buf, size_t len) {
    char *p = (char *)buf;
    while (len > 0) {
        ssize_t n = recv(fd, p, len, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

static int create_server_socket(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) die("socket");

    int one = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) != 0)
        die("setsockopt");

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) die("bind");
    if (listen(fd, 1) != 0) die("listen");

    int conn = accept(fd, NULL, NULL);
    if (conn < 0) die("accept");
    close(fd);
    return conn;
}

static int create_client_socket(const char *server, int port) {
    char service[16];
    snprintf(service, sizeof(service), "%d", port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    int rc = getaddrinfo(server, service, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
        exit(EXIT_FAILURE);
    }

    int fd = -1;
    for (struct addrinfo *it = res; it; it = it->ai_next) {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, it->ai_addr, it->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) die("connect");
    return fd;
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

    struct ibv_context *ctx = ibv_open_device(chosen);
    ibv_free_device_list(devices);
    if (!ctx) die("ibv_open_device");
    return ctx;
}

static void init_rdma(struct rdma_state *s, const struct options *opt) {
    memset(s, 0, sizeof(*s));
    s->context = open_device(opt->device);

    if (ibv_query_port(s->context, opt->ib_port, &s->port_attr) != 0)
        die("ibv_query_port");
    if (ibv_query_gid(s->context, opt->ib_port, opt->gid_index, &s->gid) != 0)
        die("ibv_query_gid");

    s->pd = ibv_alloc_pd(s->context);
    if (!s->pd) die("ibv_alloc_pd");

    s->cq = ibv_create_cq(s->context, 16, NULL, NULL, 0);
    if (!s->cq) die("ibv_create_cq");

    struct ibv_qp_init_attr qp_init;
    memset(&qp_init, 0, sizeof(qp_init));
    qp_init.send_cq = s->cq;
    qp_init.recv_cq = s->cq;
    qp_init.qp_type = IBV_QPT_RC;
    qp_init.cap.max_send_wr = 16;
    qp_init.cap.max_recv_wr = 1;
    qp_init.cap.max_send_sge = 1;
    qp_init.cap.max_recv_sge = 1;

    s->qp = ibv_create_qp(s->pd, &qp_init);
    if (!s->qp) die("ibv_create_qp");

    if (posix_memalign((void **)&s->buffer, 4096, BUFFER_SIZE) != 0)
        diex("posix_memalign failed");
    memset(s->buffer, 0, BUFFER_SIZE);

    int access = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE |
                 IBV_ACCESS_REMOTE_READ;
    s->mr = ibv_reg_mr(s->pd, s->buffer, BUFFER_SIZE, access);
    if (!s->mr) die("ibv_reg_mr");

    s->psn = (uint32_t)(rand() & 0xffffff);

    struct ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_INIT;
    attr.pkey_index = 0;
    attr.port_num = (uint8_t)opt->ib_port;
    attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;

    int flags = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT |
                IBV_QP_ACCESS_FLAGS;
    if (ibv_modify_qp(s->qp, &attr, flags) != 0) die("ibv_modify_qp INIT");
}

static struct peer_info local_info(const struct rdma_state *s) {
    struct peer_info info;
    memset(&info, 0, sizeof(info));
    info.lid = s->port_attr.lid;
    info.qpn = s->qp->qp_num;
    info.psn = s->psn;
    info.rkey = s->mr->rkey;
    info.addr = (uintptr_t)s->buffer;
    info.gid = s->gid;
    return info;
}

static int gid_is_zero(union ibv_gid gid) {
    static const union ibv_gid zero;
    return memcmp(&gid, &zero, sizeof(gid)) == 0;
}

static enum ibv_mtu choose_mtu(enum ibv_mtu active_mtu) {
    return active_mtu < IBV_MTU_1024 ? active_mtu : IBV_MTU_1024;
}

static void connect_qp(struct rdma_state *s, const struct options *opt,
                       const struct peer_info *remote) {
    struct ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTR;
    attr.path_mtu = choose_mtu(s->port_attr.active_mtu);
    attr.dest_qp_num = remote->qpn;
    attr.rq_psn = remote->psn;
    attr.max_dest_rd_atomic = 1;
    attr.min_rnr_timer = 12;
    attr.ah_attr.dlid = remote->lid;
    attr.ah_attr.sl = 0;
    attr.ah_attr.src_path_bits = 0;
    attr.ah_attr.port_num = (uint8_t)opt->ib_port;

    if (s->port_attr.link_layer == IBV_LINK_LAYER_ETHERNET ||
        !gid_is_zero(remote->gid)) {
        attr.ah_attr.is_global = 1;
        attr.ah_attr.grh.dgid = remote->gid;
        attr.ah_attr.grh.sgid_index = opt->gid_index;
        attr.ah_attr.grh.hop_limit = 1;
    }

    int flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU |
                IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
                IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
    if (ibv_modify_qp(s->qp, &attr, flags) != 0) die("ibv_modify_qp RTR");

    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTS;
    attr.timeout = 14;
    attr.retry_cnt = 7;
    attr.rnr_retry = 7;
    attr.sq_psn = s->psn;
    attr.max_rd_atomic = 1;

    flags = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
            IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;
    if (ibv_modify_qp(s->qp, &attr, flags) != 0) die("ibv_modify_qp RTS");
}

static void exchange_info(int sock, const struct peer_info *local,
                          struct peer_info *remote) {
    if (send_all(sock, local, sizeof(*local)) != 0) die("send peer_info");
    if (recv_all(sock, remote, sizeof(*remote)) != 0) die("recv peer_info");
}

static void post_write(struct rdma_state *s, const struct peer_info *remote,
                       size_t len) {
    struct ibv_sge sge;
    memset(&sge, 0, sizeof(sge));
    sge.addr = (uintptr_t)s->buffer;
    sge.length = (uint32_t)len;
    sge.lkey = s->mr->lkey;

    struct ibv_send_wr wr;
    memset(&wr, 0, sizeof(wr));
    wr.wr_id = 1;
    wr.opcode = IBV_WR_RDMA_WRITE;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.send_flags = IBV_SEND_SIGNALED;
    wr.wr.rdma.remote_addr = remote->addr;
    wr.wr.rdma.rkey = remote->rkey;

    struct ibv_send_wr *bad = NULL;
    if (ibv_post_send(s->qp, &wr, &bad) != 0) die("ibv_post_send");

    for (;;) {
        struct ibv_wc wc;
        int n = ibv_poll_cq(s->cq, 1, &wc);
        if (n < 0) diex("ibv_poll_cq failed");
        if (n == 0) continue;
        if (wc.status != IBV_WC_SUCCESS) {
            fprintf(stderr, "RDMA WRITE failed: %s (%d)\n",
                    ibv_wc_status_str(wc.status), wc.status);
            exit(EXIT_FAILURE);
        }
        return;
    }
}

static void cleanup(struct rdma_state *s) {
    if (s->mr) ibv_dereg_mr(s->mr);
    if (s->qp) ibv_destroy_qp(s->qp);
    if (s->cq) ibv_destroy_cq(s->cq);
    if (s->pd) ibv_dealloc_pd(s->pd);
    if (s->context) ibv_close_device(s->context);
    free(s->buffer);
}

static void usage(const char *prog) {
    fprintf(stderr,
            "Usage:\n"
            "  %s --server [options]\n"
            "  %s --client <server-ip> [options]\n\n"
            "Options:\n"
            "  -d, --device <name>       RDMA device, e.g. mlx5_0 or rxe0\n"
            "  -p, --tcp-port <port>    TCP control port (default: %d)\n"
            "  -i, --ib-port <port>     RDMA port (default: 1)\n"
            "  -g, --gid-index <index>  GID index (default: 0)\n"
            "  -m, --message <text>     Message written by client\n",
            prog, prog, DEFAULT_TCP_PORT);
}

static struct options parse_options(int argc, char **argv) {
    struct options opt;
    memset(&opt, 0, sizeof(opt));
    opt.tcp_port = DEFAULT_TCP_PORT;
    opt.ib_port = 1;
    opt.gid_index = 0;
    opt.message = "hello one-sided rdma";

    enum { OPT_SERVER = 1000, OPT_CLIENT };
    static const struct option long_opts[] = {
        {"server", no_argument, NULL, OPT_SERVER},
        {"client", required_argument, NULL, OPT_CLIENT},
        {"device", required_argument, NULL, 'd'},
        {"tcp-port", required_argument, NULL, 'p'},
        {"ib-port", required_argument, NULL, 'i'},
        {"gid-index", required_argument, NULL, 'g'},
        {"message", required_argument, NULL, 'm'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0},
    };

    int mode_seen = 0;
    for (;;) {
        int c = getopt_long(argc, argv, "d:p:i:g:m:h", long_opts, NULL);
        if (c == -1) break;
        switch (c) {
            case OPT_SERVER:
                mode_seen = 1;
                opt.is_client = 0;
                opt.server = NULL;
                break;
            case OPT_CLIENT:
                mode_seen = 1;
                opt.is_client = 1;
                opt.server = optarg;
                break;
            case 'd':
                opt.device = optarg;
                break;
            case 'p':
                opt.tcp_port = atoi(optarg);
                break;
            case 'i':
                opt.ib_port = atoi(optarg);
                break;
            case 'g':
                opt.gid_index = atoi(optarg);
                break;
            case 'm':
                opt.message = optarg;
                break;
            case 'h':
            default:
                usage(argv[0]);
                exit(c == 'h' ? EXIT_SUCCESS : EXIT_FAILURE);
        }
    }

    if (!mode_seen) {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }
    return opt;
}

int main(int argc, char **argv) {
    struct options opt = parse_options(argc, argv);
    srand(0x101);

    int sock = opt.is_client ? create_client_socket(opt.server, opt.tcp_port)
                             : create_server_socket(opt.tcp_port);

    struct rdma_state state;
    init_rdma(&state, &opt);

    struct peer_info local = local_info(&state);
    struct peer_info remote;
    exchange_info(sock, &local, &remote);
    connect_qp(&state, &opt, &remote);

    if (opt.is_client) {
        size_t len = strlen(opt.message) + 1;
        if (len > BUFFER_SIZE) diex("message too long");
        memcpy(state.buffer, opt.message, len);
        post_write(&state, &remote, len);
        if (send_all(sock, "D", 1) != 0) die("send done");
        printf("client: RDMA WRITE completed, wrote %zu bytes\n", len);
    } else {
        char done;
        if (recv_all(sock, &done, 1) != 0) die("recv done");
        printf("server: buffer after RDMA WRITE: \"%s\"\n", state.buffer);
    }

    close(sock);
    cleanup(&state);
    return 0;
}
