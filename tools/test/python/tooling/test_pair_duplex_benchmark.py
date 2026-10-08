"""Pair diagnostics must never report an empty or failed run as CLI success."""

import importlib.util
import sys
from types import SimpleNamespace

import pytest


def load_pair(repo_root, monkeypatch):
    # pyserial is a hardware-tool dependency, not part of the host HID test lock.
    monkeypatch.setitem(sys.modules, "serial", SimpleNamespace())
    path = repo_root / "tools/hardware/pair_duplex_benchmark.py"
    spec = importlib.util.spec_from_file_location("pair_duplex_under_test", path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


@pytest.mark.parametrize(("outcome", "expected"), [
    ("pass", 0), ("fail", 1), ("setup", 2),
])
def test_cli_preserves_failure_across_later_success(repo_root, monkeypatch, capsys, outcome, expected):
    pair = load_pair(repo_root, monkeypatch)
    monkeypatch.setattr(sys, "argv", ["pair", "stage2", "--rates", "115200", "--runs", "2"])
    calls = []

    def run(*args):
        calls.append(args)
        if len(calls) == 1 and outcome == "setup":
            raise OSError("port unavailable")
        passed = outcome != "fail" or len(calls) > 1
        return passed, {"a-to-b": (64, None if passed else "mismatch")}

    monkeypatch.setattr(pair, "run", run)
    assert pair.main() == expected
    assert len(calls) == 2
    output = capsys.readouterr().out
    assert "run=2 PASS" in output
    assert {"pass": "PASS", "fail": "FAIL", "setup": "SETUP_FAIL"}[outcome] in output


@pytest.mark.parametrize(("option", "value"), [
    ("--rates", ""), ("--rates", "abc"), ("--rates", "115200,"),
    ("--rates", "0"), ("--rates", "-1"),
    ("--runs", "0"), ("--runs", "-1"),
    ("--duration", "0"), ("--duration", "-1"),
    ("--duration", "nan"), ("--duration", "inf"),
    ("--settle", "-1"), ("--settle", "nan"), ("--settle", "inf"),
    ("--payload", "0"), ("--payload", "-1"),
])
def test_invalid_cli_settings_fail_before_hardware(repo_root, monkeypatch, option, value):
    pair = load_pair(repo_root, monkeypatch)
    args = ["pair", "stage2"]
    if option != "--rates":
        args.extend(["--rates", "115200"])
    monkeypatch.setattr(sys, "argv", args + [option, value])
    monkeypatch.setattr(pair, "configure", lambda *_: pytest.fail("invalid settings opened a port"))
    with pytest.raises(SystemExit) as error:
        pair.main()
    assert error.value.code == 2


@pytest.mark.parametrize("direction", ["both", "a-to-b", "b-to-a"])
@pytest.mark.parametrize("run_threads", [True, False])
def test_empty_stream_results_do_not_pass(repo_root, monkeypatch, direction, run_threads):
    pair = load_pair(repo_root, monkeypatch)
    closes = []
    monkeypatch.setattr(pair, "configure", lambda *_: SimpleNamespace(close=lambda: closes.append(1)))
    monkeypatch.setattr(pair, "synchronize", lambda *_: None)
    clock = [0]

    def monotonic():
        clock[0] += 1
        return clock[0]

    class ImmediateThread:
        def __init__(self, target, args):
            self.target, self.args = target, args

        def start(self):
            if run_threads:
                self.target(*self.args)

        def join(self):
            pass

    monkeypatch.setattr(pair.time, "monotonic", monotonic)
    monkeypatch.setattr(pair.threading, "Thread", ImmediateThread)
    monkeypatch.setattr(pair.threading, "Barrier", lambda *_: SimpleNamespace(wait=lambda: None))
    passed, results = pair.run(115200, ("a", "b"), 0.1, 0, 64, direction)
    assert not passed
    assert closes == [1, 1]
    if run_threads:
        assert all("without verifying" in error for _, error in results.values())
    else:
        assert results == {}


def test_second_port_setup_failure_closes_first(repo_root, monkeypatch):
    pair = load_pair(repo_root, monkeypatch)
    closes = []

    def configure(path, _rate):
        if path.endswith("b"):
            raise OSError("second port failed")
        return SimpleNamespace(close=lambda: closes.append(path))

    monkeypatch.setattr(pair, "configure", configure)
    with pytest.raises(OSError, match="second port failed"):
        pair.run(115200, ("a", "b"), 1, 0, 64, "both")
    assert closes == [pair.PICO + "a"]


def test_reset_failure_closes_new_port(repo_root, monkeypatch):
    pair = load_pair(repo_root, monkeypatch)
    closes = []

    def reset():
        raise OSError("reset failed")

    port = SimpleNamespace(reset_input_buffer=reset, close=lambda: closes.append(1))
    monkeypatch.setattr(pair.serial, "Serial", lambda *_args, **_kwargs: port, raising=False)
    with pytest.raises(OSError, match="reset failed"):
        pair.configure("fake", 115200)
    assert closes == [1]
