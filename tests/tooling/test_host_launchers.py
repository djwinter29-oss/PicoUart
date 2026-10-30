# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""POSIX host launcher contracts, without package installs, SDKs, or USB devices."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

import pytest


pytestmark = pytest.mark.skipif(sys.platform == "win32", reason="POSIX shell launchers")


@pytest.fixture
def launchers(repo_root, tmp_path):
    root = tmp_path / "checkout with spaces"
    scripts = root / "tools/host"
    scripts.mkdir(parents=True)
    for name in ("run-python.sh", "run-dotnet.sh", "run-webhid.sh"):
        shutil.copy2(repo_root / "tools/host" / name, scripts / name)
    permission_check = scripts / "hid-permissions-check.sh"
    permission_check.write_text('#!/bin/sh\nexit "${PERMISSION_STATUS:-0}"\n')
    permission_check.chmod(0o700)
    shim = tmp_path / "runtime shim"
    shim.write_text(
        f"#!{sys.executable}\n"
        + """import json, os, shutil, sys
from pathlib import Path
args = sys.argv[1:]
with open(os.environ["CALL_LOG"], "a") as log:
    log.write(json.dumps({"args": args, "pythonpath": os.environ.get("PYTHONPATH"),
        "pip_config": os.environ.get("PIP_CONFIG_FILE"), "index": os.environ.get("PIP_INDEX_URL"),
        "extra_index": os.environ.get("PIP_EXTRA_INDEX_URL")}) + "\\n")
if args[:2] == ["-m", "venv"]:
    target = Path(args[2]) / "bin/python"
    target.parent.mkdir(parents=True)
    shutil.copy2(__file__, target)
elif args[:2] == ["-m", "pip"]:
    status = int(os.environ.get("INSTALL_STATUS", "0"))
    if status:
        sys.exit(status)
    Path(os.environ["DEPS_MARKER"]).touch()
elif args[:1] == ["-c"]:
    if args[1] == "import flask, hid":
        sys.exit(0 if Path(os.environ["DEPS_MARKER"]).exists() else 1)
    sys.exit(int(os.environ.get("VERSION_STATUS", "0")))
else:
    sys.exit(int(os.environ.get("APP_STATUS", "0")))
"""
    )
    shim.chmod(0o700)
    env = {
        **os.environ,
        "CALL_LOG": str(tmp_path / "calls.jsonl"),
        "DEPS_MARKER": str(tmp_path / "deps-ready"),
        "PYTHON": str(shim),
        "DOTNET": str(shim),
        "VENV_DIR": str(tmp_path / "venv with spaces"),
        "PYTHONPATH": "existing-path",
    }
    return root, scripts, shim, env


def invoke(launchers, name, *args, **overrides):
    _, scripts, _, env = launchers
    return subprocess.run(
        [str(scripts / name), *args], cwd=scripts.parent, env={**env, **overrides},
        capture_output=True, text=True, timeout=10,
    )


def calls(launchers):
    log = Path(launchers[3]["CALL_LOG"])
    return [json.loads(line) for line in log.read_text().splitlines()] if log.exists() else []


@pytest.mark.parametrize("name", ["run-python.sh", "run-dotnet.sh", "run-webhid.sh"])
def test_help_needs_no_runtime_or_environment(launchers, name):
    result = invoke(launchers, name, "--help", PYTHON="/missing/python", DOTNET="/missing/dotnet")
    assert result.returncode == 0
    assert "Usage:" in result.stdout
    assert not calls(launchers)
    assert not Path(launchers[3]["VENV_DIR"]).exists()


@pytest.mark.parametrize("name", ["run-python.sh", "run-dotnet.sh"])
def test_permission_failure_stops_before_bootstrap_or_application(launchers, name):
    result = invoke(launchers, name, PERMISSION_STATUS="1")
    assert result.returncode == 1
    assert not calls(launchers)
    assert not Path(launchers[3]["VENV_DIR"]).exists()


def test_webhid_server_does_not_check_remote_server_usb_permissions(launchers):
    result = invoke(launchers, "run-webhid.sh", PERMISSION_STATUS="1")
    assert result.returncode == 0


def test_python_bootstraps_once_and_defaults_to_dashboard(launchers):
    root, _, _, env = launchers
    result = invoke(launchers, "run-python.sh")
    assert result.returncode == 0, result.stderr
    recorded = calls(launchers)
    assert recorded[1]["args"] == ["-m", "venv", env["VENV_DIR"]]
    assert recorded[3]["args"] == [
        "-m", "pip", "install", "--require-hashes", "-r", str(root / "host/python/requirements-runtime-lock.txt"),
    ]
    assert recorded[-1]["args"] == ["-m", "pico_uart", "web"]
    assert recorded[-1]["pythonpath"] == f"{root / 'host/python/src'}:existing-path"
    Path(env["CALL_LOG"]).unlink()
    result = invoke(launchers, "run-python.sh", "--serial", "board with spaces", "status",
                    PYTHON="/missing/base-python")
    assert result.returncode == 0, result.stderr
    assert [call["args"] for call in calls(launchers)] == [
        ["-c", "import flask, hid"], ["-m", "pico_uart", "--serial", "board with spaces", "status"],
    ]


def test_python_bootstrap_ignores_unrelated_private_package_indexes(launchers):
    result = invoke(launchers, "run-python.sh", PIP_INDEX_URL="https://invalid.example/simple",
                    PIP_EXTRA_INDEX_URL="https://private.invalid/simple", PIP_CONFIG_FILE="/missing/pip.conf")
    assert result.returncode == 0, result.stderr
    install = next(call for call in calls(launchers) if call["args"][:2] == ["-m", "pip"])
    assert install["pip_config"] == "/dev/null"
    assert install["index"] == "https://pypi.org/simple"
    assert install["extra_index"] == ""


def test_python_stops_on_install_failure_without_starting_application(launchers):
    result = invoke(launchers, "run-python.sh", INSTALL_STATUS="17")
    assert result.returncode == 17
    assert calls(launchers)[-1]["args"][:3] == ["-m", "pip", "install"]


def test_python_uses_default_repo_environment_and_propagates_application_failure(launchers):
    root, _, _, env = launchers
    Path(env["DEPS_MARKER"]).touch()
    result = invoke(launchers, "run-python.sh", "status", VENV_DIR="", APP_STATUS="19")
    assert result.returncode == 19
    assert calls(launchers)[1]["args"] == ["-m", "venv", str(root / "host/python/.venv")]
    assert calls(launchers)[-1]["args"] == ["-m", "pico_uart", "status"]


def test_python_rejects_missing_or_unsupported_interpreter(launchers):
    missing = invoke(launchers, "run-python.sh", PYTHON="/missing/python")
    assert missing.returncode == 1
    assert "Python interpreter not found" in missing.stderr
    old = invoke(launchers, "run-python.sh", VERSION_STATUS="1")
    assert old.returncode == 1
    assert "Python 3.10 or newer" in old.stderr
    assert not Path(launchers[3]["VENV_DIR"]).exists()


@pytest.mark.parametrize("args,expected", [
    ([], ["web"]),
    (["web", "--port", "5001"], ["web", "--port", "5001"]),
    (["--serial", "board with spaces", "status"], ["--serial", "board with spaces", "status"]),
])
def test_dotnet_resolves_project_and_passes_arguments_and_exit_status(launchers, args, expected):
    root = launchers[0]
    result = invoke(launchers, "run-dotnet.sh", *args, APP_STATUS="23")
    assert result.returncode == 23
    assert calls(launchers)[0]["args"] == [
        "run", "--project", str(root / "host/dotnet/PicoUart"), "--no-launch-profile", "--", *expected,
    ]


def test_dotnet_reports_missing_sdk(launchers):
    result = invoke(launchers, "run-dotnet.sh", DOTNET="/missing/dotnet")
    assert result.returncode == 1
    assert ".NET SDK executable not found" in result.stderr


@pytest.mark.parametrize("args,port", [([], "8000"), (["--port", "8765"], "8765")])
def test_webhid_uses_loopback_and_its_own_assets(launchers, args, port):
    result = invoke(launchers, "run-webhid.sh", *args)
    assert result.returncode == 0, result.stderr
    assert calls(launchers)[0]["args"] == [
        "-m", "http.server", port, "--bind", "127.0.0.1", "--directory", str(launchers[0] / "host/webhid"),
    ]
    assert not Path(launchers[3]["VENV_DIR"]).exists()


@pytest.mark.parametrize("args", [
    ["--port"], ["--port", ""], ["--port", "-1"], ["--port", "0"], ["--port", "65536"],
    ["--port", "9999999999999999999999999"], ["--port", "abc"], ["--unknown"],
])
def test_webhid_rejects_invalid_arguments_before_starting_server(launchers, args):
    result = invoke(launchers, "run-webhid.sh", *args)
    assert result.returncode == 2
    assert result.stderr
    assert not calls(launchers)
