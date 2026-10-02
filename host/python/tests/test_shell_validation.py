"""Regression tests for shell validation coverage and interruption policy."""

import json
import os
from pathlib import Path
import signal
import subprocess
import sys

import pytest

pytestmark = pytest.mark.skipif(sys.platform == "win32", reason="POSIX executable shims and signals")


def test_static_analysis_covers_every_firmware_source_from_any_cwd(repo_root, tmp_path):
    shim = tmp_path / "cppcheck"
    log = tmp_path / "cppcheck.json"
    shim.write_text(f"#!{sys.executable}\n" + '''import json, os, sys
from pathlib import Path
inputs = [arg for arg in sys.argv[1:] if arg.startswith("firmware/")]
files = []
for arg in inputs:
    path = Path(arg)
    if not path.exists():
        sys.exit(9)
    if path.is_dir():
        files.extend(str(item) for item in path.rglob("*.c"))
    else:
        files.append(str(path))
Path(os.environ["ANALYSIS_LOG"]).write_text(json.dumps({"cwd": os.getcwd(), "files": files}))
sys.exit(int(os.environ["ANALYSIS_STATUS"]))
''')
    shim.chmod(0o700)
    env = {**os.environ, "PATH": str(tmp_path) + os.pathsep + os.environ["PATH"],
           "ANALYSIS_LOG": str(log), "ANALYSIS_STATUS": "0"}
    command = ["sh", str(repo_root / "tools/test/static-analyze.sh")]
    completed = subprocess.run(command, cwd=tmp_path, env=env, capture_output=True, text=True)
    assert completed.returncode == 0, completed.stderr
    recorded = json.loads(log.read_text())
    assert Path(recorded["cwd"]) == repo_root
    files = set(recorded["files"])
    sources = {str(path.relative_to(repo_root)) for path in (repo_root / "firmware/src").rglob("*.c")}
    assert sources <= files
    assert "firmware/src/uart/hw/hw_uart_driver.c" in files
    assert "firmware/src/uart/pio/pio_uart_driver.c" in files
    assert "firmware/src/uart/dma/progress_math.h" in files
    env["ANALYSIS_STATUS"] = "7"
    assert subprocess.run(command, cwd=tmp_path, env=env).returncode == 7


@pytest.mark.parametrize("phase", ["full", "fallback", "pytest"])
@pytest.mark.parametrize(("signum", "status"), [
    (signal.SIGHUP, 129), (signal.SIGINT, 130), (signal.SIGTERM, 143),
])
def test_host_runner_signal_exits_and_cleans_fallback(repo_root, tmp_path, phase, signum, status):
    shim = tmp_path / "python-shim"
    log = tmp_path / "calls.jsonl"
    shim.write_text(f"#!{sys.executable}\n" + '''import json, os, signal, sys
from pathlib import Path
args = sys.argv[1:]
if args == ["-m", "pip", "--version"]:
    sys.exit(0)
phase = "pytest"
record = {"args": args}
if "install" in args:
    lock = Path(args[args.index("-r") + 1])
    phase = "full" if lock.name == "requirements-lock.txt" else "fallback"
    record["path"] = str(lock)
record["phase"] = phase
with open(os.environ["CALL_LOG"], "a") as stream:
    stream.write(json.dumps(record) + "\\n")
if phase == os.environ["SIGNAL_PHASE"]:
    os.kill(os.getppid(), int(os.environ["SIGNAL_NUMBER"]))
    sys.exit(0)
sys.exit(1 if phase == "full" else 0)
''')
    shim.chmod(0o700)
    completed = subprocess.run(
        ["sh", str(repo_root / "tools/test/test-host.sh"), "--skip-c"],
        cwd=repo_root,
        env={**os.environ, "PYTHON_EXE": str(shim), "CALL_LOG": str(log),
             "SIGNAL_PHASE": phase, "SIGNAL_NUMBER": str(signum), "TMPDIR": str(tmp_path)},
        capture_output=True, text=True, timeout=10,
    )
    assert completed.returncode == status, completed.stdout + completed.stderr
    calls = [json.loads(line) for line in log.read_text().splitlines()]
    assert [call["phase"] for call in calls] == {
        "full": ["full"], "fallback": ["full", "fallback"],
        "pytest": ["full", "fallback", "pytest"],
    }[phase]
    for call in calls:
        if call["phase"] == "fallback":
            assert not Path(call["path"]).exists()


@pytest.mark.parametrize("status", [129, 130, 143])
def test_interrupted_initial_pip_is_not_retried(repo_root, tmp_path, status):
    shim = tmp_path / "python-shim"
    log = tmp_path / "calls.txt"
    shim.write_text(f"#!{sys.executable}\n" + '''import os, sys
from pathlib import Path
args = sys.argv[1:]
if args == ["-m", "pip", "--version"]:
    sys.exit(0)
with open(os.environ["CALL_LOG"], "a") as stream:
    stream.write(" ".join(args) + "\\n")
sys.exit(int(os.environ["INSTALL_STATUS"]))
''')
    shim.chmod(0o700)
    completed = subprocess.run(
        ["sh", str(repo_root / "tools/test/test-host.sh"), "--skip-c"],
        cwd=repo_root,
        env={**os.environ, "PYTHON_EXE": str(shim), "CALL_LOG": str(log),
             "INSTALL_STATUS": str(status), "TMPDIR": str(tmp_path)},
        capture_output=True, text=True, timeout=10,
    )
    assert completed.returncode == status
    assert len(log.read_text().splitlines()) == 1
    assert "retrying" not in completed.stderr
