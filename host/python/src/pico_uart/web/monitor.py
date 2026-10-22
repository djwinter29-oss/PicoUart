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
        self._reset_connection_cache()
        self._thread = threading.Thread(target=self._poll, name="pico-uart-monitor", daemon=True)
        self._thread.start()

    def _reset_connection_cache(self) -> None:
        """Forget everything read from a previous connection; static reports are read once per connection."""
        self._static_loaded: set[str] = set()
        self._board_static: dict[str, Any] | None = None
        self._board_error: str | None = None
        self._telemetry_error: str | None = None
        self._temperature: float | None = None
        self._diagnostics: list[dict[str, Any] | None] = [None] * UART_CHANNEL_COUNT
        self._diagnostics_generation: list[int | None] = [None] * UART_CHANNEL_COUNT

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
                    self._reset_connection_cache()
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
        generations = self._read_telemetry(client)
        if generations is not None:
            self._read_diagnostics(client, generations)
        self._read_static(client)

    def _publish_board(self) -> None:
        """Combine static board info with the latest temperature; callers hold the lock."""
        telemetry_ok = self._telemetry_error is None and self._temperature is not None
        self._snapshot["metadata_error"] = self._telemetry_error or self._board_error
        self._snapshot["board"] = (
            {**self._board_static, "temperature_celsius": self._temperature}
            if telemetry_ok and self._board_static is not None
            else None
        )

    def _read_telemetry(self, client: PicoUartHid) -> list[int] | None:
        """Read every changing value in one transfer; returns diagnostics generations or None on failure."""
        try:
            telemetry = client.read_telemetry()
        except (OSError, RuntimeError) as error:
            message = str(error)
            with self._lock:
                self._telemetry_error = message
                self._snapshot.update(
                    led=None, led_error=message, runtime=None, runtime_error=message,
                    configuration_save=None, configuration_save_error=message,
                    overflow_counts=[None] * UART_CHANNEL_COUNT,
                )
                self._publish_board()
            return None
        with self._lock:
            self._telemetry_error = None
            self._temperature = telemetry["temperature_celsius"]
            self._snapshot.update(
                led=telemetry["led"], led_error=None,
                runtime=telemetry["runtime"], runtime_error=None,
                configuration_save=telemetry["configuration_save"], configuration_save_error=None,
                overflow_counts=telemetry["overflow_counts"],
            )
            self._publish_board()
        return telemetry["diagnostics_generation"]

    def _read_diagnostics(self, client: PicoUartHid, generations: list[int]) -> None:
        """Re-read only channels whose firmware-reported diagnostics generation changed."""
        try:
            for index, generation in enumerate(generations):
                if self._diagnostics[index] is None or self._diagnostics_generation[index] != generation:
                    self._diagnostics[index] = client.read_channel_diagnostic(index)
                    # The generation from before the read is kept: a newer change only costs one extra read.
                    self._diagnostics_generation[index] = generation
        except (OSError, RuntimeError) as error:
            self._diagnostics = [None] * UART_CHANNEL_COUNT
            self._diagnostics_generation = [None] * UART_CHANNEL_COUNT
            with self._lock:
                self._snapshot["channel_diagnostics"] = None
                self._snapshot["diagnostics_error"] = str(error)
            return
        with self._lock:
            self._snapshot["channel_diagnostics"] = list(self._diagnostics)
            self._snapshot["diagnostics_error"] = None

    def _read_static(self, client: PicoUartHid) -> None:
        """Read reports that cannot change while connected until each succeeds once."""
        for name, field, reader in (
            ("identity", "identity", client.read_identity),
            ("configuration", "configuration", client.read_configuration),
            ("hardware", "hardware", client.read_hardware_info),
        ):
            if name in self._static_loaded:
                continue
            try:
                value = reader()
            except (OSError, RuntimeError) as error:
                with self._lock:
                    self._snapshot[field] = None
                    self._snapshot[f"{field}_error"] = str(error)
            else:
                self._static_loaded.add(name)
                with self._lock:
                    self._snapshot[field] = value
                    self._snapshot[f"{field}_error"] = None
        if "board" not in self._static_loaded:
            try:
                board = client.read_board_status()
            except (OSError, RuntimeError) as error:
                with self._lock:
                    self._board_error = str(error)
                    self._publish_board()
            else:
                self._static_loaded.add("board")
                with self._lock:
                    self._board_static = {key: value for key, value in board.items() if key != "temperature_celsius"}
                    self._board_error = None
                    self._publish_board()