"""Python dashboard adapter exposes the shared per-channel version-1 schema."""

import threading

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


def test_input_snapshot_preserves_shared_fault_timestamp_without_category_timestamps():
    service = object.__new__(BoardMonitor)
    service._lock = threading.RLock()
    service._snapshot = empty_snapshot()
    status = {
        "channels": [{
            "id": 2,
            "updated_at": "2026-10-22T12:00:00.000+00:00",
            "sequence": 12,
            "observed_interval_ms": 1000,
            "traffic_gaps": 1,
            "traffic_incomplete": True,
            "health": 0x51,
            "health_labels": ["ready", "cdc_open", "rx_overrun"],
            "state": "attention",
            "cdc_open": True,
            "ring_high_watermark": 32,
            "traffic": {"uart_tx": 4, "uart_rx": 5, "usb_tx": 6, "usb_rx": 7},
            "totals": {"uart_tx": 40, "uart_rx": 50, "usb_tx": 60, "usb_rx": 70},
            "baud_rate": 115200,
            "data_bits": 8,
            "stop_bits": 1,
            "parity": "none",
            "tx_pin": 4,
            "rx_pin": 5,
            "faults": {
                "init_failed": {"count": 0},
                "control_error": {"count": 1},
                "rx_overrun": {"count": 9},
                "rx_error": {"count": 0},
            },
            "last_fault_uptime_s": 123,
        }],
    }
    service._apply_status(status)
    snapshot = service.snapshot()
    channel = snapshot["channels"][2]
    assert channel["updated_at"] == "2026-10-22T12:00:00.000+00:00"
    assert channel["sequence"] == 12
    assert channel["traffic_gaps"] == 1
    assert channel["traffic_incomplete"] is True
    diagnostic = snapshot["channel_diagnostics"][2]
    assert diagnostic["faults"]["rx_overrun"] == {"count": 9}
    assert diagnostic["last_fault_uptime_s"] == 123
    assert all("last_seen_ms" not in fault for fault in diagnostic["faults"].values())


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
                "status": 2,
                "requested_profile": 2,
                "request_id": 4,
                "error_reason": 0,
                "source": "stored",
                "reason": "none",
                "stored_profile": 2,
                "active_profile": 2,
            }

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
