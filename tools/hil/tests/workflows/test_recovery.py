# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
from __future__ import annotations

import argparse
import copy
import json
import sys

import pytest

pytestmark = pytest.mark.skipif(sys.platform != "linux", reason="Recovery checks use Linux serial endpoints")


def clean_snapshot():
    return {
        "channels": {index: "closed" for index in range(6)},
        "overruns": {index: 0 for index in range(6)},
        "diagnostics": {index: {
            "uart_measurement_state": 1, "rx_overrun_count": 0, "rx_error_count": 0,
            "last_control_error_reason": 0,
        } for index in range(6)},
        "error": None,
    }


@pytest.fixture
def recovery():
    from hil_test_suite.workflows import recovery
    return recovery


@pytest.fixture
def arguments(tmp_path):
    artifact = tmp_path / "image.elf"
    artifact.write_bytes(b"test firmware")
    return argparse.Namespace(
        serial="selected", cdc1="/fixture/1", cdc5="/fixture/5", artifact=artifact,
        output=tmp_path / "evidence.json", mode="all",
    )


@pytest.mark.parametrize("fault", ["rx_error_count", "last_control_error_reason", "uart_measurement_state"])
def test_expected_overflow_does_not_hide_other_faults(recovery, fault):
    snapshot = clean_snapshot()
    snapshot["overruns"][1] = 40
    snapshot["diagnostics"][1]["rx_overrun_count"] = 40
    original = copy.deepcopy(snapshot)
    recovery.require_health(snapshot, allow_overflow=True)
    assert snapshot == original
    with pytest.raises(RuntimeError):
        recovery.require_health(snapshot)
    snapshot["diagnostics"][1][fault] = 2
    with pytest.raises(RuntimeError):
        recovery.require_health(snapshot, allow_overflow=True)


def test_sibling_overflow_is_never_expected(recovery):
    snapshot = clean_snapshot()
    snapshot["overruns"][5] = snapshot["diagnostics"][5]["rx_overrun_count"] = 1
    with pytest.raises(RuntimeError):
        recovery.require_health(snapshot, allow_overflow=True)


def test_wrong_board_rejected_before_any_open(recovery, arguments, monkeypatch):
    monkeypatch.setattr(recovery, "pico_uart_endpoint", lambda path: ("wrong", 1))
    monkeypatch.setattr(recovery, "configure_port", lambda *args: pytest.fail("must not open a serial device"))
    with pytest.raises(ValueError, match="not CDC1"):
        recovery.run_checks(arguments, {}, [])


@pytest.fixture
def simulated(recovery, arguments, monkeypatch):
    snapshot = clean_snapshot()
    transmitted = {}
    health_calls = []
    reads = []
    closes = []

    class Board:
        def __init__(self, *, serial_number):
            assert serial_number == arguments.serial

        def __enter__(self):
            return self

        def __exit__(self, *args):
            return False

        def read_hardware_info(self):
            return {"mcu": "RP2040"}

        def read_configuration(self):
            return {"reset_cause": 0}

        def read_firmware_version(self):
            return "test"

        def wait_channels_ready(self, channels):
            assert set(channels) == {5}

    def health(serial):
        health_calls.append(serial)
        return copy.deepcopy(snapshot)

    def write(descriptor, payload, deadline):
        transmitted[descriptor] = payload

    def read(descriptor, expected, deadline):
        reads.append((descriptor, expected))
        assert transmitted[descriptor] == expected

    def sibling(descriptor, duration):
        assert descriptor == 5
        if duration == 2.0:
            snapshot["overruns"][1] = snapshot["diagnostics"][1]["rx_overrun_count"] = 100
        return {"verified_bytes": 128}

    monkeypatch.setattr(recovery, "PicoUartHid", Board)
    monkeypatch.setattr(recovery, "collect_hid_health", health)
    monkeypatch.setattr(recovery, "pico_uart_endpoint", lambda path: ("selected", int(path[-1])))
    monkeypatch.setattr(recovery, "configure_port", lambda path, rate: (int(path[-1]), []))
    monkeypatch.setattr(recovery, "close_ports", lambda ports: closes.extend(ports))
    monkeypatch.setattr(recovery, "write_all", write)
    monkeypatch.setattr(recovery, "read_exact", read)
    monkeypatch.setattr(recovery, "sibling_traffic", sibling)
    monkeypatch.setattr(recovery, "drain_backlog", lambda fd, limit: transmitted[fd][100:])
    monkeypatch.setattr(recovery, "set_port_line_coding", lambda fd, rate: None)

    def stream(label, source, destination, duration, size, timeout, barrier, results, timing):
        results[label] = (128, None)
        timing[label] = {}

    monkeypatch.setattr(recovery, "run_stream", stream)
    return snapshot, health_calls, reads, closes


def test_all_checks_record_accounted_loss_and_cleanup(recovery, arguments, simulated):
    assert recovery.main(arguments) == 0
    evidence = json.loads(arguments.output.read_text())
    assert evidence["result"] == "PASS"
    assert len(evidence["artifact_sha256"]) == 64
    assert set(evidence["checks"]) == {"churn", "reopen", "stall", "overflow"}
    assert evidence["checks"]["overflow"]["retained_bytes"] == 16384 - 100
    assert evidence["checks"]["reopen"]["cycles"] == 5
    assert evidence["checks"]["churn"]["changes"] == 12
    assert all(serial == "selected" for serial in simulated[1])
    assert len(simulated[3]) == 7
    assert any(payload.startswith(b"PU:fresh") for _, payload in simulated[2])


def test_accounting_failure_retains_evidence_and_closes(recovery, arguments, simulated, monkeypatch):
    arguments.mode = "overflow"
    monkeypatch.setattr(recovery, "drain_backlog", lambda fd, limit: b"")
    assert recovery.main(arguments) == 1
    evidence = json.loads(arguments.output.read_text())
    assert evidence["result"] == "FAIL"
    assert "accounting mismatch" in evidence["error"]
    assert len(simulated[3]) == 2


def test_cleanup_failure_is_not_success(recovery, arguments, simulated, monkeypatch):
    arguments.mode = "stall"
    monkeypatch.setattr(recovery, "close_ports", lambda ports: OSError("close failed"))
    assert recovery.main(arguments) == 1
    assert json.loads(arguments.output.read_text())["cleanup_error"] == "close failed"


def test_existing_evidence_is_never_overwritten(recovery, arguments, monkeypatch):
    arguments.output.write_text("keep")
    monkeypatch.setattr(recovery, "run_checks", lambda *args: pytest.fail("must not touch hardware"))
    with pytest.raises(FileExistsError):
        recovery.main(arguments)
    assert arguments.output.read_text() == "keep"


@pytest.mark.parametrize("chunks,limit,error", [
    ([b"abc", b"def"], 5, "more backlog"),
    ([b""], 100, "disconnected"),
])
def test_backlog_drain_rejects_extra_data_and_disconnect(recovery, monkeypatch, chunks, limit, error):
    monkeypatch.setattr(recovery.select, "select", lambda *args: ([1], [], []))
    pending = iter(chunks)
    monkeypatch.setattr(recovery.os, "read", lambda *args: next(pending))
    with pytest.raises((OSError, RuntimeError), match=error):
        recovery.drain_backlog(1, limit)


def test_backlog_drain_waits_for_quiescence(recovery, monkeypatch):
    readable = iter([([1], [], []), ([], [], [])])
    monkeypatch.setattr(recovery.select, "select", lambda *args: next(readable))
    monkeypatch.setattr(recovery.os, "read", lambda *args: b"retained")
    assert recovery.drain_backlog(1, 100) == b"retained"


def test_second_open_failure_closes_first_endpoint(recovery, arguments, simulated, monkeypatch):
    def configure(path, rate):
        if path == arguments.cdc5:
            raise OSError("second open failed")
        return (1, [])

    monkeypatch.setattr(recovery, "configure_port", configure)
    assert recovery.main(arguments) == 1
    assert simulated[3] == [(1, [])]
    assert json.loads(arguments.output.read_text())["error"] == "second open failed"


def test_overflow_accounting_does_not_accept_corrupt_retained_data(recovery, arguments, simulated, monkeypatch):
    arguments.mode = "overflow"
    monkeypatch.setattr(recovery, "drain_backlog", lambda fd, limit: b"\0" * (limit - 100))
    assert recovery.main(arguments) == 1
    assert "ordered subsequence" in json.loads(arguments.output.read_text())["error"]


def test_late_overflow_drops_fail_recovery(recovery, arguments, simulated, monkeypatch):
    arguments.mode = "overflow"
    collect = recovery.collect_hid_health
    calls = []

    def health(serial):
        calls.append(serial)
        if len(calls) == 4:
            simulated[0]["overruns"][1] = simulated[0]["diagnostics"][1]["rx_overrun_count"] = 101
        return collect(serial)

    monkeypatch.setattr(recovery, "collect_hid_health", health)
    assert recovery.main(arguments) == 1
    assert "additional RX drops" in json.loads(arguments.output.read_text())["error"]


def test_backlog_never_drains_forever(recovery, monkeypatch):
    times = iter([0, 0, 4])
    monkeypatch.setattr(recovery.time, "monotonic", lambda: next(times))
    monkeypatch.setattr(recovery.select, "select", lambda *args: ([1], [], []))
    monkeypatch.setattr(recovery.os, "read", lambda *args: b"x")
    with pytest.raises(TimeoutError, match="quiescent"):
        recovery.drain_backlog(1, 100)
