# Copyright 2026 Feng Ren
# Licensed under the Apache License, Version 2.0.
# SPDX-License-Identifier: Apache-2.0
"""Verify producer -> copy stream -> CPU consumption dependencies (not RDMA)."""
import argparse
import sys


def positive(text):
    value = int(text)
    if not 1 <= value <= 1000000:
        raise argparse.ArgumentTypeError("expected an integer in [1, 1000000]")
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iterations", type=positive, default=10)
    parser.add_argument("--elements", type=positive, default=4096)
    args = parser.parse_args()
    try:
        import torch
    except ImportError:
        print("SKIP: PyTorch is not installed")
        return 77
    if not torch.cuda.is_available():
        print("SKIP: no CUDA device available to PyTorch")
        return 77
    device = torch.device("cuda:0")
    producer = torch.cuda.Stream(device=device)
    copier = torch.cuda.Stream(device=device)
    ready = torch.cuda.Event()
    copied = torch.cuda.Event()
    gpu = torch.empty(args.elements, device=device, dtype=torch.int64)
    host = torch.empty(args.elements, dtype=torch.int64, pin_memory=True)
    # Include allocator/default-stream work before starting other streams.
    producer.wait_stream(torch.cuda.current_stream(device))
    print(f"torch={torch.__version__} cuda={torch.version.cuda} device={torch.cuda.get_device_name(device)}")
    for sequence in range(args.iterations):
        # The previous iteration ended only after the copy was synchronized.
        with torch.cuda.stream(producer):
            gpu.fill_(sequence + 1)
            ready.record(producer)
        with torch.cuda.stream(copier):
            copier.wait_event(ready)
            host.copy_(gpu, non_blocking=True)
            copied.record(copier)
        copied.synchronize()  # CPU must wait before inspecting the pinned buffer.
        if not bool(torch.all(host == sequence + 1)):
            raise RuntimeError(f"data mismatch at iteration {sequence}")
    print(f"PASS: {args.iterations} GPU->pinned-host copies verified after event synchronization")
    print("This checks CUDA stream dependencies; it does not test GPUDirect RDMA visibility.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
