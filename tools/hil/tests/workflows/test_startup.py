# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
from __future__ import annotations

import argparse
import json
import subprocess
import sys

import pytest

from hil_test_suite.workflows import startup


@pytest.fixture
def arguments(tmp_path):
    artifact = tmp_path / "image.elf"
    artifact.write_bytes(b"test firmware")
    return argparse.Namespace(serial="selected", artifact=artifact, output=tmp_path / "startup.json",
                              event="swd-reset", timeout=5.0, interval=0.01, stable_seconds=0.01,
                              sample=False, probe=False, cdc1=None)


def sample(state="closed", clock=125000000):
    return {"hardware": {"system_clock_hz": clock}, "configuration": {"reset_cause": 2},
            "runtime": {"uptime_ms": 100}, "channels": [{"state": state} for _ in range(6)],
            "firmware_version": "test"}


def test_failed_attempt_is_retained_before_success(arguments, monkeypatch):
    calls = []

    def run(command, **kwargs):
        calls.append((command, kwargs))
        if len(calls) == 1:
            raise subprocess.CalledProcessError(1, command, stderr="HID interface not found")
        return subprocess.CompletedProcess(command, 0, json.dumps(sample()))

    monkeypatch.setattr(startup.subprocess, "run", run)
    assert startup.main(arguments) == 0
    evidence = json.loads(arguments.output.read_text())
    assert len(evidence["attempts"]) >= 3
    assert evidence["attempts"][0]["stderr"] == "HID interface not found"
    assert evidence["attempts"][1]["sample"]["configuration"]["reset_cause"] == 2
    assert evidence["attempts"][1]["sample"]["runtime"]["uptime_ms"] == 100
    assert evidence["first_closed_observation_s"] <= arguments.timeout
    assert calls[0][0][2] == "hil_test_suite.workflows.startup"
    assert "--serial" in calls[0][0] and "selected" in calls[0][0]
    assert calls[0][1]["timeout"] <= 2.0


@pytest.mark.parametrize("response", [
    "not json", json.dumps(sample("ready")), json.dumps(sample(clock=0)),
    json.dumps({**sample(), "channels": []}),
])
def test_invalid_or_open_board_is_not_accepted(arguments, monkeypatch, response):
    arguments.timeout = 0.01
    monkeypatch.setattr(startup.subprocess, "run",
                        lambda *args, **kwargs: subprocess.CompletedProcess([], 0, response))
    evidence = startup.observe(arguments)
    assert evidence["result"] == "FAIL"
    assert all("error" in attempt for attempt in evidence["attempts"])


def test_blocked_hid_worker_is_bounded_and_recorded(arguments, monkeypatch):
    arguments.timeout = 0.01
    now = [0.0]
    monkeypatch.setattr(startup.time, "monotonic", lambda: now[0])

    def blocked(command, **kwargs):
        assert 0 < kwargs["timeout"] <= arguments.timeout
        now[0] += kwargs["timeout"]
        raise subprocess.TimeoutExpired(command, kwargs["timeout"], stderr=b"blocked")

    monkeypatch.setattr(startup.subprocess, "run", blocked)
    evidence = startup.observe(arguments)
    assert evidence["result"] == "FAIL"
    assert len(evidence["attempts"]) == 1
    assert evidence["attempts"][0]["stderr"] == "blocked"
    assert evidence["attempts"][0]["end_s"] == arguments.timeout


def test_late_success_cannot_pass_deadline(arguments, monkeypatch):
    now = [0.0]
    monkeypatch.setattr(startup.time, "monotonic", lambda: now[0])

    def late(command, **kwargs):
        now[0] = arguments.timeout + 1
        return subprocess.CompletedProcess(command, 0, json.dumps(sample()))

    monkeypatch.setattr(startup.subprocess, "run", late)
    evidence = startup.observe(arguments)
    assert evidence["result"] == "FAIL"
    assert "first_closed_observation_s" not in evidence


@pytest.mark.parametrize("value", [0, -1, float("nan"), float("inf")])
def test_invalid_timeouts_do_not_touch_hardware(arguments, value, monkeypatch):
    arguments.timeout = value
    monkeypatch.setattr(startup.subprocess, "run", lambda *args, **kwargs: pytest.fail("must not probe"))
    with pytest.raises(ValueError, match="finite and positive"):
        startup.main(arguments)
    assert not arguments.output.exists()


def test_startup_never_overwrites_evidence(arguments, monkeypatch):
    arguments.output.write_text("keep")
    monkeypatch.setattr(startup, "observe", lambda *args: pytest.fail("must not probe"))
    with pytest.raises(FileExistsError):
        startup.main(arguments)
    assert arguments.output.read_text() == "keep"


def test_reset_after_first_closed_sample_cannot_pass(arguments, monkeypatch):
    samples = iter([{**sample(), "runtime": {"uptime_ms": 9000}}, sample()])
    monkeypatch.setattr(startup.subprocess, "run", lambda *args, **kwargs: subprocess.CompletedProcess(
        [], 0, json.dumps(next(samples)),
    ))
    evidence = startup.observe(arguments)
    assert evidence["result"] == "FAIL"
    assert "uptime discontinuity" in evidence["error"]
    assert len(evidence["attempts"]) == 2


def test_disconnect_after_initial_readiness_cannot_retry_into_pass(arguments, monkeypatch):
    calls = []

    def run(command, **kwargs):
        calls.append(command)
        if len(calls) == 1:
            return subprocess.CompletedProcess(command, 0, json.dumps(sample()))
        raise subprocess.CalledProcessError(1, command, stderr="disconnected")

    monkeypatch.setattr(startup.subprocess, "run", run)
    evidence = startup.observe(arguments)
    assert evidence["result"] == "FAIL"
    assert evidence["error"] == "lost closed-state observation after initial readiness"
    assert len(calls) == 2


def test_stopped_uptime_cannot_pass_stability_window(arguments, monkeypatch):
    now = [0.0]
    arguments.stable_seconds = 2.0
    monkeypatch.setattr(startup.time, "monotonic", lambda: now[0])
    monkeypatch.setattr(startup.time, "sleep", lambda seconds: None)

    def run(command, **kwargs):
        now[0] += 0.5
        return subprocess.CompletedProcess(command, 0, json.dumps(sample()))

    monkeypatch.setattr(startup.subprocess, "run", run)
    evidence = startup.observe(arguments)
    assert evidence["result"] == "FAIL"
    assert evidence["error"] == "uptime discontinuity during startup observation"


def test_uptime_rollover_is_not_a_reset(arguments, monkeypatch):
    now = [0.0]
    samples = iter([0xFFFFFFF0, 484])
    monkeypatch.setattr(startup.time, "monotonic", lambda: now[0])
    monkeypatch.setattr(startup.time, "sleep", lambda seconds: None)

    def run(command, **kwargs):
        now[0] += 0.5
        return subprocess.CompletedProcess(command, 0, json.dumps(
            {**sample(), "runtime": {"uptime_ms": next(samples)}},
        ))

    monkeypatch.setattr(startup.subprocess, "run", run)
    assert startup.observe(arguments)["result"] == "PASS"


def test_active_probe_runs_once_before_stability(arguments, monkeypatch):
    arguments.cdc1 = "/fixture/cdc1"
    arguments.timeout = 20.0
    calls = []
    now = [0.0]
    monkeypatch.setattr(startup.time, "monotonic", lambda: now[0])
    monkeypatch.setattr(startup.time, "sleep", lambda seconds: None)

    def run(command, **options):
        calls.append((command, options))
        if "--probe" in command:
            now[0] += 2
            return subprocess.CompletedProcess(command, 0, json.dumps(
                {"channel": 1, "baud": 115200, "verified_bytes": 154},
            ))
        now[0] += 0.1
        return subprocess.CompletedProcess(command, 0, json.dumps(
            {**sample(), "runtime": {"uptime_ms": int(now[0] * 1000)}},
        ))

    monkeypatch.setattr(startup.subprocess, "run", run)
    evidence = startup.observe(arguments)
    assert evidence["result"] == "PASS"
    assert evidence["first_closed_observation_s"] == 0.1
    assert evidence["attempts"][0]["probe"]["verified_bytes"] == 154
    assert len([command for command, _options in calls if "--probe" in command]) == 1
    assert all(options["timeout"] <= arguments.timeout for _command, options in calls)
    assert calls[1][1]["timeout"] == 8.0
    assert calls[1][0][-2:] == ["--cdc1", "/fixture/cdc1"]


def test_probe_failure_cannot_retry_into_success(arguments, monkeypatch):
    arguments.cdc1 = "/fixture/cdc1"
    calls = []

    def run(command, **options):
        calls.append(command)
        if "--probe" in command:
            raise subprocess.CalledProcessError(1, command, stderr="CDC readiness timed out")
        return subprocess.CompletedProcess(command, 0, json.dumps(sample()))

    monkeypatch.setattr(startup.subprocess, "run", run)
    evidence = startup.observe(arguments)
    assert evidence["result"] == "FAIL"
    assert evidence["error"] == "CDC1 startup readiness/payload probe failed"
    assert evidence["attempts"][0]["stderr"] == "CDC readiness timed out"
    assert len(calls) == 2


@pytest.mark.skipif(sys.platform != "linux", reason="Linux sysfs/termios endpoint checks")
def test_probe_validates_identity_before_opening(monkeypatch):
    from hil_test_suite.serial import config
    monkeypatch.setattr(config, "pico_uart_endpoint", lambda path: ("other-board", 1))
    monkeypatch.setattr(config, "configure_port", lambda *args: pytest.fail("must not open a port"))
    with pytest.raises(ValueError, match="not CDC1"):
        startup.probe_channel("selected", "/unrelated")


@pytest.mark.skipif(sys.platform != "linux", reason="Linux sysfs/termios endpoint checks")
@pytest.mark.parametrize("failure", [None, "payload", "cleanup"])
def test_probe_verifies_payload_and_always_closes(monkeypatch, failure):
    from hil_test_suite.serial import config, stress
    closed = []
    sent = []
    monkeypatch.setattr(config, "pico_uart_endpoint", lambda path: ("selected", 1))
    monkeypatch.setattr(config, "configure_port", lambda path, baud: (42, []))
    monkeypatch.setattr(stress, "write_all", lambda descriptor, payload, deadline: sent.append(payload))

    def read(descriptor, expected, deadline):
        assert descriptor == 42 and expected == sent[0]
        if failure == "payload":
            raise ValueError("payload mismatch")

    def close(ports):
        closed.extend(ports)
        return OSError("close failed") if failure == "cleanup" else None

    monkeypatch.setattr(stress, "read_exact", read)
    monkeypatch.setattr(stress, "close_ports", close)
    if failure:
        with pytest.raises((ValueError, OSError), match="mismatch|close failed"):
            startup.probe_channel("selected", "/fixture")
    else:
        assert startup.probe_channel("selected", "/fixture") == {
            "channel": 1, "baud": 115200, "verified_bytes": len(sent[0]),
        }
        assert len(sent[0]) == 154
    assert closed == [(42, [])]
