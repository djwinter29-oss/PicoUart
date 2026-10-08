"""Hardware-free tests for the Flask dashboard and its HID polling service."""

from __future__ import annotations

import re
import threading
import time

from pico_uart.protocol import parse_status
from pico_uart.web.app import DashboardService, _empty_snapshot, create_app
from helpers import board_status_bytes, overflow_counts_bytes, status_report_bytes


class FakeDashboard:
    def __init__(self):
        self.actions = []

    def snapshot(self):
        return {"connected": False, "channels": []}

    def toggle_led(self):
        self.actions.append("toggle-led")

    def reset_board(self):
        self.actions.append("reset")


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
    response = client.post(
        "/api/actions/toggle-led", headers={"X-CSRF-Token": token}
    )
    assert response.status_code == 200
    assert response.json == {"ok": True}
    assert service.actions == ["toggle-led"]


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


def test_dashboard_passes_reset_capability_errors_to_the_host():
    class ResetDisabledDashboard(FakeDashboard):
        def reset_board(self):
            raise RuntimeError("firmware HID reset is disabled")

    client = create_app(ResetDisabledDashboard()).test_client()
    token = _csrf_token(client)
    response = client.post("/api/actions/reset", headers={"X-CSRF-Token": token})

    assert response.status_code == 409
    assert response.json["error"] == "firmware HID reset is disabled"


def test_dashboard_service_polls_status_and_metadata():
    class FakeClient:
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
    assert snapshot["channels"][0]["backend"] == "PIO"
    assert snapshot["channels"][0]["cdc_open"] is True
    assert snapshot["channels"][0]["totals"]["uart_tx"] == 10
    assert snapshot["board"]["firmware_version"] == "1.2.3"
    assert snapshot["overflow_counts"] == [0, 1, 2, 3, 4, 5]
    assert fake.closed


def test_metadata_errors_survive_telemetry_and_clear_stale_values():
    service = object.__new__(DashboardService)
    service._lock = threading.RLock()
    service._snapshot = _empty_snapshot()
    service._last_sequence = None
    service._snapshot["board"] = {"firmware_version": "stale"}
    service._snapshot["overflow_counts"] = [99] * 6

    class BrokenMetadata:
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
    service._apply_status(parse_status(status_report_bytes(sequence=0)))
    assert service.snapshot()["traffic_incomplete"] is False
    service._apply_status(parse_status(status_report_bytes(sequence=2)))
    assert service.snapshot()["traffic_incomplete"] is True