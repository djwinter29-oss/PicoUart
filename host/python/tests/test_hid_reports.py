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
    assert hid_module.decode_health(0x31) == ["ready", "cdc_open", "pio"]
    assert "control_error" in hid_module.decode_health(0x04)
    assert "control_pending" in hid_module.decode_health(0x08)
    assert "init_failed" in hid_module.decode_health(0x02)
    assert "rx_overrun" in hid_module.decode_health(0x40)
    assert "rx_error" in hid_module.decode_health(0x80)


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


def test_require_payload_rejects_bad_prefix(hid_module):
    with pytest.raises(RuntimeError, match="unexpected report"):
        hid_module.require_payload([3, 1, 2], report_id=1, payload_size=2)
    with pytest.raises(RuntimeError, match="unexpected report"):
        hid_module.require_payload([1, 1], report_id=1, payload_size=2)


def test_send_command_and_reset_sequence(hid_module):
    writes: list[list[int]] = []

    class FakeWriteDevice:
        def send_feature_report(self, report: list[int]) -> int:
            writes.append(report)
            return len(report)

    device = FakeWriteDevice()
    hid_module.send_command(device, hid_module.COMMAND_TOGGLE_LED)
    hid_module.reset_board(device)
    assert writes[0] == [hid_module.REPORT_ID_COMMAND, hid_module.COMMAND_TOGGLE_LED]
    assert writes[1] == [hid_module.REPORT_ID_COMMAND, hid_module.COMMAND_ARM_RESET]
    assert writes[2] == [hid_module.REPORT_ID_COMMAND, hid_module.COMMAND_RESET_BOARD]
