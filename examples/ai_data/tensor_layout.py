# Copyright 2026 Feng Ren
# Licensed under the Apache License, Version 2.0.
# SPDX-License-Identifier: Apache-2.0
"""Inspect a 2-D view and pack its logical elements; no third-party dependency."""
import argparse
import itertools
import struct
import sys


def element_offsets(shape, strides, storage_offset=0):
    """Offsets are in elements, as in Tensor strides; this demo accepts 2-D views."""
    if len(shape) != 2 or len(strides) != 2:
        raise ValueError("this example requires two dimensions")
    if any(n <= 0 for n in shape) or any(s < 0 for s in strides) or storage_offset < 0:
        raise ValueError("positive shapes and nonnegative strides/offset required")
    return [storage_offset + row * strides[0] + col * strides[1]
            for row, col in itertools.product(range(shape[0]), range(shape[1]))]


def pack_view(storage, shape, strides, storage_offset=0):
    offsets = element_offsets(shape, strides, storage_offset)
    if max(offsets) >= len(storage):
        raise ValueError("view reaches outside storage")
    # Explicit wire dtype: little-endian signed 64-bit integers.
    return b"".join(struct.pack("<q", storage[i]) for i in offsets)


def unpack(payload):
    if len(payload) % 8:
        raise ValueError("payload is not a whole number of int64 elements")
    return [v[0] for v in struct.iter_unpack("<q", payload)]


def run(check_torch=False):
    storage = list(range(6))
    transposed = unpack(pack_view(storage, (3, 2), (1, 3)))
    raw_copy = unpack(pack_view(storage, (2, 3), (3, 1)))
    if transposed != [0, 3, 1, 4, 2, 5] or raw_copy == transposed:
        raise RuntimeError("transpose packing check failed")
    # A sliced view has 3 elements, but spans 5 storage elements.
    offsets = element_offsets((1, 3), (6, 2), storage_offset=1)
    sliced = unpack(pack_view(storage, (1, 3), (6, 2), storage_offset=1))
    if sliced != [1, 3, 5] or offsets != [1, 3, 5]:
        raise RuntimeError("slice packing check failed")
    print("raw storage:     ", raw_copy)
    print("transpose packed:", transposed)
    print("slice offsets:   ", offsets)
    print("slice payload_bytes=24 address_span_bytes=40")
    if check_torch:
        try:
            import torch
        except ImportError:
            print("SKIP: --torch requires PyTorch; stdlib layout checks passed")
            return 77
        base = torch.arange(6, dtype=torch.int64).reshape(2, 3)
        view = base.t()
        actual = view.contiguous().reshape(-1).tolist()
        if actual != transposed or view.stride() != (1, 3) or view.is_contiguous():
            raise RuntimeError("PyTorch transpose differs from the layout model")
        sliced_tensor = base.reshape(-1)[1::2]
        if sliced_tensor.tolist() != sliced:
            raise RuntimeError("PyTorch slice differs from the layout model")
        delta = sliced_tensor.data_ptr() - base.data_ptr()
        if delta != sliced_tensor.storage_offset() * sliced_tensor.element_size():
            raise RuntimeError("storage offset / first-element pointer mismatch")
        print(f"torch={torch.__version__} shape={tuple(view.shape)} stride={view.stride()}")
    print("PASS: logical element order, slice offsets and packed payload checked")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--torch", action="store_true", help="also check real CPU PyTorch views")
    args = parser.parse_args()
    return run(args.torch)


if __name__ == "__main__":
    sys.exit(main())
