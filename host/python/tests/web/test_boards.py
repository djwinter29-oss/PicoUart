"""Shared readers and page-local board selection without physical USB devices."""

from concurrent.futures import ThreadPoolExecutor
import re

import pytest

from pico_uart.web.app import create_app
from pico_uart.web.boards import BoardRegistry


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
        self.closed += 1


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
    registry.close()
    registry.close()
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
