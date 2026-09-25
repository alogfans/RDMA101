# Copyright 2026 Feng Ren
# Licensed under the Apache License, Version 2.0.
# SPDX-License-Identifier: Apache-2.0
"""CPU checks of layouts, control messages and argument validation, not RDMA."""
import multiprocessing as mp
from pathlib import Path
import subprocess
import sys
import unittest

EXAMPLES = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(EXAMPLES / "ai_data"))
sys.path.insert(0, str(EXAMPLES / "transfer_engine"))
from tensor_layout import element_offsets, pack_view, unpack
from te_roundtrip import expect, payload, receive


class LayoutTests(unittest.TestCase):
    def test_transpose_changes_wire_order(self):
        actual = unpack(pack_view(list(range(6)), (3, 2), (1, 3)))
        self.assertEqual(actual, [0, 3, 1, 4, 2, 5])

    def test_slice_omits_gaps_and_applies_offset_once(self):
        self.assertEqual(unpack(pack_view(list(range(12)), (2, 2), (6, 2), 1)),
                         [1, 3, 7, 9])

    def test_broadcast_view_repeats_elements(self):
        self.assertEqual(unpack(pack_view([11, 22], (3, 2), (0, 1))),
                         [11, 22, 11, 22, 11, 22])

    def test_out_of_bounds_view_is_rejected(self):
        with self.assertRaises(ValueError):
            pack_view([0, 1, 2], (2, 2), (2, 1))

    def test_negative_stride_and_offset_are_rejected(self):
        for strides, offset in [((-1, 1), 0), ((2, 1), -1)]:
            with self.subTest(strides=strides, offset=offset):
                with self.assertRaises(ValueError):
                    element_offsets((2, 2), strides, offset)

    def test_incomplete_wire_element_is_rejected(self):
        with self.assertRaises(ValueError):
            unpack(b"short")


class ControlTests(unittest.TestCase):
    def setUp(self):
        self.reader, self.writer = mp.Pipe()

    def tearDown(self):
        self.reader.close()
        self.writer.close()

    def test_late_round_cannot_ack_current_round(self):
        self.writer.send(("verified", 2))
        with self.assertRaises(RuntimeError):
            expect(self.reader, 0.1, "verified", 3)

    def test_worker_failure_is_not_success(self):
        self.writer.send(("error", "native transfer failed"))
        with self.assertRaisesRegex(RuntimeError, "native transfer failed"):
            receive(self.reader, 0.1)

    def test_deadline_is_bounded(self):
        with self.assertRaises(TimeoutError):
            receive(self.reader, 0.01)

    def test_payload_changes_between_rounds(self):
        for size in (1, 31, 32, 33, 4096):
            self.assertEqual(len(payload(0, size)), size)
            self.assertNotEqual(payload(0, size), payload(1, size))


class PipelineArguments(unittest.TestCase):
    def test_invalid_arguments_fail_before_opening_device(self):
        binary = EXAMPLES / "pipeline" / "write_pipeline"
        if not binary.exists():
            self.fail("build the pipeline example before running checks")
        cases = [
            ["--depth", "0"], ["--depth", "2", "--batch", "3"],
            ["--size", "-1"], ["--iterations", "1x"],
            ["--depth", "4096", "--size", "1048576"], ["unexpected"],
        ]
        for args in cases:
            with self.subTest(args=args):
                result = subprocess.run([str(binary), *args], capture_output=True,
                                        text=True, timeout=3)
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                self.assertIn("FAIL:", result.stderr)


if __name__ == "__main__":
    unittest.main()
