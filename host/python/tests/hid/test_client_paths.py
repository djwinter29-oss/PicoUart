# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""Client paths not covered by report-level tests: LED control and channel readiness."""

import pytest
from helpers import FakeHidDevice, channel_bytes, control_bytes, device_info_bytes, telemetry_bytes

from pico_uart.hid import protocol as hid
from pico_uart.hid.client import PicoUartHid, channel_ready


def _device(*, capabilities=7, led=1):
    return FakeHidDevice({
        hid.REPORT_ID_DEVICE_INFO: device_info_bytes(capabilities=capabilities),
        hid.REPORT_ID_TELEMETRY: telemetry_bytes(led=led),
        hid.REPORT_ID_CONTROL: control_bytes(),
    })


def test_read_led_reports_state_or_unavailable():
    assert PicoUartHid(device=_device(led=1)).read_led() == {"manual_on": True, "output_on": True}
    assert PicoUartHid(device=_device(capabilities=6)).read_led() == {"manual_on": None, "output_on": None}


def test_toggle_led_requires_hardware_and_never_writes_without_it():
    device = _device(capabilities=6)
    with pytest.raises(RuntimeError, match="LED is unavailable"):
        PicoUartHid(device=device).toggle_led()
    assert device.writes == []


def test_reset_requires_enabled_firmware_flag():
    with pytest.raises(RuntimeError, match="reset is disabled"):
        PicoUartHid(device=_device(capabilities=5)).reset_board()


def test_read_board_status_merges_identity_and_temperature():
    status = PicoUartHid(device=_device()).read_board_status()
    assert status["temperature_celsius"] == pytest.approx(25.3)
    assert status["hid_led_enabled"] is True


@pytest.mark.parametrize("baud,requested,ready", [
    (115200, 115200, True), (115200 * 1.02, 115200, True), (115200 * 1.05, 115200, False), (0, 115200, False),
])
def test_channel_ready_tolerance(baud, requested, ready):
    sample = {"state": "ready", "baud_rate": int(baud), "data_bits": 8, "parity": "none", "stop_bits": 1}
    assert channel_ready(sample, requested) is ready


@pytest.mark.parametrize("override", [
    {"state": "starting"}, {"data_bits": 7}, {"parity": "even"}, {"stop_bits": 2},
])
def test_channel_ready_requires_ready_raw_8n1(override):
    sample = {"state": "ready", "baud_rate": 115200, "data_bits": 8, "parity": "none", "stop_bits": 1, **override}
    assert channel_ready(sample, 115200) is False


def test_wait_channels_ready_returns_when_applied_and_rejects_errors():
    device = _device()
    device.reports[hid.REPORT_ID_CHANNEL_BASE] = channel_bytes(state=2)
    assert PicoUartHid(device=device).wait_channels_ready({0: 115200})[0]["id"] == 0
    device.reports[hid.REPORT_ID_CHANNEL_BASE] = channel_bytes(state=4)
    with pytest.raises(OSError, match="CDC0 configuration failed"):
        PicoUartHid(device=device).wait_channels_ready({0: 115200})


def test_bounded_transfer_relays_worker_error_and_recovers_after_start_failure(monkeypatch):
    client = PicoUartHid(device=_device())

    def fail():
        raise ValueError("worker failed")

    with pytest.raises(ValueError, match="worker failed"):
        client._bounded_transfer(fail)

    def fail_start(_thread):
        raise RuntimeError("thread start failed")

    with monkeypatch.context() as patch:
        patch.setattr("threading.Thread.start", fail_start)
        with pytest.raises(RuntimeError, match="thread start failed"):
            client._bounded_transfer(lambda: 0)

    assert client._bounded_transfer(lambda: 42) == 42


def test_wait_channels_ready_validates_channels_and_timeout():
    client = PicoUartHid(device=_device())
    with pytest.raises(ValueError, match="channels must map"):
        client.wait_channels_ready({})
    with pytest.raises(ValueError, match="channels must map"):
        client.wait_channels_ready({6: 115200})
    with pytest.raises(ValueError, match="timeout_s"):
        client.wait_channels_ready({0: 115200}, timeout_s=0)


def test_module_level_readers_delegate_to_client():
    from pico_uart.hid import client

    device = _device()
    assert client.read_board_temperature(device) == pytest.approx(25.3)
    assert client.read_firmware_version(device) == "1.2.3"
    assert client.read_hardware_info(device)["mcu"] == "RP2040"
    assert client.read_telemetry(device)["led"] is True
    assert client.read_board_status(device)["product_name"]
    assert client.read_configuration(device)["channel_pins"]
    with pytest.raises(RuntimeError, match="reset is disabled"):
        client.reset_board(_device(capabilities=5))
