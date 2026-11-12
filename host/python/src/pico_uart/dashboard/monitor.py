# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""Own one board's HID connection and publish a thread-safe telemetry snapshot."""

from __future__ import annotations

import logging
import threading
import time
from collections.abc import Callable
from copy import deepcopy

from ..hid.client import ControlOutcomeUnconfirmed, PicoUartHid
from ..hid.inputs import empty_channel
from ..hid.protocol import (
    CAPABILITY_CONFIGURATION_SAVE,
    COMMAND_SAVE_CONFIGURATION,
    UART_CHANNEL_COUNT,
    UNAVAILABLE_U16,
)
from ..hid.types import ControlResult, StatusSnapshot
from .types import BoardIdentity, BoardSnapshot, HardwareSnapshot, LedSnapshot

_LOGGER = logging.getLogger(__name__)
MONITOR_CLOSE_TIMEOUT_S = 1.0
REFRESH_INTERVAL_MIN_MS = 500
REFRESH_INTERVAL_MAX_MS = 1000
RECONNECT_DELAY_S = 1.0
RECONNECT_DELAY_MAX_S = 8.0
CONFIGURATION_OUTCOME_UNCONFIRMED = (
    "board action outcome is unconfirmed; reconnect and inspect state before retrying"
)


def public_error(error: Exception) -> str:
    """Return dashboard-safe error text; raw OS details (device paths, errno text) go to the log only."""
    message = str(error)
    if isinstance(error, OSError) or message.startswith("failed to open PicoUart HID interface at "):
        _LOGGER.error("PicoUart HID I/O error: %s", error)
        return "PicoUart HID I/O error"
    return message


def empty_snapshot() -> BoardSnapshot:
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
        "configuration_save_unconfirmed": False,
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
        "channels": [empty_channel(index) for index in range(UART_CHANNEL_COUNT)],
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
        self._snapshot: BoardSnapshot = empty_snapshot()
        self._configuration_outcome_unconfirmed = False
        self._control_generation = 0
        self._thread = threading.Thread(target=self._poll, name="pico-uart-monitor", daemon=True)
        self._thread.start()

    def __enter__(self) -> BoardMonitor:
        return self

    def __exit__(self, *_exc: object) -> None:
        self.close()

    def snapshot(self) -> BoardSnapshot:
        with self._lock:
            return deepcopy(self._snapshot)

    @property
    def is_stopped(self) -> bool:
        """Whether the polling thread has finished cleanup."""
        return not self._thread.is_alive()

    # HID transfers can take seconds; the client serializes device access, so they run outside the snapshot lock.
    def toggle_led(self) -> None:
        client = self._require_client()
        client.toggle_led()
        self._read_telemetry(client)

    def reset_board(self) -> None:
        self._require_client().reset_board()

    def save_configuration(self, profile: int, unsafe_overclock: bool) -> None:
        client = self._require_client()
        with self._lock:
            self._control_generation += 1
        outcome_unconfirmed = False
        result: ControlResult | None = None
        try:
            result = client.save_configuration(profile, unsafe_overclock)
        except ControlOutcomeUnconfirmed:
            outcome_unconfirmed = True
            raise
        finally:
            with self._lock:
                self._control_generation += 1
                if outcome_unconfirmed:
                    self._configuration_outcome_unconfirmed = True
                    self._snapshot["configuration_save"] = None
                    self._snapshot["configuration_save_error"] = CONFIGURATION_OUTCOME_UNCONFIRMED
                    self._snapshot["configuration_save_unconfirmed"] = True
                elif result is not None and self._client is client:
                    self._configuration_outcome_unconfirmed = False
                    self._snapshot["configuration_save"] = result
                    self._snapshot["configuration_save_error"] = None
                    self._snapshot["configuration_save_unconfirmed"] = False

    def read_diagnostics(self) -> BoardSnapshot:
        """Refresh details only on explicit request, independently of Input cadence."""
        client = self._require_client()
        try:
            diagnostics = client.read_channel_diagnostics()
        except (OSError, RuntimeError) as error:
            with self._lock:
                if self._client is client:
                    self._snapshot["diagnostics_error"] = public_error(error)
            raise
        with self._lock:
            if self._client is not client:
                return deepcopy(self._snapshot)
            self._snapshot["channel_diagnostics"] = diagnostics
            self._snapshot["diagnostics_error"] = None
            self._snapshot["overflow_counts"] = [item["rx_overrun_count"] for item in diagnostics]
            return deepcopy(self._snapshot)

    def close(self) -> bool:
        """Stop polling and report whether the polling thread finished cleanup."""
        self._stop.set()
        self._thread.join(timeout=MONITOR_CLOSE_TIMEOUT_S)
        return not self._thread.is_alive()

    def _require_client(self) -> PicoUartHid:
        with self._lock:
            client = self._client
            error = self._snapshot["error"]
        if client is None:
            raise RuntimeError(error or "PicoUart HID interface is not connected")
        return client

    def _poll(self) -> None:
        delay = RECONNECT_DELAY_S
        while not self._stop.is_set():
            client = None
            try:
                client = self._client_factory()
                with self._lock:
                    self._client = client
                    self._snapshot = empty_snapshot()
                    self._snapshot["connected"] = True
                    if self._configuration_outcome_unconfirmed:
                        self._snapshot["configuration_save_error"] = CONFIGURATION_OUTCOME_UNCONFIRMED
                        self._snapshot["configuration_save_unconfirmed"] = True
                next_metadata_read = 0.0
                while not self._stop.is_set():
                    status = client.read_status(timeout_ms=250)
                    if status is not None:
                        delay = RECONNECT_DELAY_S
                        self._apply_status(status)
                    if self._stop.is_set():
                        break
                    now = time.monotonic()
                    if now >= next_metadata_read:
                        if self._read_metadata(client):
                            delay = RECONNECT_DELAY_S
                        with self._lock:
                            interval = self._snapshot["refresh_interval_ms"]
                        next_metadata_read = time.monotonic() + interval / 1000
            except (OSError, RuntimeError) as error:
                self._mark_disconnected(error)
            finally:
                with self._lock:
                    if self._client is client:
                        self._client = None
                if client is not None:
                    try:
                        if client.close() is False:
                            client.wait_closed()
                    except OSError as error:
                        _LOGGER.error("failed to close PicoUart HID client: %s", error)
                        with self._lock:
                            self._snapshot["error"] = "failed to close PicoUart HID client"
            self._stop.wait(delay)
            delay = min(delay * 2, RECONNECT_DELAY_MAX_S)

    def _mark_disconnected(self, error: Exception) -> None:
        """Keep live traffic but drop every value that came from the lost device session."""
        message = public_error(error)
        with self._lock:
            snapshot = self._snapshot
            snapshot["connected"] = False
            snapshot["error"] = message
            snapshot["identity"] = None
            snapshot["runtime"] = None
            snapshot["led"] = None
            snapshot["board"] = None
            snapshot["hardware"] = None
            snapshot["configuration"] = None
            snapshot["configuration_error"] = None
            snapshot["configuration_save"] = None
            snapshot["configuration_save_error"] = (
                CONFIGURATION_OUTCOME_UNCONFIRMED
                if self._configuration_outcome_unconfirmed else None
            )
            snapshot["configuration_save_unconfirmed"] = self._configuration_outcome_unconfirmed
            snapshot["channel_diagnostics"] = None

    def _apply_status(self, status: StatusSnapshot) -> None:
        with self._lock:
            self._snapshot["connected"] = True
            self._snapshot["error"] = None
            for source in status["channels"]:
                if source["updated_at"] is None:
                    continue
                channel_id = source["id"]
                current = self._snapshot["channels"][channel_id]
                sample_updated = source["updated_at"] != current["updated_at"] or any(
                    source[field] != current[field]
                    for field in ("sequence", "totals")
                )
                accounting_changed = (
                    source["traffic_gaps"] != current["traffic_gaps"]
                    or source["traffic_incomplete"] != current["traffic_incomplete"]
                )
                if not sample_updated and not accounting_changed:
                    continue
                interval = source.get("interval_ms")
                if sample_updated and interval is not None and 0 < interval < UNAVAILABLE_U16:
                    self._snapshot["refresh_interval_ms"] = max(
                        REFRESH_INTERVAL_MIN_MS,
                        min(REFRESH_INTERVAL_MAX_MS, interval),
                    )
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

    def _read_metadata(self, client: PicoUartHid) -> bool:
        identity_read = self._read_static(client)
        self._read_telemetry(client)
        self._read_control(client)
        return identity_read

    def _read_telemetry(self, client: PicoUartHid) -> None:
        try:
            telemetry = client.read_telemetry()
        except (OSError, RuntimeError) as error:
            message = public_error(error)
            with self._lock:
                if self._client is not client:
                    return
                self._snapshot["led"] = None
                self._snapshot["led_error"] = message
                self._snapshot["runtime"] = None
                self._snapshot["runtime_error"] = message
                if self._snapshot["identity"] is not None:
                    self._snapshot["hardware"] = HardwareSnapshot(
                        mcu=self._snapshot["identity"]["mcu"],
                        mcu_id=self._snapshot["identity"]["mcu_id"],
                        system_clock_hz=None,
                    )
                    self._snapshot["board"] = BoardIdentity(
                        **self._snapshot["identity"],
                        temperature_celsius=None,
                    )
                self._snapshot["metadata_error"] = message
            return
        with self._lock:
            if self._client is not client:
                return
            self._snapshot["led"] = (
                LedSnapshot(manual_on=telemetry["led"], output_on=telemetry["led"])
                if self._snapshot["identity"] and self._snapshot["identity"]["hid_led_available"]
                else None
            )
            self._snapshot["led_error"] = self._snapshot["identity_error"]
            self._snapshot["runtime"] = telemetry["runtime"]
            self._snapshot["runtime_error"] = None
            self._snapshot["metadata_error"] = self._snapshot["identity_error"]
            if self._snapshot["identity"] is not None:
                self._snapshot["board"] = BoardIdentity(
                    **self._snapshot["identity"],
                    temperature_celsius=telemetry["temperature_celsius"],
                )
                self._snapshot["hardware"] = HardwareSnapshot(
                    mcu=self._snapshot["identity"]["mcu"],
                    mcu_id=self._snapshot["identity"]["mcu_id"],
                    system_clock_hz=telemetry["system_clock_hz"],
                )

    def _read_control(self, client: PicoUartHid) -> None:
        with self._lock:
            if self._client is not client:
                return
            generation = self._control_generation
        try:
            configuration = client.read_configuration()
        except (OSError, RuntimeError) as error:
            with self._lock:
                if self._client is not client:
                    return
                self._snapshot["configuration"] = None
                self._snapshot["configuration_error"] = public_error(error)
        else:
            with self._lock:
                if self._client is not client:
                    return
                self._snapshot["configuration"] = configuration
                self._snapshot["configuration_error"] = None
        try:
            result = client.read_control()
        except (OSError, RuntimeError) as error:
            with self._lock:
                if (self._client is client and generation == self._control_generation
                        and not self._configuration_outcome_unconfirmed):
                    self._snapshot["configuration_save"] = None
                    self._snapshot["configuration_save_error"] = public_error(error)
                    self._snapshot["configuration_save_unconfirmed"] = False
            return
        save: ControlResult | None = result
        with self._lock:
            if self._client is not client or generation != self._control_generation:
                return
            identity = self._snapshot["identity"]
            if self._configuration_outcome_unconfirmed:
                self._snapshot["configuration_save"] = None
                self._snapshot["configuration_save_error"] = CONFIGURATION_OUTCOME_UNCONFIRMED
                self._snapshot["configuration_save_unconfirmed"] = True
                return
            self._snapshot["configuration_save_unconfirmed"] = False
            save_error = None
            if identity is None or not identity["capabilities"] & CAPABILITY_CONFIGURATION_SAVE:
                save = None
                save_error = self._snapshot["identity_error"] or "configuration save capability is unavailable"
            elif result["action"] != COMMAND_SAVE_CONFIGURATION:
                save = {"status": 0, "action": 0, "error_reason": 0, "error": "none"}
            self._snapshot["configuration_save"] = save
            self._snapshot["configuration_save_error"] = save_error

    def _read_static(self, client: PicoUartHid) -> bool:
        try:
            identity = client.read_identity()
        except (OSError, RuntimeError) as error:
            message = public_error(error)
            with self._lock:
                if self._client is not client:
                    return False
                self._snapshot["identity"] = None
                self._snapshot["identity_error"] = message
                self._snapshot["hardware"] = None
                self._snapshot["hardware_error"] = message
                self._snapshot["board"] = None
                self._snapshot["metadata_error"] = message
            return False
        with self._lock:
            if self._client is not client:
                return False
            self._snapshot["identity"] = identity
            self._snapshot["identity_error"] = None
            self._snapshot["hardware"] = HardwareSnapshot(
                mcu=identity["mcu"],
                mcu_id=identity["mcu_id"],
                system_clock_hz=(
                    self._snapshot["runtime"]["system_clock_hz"]
                    if self._snapshot["runtime"] is not None else None
                ),
            )
            self._snapshot["hardware_error"] = None
            if self._snapshot["runtime_error"] is None:
                self._snapshot["metadata_error"] = None
            if self._snapshot["runtime"] is not None:
                self._snapshot["board"] = BoardIdentity(
                    **identity,
                    temperature_celsius=self._snapshot["board"]["temperature_celsius"]
                    if self._snapshot["board"] else None,
                )
        return True
