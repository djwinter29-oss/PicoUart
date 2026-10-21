"""Own one board's HID connection and publish a thread-safe telemetry snapshot."""

from __future__ import annotations

from copy import deepcopy
from datetime import datetime, timezone
import threading
import time
from typing import Any, Callable

from ..client import PicoUartHid
from ..protocol import UART_CHANNEL_COUNT, decode_health


TRAFFIC_FIELDS = {
    "uart_tx": "controller_tx_bytes",
    "uart_rx": "controller_rx_bytes",
    "usb_tx": "cdc_tx_bytes",
    "usb_rx": "cdc_rx_bytes",
}
FAULT_FLAGS = {"init_failed", "control_error", "rx_overrun", "rx_error"}


def empty_snapshot() -> dict[str, Any]:
    return {
        "connected": False,
        "error": None,
        "metadata_error": None,
        "hardware_error": None,
        "hardware": None,
        "configuration": None,
        "configuration_error": None,
        "configuration_save": None,
        "configuration_save_error": None,
        "identity": None,
        "identity_error": None,
        "runtime": None,
        "runtime_error": None,
        "led": None,
        "led_error": None,
        "updated_at": None,
        "sequence": None,
        "traffic_incomplete": False,
        "traffic_gaps": 0,
        "channel_diagnostics": None,
        "diagnostics_error": None,
        "board": None,
        "overflow_counts": [None] * UART_CHANNEL_COUNT,
        "channels": [
            {
                "id": channel_id,
                "health": None,
                "health_labels": [],
                "state": "unknown",
                "cdc_open": False,
                "ring_high_watermark": 0,
                "traffic": {field: 0 for field in TRAFFIC_FIELDS},
                "totals": {field: 0 for field in TRAFFIC_FIELDS},
            }
            for channel_id in range(UART_CHANNEL_COUNT)
        ],
    }


class BoardMonitor:
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
        self._snapshot = empty_snapshot()
        self._thread = threading.Thread(target=self._poll, name="pico-uart-monitor", daemon=True)
        self._thread.start()

    def snapshot(self) -> dict[str, Any]:
        with self._lock:
            return deepcopy(self._snapshot)

    def toggle_led(self) -> None:
        with self._lock:
            client = self._require_client()
            client.toggle_led()
            self._read_led(client)

    def reset_board(self) -> None:
        with self._lock:
            self._require_client().reset_board()

    def save_configuration(self, profile: int, unsafe_overclock: bool) -> None:
        with self._lock:
            result = self._require_client().save_configuration(profile, unsafe_overclock)
            self._snapshot["configuration_save"] = result
            self._snapshot["configuration_save_error"] = None

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
                    self._snapshot = empty_snapshot()
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
                        next_metadata_read = now + 1.0
            except (OSError, RuntimeError) as error:
                with self._lock:
                    self._snapshot["connected"] = False
                    self._snapshot["error"] = str(error)
                    self._snapshot["identity"] = None
                    self._snapshot["runtime"] = None
                    self._snapshot["led"] = None
                    self._snapshot["board"] = None
                    self._snapshot["hardware"] = None
                    self._snapshot["configuration"] = None
                    self._snapshot["configuration_error"] = None
                    self._snapshot["configuration_save"] = None
                    self._snapshot["configuration_save_error"] = None
                    self._snapshot["channel_diagnostics"] = None
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
            incomplete = previous_sequence is not None and status["sequence"] != (previous_sequence + 1) % 256
            if incomplete:
                self._snapshot["traffic_incomplete"] = True
            self._last_sequence = status["sequence"]
            self._snapshot["updated_at"] = datetime.now(timezone.utc).isoformat(timespec="milliseconds")
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
                channel["cdc_open"] = "cdc_open" in labels
                channel["ring_high_watermark"] = source["ring_high_watermark"]
                for field, source_field in TRAFFIC_FIELDS.items():
                    delta = source[source_field]
                    if delta == 65535:
                        self._snapshot["traffic_incomplete"] = True
                        incomplete = True
                    channel["traffic"][field] = delta
                    channel["totals"][field] += delta
            if incomplete:
                self._snapshot["traffic_gaps"] += 1

    def _read_led(self, client: PicoUartHid) -> None:
        try:
            led = client.read_led()
        except (OSError, RuntimeError) as error:
            with self._lock:
                self._snapshot["led"] = None
                self._snapshot["led_error"] = str(error)
        else:
            with self._lock:
                self._snapshot["led"] = led
                self._snapshot["led_error"] = None

    def _read_metadata(self, client: PicoUartHid) -> None:
        self._read_led(client)
        with self._lock:
            try:
                configuration_save = client.read_configuration_save()
            except (OSError, RuntimeError) as error:
                self._snapshot["configuration_save"] = None
                self._snapshot["configuration_save_error"] = str(error)
            else:
                self._snapshot["configuration_save"] = configuration_save
                self._snapshot["configuration_save_error"] = None
        try:
            diagnostics = client.read_channel_diagnostics()
        except (OSError, RuntimeError) as error:
            with self._lock:
                self._snapshot["channel_diagnostics"] = None
                self._snapshot["diagnostics_error"] = str(error)
        else:
            with self._lock:
                self._snapshot["channel_diagnostics"] = diagnostics
                self._snapshot["diagnostics_error"] = None
        for field, reader in (("identity", client.read_identity), ("runtime", client.read_runtime)):
            try:
                value = reader()
            except (OSError, RuntimeError) as error:
                with self._lock:
                    self._snapshot[field] = None
                    self._snapshot[f"{field}_error"] = str(error)
            else:
                with self._lock:
                    self._snapshot[field] = value
                    self._snapshot[f"{field}_error"] = None
        try:
            configuration = client.read_configuration()
        except (OSError, RuntimeError) as error:
            with self._lock:
                self._snapshot["configuration"] = None
                self._snapshot["configuration_error"] = str(error)
        else:
            with self._lock:
                self._snapshot["configuration"] = configuration
                self._snapshot["configuration_error"] = None
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