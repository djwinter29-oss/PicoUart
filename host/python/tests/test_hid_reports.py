"""Version-1 HID report and host accounting tests."""

import threading
import time

import pytest

from helpers import FakeHidDevice, channel_bytes, control_bytes, device_info_bytes, telemetry_bytes
from pico_uart.client import PicoUartHid
from pico_uart.protocol import (
    CHANNEL_SIZE,
    CONTROL_SIZE,
    DEVICE_INFO_SIZE,
    REPORT_ID_CHANNEL_BASE,
    REPORT_ID_CONTROL,
    REPORT_ID_DEVICE_INFO,
    REPORT_ID_TELEMETRY,
    parse_channel,
    parse_control,
    parse_device_info,
    parse_telemetry,
)


def test_all_version1_payload_sizes_and_identity_fields():
    identity = parse_device_info(device_info_bytes())
    assert len(device_info_bytes()) == DEVICE_INFO_SIZE == 63
    assert identity["serial_number"] == "0123456789ABCDEF"
    assert identity["firmware_version"] == "1.2.3"
    assert identity["capabilities"] == 7
    assert identity["clock_profile_mask"] == 0x0F
    assert identity["ram_total_bytes"] == 270336
    assert len(control_bytes()) == CONTROL_SIZE == 24
    assert len(telemetry_bytes()) == 24
    assert len(channel_bytes()) == CHANNEL_SIZE == 54


@pytest.mark.parametrize("mutation", [
    {"profile_mask": 0}, {"capabilities": 0x8000}, {"ram_total": 0},
    {"product": b"bad\0name"}, {"serial": b"not-a-serial!!"},
])
def test_device_info_rejects_invalid_static_contract(mutation):
    with pytest.raises(RuntimeError):
        parse_device_info(device_info_bytes(**mutation))


def test_control_report_validates_status_pairing_and_boot_configuration():
    result = parse_control(control_bytes(
        command=5, flags=1, status=3, request_id=8, requested=2,
        source=2, reason=6, error_reason=3,
    ))
    assert result["status"] == 3
    assert result["requested_profile"] == 2
    assert result["error"] == "missing_overclock_acknowledgement"
    with pytest.raises(RuntimeError, match="pairing"):
        parse_control(control_bytes(status=3, error_reason=5))
    with pytest.raises(RuntimeError, match="reserved"):
        parse_control(control_bytes(reserved=1))


def test_control_parser_accepts_only_contractual_echoed_unknown_flags():
    assert parse_control(control_bytes(
        command=0x7F, flags=1, status=4, request_id=4, error_reason=5,
    ))["command"] == 0x7F
    assert parse_control(control_bytes(
        command=0x7F, flags=0x80, status=3, request_id=5, error_reason=1,
    ))["flags"] == 0x80
    with pytest.raises(RuntimeError, match="echoed control flags"):
        parse_control(control_bytes(
            command=0x7F, flags=0x80, status=4, request_id=5, error_reason=5,
        ))


def test_telemetry_and_channel_layouts_keep_shared_fault_timestamp():
    telemetry = parse_telemetry(telemetry_bytes(temperature_c=-1.25, cores=(10000, 0xFFFF)))
    assert telemetry["temperature_celsius"] == -1.25
    assert telemetry["runtime"]["cpu_usage_basis_points"] == [10000, None]
    sample = parse_channel(channel_bytes(
        sequence=65535, faults=(2, 3, 4, 5), health=0xD7, last_fault_uptime_s=9,
    ))
    assert sample["sequence"] == 65535
    assert sample["faults"]["rx_overrun"] == {"count": 4}
    assert sample["last_fault_uptime_s"] == 9
    assert "last_seen_ms" not in sample["faults"]["rx_error"]
    assert parse_channel(channel_bytes())["last_fault_uptime_s"] == 0
    with pytest.raises(RuntimeError, match="fault history"):
        parse_channel(channel_bytes(faults=(0, 0, 1, 0), health=0x01))


def test_input_accounting_is_per_channel_and_excludes_first_sample():
    device = FakeHidDevice(
        {},
        inputs=[
            (10, channel_bytes(sequence=1, traffic=(100, 200, 300, 400))),
            (11, channel_bytes(sequence=65535, traffic=(50, 60, 70, 80))),
            (10, channel_bytes(sequence=2, interval_ms=750, traffic=(10, 20, 30, 40))),
            (11, channel_bytes(sequence=0, interval_ms=500, traffic=(5, 6, 7, 8))),
        ],
    )
    client = PicoUartHid(device=device)
    first = client.read_status()
    assert first["channels"][0]["totals"]["uart_tx"] == 0
    assert first["channels"][0]["observed_interval_ms"] == 0
    client.read_status()
    after = client.read_status()
    channel0, channel1 = after["channels"][:2]
    assert channel0["totals"] == {
        "uart_tx": 10, "uart_rx": 20, "usb_tx": 30, "usb_rx": 40,
    }
    assert channel0["observed_interval_ms"] == 750
    assert channel0["sequence"] == 2
    assert channel0["traffic_gaps"] == 0
    client.read_status()
    assert client.read_status(0) is None
    assert channel1["sequence"] == 65535
    assert client._channels[1]["sequence"] == 0
    assert client._channels[1]["traffic_gaps"] == 0
    assert client._channels[1]["observed_interval_ms"] == 500


def test_sequence_gap_and_saturation_only_mark_the_affected_channel_incomplete():
    device = FakeHidDevice({}, inputs=[
        (10, channel_bytes(sequence=3)),
        (11, channel_bytes(sequence=7)),
        (10, channel_bytes(sequence=5, interval_ms=0xFFFFFFFF, traffic=(0xFFFFFFFF, 1, 1, 1))),
        (11, channel_bytes(sequence=8)),
    ])
    client = PicoUartHid(device=device)
    client.read_status()
    client.read_status()
    client.read_status()
    channel0, channel1 = client._channels[:2]
    assert channel0["traffic_incomplete"] is True
    assert channel0["traffic_gaps"] == 1
    assert channel0["totals"]["uart_tx"] == 0xFFFFFFFF
    assert channel0["observed_interval_ms"] == 0
    assert channel1["traffic_incomplete"] is False
    assert channel1["traffic_gaps"] == 0


def test_duplicate_and_old_sequence_reports_never_add_traffic_or_interval_twice():
    device = FakeHidDevice({}, inputs=[
        (10, channel_bytes(sequence=100, interval_ms=500, traffic=(8, 0, 0, 0))),
        (10, channel_bytes(sequence=101, interval_ms=500, traffic=(3, 0, 0, 0))),
        (10, channel_bytes(sequence=101, interval_ms=500, traffic=(3, 0, 0, 0))),
        (10, channel_bytes(sequence=100, interval_ms=500, traffic=(100, 0, 0, 0))),
    ])
    client = PicoUartHid(device=device)
    for _ in range(4):
        client.read_status()
    channel = client._channels[0]
    assert channel["totals"]["uart_tx"] == 3
    assert channel["observed_interval_ms"] == 500
    assert channel["traffic_incomplete"] is True
    assert channel["traffic_gaps"] == 1
    assert channel["sequence"] == 101


@pytest.mark.parametrize("resumed_sequence", [100, 99, 40000, 0])
def test_stale_stream_rebaselines_any_sequence_then_resumes_accounting(resumed_sequence):
    from pico_uart.protocol import parse_channel

    client = PicoUartHid(device=FakeHidDevice({}))
    client._apply_input(0, parse_channel(channel_bytes(sequence=100)), 0.0)
    client._apply_input(0, parse_channel(channel_bytes(
        sequence=resumed_sequence, traffic=(1000, 0, 0, 0),
    )), 3.1)
    channel = client._channels[0]
    assert channel["sequence"] == resumed_sequence
    assert channel["traffic_gaps"] == 1
    assert channel["traffic_incomplete"] is True
    assert channel["totals"]["uart_tx"] == 0
    assert channel["observed_interval_ms"] == 0
    client._apply_input(0, parse_channel(channel_bytes(
        sequence=(resumed_sequence + 1) % 65536, traffic=(7, 0, 0, 0),
    )), 3.6)
    assert channel["totals"]["uart_tx"] == 7
    assert channel["observed_interval_ms"] == 500
    assert channel["traffic_gaps"] == 1


def test_channel_feature_read_is_non_consuming_for_input_totals():
    device = FakeHidDevice(
        {REPORT_ID_CHANNEL_BASE: channel_bytes(sequence=7, traffic=(500, 0, 0, 0))},
        inputs=[
            (10, channel_bytes(sequence=1, traffic=(10, 0, 0, 0))),
            (10, channel_bytes(sequence=2, traffic=(20, 0, 0, 0))),
        ],
    )
    client = PicoUartHid(device=device)
    client.read_status()
    client.read_channel_diagnostic(0)
    assert client.read_status()["channels"][0]["totals"]["uart_tx"] == 20


class ControlDevice(FakeHidDevice):
    def __init__(self, results: list[bytes], capabilities: int = 7):
        super().__init__({
            REPORT_ID_DEVICE_INFO: device_info_bytes(capabilities=capabilities),
            REPORT_ID_TELEMETRY: telemetry_bytes(led=0),
        })
        self.results = list(results)

    def get_feature_report(self, report_id: int, size: int) -> list[int]:
        if report_id == REPORT_ID_CONTROL:
            payload = self.results.pop(0)
            assert len(payload) + 1 == size
            return [report_id, *payload]
        return super().get_feature_report(report_id, size)


def test_led_toggle_uses_observed_state_and_correlated_explicit_command():
    device = ControlDevice([
        control_bytes(),
        control_bytes(command=2, status=2, request_id=1),
    ])
    PicoUartHid(device=device).toggle_led()
    assert len(device.writes) == 1
    report = device.writes[0]
    assert report[0] == REPORT_ID_CONTROL and len(report) == 25
    assert report[1:8] == [1, 2, 0, 0, 1, 0, 0]


def test_save_configuration_uses_report2_request_and_verified_result():
    device = ControlDevice([
        control_bytes(),
        control_bytes(command=5, flags=1, status=2, request_id=1, requested=2),
    ])
    result = PicoUartHid(device=device).save_configuration(2, unsafe_overclock=True)
    assert result["status"] == 2
    assert result["requested_profile"] == 2
    assert device.writes[0][0] == REPORT_ID_CONTROL
    assert len(device.writes[0]) == 25
    assert device.writes[0][2:4] == [5, 1]
    assert int.from_bytes(device.writes[0][17:21], "little") == 2


def test_request_allocator_skips_latest_id_without_wrapping():
    client = PicoUartHid(device=FakeHidDevice({}))
    assert client._allocate_request_id(1) == 2
    assert client._allocate_request_id(2) == 3
    client._next_request_id = 0xFFFFFFFF
    with pytest.raises(RuntimeError, match="space exhausted"):
        client._allocate_request_id(0)


def test_static_identity_failure_prevents_board_control_write():
    device = ControlDevice([control_bytes()])
    device.reports[REPORT_ID_DEVICE_INFO] = bytes(DEVICE_INFO_SIZE)
    with pytest.raises(RuntimeError, match="device-information"):
        PicoUartHid(device=device).toggle_led()
    assert device.writes == []


def test_capability_masks_gate_led_reset_and_save_commands():
    device = ControlDevice([], capabilities=0)
    client = PicoUartHid(device=device)
    with pytest.raises(RuntimeError, match="LED is unavailable"):
        client.toggle_led()
    with pytest.raises(RuntimeError, match="reset is disabled"):
        client.reset_board()
    with pytest.raises(RuntimeError, match="does not support configuration save"):
        client.save_configuration(0)
    assert device.writes == []


def test_late_feature_transfer_is_bounded_and_quarantines_the_connection(monkeypatch):
    import pico_uart.client as client_module

    started = threading.Event()

    class SlowDevice(FakeHidDevice):
        def get_feature_report(self, report_id, size):
            started.set()
            time.sleep(0.05)
            return super().get_feature_report(report_id, size)

    monkeypatch.setattr(client_module, "COMMAND_TIMEOUT_S", 0.01)
    client = PicoUartHid(device=SlowDevice({REPORT_ID_DEVICE_INFO: device_info_bytes()}))
    with pytest.raises(RuntimeError, match="transfer timed out"):
        client.read_identity()
    assert started.is_set()
    with pytest.raises(RuntimeError, match="still in flight"):
        client.read_control()
