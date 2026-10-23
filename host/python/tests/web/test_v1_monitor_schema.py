"""Python dashboard adapter exposes the shared per-channel version-1 schema."""

import threading

import pytest

from pico_uart.web.monitor import BoardMonitor, empty_snapshot


def test_empty_snapshot_uses_per_channel_accounting_fields():
    snapshot = empty_snapshot()
    assert "sequence" not in snapshot
    assert len(snapshot["channels"]) == 6
    for channel in snapshot["channels"]:
        assert channel["sequence"] is None
        assert channel["updated_at"] is None
        assert channel["observed_interval_ms"] == 0
        assert channel["traffic_gaps"] == 0
        assert channel["traffic_incomplete"] is False
        assert channel["totals"] == {"uart_tx": 0, "uart_rx": 0, "usb_tx": 0, "usb_rx": 0}


def test_metadata_polling_uses_hid_cadence_without_sleeping_between_input_reads(monkeypatch):
    service = object.__new__(BoardMonitor)
    service._lock = threading.RLock()
    service._stop = threading.Event()
    service._snapshot = empty_snapshot()
    now = 0.0
    reads = []

    class Client:
        def read_status(self, timeout_ms):
            nonlocal now
            assert timeout_ms == 250
            now += 0.25
            return {}

        def close(self):
            pass

    def read_metadata(client):
        reads.append(now)
        if len(reads) == 3:
            service._stop.set()

    service._client_factory = Client
    service._apply_status = lambda status: service._snapshot.update(refresh_interval_ms=750)
    service._read_metadata = read_metadata
    monkeypatch.setattr("pico_uart.web.monitor.time.monotonic", lambda: now)
    service._poll()
    assert reads == [0.25, 1.0, 1.75]


@pytest.mark.parametrize(("interval", "refresh"), [
    (0, 500), (100, 500), (500, 500), (750, 750), (1000, 1000), (5000, 1000), (65535, 500),
])
def test_input_snapshot_preserves_shared_fault_timestamp_without_category_timestamps(interval, refresh):
    service = object.__new__(BoardMonitor)
    service._lock = threading.RLock()
    service._snapshot = empty_snapshot()
    status = {
        "channels": [{
            "id": 2,
            "updated_at": "2026-10-22T12:00:00.000+00:00",
            "sequence": 12,
            "interval_ms": interval,
            "observed_interval_ms": 1000,
            "traffic_gaps": 1,
            "traffic_incomplete": True,
            "channel_state": 2,
            "uart_measurement_state": 1,
            "state": "ready",
            "cdc_open": True,
            "traffic": {"uart_tx": 4, "uart_rx": 5, "usb_tx": 6, "usb_rx": 7},
            "totals": {"uart_tx": 40, "uart_rx": 50, "usb_tx": 60, "usb_rx": 70},
            "baud_rate": 115200,
            "data_bits": 8,
            "stop_bits": 1,
            "parity": "none",
        }],
    }
    service._apply_status(status)
    snapshot = service.snapshot()
    assert snapshot["refresh_interval_ms"] == refresh
    assert snapshot["updated_at"] == "2026-10-22T12:00:00.000+00:00"
    assert snapshot["traffic_incomplete"] is True
    assert snapshot["traffic_gaps"] == 1
    channel = snapshot["channels"][2]
    assert channel["updated_at"] == "2026-10-22T12:00:00.000+00:00"
    assert channel["sequence"] == 12
    assert channel["traffic_gaps"] == 1
    assert channel["traffic_incomplete"] is True
    assert snapshot["channel_diagnostics"] is None
    status["channels"][0]["interval_ms"] = 600
    service._apply_status(status)
    assert service.snapshot()["refresh_interval_ms"] == refresh
    status["channels"][0]["updated_at"] = "2026-10-22T12:00:00.500+00:00"
    service._apply_status(status)
    assert service.snapshot()["refresh_interval_ms"] == 600
    for second, invalid in [(1, 0), (2, 65535)]:
        status["channels"][0].update(
            updated_at=f"2026-10-22T12:00:0{second}.000+00:00", interval_ms=invalid,
        )
        service._apply_status(status)
        assert service.snapshot()["refresh_interval_ms"] == 600


def test_static_identity_masks_and_dynamic_clock_keep_their_own_report_sources():
    service = object.__new__(BoardMonitor)
    service._lock = threading.RLock()
    service._snapshot = empty_snapshot()

    class Client:
        def read_identity(self):
            return {
                "product_name": "PicoUart",
                "serial_number": "0123456789ABCDEF",
                "firmware_version": "1.2.3",
                "mcu": "RP2040",
                "mcu_id": 1,
                "capabilities": 7,
                "clock_profile_mask": 15,
                "ram_total_bytes": 270336,
                "hid_led_available": True,
            }

        def read_telemetry(self):
            return {
                "led": True,
                "temperature_celsius": 25.0,
                "system_clock_hz": 200000000,
                "runtime": {"uptime_ms": 10, "system_clock_hz": 200000000},
            }

        def read_control(self):
            return {
                "action": 4,
                "status": 2,
                "error_reason": 0,
            }

        def read_configuration(self):
            return {"stored_profile": 2, "channel_count": 6}

    client = Client()
    service._read_static(client)
    service._read_telemetry(client)
    service._read_control(client)
    snapshot = service.snapshot()
    assert snapshot["identity"]["capabilities"] == 7
    assert snapshot["identity"]["clock_profile_mask"] == 15
    assert snapshot["identity"]["mcu_id"] == 1
    assert snapshot["hardware"]["system_clock_hz"] == 200000000
    assert snapshot["runtime"]["system_clock_hz"] == 200000000
    assert snapshot["configuration_save"]["status"] == 2


def test_non_save_control_result_is_idle_for_save_ui_and_capability_failure_is_explicit():
    from helpers import control_bytes
    from pico_uart.protocol import parse_control

    service = object.__new__(BoardMonitor)
    service._lock = threading.RLock()
    service._snapshot = empty_snapshot()

    class Client:
        def read_control(self):
            return parse_control(control_bytes(action=2, status=2))

        def read_configuration(self):
            return {"stored_profile": None}

    service._snapshot["identity"] = {"capabilities": 4}
    service._read_control(Client())
    assert service.snapshot()["configuration_save"] == {"status": 0, "action": 0, "error_reason": 0, "error": "none"}
    assert service.snapshot()["configuration_save_error"] is None
    service._snapshot["identity"] = {"capabilities": 0}
    service._read_control(Client())
    assert service.snapshot()["configuration_save"] is None
    assert service.snapshot()["configuration_save_error"]
