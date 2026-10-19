"""Board identity and runtime contract checks without a USB device."""

import struct
import threading

import pytest

from helpers import FakeHidDevice
from pico_uart.client import PicoUartHid
from pico_uart.protocol import parse_identity, parse_runtime, parse_led
from pico_uart.web.app import DashboardService, _empty_snapshot


def test_identity_and_runtime_read_from_device_reports():
    identity = b"\1" + b"PicoUart".ljust(32, b"\0") + b"0123456789ABCDEF"
    runtime = struct.pack("<BQHHII", 2, 2**32, 2500, 0xffff, 65536, 270336)
    assert PicoUartHid(device=FakeHidDevice(identity, 7)).read_identity() == {
        "product_name": "PicoUart", "serial_number": "0123456789ABCDEF",
    }
    assert PicoUartHid(device=FakeHidDevice(runtime, 8)).read_runtime() == {
        "uptime_ms": 2**32, "cpu_usage_basis_points": [2500, None],
        "ram_reserved_bytes": 65536, "ram_total_bytes": 270336,
    }


@pytest.mark.parametrize("payload", [
    b"", bytes(49), b"\2" + bytes(48),
    b"\1" + b"X" * 32 + b"0123456789ABCDEF",
    b"\1" + b"Pico\0junk".ljust(32, b"\0") + b"0123456789ABCDEF",
    b"\1" + b"Pico".ljust(32, b"\0") + b"G123456789ABCDEF",
    b"\1" + b"\xff".ljust(32, b"\0") + b"0123456789ABCDEF",
])
def test_identity_rejects_malformed_payloads(payload):
    with pytest.raises(RuntimeError):
        parse_identity(payload)


@pytest.mark.parametrize("payload", [
    b"", bytes(21), struct.pack("<BQHHII", 3, 0, 0, 0, 1, 2),
    struct.pack("<BQHHII", 1, 0, 10001, 0, 1, 2),
    struct.pack("<BQHHII", 1, 0, 0, 65534, 1, 2),
    struct.pack("<BQHHII", 1, 0, 0, 0, 0, 0),
    struct.pack("<BQHHII", 1, 0, 0, 0, 3, 2),
])
def test_runtime_rejects_malformed_payloads(payload):
    with pytest.raises(RuntimeError):
        parse_runtime(payload)


def test_legacy_fixed_cpu_values_are_not_presented_as_measured_service_load():
    runtime = parse_runtime(struct.pack("<BQHHII", 1, 1234, 10000, 10000, 65536, 270336))
    assert runtime["cpu_usage_basis_points"] == [None, None]
    assert runtime["uptime_ms"] == 1234
    assert runtime["ram_reserved_bytes"] == 65536


@pytest.mark.parametrize("flags,manual,output", [(0, None, None), (1, False, False),
                                               (5, False, True), (7, True, True)])
def test_led_report_reads_actual_state(flags, manual, output):
    expected = {"manual_on": manual, "output_on": output}
    assert parse_led(bytes((1, flags))) == expected
    assert PicoUartHid(device=FakeHidDevice(bytes((1, flags)), 9)).read_led() == expected


@pytest.mark.parametrize("payload", [b"", b"\1", b"\1\1\0", b"\2\1", b"\1\10", b"\1\2", b"\1\3", b"\1\4"])
def test_led_report_rejects_invalid_layout_and_state(payload):
    with pytest.raises(RuntimeError):
        parse_led(payload)


def test_old_firmware_errors_do_not_discard_existing_reports_and_recovery_clears_errors():
    class Client:
        unsupported = True

        def read_led(self):
            if self.unsupported:
                raise OSError("LED unsupported")
            return {"manual_on": False, "output_on": True}

        def read_identity(self):
            if self.unsupported:
                raise OSError("identity unsupported")
            return {"product_name": "PicoUart", "serial_number": "0123456789ABCDEF"}

        def read_runtime(self):
            if self.unsupported:
                raise OSError("runtime unsupported")
            return {"uptime_ms": 42}

        def read_board_status(self):
            return {"firmware_version": "1.2.3"}

        def read_overflow_counts(self):
            return [0] * 6

        def read_hardware_info(self):
            return {"mcu": "RP2350"}

    service = object.__new__(DashboardService)
    service._lock = threading.RLock()
    service._snapshot = _empty_snapshot()
    service._snapshot.update(identity={"stale": True}, runtime={"stale": True})
    client = Client()
    service._read_metadata(client)
    snapshot = service.snapshot()
    assert snapshot["identity"] is None and snapshot["runtime"] is None
    assert snapshot["identity_error"] == "identity unsupported"
    assert snapshot["runtime_error"] == "runtime unsupported"
    assert snapshot["led"] is None and snapshot["led_error"] == "LED unsupported"
    assert snapshot["board"]["firmware_version"] == "1.2.3"
    assert snapshot["hardware"]["mcu"] == "RP2350"
    client.unsupported = False
    service._read_metadata(client)
    snapshot = service.snapshot()
    assert snapshot["identity_error"] is None and snapshot["runtime_error"] is None
    assert snapshot["runtime"]["uptime_ms"] == 42
    assert snapshot["led"] == {"manual_on": False, "output_on": True}
    assert snapshot["led_error"] is None
