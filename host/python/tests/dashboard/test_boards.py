# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""Shared readers and page-local board selection without physical USB devices."""

import re
from concurrent.futures import ThreadPoolExecutor

import pytest

from pico_uart.dashboard.app import create_app
from pico_uart.dashboard.boards import MISSED_SCANS_BEFORE_REAP, BoardRegistry


def device(serial, path):
    return {"serial_number": serial, "path": path.encode(), "product_string": "PicoUart"}


class Monitor:
    def __init__(self, serial_number, device_path):
        self.serial = serial_number
        self.path = device_path
        self.actions = []
        self.closed = 0

    def snapshot(self):
        return {"connected": True, "identity": {"serial_number": self.serial}}

    def toggle_led(self):
        self.actions.append("toggle-led")

    def reset_board(self):
        self.actions.append("reset")

    def close(self):
        if not self.closed:
            self.closed = 1
        return True


def test_multiple_viewers_share_one_reader_per_board_and_actions_target_only_that_board():
    created = []

    def factory(**kwargs):
        monitor = Monitor(**kwargs)
        created.append(monitor)
        return monitor

    registry = BoardRegistry(discover=lambda: [device("A", "/a"), device("B", "/b")],
                               monitor_factory=factory)
    boards = registry.boards()["boards"]
    first, second = (board["id"] for board in boards)
    with ThreadPoolExecutor(max_workers=12) as pool:
        snapshots = list(pool.map(registry.snapshot, [first, second] * 30))
    assert len(created) == 2
    assert [value["identity"]["serial_number"] for value in snapshots] == ["A", "B"] * 30
    app = create_app(registry)
    alice, bob = app.test_client(), app.test_client()
    for client, identifier, expected in [(alice, first, "A"), (bob, second, "B"), (bob, first, "A")]:
        assert client.get(f"/api/status?board={identifier}").json["identity"]["serial_number"] == expected
        token = re.search(rb'name="csrf-token" content="([^"]+)"', client.get("/").data).group(1).decode()
        assert client.post(f"/api/actions/toggle-led?board={identifier}",
                           headers={"X-CSRF-Token": token}).status_code == 200
    assert len(created) == 2
    by_serial = {monitor.serial: monitor for monitor in created}
    assert by_serial["A"].actions == ["toggle-led", "toggle-led"]
    assert by_serial["B"].actions == ["toggle-led"]
    assert alice.get(f"/api/status?board={first}").json["identity"]["serial_number"] == "A"
    assert not alice.get("/api/status").json["connected"]
    token = re.search(rb'name="csrf-token" content="([^"]+)"', alice.get("/").data).group(1).decode()
    headers = {"X-CSRF-Token": token}
    assert alice.post("/api/actions/reset", headers=headers).status_code == 409
    assert alice.get("/api/status?board=unknown").status_code == 409
    assert alice.post("/api/actions/reset?board=unknown", headers=headers).status_code == 409
    assert alice.post(f"/api/actions/reset?board={second}").status_code == 403
    assert alice.post(f"/api/actions/reset?board={second}", headers=headers).status_code == 200
    assert by_serial["B"].actions == ["toggle-led", "reset"]
    assert registry.close() is True
    assert registry.close() is True
    assert all(monitor.closed == 1 for monitor in created)
    with pytest.raises(RuntimeError, match="shutting down"):
        registry.snapshot(first)


def test_rescan_preserves_reader_identity_and_never_retargets_a_missing_board():
    attached = [device("A", "/a"), device("B", "/b")]
    registry = BoardRegistry(discover=lambda: attached, monitor_factory=Monitor)
    initial = registry.boards()
    first, second = (board["id"] for board in initial["boards"])
    assert initial["default_board_id"] == first
    reader = registry._monitor(first)
    attached[:] = [device("B", "/b")]
    assert registry.boards()["default_board_id"] == second
    assert registry._monitor(first) is reader
    assert registry.snapshot(first)["identity"]["serial_number"] == "A"
    attached[:] = [device("A", "/new-path"), device("B", "/b")]
    assert registry.boards()["boards"][0]["id"] == first
    assert registry._monitor(first) is reader
    registry.close()


def test_discovery_defaults_cli_preference_errors_and_missing_serials():
    attached = [device("A", "/a"), device("B", "/b")]
    registry = BoardRegistry(serial_number="B", discover=lambda: attached, monitor_factory=Monitor)
    listing = registry.boards()
    assert listing["default_board_id"] == listing["boards"][1]["id"]
    attached[:] = [device("A", "/a")]
    assert registry.boards()["default_board_id"] is None
    attached[:] = [device("A", "/a"), device("A", "/duplicate")]
    known = registry._known.copy()
    with pytest.raises(RuntimeError, match="duplicate"):
        registry.boards()
    assert registry._known == known
    attached[:] = [device(None, "/a"), device(None, "/b")]
    listing = registry.boards()
    assert len({board["id"] for board in listing["boards"]}) == 2
    monitor = registry._monitor(listing["boards"][0]["id"])
    assert monitor.serial is None and monitor.path == "/a"
    registry.close()


def test_discovery_failure_is_explicit_without_opening_a_reader():
    def fail():
        raise OSError("USB enumeration failed")

    registry = BoardRegistry(discover=fail, monitor_factory=Monitor)
    response = create_app(registry).test_client().get("/api/boards")
    assert response.status_code == 503
    assert response.json == {"error": "service temporarily unavailable"}
    assert not registry._monitors


@pytest.mark.parametrize(
    ("invalid_device", "message"),
    [
        ({"path": None}, "invalid board identity"),
        ({"path": b"/a", "serial_number": 1}, "invalid board identity"),
        ({"path": b"/a", "product_string": 1}, "invalid product name"),
    ],
)
def test_discovery_rejects_malformed_device_metadata(invalid_device, message):
    registry = BoardRegistry(discover=lambda: [invalid_device], monitor_factory=Monitor)

    with pytest.raises(RuntimeError, match=message):
        registry.boards()

    assert not registry._monitors


def test_close_reports_if_any_board_monitor_is_still_stopping():
    class PendingMonitor(Monitor):
        def __init__(self, **kwargs):
            super().__init__(**kwargs)
            self.pending = True

        def close(self):
            was_pending = self.pending
            self.pending = False
            super().close()
            return not was_pending

    registry = BoardRegistry(discover=lambda: [device("A", "/a")], monitor_factory=PendingMonitor)
    board_id = registry.boards()["boards"][0]["id"]
    registry.snapshot(board_id)

    assert registry.close() is False
    assert registry.close() is True


def test_reaped_monitor_remains_owned_until_cleanup_finishes():
    class PendingMonitor(Monitor):
        def __init__(self, serial_number, device_path):
            super().__init__(serial_number, device_path)
            self.finished = False
            self.close_calls = 0

        @property
        def is_stopped(self):
            return self.finished

        def close(self):
            self.close_calls += 1
            return self.finished

    attached = [device("A", "/a")]
    created = []

    def factory(**kwargs):
        monitor = PendingMonitor(**kwargs)
        created.append(monitor)
        return monitor

    registry = BoardRegistry(discover=lambda: attached, monitor_factory=factory)
    board_id = registry.boards()["boards"][0]["id"]
    first = registry._monitor(board_id)
    attached.clear()
    for _ in range(MISSED_SCANS_BEFORE_REAP):
        registry.boards()

    assert first.close_calls == 1
    attached.append(device("A", "/a"))
    registry.boards()
    with pytest.raises(RuntimeError, match="cleanup is still pending"):
        registry._monitor(board_id)
    assert created == [first]

    first.finished = True
    registry.boards()
    replacement = registry._monitor(board_id)
    assert replacement is not first
    assert len(created) == 2
    assert registry.close() is False


def test_registry_shutdown_reports_pending_reaped_monitor():
    class PendingMonitor(Monitor):
        def __init__(self, serial_number, device_path):
            super().__init__(serial_number, device_path)
            self.finished = False

        @property
        def is_stopped(self):
            return self.finished

        def close(self):
            return self.finished

    attached = [device("A", "/a")]
    registry = BoardRegistry(discover=lambda: attached, monitor_factory=PendingMonitor)
    board_id = registry.boards()["boards"][0]["id"]
    monitor = registry._monitor(board_id)
    attached.clear()
    for _ in range(MISSED_SCANS_BEFORE_REAP):
        registry.boards()

    assert registry.close() is False
    monitor.finished = True
    assert registry.close() is True
