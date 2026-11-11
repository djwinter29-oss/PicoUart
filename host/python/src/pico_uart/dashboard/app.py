# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""Local Flask dashboard backed by the shared PicoUart HID client."""

from __future__ import annotations

import logging
import os
import secrets
from pathlib import Path

from flask import Flask, Response, jsonify, render_template, request, send_from_directory, session
from flask.typing import ResponseReturnValue
from werkzeug.exceptions import BadRequest
from werkzeug.serving import WSGIRequestHandler

from .. import __version__
from .boards import BoardRegistry
from .types import DashboardService

_LOGGER = logging.getLogger(__name__)


class _QuietRequestHandler(WSGIRequestHandler):
    def log_request(self, code: int | str = "-", size: int | str = "-") -> None:
        pass


def _parse_configuration_body(body: object) -> tuple[int, bool] | None:
    if not isinstance(body, dict) or set(body) != {"profile", "unsafe_overclock"}:
        return None
    profile = body.get("profile")
    unsafe_overclock = body.get("unsafe_overclock")
    if (type(profile) is not int or profile not in (0, 1, 2, 3, 4)
            or type(unsafe_overclock) is not bool):
        return None
    return profile, unsafe_overclock


def _control_failure(error: RuntimeError) -> tuple[dict[str, str], int]:
    if "outcome is unconfirmed" in str(error):
        _LOGGER.warning("PicoUart board-control outcome is unconfirmed: %s", error)
        return {
            "error": "board action outcome is unconfirmed; reconnect and inspect state before retrying"
        }, 409
    return {"error": "request could not be completed"}, 409


def create_app(service: DashboardService | None = None, port: int = 5000) -> Flask:
    """Create the web app; an injected service keeps route tests hardware-free."""
    web_root = Path(__file__).parent / "assets"
    if not web_root.is_dir():
        web_root = Path(__file__).resolve().parents[4] / "web" / "src"
    app = Flask(__name__, template_folder=str(web_root), static_folder=None)
    app.config.update(
        SECRET_KEY=secrets.token_bytes(32),
        TRUSTED_HOSTS=["localhost", "127.0.0.1"],
        SESSION_COOKIE_NAME=f"PicoUart.Python.Session.{port}",
        SESSION_COOKIE_HTTPONLY=True,
        SESSION_COOKIE_SAMESITE="Strict",
    )
    dashboard = service if service is not None else BoardRegistry()

    @app.after_request
    def set_security_headers(response: Response) -> Response:
        response.headers["X-Content-Type-Options"] = "nosniff"
        response.headers["X-Frame-Options"] = "DENY"
        response.headers["Referrer-Policy"] = "no-referrer"
        response.headers["Cache-Control"] = "no-store"
        response.headers["Content-Security-Policy"] = (
            "default-src 'self'; connect-src 'self'; img-src 'self'; "
            "style-src 'self'; script-src 'self'; frame-ancestors 'none'"
        )
        return response

    @app.before_request
    def require_csrf_token() -> ResponseReturnValue | None:
        if request.method != "POST":
            return None
        request.host  # noqa: B018 - reject untrusted hosts (400) before reporting a token failure
        token = session.get("csrf_token", "")
        supplied_token = request.headers.get("X-CSRF-Token", "")
        if not token or not supplied_token.isascii() or not secrets.compare_digest(token, supplied_token):
            return jsonify(error="request token missing or invalid"), 403
        return None

    @app.get("/")
    def index() -> str:
        token = session.setdefault("csrf_token", secrets.token_urlsafe(32))
        host_version = os.environ.get("PICO_UART_HOST_VERSION") or __version__
        if host_version == "0.0.0.dev0":
            host_version = "0.0.0-debug"
        return render_template("index.html", csrf_token=token, dashboard_type="Python",
                               host_version=host_version)

    @app.get("/favicon.svg")
    def favicon() -> Response:
        return send_from_directory(web_root, "favicon.svg", mimetype="image/svg+xml")

    @app.get("/<any(css,js):asset_type>/<path:filename>")
    def asset(asset_type: str, filename: str) -> Response:
        return send_from_directory(web_root / asset_type, filename)

    @app.get("/api/status")
    def status() -> ResponseReturnValue:
        try:
            return jsonify(dashboard.snapshot(request.args.get("board")))
        except RuntimeError:
            return jsonify(error="request could not be completed"), 409

    @app.get("/api/boards")
    def boards() -> ResponseReturnValue:
        try:
            return jsonify(dashboard.boards())
        except RuntimeError:
            return jsonify(error="request could not be completed"), 409
        except OSError:
            return jsonify(error="service temporarily unavailable"), 503

    @app.get("/api/diagnostics")
    def diagnostics() -> ResponseReturnValue:
        try:
            return jsonify(dashboard.read_diagnostics(request.args.get("board")))
        except RuntimeError:
            return jsonify(error="diagnostic read could not be completed"), 409
        except OSError:
            return jsonify(error="service temporarily unavailable"), 503

    @app.post("/api/configuration")
    def save_configuration() -> ResponseReturnValue:
        if not request.is_json:
            return jsonify(error="configuration request must be JSON"), 415
        try:
            body: object = request.get_json()
        except BadRequest:
            return jsonify(error="malformed JSON body"), 400
        parsed_body = _parse_configuration_body(body)
        if parsed_body is None:
            return jsonify(error="expected a clock profile and boolean unsafe_overclock acknowledgement"), 400
        profile, unsafe_overclock = parsed_body
        try:
            dashboard.save_configuration(request.args.get("board"), profile, unsafe_overclock)
        except ValueError:
            return jsonify(error="invalid request"), 400
        except RuntimeError as error:
            response, status_code = _control_failure(error)
            return jsonify(response), status_code
        except OSError:
            return jsonify(error="service temporarily unavailable"), 503
        return jsonify(ok=True)

    @app.post("/api/actions/<action>")
    def action(action: str) -> ResponseReturnValue:
        try:
            if action == "toggle-led":
                dashboard.toggle_led(request.args.get("board"))
            elif action == "reset":
                dashboard.reset_board(request.args.get("board"))
            else:
                return jsonify(error="unknown action"), 404
        except RuntimeError as error:
            response, status_code = _control_failure(error)
            return jsonify(response), status_code
        except OSError:
            return jsonify(error="service temporarily unavailable"), 503
        return jsonify(ok=True)

    return app


def run_server(
    port: int = 5000,
    serial_number: str | None = None,
    device_path: str | None = None,
) -> None:
    """Run the dashboard on loopback only; never expose board controls to a LAN."""
    dashboard = BoardRegistry(serial_number, device_path)
    try:
        create_app(dashboard, port=port).run(
            host="127.0.0.1",
            port=port,
            debug=False,
            use_reloader=False,
            request_handler=_QuietRequestHandler,
        )
    finally:
        if not dashboard.close():
            _LOGGER.warning("dashboard monitor shutdown is still pending; cleanup will continue in the background")
