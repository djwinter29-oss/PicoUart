# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""Hardware-free tests for the Flask dashboard's control endpoints."""

import logging
import re

import pytest

from pico_uart.dashboard import app as web_app
from pico_uart.dashboard.app import create_app


class FakeDashboard:
    def __init__(self):
        self.actions = []

    def snapshot(self, board_id=None):
        return {"connected": False, "channels": []}

    def toggle_led(self, board_id=None):
        self.actions.append("toggle-led")

    def reset_board(self, board_id=None):
        self.actions.append("reset")


def test_dashboard_session_cookie_names_are_backend_and_port_specific():
    first = create_app(FakeDashboard(), port=5101)
    second = create_app(FakeDashboard(), port=5102)

    assert first.config["SESSION_COOKIE_NAME"] == "PicoUart.Python.Session.5101"
    assert second.config["SESSION_COOKIE_NAME"] == "PicoUart.Python.Session.5102"
    client = first.test_client()
    client.set_cookie("PicoUart.Session.5101", "legacy-cookie")
    client.set_cookie("PicoUart.DotNet.Session.5101", "dotnet-cookie")
    assert client.get("/").headers["Set-Cookie"].startswith("PicoUart.Python.Session.5101=")


def _csrf_token(client):
    response = client.get("/")
    match = re.search(rb'name="csrf-token" content="([^"]+)"', response.data)
    assert match is not None
    return match.group(1).decode()


def test_dashboard_action_routes_require_csrf_and_dispatch_controls():
    service = FakeDashboard()
    client = create_app(service).test_client()
    assert client.get("/api/status").json == {"connected": False, "channels": []}
    assert client.post("/api/actions/toggle-led").status_code == 403
    response = client.post(
        "/api/actions/toggle-led",
        headers={"X-CSRF-Token": _csrf_token(client)},
    )
    assert response.status_code == 200
    assert response.json == {"ok": True}
    assert service.actions == ["toggle-led"]


def test_dashboard_maps_action_errors_and_unknown_actions():
    class BrokenDashboard(FakeDashboard):
        def toggle_led(self, board_id=None):
            raise OSError("HID write failed")

    client = create_app(BrokenDashboard()).test_client()
    headers = {"X-CSRF-Token": _csrf_token(client)}
    response = client.post("/api/actions/toggle-led", headers=headers)
    assert response.status_code == 503
    assert response.json == {"error": "service temporarily unavailable"}
    assert client.post("/api/actions/unknown", headers=headers).status_code == 404


def test_dashboard_preserves_unconfirmed_action_outcome(caplog):
    class UnconfirmedDashboard(FakeDashboard):
        def toggle_led(self, board_id=None):
            raise RuntimeError("HID feature transfer timed out; outcome is unconfirmed")

    client = create_app(UnconfirmedDashboard()).test_client()
    response = client.post(
        "/api/actions/toggle-led",
        headers={"X-CSRF-Token": _csrf_token(client)},
    )

    assert response.status_code == 409
    assert response.json == {
        "error": "board action outcome is unconfirmed; reconnect and inspect state before retrying"
    }
    assert "outcome is unconfirmed" in caplog.text


def test_dashboard_rejects_untrusted_host_before_serving_controls():
    service = FakeDashboard()
    client = create_app(service).test_client()
    assert client.get("/", base_url="http://attacker.example").status_code == 400
    token = _csrf_token(client)
    response = client.post(
        "/api/actions/toggle-led",
        base_url="http://attacker.example",
        headers={"X-CSRF-Token": token},
    )
    assert response.status_code == 400
    assert service.actions == []


def test_dashboard_rejects_non_ascii_csrf_token_without_server_error():
    service = FakeDashboard()
    client = create_app(service).test_client()
    token = _csrf_token(client)

    response = client.post(
        "/api/actions/toggle-led",
        headers={"X-CSRF-Token": token + "é"},
    )

    assert response.status_code == 403
    assert response.json == {"error": "request token missing or invalid"}
    assert service.actions == []


def test_dashboard_serves_static_assets_safely():
    client = create_app(FakeDashboard()).test_client()
    page = client.get("/")
    assert page.status_code == 200
    assert page.headers["Cache-Control"] == "no-store"
    assert b"<title>PicoUart Diagnostics | Python</title>" in page.data
    assert client.get("/api/status").headers["Cache-Control"] == "no-store"
    for path in ("css/dashboard.css", "js/dashboard.js", "favicon.svg"):
        with client.get(f"/{path}") as response:
            assert response.status_code == 200
            assert response.headers["Cache-Control"] == "no-store"
            assert response.data
            assert response.headers["X-Content-Type-Options"] == "nosniff"


@pytest.mark.parametrize("version,expected", [
    ("", "0.0.0-debug"), ("0.0.0.dev0", "0.0.0-debug"), ("1.2.3", "1.2.3"),
    ("<script>", "&lt;script&gt;"),
])
def test_dashboard_shows_host_version_safely(monkeypatch, version, expected):
    monkeypatch.setattr("pico_uart.dashboard.app.__version__", "0.0.0-debug")
    monkeypatch.setenv("PICO_UART_HOST_VERSION", version)
    page = create_app(FakeDashboard()).test_client().get("/").data.decode()
    assert f'aria-label="Host application version">{expected}</span>' in page


def test_run_server_suppresses_routine_access_logs(monkeypatch, caplog):
    class FakeApp:
        def run(self, **kwargs):
            assert kwargs["request_handler"] is web_app._QuietRequestHandler
            kwargs["request_handler"].log_request(None, 200, 1)
            logger = logging.getLogger("werkzeug")
            logger.warning("request warning")
            logger.error("request error")

    class FakeRegistry:
        def __init__(self, *_args):
            pass

        def close(self):
            return True

    monkeypatch.setattr(web_app, "BoardRegistry", FakeRegistry)
    monkeypatch.setattr(web_app, "create_app", lambda *_args, **_kwargs: FakeApp())
    logger = logging.getLogger("werkzeug")
    previous_level = logger.level
    with caplog.at_level(logging.INFO, logger="werkzeug"):
        web_app.run_server()
        assert logger.level == logging.INFO
    assert logger.level == previous_level
    assert [record.getMessage() for record in caplog.records] == ["request warning", "request error"]


def test_run_server_warns_when_monitor_cleanup_is_pending(monkeypatch, caplog):
    class FakeApp:
        def run(self, **kwargs):
            pass

    class PendingRegistry:
        def __init__(self, *_args):
            pass

        def close(self):
            return False

    monkeypatch.setattr(web_app, "BoardRegistry", PendingRegistry)
    monkeypatch.setattr(web_app, "create_app", lambda *_args, **_kwargs: FakeApp())

    with caplog.at_level(logging.WARNING, logger="pico_uart.dashboard.app"):
        web_app.run_server()

    assert "dashboard monitor shutdown is still pending" in caplog.text


def test_dashboard_sanitizes_reset_capability_errors():
    class ResetDisabledDashboard(FakeDashboard):
        def reset_board(self, board_id=None):
            raise RuntimeError("firmware HID reset is disabled")

    client = create_app(ResetDisabledDashboard()).test_client()
    response = client.post(
        "/api/actions/reset",
        headers={"X-CSRF-Token": _csrf_token(client)},
    )
    assert response.status_code == 409
    assert response.json == {"error": "request could not be completed"}
