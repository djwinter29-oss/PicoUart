# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""Hardware-free tests for dashboard monitor shutdown."""

import threading

from helpers import FakeHidDevice

import pico_uart.dashboard.monitor as monitor_module
import pico_uart.hid.client as client_module
from pico_uart.dashboard.monitor import BoardMonitor


def test_close_reports_pending_slow_read_and_succeeds_after_deferred_cleanup(monkeypatch):
    entered_read = threading.Event()
    release_read = threading.Event()
    client_closed = threading.Event()
    metadata_read = threading.Event()
    monkeypatch.setattr(monitor_module, "MONITOR_CLOSE_TIMEOUT_S", 0.01)

    class SlowClient:
        def read_status(self, timeout_ms):
            assert timeout_ms == 250
            entered_read.set()
            release_read.wait(timeout=2)
            return

        def read_identity(self):
            metadata_read.set()
            raise RuntimeError("shutdown should have skipped metadata reads")

        def close(self):
            client_closed.set()

    monitor = BoardMonitor(client_factory=SlowClient)
    try:
        assert entered_read.wait(timeout=1)
        assert monitor.close() is False
        assert not client_closed.is_set()
        release_read.set()
        assert client_closed.wait(timeout=1)
        assert monitor.close() is True
        assert not metadata_read.is_set()
        assert not monitor._thread.is_alive()
    finally:
        release_read.set()
        monitor.close()


def test_close_reports_pending_timed_out_hid_transfer_until_device_handle_closes(monkeypatch):
    entered_transfer = threading.Event()
    release_transfer = threading.Event()
    device_closed = threading.Event()
    monkeypatch.setattr(monitor_module, "MONITOR_CLOSE_TIMEOUT_S", 0.01)
    monkeypatch.setattr(client_module, "COMMAND_TIMEOUT_S", 0.02)

    class SlowDevice(FakeHidDevice):
        def get_feature_report(self, report_id, size):
            entered_transfer.set()
            release_transfer.wait(timeout=2)
            return super().get_feature_report(report_id, size)

        def close(self):
            super().close()
            device_closed.set()

    client = client_module.PicoUartHid(device=SlowDevice({}))
    monitor = BoardMonitor(client_factory=lambda: client)
    try:
        assert entered_transfer.wait(timeout=1)
        assert monitor.close() is False
        assert not device_closed.is_set()
        release_transfer.set()
        assert device_closed.wait(timeout=1)
        assert monitor.close() is True
    finally:
        release_transfer.set()
        monitor.close()
