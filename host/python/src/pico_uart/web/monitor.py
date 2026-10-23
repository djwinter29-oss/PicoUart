"""Own one board's HID connection and publish a thread-safe telemetry snapshot."""

from __future__ import annotations

from copy import deepcopy
import threading
import time
from typing import Any, Callable

from ..client import PicoUartHid, _empty_channel
from ..protocol import UART_CHANNEL_COUNT


TRAFFIC_FIELDS = {
    "uart_tx": "uart_tx_bytes",
    "uart_rx": "uart_rx_bytes",
    "usb_tx": "cdc_tx_bytes",
    "usb_rx": "cdc_rx_bytes",
}


def empty_snapshot() -> dict[str, Any]:
    return {
        "connected": False,
        "refresh_interval_ms": 500,
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
        "traffic_incomplete": False,
        "traffic_gaps": 0,
        "channel_diagnostics": None,
        "diagnostics_error": None,
        "board": None,
        "overflow_counts": [None] * UART_CHANNEL_COUNT,
        "channels": [_empty_channel(index) for index in range(UART_CHANNEL_COUNT)],
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
            self._read_telemetry(client)

    def reset_board(self) -> None:
        with self._lock:
            self._require_client().reset_board()

    def save_configuration(self, profile: int, unsafe_overclock: bool) -> None:
        with self._lock:
            result = self._require_client().save_configuration(profile, unsafe_overclock)
            self._snapshot["configuration_save"] = result
            self._snapshot["configuration_save_error"] = None

    def read_diagnostics(self) -> dict[str, Any]:
        """Refresh details only on explicit request, independently of Input cadence."""
        with self._lock:
            client = self._require_client()
            try:
                diagnostics = client.read_channel_diagnostics()
            except (OSError, RuntimeError) as error:
                self._snapshot["diagnostics_error"] = str(error)
                raise
            self._snapshot["channel_diagnostics"] = diagnostics
            self._snapshot["diagnostics_error"] = None
            self._snapshot["overflow_counts"] = [item["rx_overrun_count"] for item in diagnostics]
            return deepcopy(self._snapshot)

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
                    self._snapshot["connected"] = True
                next_metadata_read = 0.0
                while not self._stop.is_set():
                    status = client.read_status(timeout_ms=250)
                    if status is not None:
                        self._apply_status(status)
                    now = time.monotonic()
                    if now >= next_metadata_read:
                        self._read_metadata(client)
                        with self._lock:
                            interval = self._snapshot["refresh_interval_ms"]
                        next_metadata_read = time.monotonic() + interval / 1000
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
            for source in status["channels"]:
                if source["updated_at"] is None:
                    continue
                channel_id = source["id"]
                current = self._snapshot["channels"][channel_id]
                sample_updated = source["updated_at"] != current["updated_at"]
                accounting_changed = (
                    source["traffic_gaps"] != current["traffic_gaps"]
                    or source["traffic_incomplete"] != current["traffic_incomplete"]
                )
                if not sample_updated and not accounting_changed:
                    continue
                interval = source.get("interval_ms")
                if sample_updated and interval is not None and 0 < interval < 65535:
                    self._snapshot["refresh_interval_ms"] = max(500, min(1000, interval))
                self._snapshot["channels"][channel_id] = deepcopy(source)
            channels = self._snapshot["channels"]
            self._snapshot["updated_at"] = max(
                (channel["updated_at"] for channel in channels if channel["updated_at"] is not None),
                default=None,
            )
            self._snapshot["traffic_incomplete"] = any(
                channel["traffic_incomplete"] for channel in channels
            )
            self._snapshot["traffic_gaps"] = sum(channel["traffic_gaps"] for channel in channels)

    def _read_metadata(self, client: PicoUartHid) -> None:
        self._read_static(client)
        self._read_telemetry(client)
        self._read_control(client)

    def _read_telemetry(self, client: PicoUartHid) -> None:
        try:
            telemetry = client.read_telemetry()
        except (OSError, RuntimeError) as error:
            message = str(error)
            with self._lock:
                self._snapshot.update(
                    led=None, led_error=message, runtime=None, runtime_error=message,
                )
                if self._snapshot["identity"] is not None:
                    self._snapshot["hardware"] = {
                        "mcu": self._snapshot["identity"]["mcu"],
                        "mcu_id": self._snapshot["identity"]["mcu_id"],
                        "system_clock_hz": None,
                    }
                    self._snapshot["board"] = {
                        **self._snapshot["identity"],
                        "temperature_celsius": None,
                    }
                self._snapshot["metadata_error"] = message
            return
        with self._lock:
            self._snapshot.update(
                led=(
                    {"manual_on": telemetry["led"], "output_on": telemetry["led"]}
                    if self._snapshot["identity"] and self._snapshot["identity"]["hid_led_available"]
                    else None
                ),
                led_error=self._snapshot["identity_error"],
                runtime=telemetry["runtime"],
                runtime_error=None,
                metadata_error=self._snapshot["identity_error"],
            )
            if self._snapshot["identity"] is not None:
                self._snapshot["board"] = {
                    **self._snapshot["identity"],
                    "temperature_celsius": telemetry["temperature_celsius"],
                }
                self._snapshot["hardware"] = {
                    "mcu": self._snapshot["identity"]["mcu"],
                    "mcu_id": self._snapshot["identity"]["mcu_id"],
                    "system_clock_hz": telemetry["system_clock_hz"],
                }

    def _read_control(self, client: PicoUartHid) -> None:
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
            result = client.read_control()
        except (OSError, RuntimeError) as error:
            with self._lock:
                self._snapshot["configuration_save_error"] = str(error)
            return
        save = result
        with self._lock:
            identity = self._snapshot["identity"]
            save_error = None
            if identity is None or not identity["capabilities"] & 4:
                save = None
                save_error = self._snapshot["identity_error"] or "configuration save capability is unavailable"
            elif result["action"] != 4:
                save = {"status": 0, "action": 0, "error_reason": 0, "error": "none"}
            self._snapshot.update(
                configuration_save=save,
                configuration_save_error=save_error,
            )

    def _read_static(self, client: PicoUartHid) -> None:
        try:
            identity = client.read_identity()
        except (OSError, RuntimeError) as error:
            with self._lock:
                self._snapshot["identity"] = None
                self._snapshot["identity_error"] = str(error)
                self._snapshot["hardware"] = None
                self._snapshot["hardware_error"] = str(error)
                self._snapshot["board"] = None
                self._snapshot["metadata_error"] = str(error)
            return
        with self._lock:
            self._snapshot.update(
                identity=identity,
                identity_error=None,
                hardware={
                    "mcu": identity["mcu"],
                    "mcu_id": identity["mcu_id"],
                    "system_clock_hz": (
                        self._snapshot["runtime"]["system_clock_hz"]
                        if self._snapshot["runtime"] is not None else None
                    ),
                },
                hardware_error=None,
            )
            if self._snapshot["runtime_error"] is None:
                self._snapshot["metadata_error"] = None
            if self._snapshot["runtime"] is not None:
                self._snapshot["board"] = {
                    **identity,
                    "temperature_celsius": self._snapshot["board"]["temperature_celsius"]
                    if self._snapshot["board"] else None,
                }
