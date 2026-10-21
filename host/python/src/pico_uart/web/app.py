"""Local Flask dashboard backed by the shared PicoUart HID client."""

from __future__ import annotations

from importlib.metadata import PackageNotFoundError, version as distribution_version
from pathlib import Path
import secrets
import os

from flask import Flask, jsonify, render_template, request, send_from_directory, session
from werkzeug.exceptions import BadRequest

from .boards import BoardRegistry


def create_app(service: BoardRegistry | None = None) -> Flask:
    """Create the web app; an injected service keeps route tests hardware-free."""
    web_root = Path(__file__).parent / "assets"
    if not web_root.is_dir():
        web_root = Path(__file__).resolve().parents[4] / "web"
    try:
        packaged_version = distribution_version("pico-uart")
    except PackageNotFoundError:
        packaged_version = "0.0.0-debug"
    if packaged_version == "0.0.0.dev0":
        packaged_version = "0.0.0-debug"
    app = Flask(__name__, template_folder=str(web_root), static_folder=None)
    app.config.update(
        SECRET_KEY=secrets.token_bytes(32),
        TRUSTED_HOSTS=["localhost", "127.0.0.1"],
        SESSION_COOKIE_HTTPONLY=True,
        SESSION_COOKIE_SAMESITE="Strict",
    )
    dashboard = service if service is not None else BoardRegistry()

    @app.after_request
    def set_security_headers(response):
        response.headers["X-Content-Type-Options"] = "nosniff"
        response.headers["X-Frame-Options"] = "DENY"
        response.headers["Referrer-Policy"] = "no-referrer"
        response.headers["Content-Security-Policy"] = (
            "default-src 'self'; connect-src 'self'; img-src 'self'; "
            "style-src 'self'; script-src 'self'; frame-ancestors 'none'"
        )
        return response

    @app.get("/")
    def index():
        token = session.setdefault("csrf_token", secrets.token_urlsafe(32))
        return render_template("index.html", csrf_token=token, dashboard_type="Python",
                       host_version=os.environ.get("PICO_UART_HOST_VERSION") or packaged_version)

    @app.get("/favicon.svg")
    def favicon():
        return send_from_directory(web_root, "favicon.svg", mimetype="image/svg+xml")

    @app.get("/<any(css,js):asset_type>/<path:filename>")
    def asset(asset_type: str, filename: str):
        return send_from_directory(web_root / asset_type, filename)

    @app.get("/api/status")
    def status():
        try:
            return jsonify(dashboard.snapshot(request.args.get("board")))
        except RuntimeError:
            return jsonify(error="request could not be completed"), 409

    @app.get("/api/boards")
    def boards():
        try:
            return jsonify(dashboard.boards())
        except RuntimeError:
            return jsonify(error="request could not be completed"), 409
        except OSError:
            return jsonify(error="service temporarily unavailable"), 503

    @app.post("/api/configuration")
    def save_configuration():
        token = session.get("csrf_token", "")
        supplied_token = request.headers.get("X-CSRF-Token", "")
        if not token or not secrets.compare_digest(token, supplied_token):
            return jsonify(error="request token missing or invalid"), 403
        if not request.is_json:
            return jsonify(error="configuration request must be JSON"), 415
        try:
            body = request.get_json()
        except BadRequest:
            return jsonify(error="malformed JSON body"), 400
        if (not isinstance(body, dict) or set(body) != {"profile", "unsafe_overclock"}
                or type(body["profile"]) is not int or body["profile"] not in (0, 1, 2)
                or type(body["unsafe_overclock"]) is not bool):
            return jsonify(error="expected a clock profile and boolean unsafe_overclock acknowledgement"), 400
        try:
            dashboard.save_configuration(request.args.get("board"), body["profile"], body["unsafe_overclock"])
        except ValueError:
            return jsonify(error="invalid request"), 400
        except RuntimeError:
            return jsonify(error="request could not be completed"), 409
        except OSError:
            return jsonify(error="service temporarily unavailable"), 503
        return jsonify(ok=True)

    @app.post("/api/actions/<action>")
    def action(action: str):
        token = session.get("csrf_token", "")
        supplied_token = request.headers.get("X-CSRF-Token", "")
        if not token or not secrets.compare_digest(token, supplied_token):
            return jsonify(error="request token missing or invalid"), 403
        try:
            if action == "toggle-led":
                dashboard.toggle_led(request.args.get("board"))
            elif action == "reset":
                dashboard.reset_board(request.args.get("board"))
            else:
                return jsonify(error="unknown action"), 404
        except RuntimeError:
            return jsonify(error="request could not be completed"), 409
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
        create_app(dashboard).run(host="127.0.0.1", port=port, debug=False, use_reloader=False)
    finally:
        dashboard.close()
