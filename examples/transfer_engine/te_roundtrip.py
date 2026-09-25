# Copyright 2026 Feng Ren
# Licensed under the Apache License, Version 2.0.
# SPDX-License-Identifier: Apache-2.0
"""Two spawned TE processes: WRITE, READ-back, target verification, then reuse."""
import argparse
import ctypes
import hashlib
import importlib.util
import multiprocessing as mp
import os
import sys


def payload(sequence, size):
    block = hashlib.sha256(str(sequence).encode("ascii")).digest()
    return (block * ((size + len(block) - 1) // len(block)))[:size]


def check(code, operation):
    if code != 0:
        raise RuntimeError(f"{operation} returned {code}")


def receive(conn, timeout):
    if not conn.poll(timeout):
        raise TimeoutError("control message deadline exceeded")
    message = conn.recv()
    if message[0] == "error":
        raise RuntimeError(message[1])
    return message


def worker(conn, args, role, target=None):
    # Buffers stay in this frame even on failure. _exit avoids finalizers that
    # might free an allocation while a timed-out native request still uses it.
    engine = source = result = None
    try:
        from mooncake.engine import TransferEngine
        engine = TransferEngine()
        check(engine.initialize("127.0.0.1", "P2PHANDSHAKE", args.protocol, args.device), "initialize")
        source = ctypes.create_string_buffer(args.size)
        source_addr = ctypes.addressof(source)
        check(engine.register_memory(source_addr, args.size), "register source/target")
        if role == "target":
            conn.send(("ready", f"127.0.0.1:{engine.get_rpc_port()}", source_addr))
        else:
            result = ctypes.create_string_buffer(args.size)
            result_addr = ctypes.addressof(result)
            check(engine.register_memory(result_addr, args.size), "register READ result")
            conn.send(("ready",))
        while True:
            command = receive(conn, args.timeout)
            if command[0] == "close":
                break
            if role == "source" and command[0] == "run":
                sequence = command[1]
                expected = payload(sequence, args.size)
                ctypes.memmove(source_addr, expected, args.size)
                ctypes.memset(result_addr, 0, args.size)
                check(engine.transfer_sync_write(target[0], source_addr, target[1], args.size), "WRITE")
                check(engine.transfer_sync_read(target[0], result_addr, target[1], args.size), "READ")
                if result.raw != expected:
                    raise RuntimeError(f"READ-back mismatch at sequence {sequence}")
                conn.send(("done", sequence))
            elif role == "target" and command[0] == "verify":
                sequence = command[1]
                if source.raw != payload(sequence, args.size):
                    raise RuntimeError(f"target mismatch at sequence {sequence}")
                conn.send(("verified", sequence))
            else:
                raise RuntimeError(f"unexpected command: {command!r}")
        # Reached only after parent has coordinated completion on both ends.
        if result is not None:
            check(engine.unregister_memory(result_addr), "unregister READ result")
        check(engine.unregister_memory(source_addr), "unregister source/target")
        conn.send(("closed",))
    except BaseException as exc:
        try:
            conn.send(("error", f"{role}: {type(exc).__name__}: {exc}"))
        finally:
            sys.stdout.flush()
            sys.stderr.flush()
            os._exit(1)


def expect(conn, timeout, tag, sequence=None):
    message = receive(conn, timeout)
    if message[0] != tag or (sequence is not None and message[1:] != (sequence,)):
        raise RuntimeError(f"expected {tag}, got {message!r}")
    return message


def positive(text):
    value = int(text)
    if not 1 <= value <= 1048576:
        raise argparse.ArgumentTypeError("expected an integer in [1, 1048576]")
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--protocol", choices=("tcp", "rdma"), default="tcp")
    parser.add_argument("--device", default="", help="TE device filter, if required")
    parser.add_argument("--size", type=positive, default=4096)
    parser.add_argument("--iterations", type=positive, default=3)
    parser.add_argument("--timeout", type=positive, default=30, help="seconds per control stage")
    args = parser.parse_args()
    if importlib.util.find_spec("mooncake") is None:
        print("SKIP: install a compatible Mooncake Python package (mooncake.engine)")
        return 77
    if "MC_LEGACY_RPC_PORT_BINDING" in os.environ:
        parser.error("unset MC_LEGACY_RPC_PORT_BINDING; this example uses dynamic local ports")
    ctx = mp.get_context("spawn")  # Never fork an initialized native engine.
    target_conn, target_child = ctx.Pipe()
    source_conn, source_child = ctx.Pipe()
    processes = []
    try:
        receiver = ctx.Process(target=worker, args=(target_child, args, "target"))
        receiver.start()
        processes.append(receiver)
        target_child.close()
        ready = expect(target_conn, args.timeout, "ready")
        sender = ctx.Process(target=worker, args=(source_child, args, "source", ready[1:]))
        sender.start()
        processes.append(sender)
        source_child.close()
        expect(source_conn, args.timeout, "ready")
        print(f"requested_protocol={args.protocol} size={args.size}; check TE logs for actual transport", flush=True)
        for sequence in range(args.iterations):
            source_conn.send(("run", sequence))
            expect(source_conn, args.timeout, "done", sequence)
            target_conn.send(("verify", sequence))
            expect(target_conn, args.timeout, "verified", sequence)
            print(f"sequence={sequence}: WRITE + READ-back + target verification OK", flush=True)
        # No new accesses; source retires before the target unregisters.
        source_conn.send(("close",))
        expect(source_conn, args.timeout, "closed")
        target_conn.send(("close",))
        expect(target_conn, args.timeout, "closed")
        for process in processes:
            process.join(timeout=args.timeout)
            if process.exitcode != 0:
                raise RuntimeError(f"worker did not exit cleanly: {process.exitcode}")
        print(f"PASS: {args.iterations} rounds verified in two independent TE processes")
        return 0
    except (Exception, KeyboardInterrupt) as exc:
        print(f"FAIL: {type(exc).__name__}: {exc}", file=sys.stderr)
        return 1
    finally:
        # On failure, stop the isolated processes; never imitate cancellation by
        # unregistering memory that their native engine may still be using.
        for process in processes:
            if process.is_alive():
                process.terminate()
        for process in processes:
            process.join(timeout=2)
            if process.is_alive():
                process.kill()
                process.join(timeout=2)
        target_conn.close()
        source_conn.close()
        target_child.close()
        source_child.close()


if __name__ == "__main__":
    sys.exit(main())
