"""Golden-buffer pytest suite for PicoUart HID report parsers (no hardware)."""

from __future__ import annotations

import pytest

from helpers import FakeHidDevice, board_status_bytes, status_report_bytes


def test_version_and_temperature(hid_module):
    device = FakeHidDevice(board_status_bytes(temperature_c=25.30, major=1, minor=2, patch=3))
    status = hid_module.read_board_status(device)
    assert status["firmware_version"] == "1.2.3"
    assert status["temperature_celsius"] == pytest.approx(25.30, abs=0.01)
    assert hid_module.read_firmware_version(device) == "1.2.3"
    assert hid_module.read_board_temperature(device) == pytest.approx(25.30, abs=0.01)


def test_rejects_unsupported_board_status_layout(hid_module):
    device = FakeHidDevice(board_status_bytes(layout_version=13))
    with pytest.raises(RuntimeError, match="unsupported board-status"):
        hid_module.read_board_status(device)


def test_parse_status_channels(hid_module):
    status = hid_module.parse_status(status_report_bytes(sequence=9, health0=0x31))
    assert status["sequence"] == 9
    assert len(status["channels"]) == 6
    channel0 = status["channels"][0]
    assert channel0["health"] == 0x31
    assert channel0["ring_high_watermark"] == 32
    assert channel0["controller_tx_bytes"] == 10
    assert channel0["cdc_rx_bytes"] == 40


def test_rejects_bad_signature(hid_module):
    payload = bytearray(status_report_bytes())
    payload[0] = ord("X")
    with pytest.raises(RuntimeError, match="invalid signature"):
        hid_module.parse_status(bytes(payload))


def test_rejects_wrong_status_layout_version(hid_module):
    payload = bytearray(status_report_bytes())
    payload[2] = 13
    with pytest.raises(RuntimeError, match="unsupported status report version"):
        hid_module.parse_status(bytes(payload))


def test_usb_ids_match_firmware_policy_header(hid_module, repo_root):
    identity = repo_root / "firmware" / "src" / "config" / "usb_identity.h"
    text = identity.read_text(encoding="utf-8")
    assert "0xCafe" in text
    assert "0x4010" in text
    assert hid_module.VENDOR_ID == 0xCAFE
    assert hid_module.PRODUCT_ID == 0x4010
