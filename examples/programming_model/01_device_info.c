// Copyright 2026 Feng Ren
//
// Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#define _POSIX_C_SOURCE 200112L

#include <errno.h>
#include <getopt.h>
#include <infiniband/verbs.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct options {
    const char *device;
    int port;
    int gid_index;
};

static const char *link_layer_name(uint8_t layer) {
    switch (layer) {
        case IBV_LINK_LAYER_UNSPECIFIED:
            return "Unspecified";
        case IBV_LINK_LAYER_INFINIBAND:
            return "InfiniBand";
        case IBV_LINK_LAYER_ETHERNET:
            return "Ethernet";
        default:
            return "Unknown";
    }
}

static void print_gid(union ibv_gid gid) {
    for (int i = 0; i < 16; ++i) {
        printf("%02x", gid.raw[i]);
        if (i % 2 == 1 && i != 15) printf(":");
    }
}

static void usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [options]\n\n"
            "Options:\n"
            "  -d, --device <name>      Only inspect this RDMA device\n"
            "  -p, --port <port>        Port to inspect in detail (default: 1)\n"
            "  -g, --gid-index <index>  GID index to print (default: 0)\n",
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
    return opt;
}

static void print_device_summary(struct ibv_device *dev, int index) {
    uint64_t guid = ibv_get_device_guid(dev);
    printf("[%d] %-16s node_guid=0x%016" PRIx64 "\n", index,
           ibv_get_device_name(dev), guid);
}

static void inspect_device(struct ibv_device *dev, const struct options *opt) {
    struct ibv_context *ctx = ibv_open_device(dev);
    if (!ctx) {
        fprintf(stderr, "  open failed: %s\n", strerror(errno));
        return;
    }

    struct ibv_device_attr dev_attr;
    if (ibv_query_device(ctx, &dev_attr) != 0) {
        fprintf(stderr, "  ibv_query_device failed: %s\n", strerror(errno));
        ibv_close_device(ctx);
        return;
    }

    printf("\nDevice %s\n", ibv_get_device_name(dev));
    printf("  fw_ver:          %s\n", dev_attr.fw_ver);
    printf("  phys_port_cnt:   %u\n", dev_attr.phys_port_cnt);
    printf("  max_qp:          %d\n", dev_attr.max_qp);
    printf("  max_cq:          %d\n", dev_attr.max_cq);
    printf("  max_mr:          %d\n", dev_attr.max_mr);
    printf("  max_qp_wr:       %d\n", dev_attr.max_qp_wr);
    printf("  max_sge:         %d\n", dev_attr.max_sge);
    printf("  max_cqe:         %d\n", dev_attr.max_cqe);
    printf("  max_inline_data: not a device attr; query it after QP creation\n");

    if (opt->port >= 1 && opt->port <= dev_attr.phys_port_cnt) {
        struct ibv_port_attr port_attr;
        if (ibv_query_port(ctx, (uint8_t)opt->port, &port_attr) == 0) {
            printf("  port %d:\n", opt->port);
            printf("    state:        %s (%d)\n",
                   ibv_port_state_str(port_attr.state), port_attr.state);
            printf("    link_layer:   %s\n",
                   link_layer_name(port_attr.link_layer));
            printf("    lid:          0x%x\n", port_attr.lid);
            printf("    active_mtu:   %d\n", 1 << (port_attr.active_mtu + 7));
            printf("    max_msg_sz:   %u\n", port_attr.max_msg_sz);
        } else {
            fprintf(stderr, "  ibv_query_port(%d) failed: %s\n", opt->port,
                    strerror(errno));
        }

        union ibv_gid gid;
        if (ibv_query_gid(ctx, (uint8_t)opt->port, opt->gid_index, &gid) == 0) {
            printf("    gid[%d]:      ", opt->gid_index);
            print_gid(gid);
            printf("\n");
        } else {
            fprintf(stderr, "  ibv_query_gid(port=%d,index=%d) failed: %s\n",
                    opt->port, opt->gid_index, strerror(errno));
        }
    } else {
        printf("  port %d is outside valid range 1..%u\n", opt->port,
               dev_attr.phys_port_cnt);
    }

    ibv_close_device(ctx);
}

int main(int argc, char **argv) {
    struct options opt = parse_options(argc, argv);

    int num_devices = 0;
    struct ibv_device **devices = ibv_get_device_list(&num_devices);
    if (!devices) {
        perror("ibv_get_device_list");
        return EXIT_FAILURE;
    }

    printf("Found %d RDMA device(s)\n", num_devices);
    for (int i = 0; i < num_devices; ++i) {
        print_device_summary(devices[i], i);
    }

    if (num_devices == 0) {
        ibv_free_device_list(devices);
        return EXIT_FAILURE;
    }

    for (int i = 0; i < num_devices; ++i) {
        if (opt.device && strcmp(opt.device, ibv_get_device_name(devices[i])) != 0)
            continue;
        inspect_device(devices[i], &opt);
    }

    ibv_free_device_list(devices);
    return EXIT_SUCCESS;
}
