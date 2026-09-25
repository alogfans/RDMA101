# Copyright 2026 Feng Ren
# Licensed under the Apache License, Version 2.0.
# SPDX-License-Identifier: Apache-2.0
"""Run tutorial checks, reporting missing optional capabilities as SKIP."""
import argparse
import os
import signal
from pathlib import Path
import subprocess
import sys


def stop_group(process):
    # Native TE calls may have spawned workers; stopping only the parent leaks them.
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    try:
        process.wait(timeout=2)
    except subprocess.TimeoutExpired:
        pass
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    process.wait()


def run_case(command, timeout):
    with subprocess.Popen(command, start_new_session=True) as process:
        try:
            return process.wait(timeout=timeout)
        except (subprocess.TimeoutExpired, KeyboardInterrupt):
            stop_group(process)
            raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", help="RDMA device for the Verbs examples")
    parser.add_argument("--port", type=int, default=1)
    parser.add_argument("--gid-index", type=int, default=0)
    parser.add_argument("--timeout", type=int, default=120, help="seconds per subprocess")
    parser.add_argument("--require", action="append", default=[],
                        choices=("rdma", "torch", "gpu", "te"),
                        help="treat an optional capability's SKIP as a failure; repeatable")
    args = parser.parse_args()
    if args.timeout <= 0 or not 1 <= args.port <= 255 or not 0 <= args.gid_index <= 255:
        parser.error("invalid timeout, port or GID index")
    root = Path(__file__).resolve().parent
    py = sys.executable
    rdma = [str(root / "pipeline/write_pipeline"), "--port", str(args.port),
            "--gid-index", str(args.gid_index)]
    if args.device:
        rdma.extend(["--device", args.device])
    jobs = [
        ("CPU assertions", "cpu", [py, "-m", "unittest", "discover", "-s", str(root / "tests"), "-v"]),
        ("Layout experiment", "cpu", [py, str(root / "ai_data/tensor_layout.py")]),
        ("PyTorch views", "torch", [py, str(root / "ai_data/tensor_layout.py"), "--torch"]),
        ("CUDA streams", "gpu", [py, str(root / "ai_data/gpu_streams.py")]),
        ("TE TCP WRITE/READ", "te", [py, str(root / "transfer_engine/te_roundtrip.py")]),
        ("RC serial WRITE", "rdma", rdma + ["--depth", "1", "--batch", "1", "--iterations", "37", "--size", "64"]),
        ("RC batched WRITE and ring reuse", "rdma", rdma + ["--depth", "7", "--batch", "3", "--iterations", "37", "--size", "257"]),
    ]
    passed = skipped = failed = 0
    for name, capability, command in jobs:
        print(f"\n--- {name} ---", flush=True)
        try:
            code = run_case(command, args.timeout)
        except (OSError, subprocess.TimeoutExpired) as exc:
            print(f"FAIL: {exc}", flush=True)
            code = 1
        if code == 0:
            passed += 1
        elif code == 77 and capability not in args.require and capability != "cpu":
            skipped += 1
        else:
            failed += 1
            if code == 77:
                print(f"FAIL: required capability {capability} was skipped", flush=True)
    print(f"\nRESULT: passed={passed} skipped={skipped} failed={failed}", flush=True)
    return int(failed != 0)


if __name__ == "__main__":
    sys.exit(main())
