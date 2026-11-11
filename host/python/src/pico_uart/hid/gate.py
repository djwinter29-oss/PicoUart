# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""Close/transfer coordination for one HID handle shared with abandoned (timed-out) transfers."""

import threading

from .transport import HidDevice


class DeviceGate:
    """Track the single in-flight transfer and defer closing the handle until that transfer returns."""

    def __init__(self, device: HidDevice) -> None:
        self.device = device
        self._state_lock = threading.Lock()
        self._closing = False
        self._device_closed = False
        self._close_complete = threading.Event()
        self._close_error: OSError | None = None
        self._in_flight = False

    @property
    def closing(self) -> bool:
        """Whether close was requested; new device operations must be refused."""
        return self._closing

    def begin_transfer(self) -> None:
        """Claim the device-I/O slot; a quarantined operation keeps it until it returns."""
        with self._state_lock:
            if self._in_flight:
                raise RuntimeError("a previous HID operation is still in flight")
            self._in_flight = True

    def abort_transfer(self) -> None:
        """Release the slot when the transfer worker could not be started."""
        with self._state_lock:
            self._in_flight = False

    def end_transfer(self, completed: threading.Event | None = None) -> None:
        """Release the slot, finish a deferred close, then signal a waiting feature caller."""
        with self._state_lock:
            self._in_flight = False
            if self._closing and not self._device_closed:
                self._device_closed = True
                try:
                    self.device.close()
                except OSError as error:
                    self._close_error = error
                finally:
                    self._close_complete.set()
            if completed is not None:
                completed.set()

    def close(self) -> bool:
        """Close now, or defer while a transfer is in flight; return whether the handle is closed."""
        if not self._closing:
            self._closing = True
            with self._state_lock:
                if not self._in_flight:
                    self._device_closed = True
                    try:
                        self.device.close()
                    except OSError as error:
                        self._close_error = error
                        raise
                    finally:
                        self._close_complete.set()
        self._raise_close_error()
        return self._close_complete.is_set()

    def wait_closed(self, timeout_s: float | None) -> bool:
        """Wait for the handle to close; return false on timeout."""
        if not self._close_complete.wait(timeout_s):
            return False
        self._raise_close_error()
        return True

    def _raise_close_error(self) -> None:
        with self._state_lock:
            if self._close_error is not None:
                raise self._close_error
