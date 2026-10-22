"""Board identity and telemetry contract checks without a USB device."""

import threading

import pytest

from helpers import FakeHidDevice, telemetry_bytes
from pico_uart.client import PicoUartHid
from pico_uart.protocol import REPORT_ID_TELEMETRY, parse_identity, parse_led_flags, parse_telemetry
from pico_uart.web.monitor import BoardMonitor, empty_snapshot


def bare_monitor(**snapshot):
    service = object.__new__(BoardMonitor)
    service._lock = threading.RLock()
    service._snapshot = empty_snapshot()
    service._snapshot.update(snapshot)
    service._reset_connection_cache()
    return service


def test_identity_and_runtime_read_from_device_reports():
    identity = b"\1" + b"PicoUart".ljust(32, b"\0") + b"0123456789ABCDEF"
    telemetry = telemetry_bytes(uptime_ms=2**32, cores=(2500, 0xffff), ram=(65536, 270336))
    assert PicoUartHid(device=FakeHidDevice(identity, 7)).read_identity() == {
        "product_name": "PicoUart", "serial_number": "0123456789ABCDEF",
    }
    assert PicoUartHid(device=FakeHidDevice(telemetry, REPORT_ID_TELEMETRY)).read_runtime() == {
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


@pytest.mark.parametrize("kwargs", [
    {"cores": (10001, 0)}, {"cores": (0, 65534)}, {"ram": (0, 0)}, {"ram": (3, 2)},
    {"save": (6, 0)}, {"save": (2, 5)}, {"save": (2, 0xFFFFFFFF)}, {"save": (1, 0xFFFFFFFF)},
    {"led": 8}, {"led": 2}, {"led": 3},
])
def test_telemetry_rejects_malformed_values(kwargs):
    with pytest.raises(RuntimeError):
        parse_telemetry(telemetry_bytes(**kwargs))


@pytest.mark.parametrize("payload", [b"", bytes(58), bytes(60), telemetry_bytes(layout_version=2)])
def test_telemetry_rejects_wrong_size_or_version(payload):
    with pytest.raises(RuntimeError, match="size or version"):
        parse_telemetry(payload)


def test_telemetry_decodes_every_section():
    telemetry = parse_telemetry(telemetry_bytes(
        led=7, temperature_c=-1.25, uptime_ms=1234, cores=(10000, 10000), overflow=(0, 1, 2, 3, 4, 0xffffffff),
        save=(2, 3), generations=(0, 1, 2, 3, 254, 255)))
    assert telemetry == {
        "led": {"manual_on": True, "output_on": True},
        "temperature_celsius": -1.25,
        "runtime": {"uptime_ms": 1234, "cpu_usage_basis_points": [10000, 10000],
                    "ram_reserved_bytes": 65536, "ram_total_bytes": 270336},
        "overflow_counts": [0, 1, 2, 3, 4, 0xffffffff],
        "configuration_save": {"status": 2, "profile": 3},
        "diagnostics_generation": [0, 1, 2, 3, 254, 255],
    }
    assert parse_telemetry(telemetry_bytes(save=(0, 0xFFFFFFFF)))["configuration_save"] == {
        "status": 0, "profile": None}


@pytest.mark.parametrize("flags,manual,output", [(0, None, None), (1, False, False),
                                               (5, False, True), (7, True, True)])
def test_led_report_reads_actual_state(flags, manual, output):
    expected = {"manual_on": manual, "output_on": output}
    assert parse_led_flags(flags) == expected
    assert PicoUartHid(device=FakeHidDevice(telemetry_bytes(led=flags), REPORT_ID_TELEMETRY)).read_led() == expected


@pytest.mark.parametrize("flags", [8, 2, 3, 4, 0x80])
def test_led_report_rejects_invalid_state(flags):
    with pytest.raises(RuntimeError):
        parse_led_flags(flags)


class MonitorClient:
    """Counts feature reads so tests can assert how often each report is fetched."""

    def __init__(self):
        self.reads = []
        self.generations = [1] * 6
        self.fail = set()

    def _read(self, name, value):
        self.reads.append(name)
        if name in self.fail:
            raise OSError(f"{name} unavailable")
        return value

    def read_telemetry(self):
        return self._read("telemetry", {
            "led": {"manual_on": False, "output_on": True}, "temperature_celsius": 25.3,
            "runtime": {"uptime_ms": 42}, "overflow_counts": [0] * 6,
            "configuration_save": {"status": 0, "profile": None},
            "diagnostics_generation": list(self.generations),
        })

    def read_channel_diagnostic(self, index):
        return self._read(f"diagnostic{index}", {"faults": {}, "index": index})

    def read_identity(self):
        return self._read("identity", {"product_name": "PicoUart", "serial_number": "0123456789ABCDEF"})

    def read_configuration(self):
        return self._read("configuration", {"source": "defaults", "reason": "erased", "active_profile": 1})

    def read_board_status(self):
        return self._read("board", {"firmware_version": "1.2.3", "temperature_celsius": 0.0,
                                    "hid_reset_enabled": True})

    def read_hardware_info(self):
        return self._read("hardware", {"mcu": "RP2350"})


def test_steady_state_polls_only_telemetry_and_changed_diagnostics():
    service, client = bare_monitor(), MonitorClient()
    service._read_metadata(client)
    assert sorted(client.reads) == sorted(
        ["telemetry", "identity", "configuration", "board", "hardware", *(f"diagnostic{i}" for i in range(6))])
    snapshot = service.snapshot()
    assert snapshot["board"] == {"firmware_version": "1.2.3", "hid_reset_enabled": True, "temperature_celsius": 25.3}
    assert [item["index"] for item in snapshot["channel_diagnostics"]] == list(range(6))

    client.reads.clear()
    service._read_metadata(client)
    assert client.reads == ["telemetry"]

    client.reads.clear()
    client.generations[2] = 2
    service._read_metadata(client)
    assert client.reads == ["telemetry", "diagnostic2"]

    client.reads.clear()
    service._read_metadata(client)
    assert client.reads == ["telemetry"]


def test_diagnostics_generation_wraparound_and_failure_force_full_refresh():
    service, client = bare_monitor(), MonitorClient()
    client.generations = [255] * 6
    service._read_metadata(client)
    client.reads.clear()
    client.generations[0] = 0
    service._read_metadata(client)
    assert client.reads == ["telemetry", "diagnostic0"]

    client.reads.clear()
    client.generations[1] = 7
    client.fail = {"diagnostic1"}
    service._read_metadata(client)
    snapshot = service.snapshot()
    assert snapshot["channel_diagnostics"] is None
    assert snapshot["diagnostics_error"] == "diagnostic1 unavailable"

    client.reads.clear()
    client.fail = set()
    service._read_metadata(client)
    assert sorted(client.reads) == sorted(["telemetry", *(f"diagnostic{i}" for i in range(6))])
    assert service.snapshot()["diagnostics_error"] is None


def test_telemetry_errors_are_explicit_and_recovery_clears_them():
    service, client = bare_monitor(runtime={"stale": True}), MonitorClient()
    client.fail = {"telemetry"}
    service._read_metadata(client)
    snapshot = service.snapshot()
    assert snapshot["runtime"] is None and snapshot["runtime_error"] == "telemetry unavailable"
    assert snapshot["led"] is None and snapshot["led_error"] == "telemetry unavailable"
    assert snapshot["configuration_save"] is None
    assert snapshot["configuration_save_error"] == "telemetry unavailable"
    assert snapshot["metadata_error"] == "telemetry unavailable"
    assert snapshot["board"] is None and snapshot["overflow_counts"] == [None] * 6
    assert snapshot["identity"]["product_name"] == "PicoUart"
    assert "diagnostic0" not in client.reads

    client.fail = set()
    service._read_metadata(client)
    snapshot = service.snapshot()
    assert snapshot["runtime"] == {"uptime_ms": 42} and snapshot["runtime_error"] is None
    assert snapshot["led"] == {"manual_on": False, "output_on": True} and snapshot["led_error"] is None
    assert snapshot["configuration_save_error"] is None and snapshot["metadata_error"] is None
    assert snapshot["board"]["temperature_celsius"] == 25.3


def test_static_report_failures_are_retried_until_they_succeed_once():
    service, client = bare_monitor(identity={"stale": True}), MonitorClient()
    client.fail = {"identity", "board", "hardware"}
    service._read_metadata(client)
    snapshot = service.snapshot()
    assert snapshot["identity"] is None and snapshot["identity_error"] == "identity unavailable"
    assert snapshot["hardware"] is None and snapshot["hardware_error"] == "hardware unavailable"
    assert snapshot["board"] is None and snapshot["metadata_error"] == "board unavailable"
    assert snapshot["configuration"]["reason"] == "erased"

    client.reads.clear()
    client.fail = set()
    service._read_metadata(client)
    assert sorted(client.reads) == ["board", "hardware", "identity", "telemetry"]
    snapshot = service.snapshot()
    assert snapshot["identity_error"] is None and snapshot["hardware_error"] is None
    assert snapshot["metadata_error"] is None and snapshot["board"]["firmware_version"] == "1.2.3"

    client.reads.clear()
    service._read_metadata(client)
    assert client.reads == ["telemetry"]


def test_new_connection_rereads_static_reports_and_all_diagnostics():
    service, client = bare_monitor(), MonitorClient()
    service._read_metadata(client)
    service._reset_connection_cache()
    client.reads.clear()
    service._read_metadata(client)
    assert len(client.reads) == 11
