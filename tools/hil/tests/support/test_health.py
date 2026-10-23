from __future__ import annotations

import importlib
import json
import os
import subprocess

import pytest


def _load_health():
    module = importlib.import_module("hil_test_suite.support.health")
    return importlib.reload(module)


def _clean_snapshot():
    return {
        "channels": {index: "ready" for index in range(6)},
        "overruns": {index: 0 for index in range(6)},
        "diagnostics": {index: {
            "uart_measurement_state": 1, "rx_overrun_count": 0, "rx_error_count": 0,
            "last_control_error_reason": 0,
        } for index in range(6)},
        "firmware_version": "1.2.3",
        "error": None,
    }


@pytest.mark.parametrize("measurement", [0, 1, 2])
def test_collects_six_channel_health_and_overruns(monkeypatch, measurement) -> None:
    health = _load_health()
    calls = []

    class Board:
        def __enter__(self):
            return self

        def __exit__(self, *_arguments):
            calls.append("close")

        def read_firmware_version(self):
            return "1.2.3"

        def read_channel(self, index):
            calls.append(("channel", index))
            return {"state": "ready"}

        def read_channel_diagnostic(self, index):
            calls.append(("diagnostic", index))
            return {**_clean_snapshot()["diagnostics"][index], "rx_overrun_count": index,
                    "uart_measurement_state": measurement}

    monkeypatch.setattr(health, "PicoUartHid", Board)
    monkeypatch.setattr(health.time, "sleep", lambda _seconds: None)

    snapshot = health._collect_hid_health_once()

    assert snapshot["channels"] == {index: "ready" for index in range(6)}
    assert snapshot["overruns"] == {index: index for index in range(6)}
    assert snapshot["firmware_version"] == "1.2.3"
    assert snapshot["error"] == (None if measurement == 1 else "CDC5 UART diagnostics are not current")
    diagnostic_reads = 1 if measurement == 1 else 3
    assert calls == [item for index in range(6)
                     for item in [("channel", index)] + [("diagnostic", index)] * diagnostic_reads] + ["close"]


def test_hid_command_uses_checkout_and_host_source_path(monkeypatch) -> None:
    health = _load_health()
    monkeypatch.setenv("PYTHONPATH", "/existing/python/path")
    calls = []
    completed = subprocess.CompletedProcess([], 0, "ok", "")

    def run(command, **options):
        calls.append((command, options))
        return completed

    monkeypatch.setattr(health.subprocess, "run", run)

    assert health._run_hid("version") is completed
    command, options = calls[0]
    assert command == [health.sys.executable, "-m", "pico_uart", "version"]
    assert options["cwd"] == health.REPO_ROOT
    assert options["env"]["PYTHONPATH"] == os.pathsep.join(
        (str(health.HOST_PYTHON_SRC), "/existing/python/path")
    )


def test_reads_live_system_clock_from_hardware_json(monkeypatch) -> None:
    health = _load_health()
    monkeypatch.setattr(
        health,
        "_run_hid",
        lambda *arguments: subprocess.CompletedProcess(arguments, 0, json.dumps({"system_clock_hz": 150000000}), ""),
    )

    assert health.read_system_clock_hz() == 150000000


def test_health_collection_reports_command_and_sample_errors(monkeypatch) -> None:
    health = _load_health()
    def unavailable():
        raise OSError("device unavailable")

    monkeypatch.setattr(health, "PicoUartHid", unavailable)

    snapshot = health._collect_hid_health_once()

    assert "HID health query failed: device unavailable" == snapshot["error"]
    assert not health.health_is_clean(snapshot)


def test_health_collection_retries_a_transient_failure(monkeypatch) -> None:
    health = _load_health()
    recovered = _clean_snapshot()
    sleeps = []
    snapshots = iter([{"error": "device re-enumerating"}, recovered])
    monkeypatch.setattr(health, "_collect_hid_health_once", lambda: next(snapshots))
    monkeypatch.setattr(health.time, "sleep", sleeps.append)

    assert health.collect_hid_health() is recovered
    assert sleeps == [0.5]


def test_health_cleanliness_checks_states_diagnostics_and_overrun_baseline() -> None:
    health = _load_health()
    baseline = _clean_snapshot()

    assert health.health_is_clean(baseline, baseline)
    closed = {**baseline, "channels": {index: "closed" for index in range(6)}}
    assert health.health_is_clean(closed)
    for state in ("starting", "configuring", "error", "unknown"):
        assert not health.health_is_clean({**baseline, "channels": {**baseline["channels"], 0: state}})
    assert not health.health_is_clean(None)
    assert not health.health_is_clean({**baseline, "error": "query failed"})
    assert not health.health_is_clean({**baseline, "channels": {0: "ready"}})
    assert not health.health_is_clean({**baseline, "diagnostics": {}})

    for field, value in (("uart_measurement_state", 0), ("uart_measurement_state", 2),
                         ("rx_error_count", 1), ("rx_overrun_count", 1), ("last_control_error_reason", 1)):
        unhealthy = _clean_snapshot()
        unhealthy["diagnostics"][2][field] = value
        assert not health.health_is_clean(unhealthy)

    changed_overruns = _clean_snapshot()
    changed_overruns["overruns"][3] = 1
    assert not health.health_is_clean(changed_overruns, baseline)


def test_health_text_helpers_cover_missing_and_error_snapshots() -> None:
    health = _load_health()
    snapshot = _clean_snapshot()

    assert health.health_summary(None) == "not collected"
    assert health.health_summary({"error": "query failed"}) == "ERROR: query failed"
    assert "cdc0=ready" in health.health_summary(snapshot)
    assert health.health_evidence(None) == "HID health: not collected\n"
    evidence = health.health_evidence(snapshot)
    assert "HID health summary:" in evidence
    assert '"rx_error_count": 0' in evidence