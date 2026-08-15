# Copyright 2026 Feng Ren
#
# Licensed under the Apache License, Version 2.0.
# SPDX-License-Identifier: Apache-2.0

CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
LDLIBS ?= -libverbs

PROGRAMS := 01_device_info 02_resource_lifecycle 03_rc_loopback

all: $(PROGRAMS)

01_device_info: 01_device_info.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

02_resource_lifecycle: 02_resource_lifecycle.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

03_rc_loopback: 03_rc_loopback.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f $(PROGRAMS)

.PHONY: all clean
