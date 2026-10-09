"""Local Flask dashboard backed by the shared PicoUart HID client."""

from __future__ import annotations

from copy import deepcopy
from datetime import datetime, timezone
from pathlib import Path
import secrets
import threading
import time
from typing import Any, Callable

from flask import Flask, jsonify, render_template, request, send_from_directory, session

from ..client import PicoUartHid
from ..protocol import UART_CHANNEL_COUNT, decode_health


TRAFFIC_FIELDS = {
    "uart_tx": "controller_tx_bytes",
    "uart_rx": "controller_rx_bytes",
    "usb_tx": "cdc_tx_bytes",
    "usb_rx": "cdc_rx_bytes",
}
FAULT_FLAGS = {"init_failed", "control_error", "rx_overrun", "rx_error"}


def _empty_snapshot() -> dict[str, Any]:
    return {
        "connected": False,
        "error": None,
        "metadata_error": None,
        "hardware_error": None,
        "hardware": None,
        "updated_at": None,
        "sequence": None,
        "traffic_incomplete": False,
        "board": None,
        "overflow_counts": [None] * UART_CHANNEL_COUNT,
        "channels": [
            {
                "id": channel_id,
                "health": None,
                "health_labels": [],
                "state": "unknown",
                "backend": "unknown",
                "cdc_open": False,
                "ring_high_watermark": 0,
                "traffic": {field: 0 for field in TRAFFIC_FIELDS},
                "totals": {field: 0 for field in TRAFFIC_FIELDS},
            }
            for channel_id in range(UART_CHANNEL_COUNT)
        ],
    }


class DashboardService:
    """Maintain one HID session and a thread-safe snapshot for web requests."""

    def __init__(
        self,
        serial_number: str | None = None,
        device_path: str | None = None,
        client_factory: Callable[[], PicoUartHid] | None = None,
    ) -> None:
        self._client_factory = client_factory or (lambda: PicoUartHid(serial_number, device_path))
        self._lock = threading.RLock()
        self._stop = threading.Event()
        self._client: PicoUartHid | None = None
        self._last_sequence: int | None = None
        self._snapshot = _empty_snapshot()
        self._thread = threading.Thread(target=self._poll, name="pico-uart-monitor", daemon=True)
        self._thread.start()

    def snapshot(self) -> dict[str, Any]:
        with self._lock:
            return deepcopy(self._snapshot)

    def toggle_led(self) -> None:
        self._require_client().toggle_led()

    def reset_board(self) -> None:
        self._require_client().reset_board()

    def close(self) -> None:
        self._stop.set()
        self._thread.join(timeout=1.0)

    def _require_client(self) -> PicoUartHid:
        with self._lock:
            client = self._client
            error = self._snapshot["error"]
        if client is None:
            raise RuntimeError(error or "PicoUart HID interface is not connected")
        return client

    def _poll(self) -> None:
        while not self._stop.is_set():
            client = None
            try:
                client = self._client_factory()
                with self._lock:
                    self._client = client
                    self._snapshot = _empty_snapshot()
                    self._last_sequence = None
                    self._snapshot["connected"] = True
                next_metadata_read = 0.0
                while not self._stop.is_set():
                    status = client.read_status(timeout_ms=250)
                    if status is not None:
                        self._apply_status(status)
                    now = time.monotonic()
                    if now >= next_metadata_read:
                        self._read_metadata(client)
                        next_metadata_read = now + 2.0
            except (OSError, RuntimeError) as error:
                with self._lock:
                    self._snapshot["connected"] = False
                    self._snapshot["error"] = str(error)
            finally:
                with self._lock:
                    if self._client is client:
                        self._client = None
                if client is not None:
                    try:
                        client.close()
                    except OSError:
                        pass
            self._stop.wait(1.0)

    def _apply_status(self, status: dict[str, Any]) -> None:
        with self._lock:
            self._snapshot["connected"] = True
            self._snapshot["error"] = None
            self._snapshot["sequence"] = status["sequence"]
            previous_sequence = self._last_sequence
            if previous_sequence is not None and status["sequence"] != (previous_sequence + 1) % 256:
                self._snapshot["traffic_incomplete"] = True
            self._last_sequence = status["sequence"]
            self._snapshot["updated_at"] = datetime.now(timezone.utc).isoformat(timespec="seconds")
            for source in status["channels"]:
                channel = self._snapshot["channels"][source["id"]]
                labels = decode_health(source["health"])
                channel["health"] = source["health"]
                channel["health_labels"] = labels
                channel["state"] = (
                    "attention"
                    if FAULT_FLAGS.intersection(labels)
                    else "ready"
                    if "ready" in labels
                    else "initializing"
                )
                channel["backend"] = "PIO" if "pio" in labels else "Hardware"
                channel["cdc_open"] = "cdc_open" in labels
                channel["ring_high_watermark"] = source["ring_high_watermark"]
                for field, source_field in TRAFFIC_FIELDS.items():
                    delta = source[source_field]
                    if delta == 65535:
                        self._snapshot["traffic_incomplete"] = True
                    channel["traffic"][field] = delta
                    channel["totals"][field] += delta

    def _read_metadata(self, client: PicoUartHid) -> None:
        try:
            board = client.read_board_status()
            overflows = client.read_overflow_counts()
        except (OSError, RuntimeError) as error:
            with self._lock:
                self._snapshot["metadata_error"] = str(error)
                self._snapshot["board"] = None
                self._snapshot["overflow_counts"] = [None] * UART_CHANNEL_COUNT
                self._snapshot["hardware"] = None
                self._snapshot["hardware_error"] = None
            return
        with self._lock:
            self._snapshot["metadata_error"] = None
            self._snapshot["board"] = board
            self._snapshot["overflow_counts"] = overflows
        try:
            hardware = client.read_hardware_info()
        except (OSError, RuntimeError) as error:
            with self._lock:
                self._snapshot["hardware"] = None
                self._snapshot["hardware_error"] = str(error)
            return
        with self._lock:
            self._snapshot["hardware"] = hardware
            self._snapshot["hardware_error"] = None


def create_app(service: DashboardService | None = None) -> Flask:
    """Create the web app; an injected service keeps route tests hardware-free."""
    web_root = Path(__file__).parent / "assets"
    if not web_root.is_dir():
        web_root = Path(__file__).resolve().parents[4] / "web"
    app = Flask(__name__, template_folder=str(web_root), static_folder=None)
    app.config.update(
        SECRET_KEY=secrets.token_bytes(32),
        TRUSTED_HOSTS=["localhost", "127.0.0.1"],
        SESSION_COOKIE_HTTPONLY=True,
        SESSION_COOKIE_SAMESITE="Strict",
    )
    dashboard = service if service is not None else DashboardService()

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
        return render_template("index.html", csrf_token=token)

    @app.get("/favicon.svg")
    def favicon():
        return send_from_directory(web_root, "favicon.svg", mimetype="image/svg+xml")

    @app.get("/<any(css,js):asset_type>/<path:filename>")
    def asset(asset_type: str, filename: str):
        return send_from_directory(web_root / asset_type, filename)

    @app.get("/api/status")
    def status():
        return jsonify(dashboard.snapshot())

    @app.post("/api/actions/<action>")
    def action(action: str):
        token = session.get("csrf_token", "")
        supplied_token = request.headers.get("X-CSRF-Token", "")
        if not token or not secrets.compare_digest(token, supplied_token):
            return jsonify(error="request token missing or invalid"), 403
        try:
            if action == "toggle-led":
                dashboard.toggle_led()
            elif action == "reset":
                dashboard.reset_board()
            else:
                return jsonify(error="unknown action"), 404
        except RuntimeError as error:
            return jsonify(error=str(error)), 409
        except OSError as error:
            return jsonify(error=str(error)), 503
        return jsonify(ok=True)

    return app


def run_server(
    port: int = 5000,
    serial_number: str | None = None,
    device_path: str | None = None,
) -> None:
    """Run the dashboard on loopback only; never expose board controls to a LAN."""
    dashboard = DashboardService(serial_number, device_path)
    try:
        create_app(dashboard).run(host="127.0.0.1", port=port, debug=False, use_reloader=False)
    finally:
        dashboard.close()
