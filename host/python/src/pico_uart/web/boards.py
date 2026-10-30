# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""Discover boards and share one monitor per board across dashboard requests."""

import hashlib
import os
import threading
from typing import Any, Callable

from ..transport import enumerate_devices
from .monitor import BoardMonitor, empty_snapshot


class BoardRegistry:
    """Resolve page-local board selection and own shared board monitors."""

    def __init__(
        self,
        serial_number: str | None = None,
        device_path: str | None = None,
        discover: Callable[[], list[dict[str, Any]]] = enumerate_devices,
        monitor_factory: Callable[..., BoardMonitor] = BoardMonitor,
    ) -> None:
        self._serial = serial_number
        self._path = device_path
        self._discover = discover
        self._monitor_factory = monitor_factory
        self._lock = threading.RLock()
        self._known: dict[str, dict[str, Any]] = {}
        self._monitors: dict[str, BoardMonitor] = {}
        self._closed = False

    def boards(self) -> dict[str, Any]:
        with self._lock:
            if self._closed:
                raise RuntimeError("dashboard is shutting down")
            devices = self._discover()
            boards = []
            seen = set()
            for device in devices:
                serial = device.get("serial_number") or None
                path = os.fsdecode(device["path"])
                identifier = hashlib.sha256((f"serial:{serial}" if serial else f"path:{path}").encode()).hexdigest()
                if identifier in seen:
                    raise RuntimeError("duplicate PicoUart board identity; board serials must be unique")
                seen.add(identifier)
                board = {"id": identifier, "product_name": device.get("product_string") or "PicoUart",
                         "serial_number": serial, "device_path": path}
                boards.append(board)
            self._known.update((board["id"], board) for board in boards)
            preferred = next((board["id"] for board in boards
                              if (self._serial and board["serial_number"] == self._serial)
                              or (self._path and board["device_path"] == self._path)), None)
            return {"boards": boards, "default_board_id": preferred or (
                None if self._serial or self._path else boards[0]["id"] if boards else None)}

    def _monitor(self, board_id: str | None) -> BoardMonitor:
        with self._lock:
            if self._closed:
                raise RuntimeError("dashboard is shutting down")
            if not board_id or board_id not in self._known:
                raise RuntimeError("unknown board; rescan and select a listed board")
            if board_id not in self._monitors:
                board = self._known[board_id]
                self._monitors[board_id] = self._monitor_factory(
                    serial_number=board["serial_number"],
                    device_path=None if board["serial_number"] else board["device_path"])
            return self._monitors[board_id]

    def snapshot(self, board_id: str | None = None) -> dict[str, Any]:
        if board_id is None:
            return empty_snapshot()
        return self._monitor(board_id).snapshot()

    def toggle_led(self, board_id: str | None) -> None:
        self._monitor(board_id).toggle_led()

    def reset_board(self, board_id: str | None) -> None:
        self._monitor(board_id).reset_board()

    def save_configuration(self, board_id: str | None, profile: int, unsafe_overclock: bool) -> None:
        self._monitor(board_id).save_configuration(profile, unsafe_overclock)

    def read_diagnostics(self, board_id: str | None) -> dict[str, Any]:
        return self._monitor(board_id).read_diagnostics()

    def close(self) -> None:
        with self._lock:
            if self._closed:
                return
            self._closed = True
            monitors = list(self._monitors.values())
        for monitor in monitors:
            monitor.close()