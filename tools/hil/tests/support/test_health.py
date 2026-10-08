from __future__ import annotations

import importlib
import os
import subprocess


def _load_health():
    module = importlib.import_module("hil_test_suite.support.health")
    return importlib.reload(module)


def _clean_snapshot():
    return {
        "channels": {index: 1 for index in range(6)},
        "overruns": {index: 0 for index in range(6)},
        "firmware_version": "1.2.3",
        "error": None,
    }


def test_collects_six_channel_health_and_overruns(monkeypatch) -> None:
    health = _load_health()
    monitor = "\n".join(f"cdc{index} health=0x1[ready]" for index in range(6))
    overruns = "\n".join(f"cdc{index}={index}" for index in range(6))
    responses = iter(
        [
            subprocess.CompletedProcess([], 0, monitor, ""),
            subprocess.CompletedProcess([], 0, overruns, ""),
            subprocess.CompletedProcess([], 0, "1.2.3\n", ""),
        ]
    )
    calls = []

    def run_hid(*arguments):
        calls.append(arguments)
        return next(responses)

    monkeypatch.setattr(health, "_run_hid", run_hid)

    snapshot = health._collect_hid_health_once()

    assert snapshot["channels"] == {index: 1 for index in range(6)}
    assert snapshot["overruns"] == {index: index for index in range(6)}
    assert snapshot["firmware_version"] == "1.2.3"
    assert snapshot["error"] is None
    assert calls == [
        ("monitor", "--duration", "1"),
        ("overruns",),
        ("version",),
    ]


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


def test_health_collection_reports_command_and_sample_errors(monkeypatch) -> None:
    health = _load_health()
    responses = iter(
        [
            subprocess.CompletedProcess([], 1, "", "monitor unavailable"),
            subprocess.CompletedProcess([], 2, "", "overruns unavailable"),
            subprocess.CompletedProcess([], 3, "", "version unavailable"),
        ]
    )
    monkeypatch.setattr(health, "_run_hid", lambda *_arguments: next(responses))

    snapshot = health._collect_hid_health_once()

    assert "HID monitor failed" in snapshot["error"]
    assert "HID overflow query failed" in snapshot["error"]
    assert "HID version query failed" in snapshot["error"]
    assert "returned 0/6" in snapshot["error"]


def test_health_collection_retries_a_transient_failure(monkeypatch) -> None:
    health = _load_health()
    recovered = _clean_snapshot()
    sleeps = []
    snapshots = iter([{"error": "device re-enumerating"}, recovered])
    monkeypatch.setattr(health, "_collect_hid_health_once", lambda: next(snapshots))
    monkeypatch.setattr(health.time, "sleep", sleeps.append)

    assert health.collect_hid_health() is recovered
    assert sleeps == [0.5]


def test_health_cleanliness_checks_flags_and_overrun_baseline() -> None:
    health = _load_health()
    baseline = _clean_snapshot()

    assert health.health_is_clean(baseline, baseline)
    assert not health.health_is_clean(None)
    assert not health.health_is_clean({**baseline, "error": "query failed"})
    assert not health.health_is_clean({**baseline, "channels": {0: 1}})

    unhealthy = _clean_snapshot()
    unhealthy["channels"][2] |= health.BAD_HEALTH_BITS
    assert not health.health_is_clean(unhealthy)

    changed_overruns = _clean_snapshot()
    changed_overruns["overruns"][3] = 1
    assert not health.health_is_clean(changed_overruns, baseline)


def test_health_text_helpers_cover_missing_and_error_snapshots() -> None:
    health = _load_health()
    snapshot = _clean_snapshot()

    assert health.health_summary(None) == "not collected"
    assert health.health_summary({"error": "query failed"}) == "ERROR: query failed"
    assert "cdc0=0x01" in health.health_summary(snapshot)
    assert health.health_evidence(None) == "HID health: not collected\n"
    evidence = health.health_evidence({**snapshot, "monitor_output": "monitor raw\n"})
    assert "HID health summary:" in evidence
    assert "monitor raw" in evidence