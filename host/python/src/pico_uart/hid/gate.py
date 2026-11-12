# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""Close/transfer coordination for one HID handle shared with abandoned (timed-out) transfers."""

import threading
import weakref

from .transport import HidDevice


class _DeviceState:
    """I/O and close state shared by wrappers around the same HID object."""

    def __init__(self) -> None:
        self.lock = threading.Lock()
        self.references = 0
        self.in_flight = False
        self.closing = False
        self.device_closed = False
        self.close_complete = threading.Event()
        self.close_error: OSError | None = None


# Finalizers may re-enter while cyclic GC runs during state allocation.
_DEVICE_STATES: dict[int, _DeviceState] = {}
_DEVICE_STATES_LOCK = threading.RLock()


def _acquire_device_state(device: HidDevice) -> tuple[int, _DeviceState]:
    key = id(device)
    with _DEVICE_STATES_LOCK:
        state = _DEVICE_STATES.get(key)
        if state is None:
            state = _DEVICE_STATES[key] = _DeviceState()
        state.references += 1
        return key, state


def _release_device_state(key: int, state: _DeviceState) -> None:
    with _DEVICE_STATES_LOCK:
        state.references -= 1
        if state.references == 0 and _DEVICE_STATES.get(key) is state:
            del _DEVICE_STATES[key]


class DeviceGate:
    """Serialize wrappers around one HID handle and defer close until its transfer returns."""

    def __init__(self, device: HidDevice) -> None:
        self.device = device
        self._state_key, self._state = _acquire_device_state(device)
        weakref.finalize(self, _release_device_state, self._state_key, self._state)

    @property
    def closing(self) -> bool:
        """Whether close was requested; new device operations must be refused."""
        with self._state.lock:
            return self._state.closing

    def begin_transfer(self) -> None:
        """Claim the device-I/O slot; a quarantined operation keeps it until it returns."""
        with self._state.lock:
            if self._state.closing:
                raise RuntimeError("PicoUart HID connection is closed")
            if self._state.in_flight:
                raise RuntimeError("a previous HID operation is still in flight")
            self._state.in_flight = True

    def abort_transfer(self) -> None:
        """Release the slot when the transfer worker could not be started."""
        self._end_transfer()

    def end_transfer(self, completed: threading.Event | None = None) -> None:
        """Release the slot, finish a deferred close, then signal a waiting feature caller."""
        self._end_transfer(completed)

    def _end_transfer(self, completed: threading.Event | None = None) -> None:
        close_device = False
        with self._state.lock:
            self._state.in_flight = False
            if self._state.closing and not self._state.device_closed:
                self._state.device_closed = True
                close_device = True
        if close_device:
            self._close_device()
        if completed is not None:
            completed.set()

    def close(self) -> bool:
        """Close now, or defer while a transfer is in flight; return whether the handle is closed."""
        close_device = False
        with self._state.lock:
            self._state.closing = True
            if not self._state.in_flight and not self._state.device_closed:
                self._state.device_closed = True
                close_device = True
        if close_device:
            self._close_device()
        self._raise_close_error()
        return self._state.close_complete.is_set()

    def _close_device(self) -> None:
        try:
            self.device.close()
        except OSError as error:
            with self._state.lock:
                self._state.close_error = error
        finally:
            with self._state.lock:
                self._state.close_complete.set()

    def wait_closed(self, timeout_s: float | None) -> bool:
        """Wait for the handle to close; return false on timeout."""
        if not self._state.close_complete.wait(timeout_s):
            return False
        self._raise_close_error()
        return True

    def _raise_close_error(self) -> None:
        with self._state.lock:
            if self._state.close_error is not None:
                raise self._state.close_error
