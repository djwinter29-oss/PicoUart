"""Golden-buffer pytest suite for PicoUart HID report parsers (no hardware)."""

from __future__ import annotations

import json
import sys
import pytest

from helpers import FakeHidDevice, board_status_bytes, overflow_counts_bytes, status_report_bytes


def test_version_and_temperature(hid_module):
    device = FakeHidDevice(board_status_bytes(temperature_c=25.30, major=1, minor=2, patch=3))
    status = hid_module.read_board_status(device)
    assert status["firmware_version"] == "1.2.3"
    assert status["temperature_celsius"] == pytest.approx(25.30, abs=0.01)
    assert status["hid_reset_enabled"] is False
    assert hid_module.read_firmware_version(device) == "1.2.3"
    assert hid_module.read_board_temperature(device) == pytest.approx(25.30, abs=0.01)


def test_board_status_reports_hid_reset_capability(hid_module):
    device = FakeHidDevice(
        board_status_bytes(reserved0=hid_module.BOARD_STATUS_FLAG_HID_RESET)
    )
    assert hid_module.read_board_status(device)["hid_reset_enabled"] is True


def test_overflow_counts(hid_module):
    device = FakeHidDevice(
        overflow_counts_bytes(), report_id=hid_module.REPORT_ID_OVERFLOW_COUNTS
    )
    assert hid_module.read_overflow_counts(device) == [1, 2, 3, 4, 5, 6]


def test_rejects_unsupported_board_status_layout(hid_module):
    device = FakeHidDevice(board_status_bytes(layout_version=13))
    with pytest.raises(RuntimeError, match="unsupported board-status"):
        hid_module.read_board_status(device)


@pytest.mark.parametrize("reserved0,reserved1", [(0x02, 0), (0, 1), (0x03, 0)])
def test_rejects_unknown_board_status_reserved_fields(
    hid_module, reserved0, reserved1
):
    device = FakeHidDevice(
        board_status_bytes(reserved0=reserved0, reserved1=reserved1)
    )
    with pytest.raises(RuntimeError, match="unknown reserved fields"):
        hid_module.read_board_status(device)


def test_parse_status_channels(hid_module):
    from pico_uart.protocol import decode_health, parse_status

    status = hid_module.parse_status(status_report_bytes(sequence=9, health0=0x31))
    modular_status = parse_status(status_report_bytes(sequence=9, health0=0x31))
    assert status["sequence"] == 9
    assert modular_status == status
    assert len(status["channels"]) == hid_module.UART_CHANNEL_COUNT
    channel0 = status["channels"][0]
    assert channel0["health"] == 0x31
    assert channel0["ring_high_watermark"] == 32
    assert channel0["controller_tx_bytes"] == 10
    assert channel0["cdc_rx_bytes"] == 40
    assert hid_module.decode_health(0x31) == ["ready", "cdc_open", "pio"]
    assert decode_health(0x31) == ["ready", "cdc_open", "pio"]
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
    payload[1] = 13
    with pytest.raises(RuntimeError, match="unsupported status report version"):
        hid_module.parse_status(bytes(payload))


def test_require_payload_rejects_bad_prefix(hid_module):
    with pytest.raises(RuntimeError, match="unexpected report"):
        hid_module.require_payload(
            [hid_module.REPORT_ID_BOARD_STATUS, 1, 2],
            report_id=hid_module.REPORT_ID_STATUS,
            payload_size=2,
        )
    with pytest.raises(RuntimeError, match="unexpected report"):
        hid_module.require_payload(
            [hid_module.REPORT_ID_STATUS, 1],
            report_id=hid_module.REPORT_ID_STATUS,
            payload_size=2,
        )


def test_send_command_and_reset_sequence(hid_module):
    writes: list[list[int]] = []

    class FakeWriteDevice:
        def get_feature_report(self, report_id: int, size: int) -> list[int]:
            assert report_id == hid_module.REPORT_ID_BOARD_STATUS
            payload = board_status_bytes(reserved0=hid_module.BOARD_STATUS_FLAG_HID_RESET)
            assert size == len(payload) + 1
            return [report_id, *payload]

        def send_feature_report(self, report: list[int]) -> int:
            writes.append(report)
            return len(report)

    device = FakeWriteDevice()
    hid_module.send_command(device, hid_module.COMMAND_TOGGLE_LED)
    hid_module.reset_board(device)
    assert writes[0] == [hid_module.REPORT_ID_COMMAND, hid_module.COMMAND_TOGGLE_LED]
    assert writes[1] == [hid_module.REPORT_ID_COMMAND, hid_module.COMMAND_ARM_RESET]
    assert writes[2] == [hid_module.REPORT_ID_COMMAND, hid_module.COMMAND_RESET_BOARD]


def test_reset_fails_closed_when_firmware_disables_hid_reset(hid_module):
    writes: list[list[int]] = []

    class FakeWriteDevice:
        def get_feature_report(self, report_id: int, size: int) -> list[int]:
            payload = board_status_bytes()
            assert size == len(payload) + 1
            return [report_id, *payload]

        def send_feature_report(self, report: list[int]) -> int:
            writes.append(report)
            return len(report)

    with pytest.raises(RuntimeError, match="HID reset is disabled"):
        hid_module.reset_board(FakeWriteDevice())
    assert writes == []


def _run_one_monitor_iteration(monkeypatch, report):
    from pico_uart import cli

    times = iter((0.0, 0.0, 1.0))
    monkeypatch.setattr(cli.time, "monotonic", lambda: next(times))

    class FakeClient:
        def read_status(self, timeout_ms):
            assert timeout_ms == 250
            if isinstance(report, Exception):
                raise report
            return report

    return FakeClient()


def test_monitor_rejects_empty_reads(monkeypatch, hid_module):
    from pico_uart import cli

    client = _run_one_monitor_iteration(monkeypatch, None)
    with pytest.raises(RuntimeError, match="timed out without receiving"):
        cli.monitor(client, 1.0)


def test_monitor_rejects_malformed_status(monkeypatch, hid_module):
    from pico_uart import cli

    client = _run_one_monitor_iteration(
        monkeypatch, RuntimeError("unexpected status report size 62")
    )
    with pytest.raises(RuntimeError, match="unexpected status report size"):
        cli.monitor(client, 1.0)


def test_monitor_accepts_valid_status(monkeypatch, hid_module, capsys):
    from pico_uart import cli

    status = hid_module.parse_status(status_report_bytes(sequence=9))
    client = _run_one_monitor_iteration(monkeypatch, status)
    cli.monitor(client, 1.0)
    assert capsys.readouterr().out.startswith("seq=9 ")


def test_client_owns_connection_and_exposes_decoded_operations(hid_module):
    from pico_uart import PicoUartHid as PackageClient

    class FakeDevice:
        def __init__(self):
            self.closed = False
            self.commands = []

        def read(self, size, timeout_ms):
            assert size == hid_module.STATUS_SIZE + 1
            assert timeout_ms == 100
            return [hid_module.REPORT_ID_STATUS, *status_report_bytes(sequence=12)]

        def get_feature_report(self, report_id, size):
            payload = {
                hid_module.REPORT_ID_BOARD_STATUS: board_status_bytes(),
                hid_module.REPORT_ID_OVERFLOW_COUNTS: overflow_counts_bytes(),
            }[report_id]
            assert size == len(payload) + 1
            return [report_id, *payload]

        def send_feature_report(self, report):
            self.commands.append(report)
            return len(report)

        def close(self):
            self.closed = True

    device = FakeDevice()
    client = PackageClient(device=device)

    assert client.read_status(100)["sequence"] == 12
    assert client.read_board_status()["firmware_version"] == "1.2.3"
    assert client.read_overflow_counts() == [1, 2, 3, 4, 5, 6]
    client.toggle_led()
    client.close()

    assert device.commands == [[hid_module.REPORT_ID_COMMAND, hid_module.COMMAND_TOGGLE_LED]]
    assert device.closed
    with pytest.raises(RuntimeError, match="connection is closed"):
        client.read_status()


def test_client_rejects_unexpected_status_report_id(hid_module):
    from pico_uart import PicoUartHid as PackageClient

    class FakeDevice:
        def read(self, _size, _timeout_ms):
            return [hid_module.REPORT_ID_BOARD_STATUS]

        def close(self):
            pass

    with PackageClient(device=FakeDevice()) as client:
        with pytest.raises(RuntimeError, match="unexpected HID report ID 3"):
            client.read_status()


def test_cli_status_supports_json_output(monkeypatch, capsys, hid_module):
    from pico_uart import cli

    expected = hid_module.parse_status(status_report_bytes(sequence=21))

    class FakeClient:
        def __init__(self, *_args):
            pass

        def __enter__(self):
            return self

        def __exit__(self, *_args):
            pass

        def read_status(self, timeout_ms):
            assert timeout_ms > 0
            return expected

    monkeypatch.setattr(cli, "PicoUartHid", FakeClient)
    monkeypatch.setattr(sys, "argv", ["pico-uart", "status", "--json"])

    assert cli.main() == 0
    assert json.loads(capsys.readouterr().out) == expected
