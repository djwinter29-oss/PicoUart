"""Static identity and dynamic board telemetry report-1/report-3 checks."""

import pytest

from helpers import FakeHidDevice, device_info_bytes, telemetry_bytes
from pico_uart.client import PicoUartHid
from pico_uart.protocol import (
    REPORT_ID_DEVICE_INFO,
    REPORT_ID_TELEMETRY,
    parse_device_info,
    parse_telemetry,
)


def test_identity_contains_static_capabilities_and_total_ram():
    info = parse_device_info(device_info_bytes(mcu=2, capabilities=7, profile_mask=0x1F))
    assert info["mcu"] == "RP2350"
    assert info["capabilities"] == 7
    assert info["clock_profile_mask"] == 0x1F
    assert info["ram_total_bytes"] == 270336


@pytest.mark.parametrize("profile_mask", [0, 0x20, 0xFF])
def test_identity_rejects_invalid_profile_mask(profile_mask):
    with pytest.raises(RuntimeError):
        parse_device_info(device_info_bytes(profile_mask=profile_mask))


@pytest.mark.parametrize("payload", [
    b"", bytes(23), bytes(25), bytes((2,)) + bytes(23),
    telemetry_bytes(led=2), telemetry_bytes(cores=(10001, 0)),
    telemetry_bytes(clock_hz=0),
])
def test_telemetry_rejects_invalid_values(payload):
    with pytest.raises(RuntimeError):
        parse_telemetry(payload)


def test_runtime_telemetry_uses_board_total_from_static_report():
    device = FakeHidDevice({
        REPORT_ID_DEVICE_INFO: device_info_bytes(),
        REPORT_ID_TELEMETRY: telemetry_bytes(uptime_ms=2**32, cores=(2500, 0xFFFF)),
    })
    client = PicoUartHid(device=device)
    client.read_identity()
    assert client.read_runtime() == {
        "uptime_ms": 2**32,
        "service_load_basis_points": [2500, None],
        "system_clock_hz": 250000000,
    }


def test_failed_static_identity_does_not_hide_dynamic_telemetry():
    device = FakeHidDevice({
        REPORT_ID_TELEMETRY: telemetry_bytes(temperature_c=-1.25),
    })
    assert PicoUartHid(device=device).read_board_temperature() == -1.25
