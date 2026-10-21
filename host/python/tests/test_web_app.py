"""Hardware-free tests for the Flask dashboard and its HID polling service."""

from __future__ import annotations

import re
import threading
import time

import pytest

from pico_uart.protocol import parse_hardware_info, parse_status
from pico_uart.web.app import DashboardService, _empty_snapshot, create_app
from helpers import board_status_bytes, overflow_counts_bytes, status_report_bytes


class FakeDashboard:
    def __init__(self):
        self.actions = []

    def snapshot(self, board_id=None):
        return {"connected": False, "channels": []}

    def toggle_led(self, board_id=None):
        self.actions.append("toggle-led")

    def reset_board(self, board_id=None):
        self.actions.append("reset")


class BoardTelemetry:
    def read_board_status(self):
        return {"firmware_version": "1.2.3"}

    def read_overflow_counts(self):
        return [0] * 6

    def read_hardware_info(self):
        return {"mcu": "RP2040", "mcu_id": 1, "system_clock_hz": 125_000_000}

    def read_configuration(self):
        return {"source": "defaults", "reason": "erased", "stored_profile": None, "active_profile": 1}

    def read_configuration_save(self):
        return {"status": 0, "profile": None}

    def read_channel_diagnostics(self):
        return [{"baud_rate": 115200, "data_bits": 8, "parity": "N", "stop_bits": 1,
                 "tx_pin": index * 4, "rx_pin": index * 4 + 1, "faults": {}} for index in range(6)]

    def read_led(self):
        return {"manual_on": False, "output_on": False}

    def read_identity(self):
        return {"product_name": "PicoUart", "serial_number": "0123456789ABCDEF"}

    def read_runtime(self):
        return {"uptime_ms": 4294967296, "cpu_usage_basis_points": [10000, 10000],
                "ram_reserved_bytes": 65536, "ram_total_bytes": 270336}


def _csrf_token(client):
    response = client.get("/")
    match = re.search(rb'name="csrf-token" content="([^"]+)"', response.data)
    assert match is not None
    return match.group(1).decode()


def test_dashboard_routes_require_csrf_and_dispatch_controls():
    service = FakeDashboard()
    client = create_app(service).test_client()

    assert client.get("/api/status").json == {"connected": False, "channels": []}
    assert client.post("/api/actions/toggle-led").status_code == 403

    token = _csrf_token(client)
    response = client.post("/api/actions/toggle-led", headers={"X-CSRF-Token": token})
    assert response.status_code == 200
    assert response.json == {"ok": True}
    assert service.actions == ["toggle-led"]


def test_dashboard_serves_shared_assets():
    client = create_app(FakeDashboard()).test_client()
    page = client.get("/")
    assert b"<small>Python</small>" in page.data
    assert b"<title>PicoUart Diagnostics | Python</title>" in page.data
    assert b'rel="icon" type="image/svg+xml" href="/favicon.svg"' in page.data
    assert b'id="mcu"' in page.data
    assert b'id="system-clock"' in page.data
    assert b'href="/css/dashboard.css?v=6"' in page.data
    assert b'src="/js/dashboard.js?v=6"' in page.data
    assert b"{{" not in page.data
    for path in ("css/dashboard.css", "js/dashboard.js", "favicon.svg"):
        response = client.get(f"/{path}")
        assert response.status_code == 200
        assert response.data
        assert response.headers["X-Content-Type-Options"] == "nosniff"
        if path == "favicon.svg":
            assert response.mimetype == "image/svg+xml"
            assert b'<svg xmlns="http://www.w3.org/2000/svg"' in response.data
    for path in ("index.html", "css/index.html", "js/index.html", "css/../index.html", "js/../index.html"):
        assert client.get(f"/{path}").status_code == 404


def test_dashboard_rejects_untrusted_host_before_serving_controls():
    service = FakeDashboard()
    client = create_app(service).test_client()

    assert client.get("/", base_url="http://attacker.example").status_code == 400
    page = client.get("/", base_url="http://localhost")
    token = re.search(rb'name="csrf-token" content="([^"]+)"', page.data).group(1).decode()
    response = client.post(
        "/api/actions/toggle-led",
        base_url="http://attacker.example",
        headers={"X-CSRF-Token": token},
    )

    assert response.status_code == 400
    assert service.actions == []


def test_dashboard_sanitizes_reset_capability_errors():
    class ResetDisabledDashboard(FakeDashboard):
        def reset_board(self, board_id=None):
            raise RuntimeError("firmware HID reset is disabled")

    client = create_app(ResetDisabledDashboard()).test_client()
    token = _csrf_token(client)
    response = client.post("/api/actions/reset", headers={"X-CSRF-Token": token})

    assert response.status_code == 409
    assert response.json == {"error": "request could not be completed"}


def test_dashboard_maps_action_io_errors_and_unknown_actions():
    class BrokenDashboard(FakeDashboard):
        def toggle_led(self, board_id=None):
            raise OSError("HID write failed")

    client = create_app(BrokenDashboard()).test_client()
    token = _csrf_token(client)
    headers = {"X-CSRF-Token": token}

    response = client.post("/api/actions/toggle-led", headers=headers)
    assert response.status_code == 503
    assert response.json == {"error": "service temporarily unavailable"}

    response = client.post("/api/actions/unknown", headers=headers)
    assert response.status_code == 404


def test_dashboard_service_polls_status_and_metadata():
    class FakeClient(BoardTelemetry):
        def __init__(self):
            self.first_report = True
            self.closed = False

        def read_status(self, timeout_ms):
            if self.first_report:
                self.first_report = False
                return parse_status(status_report_bytes(sequence=4, health0=0x31))
            time.sleep(min(timeout_ms / 1000, 0.01))
            return None

        def read_board_status(self):
            return {"firmware_version": "1.2.3", "temperature_celsius": 25.3, "hid_reset_enabled": False}

        def read_overflow_counts(self):
            return [0, 1, 2, 3, 4, 5]

        def read_hardware_info(self):
            return {"mcu": "RP2040", "mcu_id": 1, "system_clock_hz": 125_000_000}

        def close(self):
            self.closed = True

    fake = FakeClient()
    service = DashboardService(client_factory=lambda: fake)
    try:
        deadline = time.monotonic() + 2
        snapshot = service.snapshot()
        while snapshot["sequence"] != 4 and time.monotonic() < deadline:
            time.sleep(0.01)
            snapshot = service.snapshot()
    finally:
        service.close()

    assert snapshot["connected"] is True
    assert snapshot["channels"][0]["state"] == "ready"
    assert "backend" not in snapshot["channels"][0]
    assert snapshot["channels"][0]["cdc_open"] is True
    assert snapshot["channel_diagnostics"][0]["baud_rate"] == 115200
    assert snapshot["channel_diagnostics"][5]["tx_pin"] == 20
    assert snapshot["diagnostics_error"] is None
    assert snapshot["channels"][0]["totals"]["uart_tx"] == 10
    assert snapshot["board"]["firmware_version"] == "1.2.3"
    assert snapshot["overflow_counts"] == [0, 1, 2, 3, 4, 5]
    assert snapshot["hardware"] == {"mcu": "RP2040", "mcu_id": 1, "system_clock_hz": 125_000_000}
    assert snapshot["hardware_error"] is None
    assert snapshot["configuration"] == fake.read_configuration()
    assert snapshot["configuration_error"] is None
    assert snapshot["identity"] == fake.read_identity()
    assert snapshot["runtime"] == fake.read_runtime()
    assert fake.closed


def test_dashboard_metadata_refreshes_every_second(monkeypatch):
    service = object.__new__(DashboardService)
    service._lock = threading.RLock()
    service._stop = threading.Event()
    service._client = None
    samples = iter([0.0, 0.99, 1.0, 1.99, 2.0])
    now = 0.0
    reads = []

    class FakeClient:
        def read_status(self, timeout_ms):
            nonlocal now
            now = next(samples)
            if now == 2.0:
                service._stop.set()
            return None

        def close(self):
            pass

    service._client_factory = FakeClient
    monkeypatch.setattr(time, "monotonic", lambda: now)
    monkeypatch.setattr(service, "_read_metadata", lambda _client: reads.append(now))

    service._poll()

    assert reads == [0.0, 1.0, 2.0]


def test_metadata_errors_survive_telemetry_and_clear_stale_values():
    service = object.__new__(DashboardService)
    service._lock = threading.RLock()
    service._snapshot = _empty_snapshot()
    service._last_sequence = None
    service._snapshot["board"] = {"firmware_version": "stale"}
    service._snapshot["overflow_counts"] = [99] * 6

    class BrokenMetadata(BoardTelemetry):
        def read_board_status(self):
            raise OSError("feature read failed")

        def read_overflow_counts(self):
            return [0] * 6

    service._read_metadata(BrokenMetadata())
    service._apply_status(parse_status(status_report_bytes(sequence=4)))

    snapshot = service.snapshot()
    assert snapshot["connected"] is True
    assert snapshot["error"] is None
    assert snapshot["metadata_error"] == "feature read failed"
    assert snapshot["board"] is None
    assert snapshot["overflow_counts"] == [None] * 6
    assert snapshot["configuration"] == BoardTelemetry().read_configuration()
    assert snapshot["configuration_error"] is None

    class HealthyMetadata(BoardTelemetry):
        def read_board_status(self):
            return {"firmware_version": "1.2.3"}

        def read_overflow_counts(self):
            return [0] * 6

        def read_hardware_info(self):
            return {"mcu": "RP2350", "mcu_id": 2, "system_clock_hz": 150_000_000}

    service._read_metadata(HealthyMetadata())
    snapshot = service.snapshot()
    assert snapshot["metadata_error"] is None
    assert snapshot["board"]["firmware_version"] == "1.2.3"
    assert snapshot["overflow_counts"] == [0] * 6


@pytest.mark.parametrize("mcu,model", [(2, "RP2350"), (3, "Unknown MCU (3)")])
def test_older_firmware_hardware_query_failure_preserves_existing_metadata(mcu, model):
    service = object.__new__(DashboardService)
    service._lock = threading.RLock()
    service._snapshot = _empty_snapshot()
    service._snapshot["hardware"] = {"mcu": "RP2040", "mcu_id": 1, "system_clock_hz": 125_000_000}

    class OlderFirmware(BoardTelemetry):
        unsupported = True

        def read_board_status(self):
            return {"firmware_version": "0.5.0"}

        def read_overflow_counts(self):
            return [0] * 6

        def read_hardware_info(self):
            if self.unsupported:
                raise OSError("hardware-info report is unsupported")
            return parse_hardware_info(bytes([1, mcu]) + (200_000_000).to_bytes(4, "little"))

    client = OlderFirmware()
    service._read_metadata(client)
    snapshot = service.snapshot()
    assert snapshot["board"] == {"firmware_version": "0.5.0"}
    assert snapshot["overflow_counts"] == [0] * 6
    assert snapshot["metadata_error"] is None
    assert snapshot["hardware"] is None
    assert snapshot["hardware_error"] == "hardware-info report is unsupported"
    client.unsupported = False
    service._read_metadata(client)
    recovered = service.snapshot()
    assert recovered["hardware_error"] is None
    assert recovered["hardware"] == {"mcu": model, "mcu_id": mcu, "system_clock_hz": 200_000_000}


def test_configuration_query_errors_clear_stale_values_and_recover():
    service = object.__new__(DashboardService)
    service._lock = threading.RLock()
    service._snapshot = _empty_snapshot()
    service._snapshot["configuration"] = {"source": "stored"}

    class ConfigError(BoardTelemetry):
        def read_configuration(self):
            raise OSError("configuration report unsupported")

    service._read_metadata(ConfigError())
    assert service.snapshot()["configuration"] is None
    assert service.snapshot()["configuration_error"] == "configuration report unsupported"

    service._read_metadata(BoardTelemetry())
    assert service.snapshot()["configuration"] == {
        "source": "defaults", "reason": "erased", "stored_profile": None, "active_profile": 1,
    }
    assert service.snapshot()["configuration_error"] is None


def test_dashboard_service_disconnects_and_closes_after_read_failure():
    closed = threading.Event()

    class BrokenClient:
        def read_status(self, timeout_ms):
            raise OSError("HID stream disconnected")

        def close(self):
            closed.set()
            raise OSError("already closed")

    service = DashboardService(client_factory=BrokenClient)
    try:
        assert closed.wait(1.0)
        snapshot = service.snapshot()
    finally:
        service.close()

    assert snapshot["connected"] is False
    assert snapshot["error"] == "HID stream disconnected"


def test_dashboard_controls_fail_clearly_without_a_connected_client():
    service = object.__new__(DashboardService)
    service._lock = threading.RLock()
    service._client = None
    service._snapshot = _empty_snapshot()

    for action in (service.toggle_led, service.reset_board):
        try:
            action()
        except RuntimeError as error:
            assert str(error) == "PicoUart HID interface is not connected"
        else:
            raise AssertionError("disconnected dashboard action unexpectedly succeeded")


def test_run_server_binds_loopback_and_closes_service(monkeypatch):
    from pico_uart.web import app as web_app

    class FakeService:
        closed = False

        def close(self):
            self.closed = True

    service = FakeService()
    server_options = {}
    monkeypatch.setattr(web_app, "DashboardBoards", lambda *_args: service)
    monkeypatch.setattr(
        web_app.Flask,
        "run",
        lambda _app, **kwargs: server_options.update(kwargs),
    )

    web_app.run_server(port=5501, serial_number="board-1")

    assert server_options == {
        "host": "127.0.0.1",
        "port": 5501,
        "debug": False,
        "use_reloader": False,
    }
    assert service.closed


def test_dashboard_marks_traffic_incomplete_on_gaps_and_saturated_deltas():
    service = object.__new__(DashboardService)
    service._lock = threading.RLock()
    service._snapshot = _empty_snapshot()
    service._last_sequence = None

    first = parse_status(status_report_bytes(sequence=255))
    first["channels"][0]["controller_tx_bytes"] = 65535
    service._apply_status(first)
    assert service.snapshot()["traffic_incomplete"] is True

    service._snapshot["traffic_incomplete"] = False
    assert service.snapshot()["traffic_gaps"] == 1
    service._apply_status(parse_status(status_report_bytes(sequence=0)))
    assert service.snapshot()["traffic_incomplete"] is False
    assert service.snapshot()["traffic_gaps"] == 1
    service._apply_status(parse_status(status_report_bytes(sequence=2)))
    assert service.snapshot()["traffic_incomplete"] is True
    assert service.snapshot()["traffic_gaps"] == 2
