# Copyright 2026 Feng Ren
#
# Licensed under the Apache License, Version 2.0.
# SPDX-License-Identifier: Apache-2.0

CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
LDLIBS ?= -libverbs

all: one_sided_write

one_sided_write: one_sided_write.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f one_sided_write

.PHONY: all clean
