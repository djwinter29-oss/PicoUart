# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""Discover boards and share one monitor per dashboard across requests."""

import hashlib
import os
import threading
from collections.abc import Callable
from typing import Protocol

from ..hid.transport import enumerate_devices
from .monitor import BoardMonitor, empty_snapshot
from .types import BoardList, BoardSnapshot, BoardSummary

MISSED_SCANS_BEFORE_REAP = 3


class MonitorFactory(Protocol):
    """Construct a monitor for one selected HID interface."""

    def __call__(self, *, serial_number: str | None, device_path: str | None) -> BoardMonitor: ...


class BoardRegistry:
    """Resolve page-local board selection and own shared board monitors."""

    def __init__(
        self,
        serial_number: str | None = None,
        device_path: str | None = None,
        discover: Callable[[], list[dict[str, object]]] = enumerate_devices,
        monitor_factory: MonitorFactory = BoardMonitor,
    ) -> None:
        self._serial = serial_number
        self._path = device_path
        self._discover = discover
        self._monitor_factory = monitor_factory
        self._lock = threading.RLock()
        self._retired_cleanup_lock = threading.Lock()
        self._known: dict[str, BoardSummary] = {}
        self._monitors: dict[str, BoardMonitor] = {}
        self._retired_monitors: dict[str, BoardMonitor] = {}
        self._missed_scans: dict[str, int] = {}
        self._closed = False

    def boards(self) -> BoardList:
        with self._lock:
            if self._closed:
                raise RuntimeError("dashboard is shutting down")
        devices = self._discover()  # blocking hidapi enumeration stays outside the registry lock
        reaped: list[tuple[str, BoardMonitor]] = []
        with self._lock:
            if self._closed:
                raise RuntimeError("dashboard is shutting down")
            boards: list[BoardSummary] = []
            seen: set[str] = set()
            for device in devices:
                serial_value = device.get("serial_number")
                path_value = device.get("path")
                if (serial_value is not None and not isinstance(serial_value, str)
                        or not isinstance(path_value, (str, bytes))):
                    raise RuntimeError("HID enumeration returned invalid board identity")
                serial = serial_value or None
                path = os.fsdecode(path_value)
                identifier = hashlib.sha256((f"serial:{serial}" if serial else f"path:{path}").encode()).hexdigest()
                if identifier in seen:
                    raise RuntimeError("duplicate PicoUart board identity; board serials must be unique")
                seen.add(identifier)
                product_name = device.get("product_string")
                if product_name is not None and not isinstance(product_name, str):
                    raise RuntimeError("HID enumeration returned an invalid product name")
                board: BoardSummary = {
                    "id": identifier,
                    "product_name": product_name or "PicoUart",
                    "serial_number": serial,
                    "device_path": path,
                }
                boards.append(board)
            self._known.update((board["id"], board) for board in boards)
            reaped = self._reap_missing(seen)
            result: BoardList = {"boards": boards, "default_board_id": self._default_board_id(boards)}
        for board_id, monitor in reaped:
            if monitor.close():
                self._forget_retired(board_id, monitor)
        self._cleanup_retired()
        return result

    def _reap_missing(self, present: set[str]) -> list[tuple[str, BoardMonitor]]:
        """Forget boards absent for several scans so unplugged boards stop retrying; caller holds the lock."""
        reaped: list[tuple[str, BoardMonitor]] = []
        for board_id in list(self._known):
            if board_id in present:
                self._missed_scans.pop(board_id, None)
                continue
            missed = self._missed_scans.get(board_id, 0) + 1
            if missed < MISSED_SCANS_BEFORE_REAP:
                self._missed_scans[board_id] = missed
                continue
            self._missed_scans.pop(board_id, None)
            del self._known[board_id]
            monitor = self._monitors.pop(board_id, None)
            if monitor is not None:
                self._retired_monitors[board_id] = monitor
                reaped.append((board_id, monitor))
        return reaped

    def _forget_retired(self, board_id: str, monitor: BoardMonitor) -> None:
        with self._lock:
            if self._retired_monitors.get(board_id) is monitor:
                del self._retired_monitors[board_id]

    def _cleanup_retired(self) -> None:
        """Release ownership of retired monitors only after their polling threads finish."""
        with self._retired_cleanup_lock:
            with self._lock:
                pending = list(self._retired_monitors.items())
            for board_id, monitor in pending:
                if monitor.is_stopped:
                    self._forget_retired(board_id, monitor)

    def _default_board_id(self, boards: list[BoardSummary]) -> str | None:
        """Prefer the board named on the command line; with a selector but no match, choose none."""
        for board in boards:
            if (self._serial and board["serial_number"] == self._serial) or (
                self._path and board["device_path"] == self._path
            ):
                return board["id"]
        if self._serial or self._path:
            return None
        return boards[0]["id"] if boards else None

    def _monitor(self, board_id: str | None) -> BoardMonitor:
        with self._lock:
            if self._closed:
                raise RuntimeError("dashboard is shutting down")
            if not board_id or board_id not in self._known:
                raise RuntimeError("unknown board; rescan and select a listed board")
            retired = self._retired_monitors.get(board_id)
            if retired is not None:
                if retired.is_stopped:
                    del self._retired_monitors[board_id]
                else:
                    raise RuntimeError("previous board monitor cleanup is still pending")
            if board_id not in self._monitors:
                board = self._known[board_id]
                self._monitors[board_id] = self._monitor_factory(
                    serial_number=board["serial_number"],
                    device_path=None if board["serial_number"] else board["device_path"])
            return self._monitors[board_id]

    def snapshot(self, board_id: str | None = None) -> BoardSnapshot:
        if board_id is None:
            return empty_snapshot()
        return self._monitor(board_id).snapshot()

    def toggle_led(self, board_id: str | None) -> None:
        self._monitor(board_id).toggle_led()

    def reset_board(self, board_id: str | None) -> None:
        self._monitor(board_id).reset_board()

    def save_configuration(self, board_id: str | None, profile: int, unsafe_overclock: bool) -> None:
        self._monitor(board_id).save_configuration(profile, unsafe_overclock)

    def read_diagnostics(self, board_id: str | None) -> BoardSnapshot:
        return self._monitor(board_id).read_diagnostics()

    def __enter__(self) -> "BoardRegistry":
        return self

    def __exit__(self, *_exc: object) -> None:
        self.close()

    def close(self) -> bool:
        """Stop all board readers and report whether every monitor finished cleanup."""
        with self._lock:
            self._closed = True
            monitors = list(self._monitors.values()) + list(self._retired_monitors.values())
        complete = True
        for monitor in monitors:
            if not monitor.close():
                complete = False
        return complete