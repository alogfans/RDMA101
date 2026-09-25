# Copyright 2026 Feng Ren
# Licensed under the Apache License, Version 2.0.
# SPDX-License-Identifier: Apache-2.0
CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror -std=c11
LDLIBS ?= -libverbs
all: write_pipeline
write_pipeline: write_pipeline.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)
clean:
	rm -f write_pipeline
.PHONY: all clean
