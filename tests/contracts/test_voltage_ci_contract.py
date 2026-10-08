"""Contracts for opt-in SDK voltage checks and their minimal CI dependency lock."""

import importlib.util
import json
import re
import shlex
import shutil
import subprocess

import pytest


@pytest.fixture
def voltage_policy_without_native_compiler(repo_root, tmp_path, monkeypatch):
    monkeypatch.setenv("PICO_UART_VOLTAGE_BUILD_DIRS", str(tmp_path))
    monkeypatch.setattr(
        shutil, "which", lambda name: "arm-none-eabi-nm" if name == "arm-none-eabi-nm" else None,
    )
    spec = importlib.util.spec_from_file_location(
        "voltage_policy_without_native_compiler",
        repo_root / "tests/firmware/test_system_clock_voltage_policy.py",
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    argv = [
        "arm-none-eabi-gcc", "-DSYS_CLK_VREG_VOLTAGE_AUTO_ADJUST=0",
        "-c", "runtime_init_clocks.c", "-o", "runtime_init_clocks.c.o",
    ]
    (tmp_path / "compile_commands.json").write_text(json.dumps([
        {"file": "runtime_init_clocks.c", "directory": str(tmp_path), "arguments": argv},
    ]))
    for name in ("runtime_init_clocks.c.o", "pico_uart.elf"):
        (tmp_path / name).write_bytes(b"mock artifact")

    calls = []

    def run(command, **kwargs):
        calls.append(command)
        if command[0] == "arm-none-eabi-gcc":
            assert command[-2:] == ["-E", "-dM"]
            output = "#define SYS_CLK_VREG_VOLTAGE_AUTO_ADJUST 0\n"
        else:
            assert command[0] == "arm-none-eabi-nm"
            output = "00000000 T runtime_init_clocks\n"
        assert kwargs["check"]
        return subprocess.CompletedProcess(command, 0, stdout=output, stderr="")

    monkeypatch.setattr(module.subprocess, "run", run)
    return module, calls


def test_only_native_mock_skips_without_native_compiler(voltage_policy_without_native_compiler):
    module, _ = voltage_policy_without_native_compiler
    assert not getattr(module, "pytestmark", [])
    marks = module.test_system_init_clock_never_writes_voltage.pytestmark
    skips = [mark for mark in marks if mark.name == "skipif"]
    assert len(skips) == 1
    assert skips[0].args == (True,)
    assert not any(mark.name in {"skip", "skipif"}
                   for mark in module.test_real_sdk_startup_voltage_policy.pytestmark)


def test_configured_sdk_check_runs_without_native_compiler(
    voltage_policy_without_native_compiler, tmp_path,
):
    module, calls = voltage_policy_without_native_compiler
    module.test_real_sdk_startup_voltage_policy(str(tmp_path))
    assert [command[0] for command in calls] == [
        "arm-none-eabi-gcc", "arm-none-eabi-nm", "arm-none-eabi-nm",
    ]


@pytest.mark.parametrize("missing", [
    "compile_commands.json", "runtime_init_clocks.c.o", "pico_uart.elf", "compiler", "nm",
])
def test_configured_sdk_check_fails_not_skips_without_native_compiler(
    voltage_policy_without_native_compiler, tmp_path, monkeypatch, missing,
):
    module, _ = voltage_policy_without_native_compiler
    if missing == "compiler":
        def missing_compiler(*args, **kwargs):
            raise FileNotFoundError("arm-none-eabi-gcc")
        monkeypatch.setattr(module.subprocess, "run", missing_compiler)
        expected, message = FileNotFoundError, "arm-none-eabi-gcc"
    elif missing == "nm":
        monkeypatch.setattr(module.shutil, "which", lambda name: None)
        expected, message = AssertionError, "ARM nm required"
    else:
        (tmp_path / missing).unlink()
        expected = FileNotFoundError if missing == "compile_commands.json" else AssertionError
        message = re.escape(missing)
    with pytest.raises(expected, match=message):
        module.test_real_sdk_startup_voltage_policy(str(tmp_path))


def test_sdk_check_skips_only_when_unconfigured(
    voltage_policy_without_native_compiler, monkeypatch,
):
    module, _ = voltage_policy_without_native_compiler
    monkeypatch.delenv("PICO_UART_VOLTAGE_BUILD_DIRS")
    with pytest.raises(pytest.skip.Exception, match="set PICO_UART_VOLTAGE_BUILD_DIRS"):
        module.test_real_sdk_startup_voltage_policy("")
    monkeypatch.setenv("PICO_UART_VOLTAGE_BUILD_DIRS", ":")
    with pytest.raises(pytest.fail.Exception, match="empty build directory"):
        module.test_real_sdk_startup_voltage_policy("")


@pytest.mark.parametrize("workflow_name", ["pr-check.yml", "release.yml"])
def test_voltage_workflow_uses_hash_required_minimal_lock(repo_root, workflow_name):
    workflow = (repo_root / ".github/workflows" / workflow_name).read_text()
    step = workflow.split("      - name: Verify no SDK regulator auto-adjust in the real build\n", 1)[1]
    step = step.split("\n      - name:", 1)[0]
    installs = [shlex.split(line.strip()) for line in step.splitlines()
                if line.strip().startswith("python3 -m pip install ")]
    assert len(installs) == 1
    command = installs[0]
    assert "--require-hashes" in command
    assert command[command.index("-r") + 1] == "host/python/requirements-voltage-lock.txt"
    assert 'PICO_UART_VOLTAGE_BUILD_DIRS="$PWD/build/firmware-${LABEL}"' in step
    assert "tests/firmware/test_system_clock_voltage_policy.py -k real_sdk" in step
    assert 'python-version: "3.12"' in workflow


def _lock_entries(path):
    entries = {}
    current = None
    for line in path.read_text().splitlines():
        line = line.strip().removesuffix("\\").strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("--hash="):
            assert current is not None
            assert re.fullmatch(r"--hash=sha256:[0-9a-f]{64}", line)
            entries[current].add(line)
        else:
            assert "==" in line
            assert line not in entries
            current = line
            entries[current] = set()
    assert all(entries.values()), "every locked requirement must have hashes"
    return entries


def test_voltage_lock_is_exact_pytest_subset_of_host_lock(repo_root):
    host = repo_root / "host/python"
    subset = _lock_entries(host / "requirements-voltage-lock.txt")
    full = _lock_entries(host / "requirements-lock.txt")
    assert {entry.split("==")[0] for entry in subset} == {
        "iniconfig", "packaging", "pluggy", "pytest",
    }
    assert subset == {entry: full[entry] for entry in subset}
