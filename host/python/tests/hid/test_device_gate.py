# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""DeviceGate close/transfer coordination."""

import threading

import pytest

from pico_uart.hid.gate import DeviceGate


class Device:
    def __init__(self, error=None):
        self.error = error
        self.closes = 0

    def close(self):
        self.closes += 1
        if self.error:
            raise self.error


def test_second_transfer_is_refused_until_first_ends_and_abort_releases_slot():
    gate = DeviceGate(Device())
    gate.begin_transfer()
    with pytest.raises(RuntimeError, match="still in flight"):
        gate.begin_transfer()
    gate.abort_transfer()
    gate.begin_transfer()
    gate.end_transfer()


def test_close_during_transfer_is_deferred_until_transfer_ends():
    device = Device()
    gate = DeviceGate(device)
    gate.begin_transfer()
    assert gate.close() is False and device.closes == 0 and gate.closing
    assert gate.wait_closed(0) is False
    completed = threading.Event()
    gate.end_transfer(completed)
    assert completed.is_set() and device.closes == 1
    assert gate.wait_closed(0) is True and gate.close() is True and device.closes == 1


def test_wrappers_share_quarantine_and_defer_close_for_the_same_device():
    device = Device()
    reader = DeviceGate(device)
    owner = DeviceGate(device)
    reader.begin_transfer()

    with pytest.raises(RuntimeError, match="still in flight"):
        owner.begin_transfer()
    assert owner.close() is False
    assert device.closes == 0

    reader.end_transfer()
    assert owner.wait_closed(0) is True
    assert device.closes == 1


def test_close_error_is_reported_on_every_later_call():
    gate = DeviceGate(Device(OSError("boom")))
    with pytest.raises(OSError, match="boom"):
        gate.close()
    with pytest.raises(OSError, match="boom"):
        gate.close()
    with pytest.raises(OSError, match="boom"):
        gate.wait_closed(0)


def test_deferred_close_error_surfaces_through_wait_closed():
    gate = DeviceGate(Device(OSError("late")))
    gate.begin_transfer()
    gate.close()
    gate.end_transfer(threading.Event())
    with pytest.raises(OSError, match="late"):
        gate.wait_closed(0)
