# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""Version-1 HID report and host accounting tests."""

import threading
import time

import pytest

from helpers import (
    FakeHidDevice, channel_bytes, control_bytes, device_info_bytes, telemetry_bytes,
    configuration_bytes, diagnostic_bytes,
)
from pico_uart.client import PicoUartHid, read_telemetry as read_telemetry_from_device
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
    parse_configuration,
    parse_diagnostic,
)


def test_all_version1_payload_sizes_and_identity_fields():
    identity = parse_device_info(device_info_bytes())
    assert len(device_info_bytes()) == DEVICE_INFO_SIZE == 27
    assert identity["serial_number"] == "0123456789ABCDEF"
    assert identity["firmware_version"] == "1.2.3"
    assert identity["capabilities"] == 7
    assert identity["clock_profile_mask"] == 0x1F
    assert identity["ram_total_bytes"] == 270336
    assert identity["ram_reserved_bytes"] == 65536
    assert len(configuration_bytes()) == 36
    assert len(diagnostic_bytes()) == 24
    assert len(control_bytes()) == CONTROL_SIZE == 4
    assert len(telemetry_bytes()) == 20
    assert len(channel_bytes()) == CHANNEL_SIZE == 30


def seed_client_metadata(client):
    client._device_info = parse_device_info(device_info_bytes())
    client._configuration = parse_configuration(configuration_bytes(), client._device_info)


def test_ready_wait_ignores_historical_control_faults(monkeypatch):
    client = PicoUartHid(device=FakeHidDevice({}))
    seed_client_metadata(client)
    monkeypatch.setattr(client, "_read_feature", lambda *_args: channel_bytes())
    baud = parse_channel(channel_bytes())["baud_rate"]
    assert client.wait_channels_ready({0: baud})[0]["state"] == "ready"


@pytest.mark.parametrize("state", [4])
def test_ready_wait_rejects_current_errors(monkeypatch, state):
    client = PicoUartHid(device=FakeHidDevice({}))
    seed_client_metadata(client)
    monkeypatch.setattr(client, "_read_feature", lambda *_args: channel_bytes(state=state))
    with pytest.raises(OSError, match="CDC0 configuration failed"):
        client.wait_channels_ready({0: 115200})


@pytest.mark.parametrize("state", [0, 1, 3])
def test_ready_wait_times_out_without_open_ready_and_applied(monkeypatch, state):
    client = PicoUartHid(device=FakeHidDevice({}))
    seed_client_metadata(client)
    monkeypatch.setattr(client, "_read_feature", lambda *_args: channel_bytes(state=state))
    with pytest.raises(TimeoutError, match="readiness"):
        client.wait_channels_ready({0: 115200}, timeout_s=0.001)


def test_ready_wait_propagates_disconnect(monkeypatch):
    client = PicoUartHid(device=FakeHidDevice({}))
    seed_client_metadata(client)
    def disconnected(*_args):
        raise OSError("disconnected")
    monkeypatch.setattr(client, "_read_feature", disconnected)
    with pytest.raises(OSError, match="disconnected"):
        client.wait_channels_ready({0: 115200})


@pytest.mark.parametrize("timeout", [0, -1, float("inf"), float("nan")])
def test_ready_wait_validates_timeout(timeout):
    with pytest.raises(ValueError, match="timeout_s"):
        PicoUartHid(device=FakeHidDevice({})).wait_channels_ready({0: 115200}, timeout_s=timeout)


@pytest.mark.parametrize("changes", [{"baud": 9600}, {"framing": (7, 1, 0)}, {"framing": (8, 2, 0)},
                                     {"framing": (8, 1, 1)}])
def test_ready_wait_requires_requested_baud_and_framing(monkeypatch, changes):
    client = PicoUartHid(device=FakeHidDevice({}))
    seed_client_metadata(client)
    monkeypatch.setattr(client, "_read_feature", lambda *_args: channel_bytes(**changes))
    with pytest.raises(TimeoutError, match="readiness"):
        client.wait_channels_ready({0: 115200}, timeout_s=0.001)


def test_ready_wait_accepts_pending_then_applied_for_all_channels(monkeypatch):
    client = PicoUartHid(device=FakeHidDevice({}))
    seed_client_metadata(client)
    reports = iter([channel_bytes(state=3), channel_bytes(), channel_bytes(), channel_bytes()])
    calls = []
    def read(report_id, _size, deadline):
        calls.append((report_id, deadline))
        return next(reports)
    monkeypatch.setattr(client, "_read_feature", read)
    assert set(client.wait_channels_ready({0: 115200, 2: 115200})) == {0, 2}
    assert [report_id for report_id, _deadline in calls] == [10, 12, 10, 12]
    assert len({deadline for _report_id, deadline in calls}) == 1


def test_ready_wait_rejects_ready_sample_returned_after_deadline(monkeypatch):
    from types import SimpleNamespace
    import pico_uart.client as client_module
    now = [0.0]
    monkeypatch.setattr(client_module, "time", SimpleNamespace(monotonic=lambda: now[0]))
    client = PicoUartHid(device=FakeHidDevice({}))
    seed_client_metadata(client)
    def late_read(*_args):
        now[0] = 4.0
        return channel_bytes()
    monkeypatch.setattr(client, "_read_feature", late_read)
    with pytest.raises(TimeoutError, match="HID read timed out"):
        client.wait_channels_ready({0: 115200}, timeout_s=3.0)


def test_ready_wait_bounds_lock_contention(monkeypatch):
    client = PicoUartHid(device=FakeHidDevice({}))
    seed_client_metadata(client)
    held = threading.Event()
    release = threading.Event()
    def hold_lock():
        with client._lock:
            held.set()
            release.wait(timeout=1)
    worker = threading.Thread(target=hold_lock)
    worker.start()
    try:
        assert held.wait(timeout=1)
        monkeypatch.setattr(client, "_read_feature", lambda *_args: pytest.fail("read while lock unavailable"))
        start = time.monotonic()
        with pytest.raises(TimeoutError, match="HID lock"):
            client.wait_channels_ready({0: 115200}, timeout_s=0.02)
        assert time.monotonic() - start < 0.5
    finally:
        release.set()
        worker.join(timeout=1)


def test_ready_wait_releases_lock_on_feature_error(monkeypatch):
    client = PicoUartHid(device=FakeHidDevice({}))
    seed_client_metadata(client)
    def fail(*_args):
        raise OSError("device disconnected")
    monkeypatch.setattr(client, "_read_feature", fail)
    with pytest.raises(OSError, match="disconnected"):
        client.wait_channels_ready({0: 115200})
    acquired = []
    def acquire_from_other_thread():
        if client._lock.acquire(timeout=0.1):
            acquired.append(True)
            client._lock.release()
    worker = threading.Thread(target=acquire_from_other_thread)
    worker.start()
    worker.join(timeout=1)
    assert acquired == [True]


@pytest.mark.parametrize("mutation", [
    {"profile_mask": 0}, {"capabilities": 0x80}, {"ram_total": 0},
    {"ram_reserved": 270337}, {"product_id": 2},
])
def test_device_info_rejects_invalid_static_contract(mutation):
    with pytest.raises(RuntimeError):
        parse_device_info(device_info_bytes(**mutation))


def test_feature_and_input_reports_require_validated_product_identity():
    device = FakeHidDevice({
        REPORT_ID_DEVICE_INFO: device_info_bytes(product_id=2),
        REPORT_ID_TELEMETRY: telemetry_bytes(),
    }, inputs=[(10, channel_bytes())])
    client = PicoUartHid(device=device)
    with pytest.raises(RuntimeError, match="Unknown product"):
        client.read_telemetry()
    with pytest.raises(RuntimeError, match="Unknown product"):
        client.read_status()
    assert device.inputs
    with pytest.raises(RuntimeError, match="Unknown product"):
        read_telemetry_from_device(device)


@pytest.mark.parametrize("channel_id", [2, 5])
def test_pio_channel_rejects_non_8n1_but_hardware_uart_accepts_it(channel_id):
    identity = parse_device_info(device_info_bytes(hardware_mask=0x03))
    configuration = parse_configuration(configuration_bytes(), identity)
    with pytest.raises(RuntimeError, match="PIO UART channels only support 8N1"):
        parse_channel(channel_bytes(framing=(7, 1, 0)), channel_id, identity, configuration)
    assert parse_channel(channel_bytes(framing=(7, 1, 0)), 0, identity, configuration)["data_bits"] == 7


@pytest.mark.parametrize("baud", [1, 49, 3_000_001])
def test_channel_rejects_out_of_range_applied_baud(baud):
    with pytest.raises(RuntimeError, match="invalid applied UART framing"):
        parse_channel(channel_bytes(baud=baud))


def test_failed_control_result_requires_the_save_action():
    assert parse_control(control_bytes(action=4, status=6, error_reason=7))["error"] == "flash_operation_failed"
    with pytest.raises(RuntimeError, match="action"):
        parse_control(control_bytes(action=1, status=6, error_reason=7))


def test_identity_read_rejects_a_missing_feature_report():
    class MissingIdentity(FakeHidDevice):
        def get_feature_report(self, report_id, size):
            if report_id == REPORT_ID_DEVICE_INFO:
                return None
            return super().get_feature_report(report_id, size)

    client = PicoUartHid(device=MissingIdentity({}))
    with pytest.raises(RuntimeError, match="invalid identity report ID"):
        client.read_identity()


def test_feature_read_rejects_a_missing_report():
    class MissingTelemetry(FakeHidDevice):
        def get_feature_report(self, report_id, size):
            if report_id == REPORT_ID_TELEMETRY:
                return None
            return super().get_feature_report(report_id, size)

    client = PicoUartHid(device=MissingTelemetry({}))
    with pytest.raises(RuntimeError, match="received no report"):
        client.read_telemetry()


def test_identity_read_rejects_an_empty_feature_report():
    class EmptyIdentity(FakeHidDevice):
        def get_feature_report(self, report_id, size):
            if report_id == REPORT_ID_DEVICE_INFO:
                return []
            return super().get_feature_report(report_id, size)

    client = PicoUartHid(device=EmptyIdentity({}))
    with pytest.raises(RuntimeError, match="unexpected report 1"):
        client.read_identity()


def test_identity_read_rejects_a_padded_feature_buffer():
    class PaddedIdentity(FakeHidDevice):
        def get_feature_report(self, report_id, size):
            if report_id == REPORT_ID_DEVICE_INFO:
                return [report_id, *device_info_bytes(), 0]
            return super().get_feature_report(report_id, size)

    client = PicoUartHid(device=PaddedIdentity({}))
    with pytest.raises(RuntimeError, match="unexpected report 1"):
        client.read_identity()


def test_control_report_validates_status_pairing():
    result = parse_control(control_bytes(
        action=4, status=3, error_reason=3,
    ))
    assert result["status"] == 3
    assert result["error"] == "missing_overclock_acknowledgement"
    with pytest.raises(RuntimeError, match="pairing"):
        parse_control(control_bytes(status=3, error_reason=5))
    with pytest.raises(RuntimeError, match="action"):
        parse_control(control_bytes(action=1))


@pytest.mark.parametrize("status,error", [(3, 1), (4, 5)])
def test_control_parser_accepts_only_contractual_unknown_action(status, error):
    assert parse_control(control_bytes(action=0, status=status, error_reason=error))["action"] == 0
    with pytest.raises(RuntimeError, match="action"):
        parse_control(control_bytes(action=127, status=status, error_reason=error))


def test_telemetry_and_channel_layouts_keep_shared_fault_timestamp():
    telemetry = parse_telemetry(telemetry_bytes(temperature_c=-1.25, cores=(10000, 0xFFFF)))
    assert telemetry["temperature_celsius"] == -1.25
    assert telemetry["runtime"]["service_load_basis_points"] == [10000, None]
    sample = parse_channel(channel_bytes(sequence=65535, state=4))
    assert sample["sequence"] == 65535
    assert sample["state"] == "error"
    diagnostic = parse_diagnostic(diagnostic_bytes(dropped=4, errors=5, timestamp=9))
    assert diagnostic["rx_overrun_count"] == 4
    assert diagnostic["last_fault_uptime_s"] == 9
    with pytest.raises(RuntimeError, match="recorded history"):
        parse_diagnostic(diagnostic_bytes(timestamp=1))


def test_input_totals_seed_from_boot_counters_and_deltas_are_per_channel():
    device = FakeHidDevice(
        {},
        inputs=[
            (10, channel_bytes(sequence=1, traffic=(100, 200, 300, 400))),
            (11, channel_bytes(sequence=65535, traffic=(50, 60, 70, 80))),
            (10, channel_bytes(sequence=2, interval_ms=750, traffic=(110, 220, 330, 440))),
            (11, channel_bytes(sequence=0, interval_ms=500, traffic=(55, 66, 77, 88))),
        ],
    )
    client = PicoUartHid(device=device)
    first = client.read_status()
    assert first["channels"][0]["totals"]["uart_tx"] == 100
    assert first["channels"][0]["observed_interval_ms"] == 0
    client.read_status()
    after = client.read_status()
    channel0, channel1 = after["channels"][:2]
    assert channel0["totals"] == {
        "uart_tx": 110, "uart_rx": 220, "usb_tx": 330, "usb_rx": 440,
    }
    assert channel0["traffic"] == {"uart_tx": 10, "uart_rx": 20, "usb_tx": 30, "usb_rx": 40}
    assert channel0["observed_interval_ms"] == 750
    assert channel0["sequence"] == 2
    assert channel0["traffic_gaps"] == 0
    client.read_status()
    assert client.read_status(0) is None
    assert channel1["sequence"] == 65535
    assert client._channels[1]["sequence"] == 0
    assert client._channels[1]["traffic_gaps"] == 0
    assert client._channels[1]["observed_interval_ms"] == 500
    assert client._channels[1]["totals"] == {
        "uart_tx": 55, "uart_rx": 66, "usb_tx": 77, "usb_rx": 88,
    }


def test_sequence_gap_and_invalid_interval_only_mark_the_affected_channel_incomplete():
    device = FakeHidDevice({}, inputs=[
        (10, channel_bytes(sequence=3)),
        (11, channel_bytes(sequence=7)),
        (10, channel_bytes(sequence=5, interval_ms=65535, traffic=(0xFFFFFFFF, 1, 1, 1))),
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


def test_boot_counter_wrap_derives_modulo_delta():
    device = FakeHidDevice({}, inputs=[
        (10, channel_bytes(sequence=1, traffic=(0xFFFFFFF0, 0, 0, 0))),
        (10, channel_bytes(sequence=2, traffic=(32, 0, 0, 0))),
    ])
    client = PicoUartHid(device=device)
    client.read_status()
    channel = client.read_status()["channels"][0]
    assert channel["totals"]["uart_tx"] == 32
    assert channel["traffic"]["uart_tx"] == 48
    assert channel["observed_interval_ms"] == 500


def test_duplicate_and_old_sequence_reports_never_add_traffic_or_interval_twice():
    device = FakeHidDevice({}, inputs=[
        (10, channel_bytes(sequence=100, interval_ms=500, traffic=(8, 0, 0, 0))),
        (10, channel_bytes(sequence=101, interval_ms=500, traffic=(11, 0, 0, 0))),
        (10, channel_bytes(sequence=101, interval_ms=500, traffic=(3, 0, 0, 0))),
        (10, channel_bytes(sequence=100, interval_ms=500, traffic=(100, 0, 0, 0))),
    ])
    client = PicoUartHid(device=device)
    for _ in range(4):
        client.read_status()
    channel = client._channels[0]
    assert channel["totals"]["uart_tx"] == 11
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
    assert channel["totals"]["uart_tx"] == 1000
    assert channel["observed_interval_ms"] == 0
    client._apply_input(0, parse_channel(channel_bytes(
        sequence=(resumed_sequence + 1) % 65536, traffic=(1007, 0, 0, 0),
    )), 3.6)
    assert channel["totals"]["uart_tx"] == 1007
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
    client.read_channel(0)
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
        control_bytes(action=2, status=2),
    ])
    PicoUartHid(device=device).toggle_led()
    assert len(device.writes) == 1
    report = device.writes[0]
    assert report == [20, 1, 2, 0, 0]


def test_save_configuration_uses_report20_request_and_verified_result():
    device = ControlDevice([
        control_bytes(),
        control_bytes(action=4, status=2),
    ])
    result = PicoUartHid(device=device).save_configuration(2, unsafe_overclock=True)
    assert result["status"] == 2
    assert result["action"] == 4
    assert device.writes == [[20, 1, 4, 3, 2]]


def test_reboot_is_one_confirmed_action():
    device = ControlDevice([control_bytes(), control_bytes(action=3, status=2)])
    PicoUartHid(device=device).reset_board()
    assert device.writes == [[20, 1, 3, 1, 0]]


def test_static_identity_failure_prevents_board_control_write():
    device = ControlDevice([control_bytes()])
    device.reports[REPORT_ID_DEVICE_INFO] = bytes(DEVICE_INFO_SIZE)
    with pytest.raises(RuntimeError, match="Unknown product"):
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


def test_close_waits_for_timed_out_feature_transfer_before_closing_device(monkeypatch):
    import pico_uart.client as client_module

    started = threading.Event()
    release = threading.Event()

    class SlowDevice(FakeHidDevice):
        def get_feature_report(self, report_id, size):
            started.set()
            release.wait(timeout=1)
            return super().get_feature_report(report_id, size)

    monkeypatch.setattr(client_module, "COMMAND_TIMEOUT_S", 0.01)
    device = SlowDevice({})
    client = PicoUartHid(device=device)
    with pytest.raises(RuntimeError, match="transfer timed out"):
        client.read_identity()
    assert started.is_set()
    client.close()
    assert not device.closed
    release.set()
    assert client._transfer_idle.wait(timeout=1)
    assert device.closed
