# Copyright 2026 Feng Ren
# Licensed under the Apache License, Version 2.0.
# SPDX-License-Identifier: Apache-2.0
PYTHON ?= python3
CHECK_ARGS ?=
all:
	$(MAKE) -C one_sided_write
	$(MAKE) -C programming_model
	$(MAKE) -C pipeline
check: all
	$(PYTHON) run_tests.py $(CHECK_ARGS)
clean:
	$(MAKE) -C one_sided_write clean
	$(MAKE) -C programming_model clean
	$(MAKE) -C pipeline clean
.PHONY: all check clean
