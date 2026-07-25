#!/usr/bin/env python3
"""Golden-buffer unit tests for PicoUart HID report parsers (no hardware)."""

from __future__ import annotations

import struct
import sys
import unittest
from pathlib import Path

HOST_PYTHON = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HOST_PYTHON))

import pico_uart_hid as hid  # noqa: E402


def board_status_bytes(
    layout_version: int = 14,
    temperature_c: float = 25.30,
    major: int = 1,
    minor: int = 2,
    patch: int = 3,
) -> bytes:
    centi = int(round(temperature_c * 100.0))
    return struct.pack("<BBhBBBB", layout_version, 0, centi, major, minor, patch, 0)


def status_report_bytes(sequence: int = 7, health0: int = 0x11) -> bytes:
    header = struct.pack("<BBBB", ord("P"), ord("U"), 14, sequence)
    channels = b""
    for index in range(6):
        health = health0 if index == 0 else 0x01
        channels += struct.pack("<BB4H", health, 2, 10 + index, 20 + index, 30 + index, 40 + index)
    assert len(header) + len(channels) == 64
    return header + channels


class FakeHidDevice:
    """Minimal stand-in for hidapi feature-report reads."""

    def __init__(self, payload: bytes, report_id: int = hid.REPORT_ID_BOARD_STATUS) -> None:
        self._payload = payload
        self._report_id = report_id

    def get_feature_report(self, report_id: int, size: int) -> list[int]:
        assert report_id == self._report_id
        assert size == len(self._payload) + 1
        return [report_id, *self._payload]


class BoardStatusTests(unittest.TestCase):
    def test_version_and_temperature(self) -> None:
        device = FakeHidDevice(board_status_bytes(temperature_c=25.30, major=1, minor=2, patch=3))
        status = hid.read_board_status(device)
        self.assertEqual(status["firmware_version"], "1.2.3")
        self.assertAlmostEqual(status["temperature_celsius"], 25.30, places=2)
        self.assertEqual(hid.read_firmware_version(device), "1.2.3")
        self.assertAlmostEqual(hid.read_board_temperature(device), 25.30, places=2)

    def test_rejects_unsupported_layout(self) -> None:
        device = FakeHidDevice(board_status_bytes(layout_version=13))
        with self.assertRaisesRegex(RuntimeError, "unsupported board-status"):
            hid.read_board_status(device)


class StatusReportTests(unittest.TestCase):
    def test_parse_status_channels(self) -> None:
        status = hid.parse_status(status_report_bytes(sequence=9, health0=0x31))
        self.assertEqual(status["sequence"], 9)
        self.assertEqual(len(status["channels"]), 6)
        channel0 = status["channels"][0]
        self.assertEqual(channel0["health"], 0x31)
        self.assertEqual(channel0["ring_high_watermark"], 32)
        self.assertEqual(channel0["controller_tx_bytes"], 10)
        self.assertEqual(channel0["cdc_rx_bytes"], 40)

    def test_rejects_bad_signature(self) -> None:
        payload = bytearray(status_report_bytes())
        payload[0] = ord("X")
        with self.assertRaisesRegex(RuntimeError, "invalid signature"):
            hid.parse_status(bytes(payload))

    def test_rejects_wrong_layout_version(self) -> None:
        payload = bytearray(status_report_bytes())
        payload[2] = 13
        with self.assertRaisesRegex(RuntimeError, "unsupported status report version"):
            hid.parse_status(bytes(payload))


class IdentityTests(unittest.TestCase):
    def test_usb_ids_match_firmware_policy_header(self) -> None:
        identity = Path(__file__).resolve().parents[3] / "firmware" / "src" / "config" / "usb_identity.h"
        text = identity.read_text(encoding="utf-8")
        self.assertIn("0xCafe", text)
        self.assertIn("0x4010", text)
        self.assertEqual(hid.VENDOR_ID, 0xCAFE)
        self.assertEqual(hid.PRODUCT_ID, 0x4010)


if __name__ == "__main__":
    raise SystemExit(unittest.main(verbosity=2))
