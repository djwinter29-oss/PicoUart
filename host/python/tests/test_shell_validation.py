"""Regression tests for shell validation coverage and interruption policy."""

import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import time

import pytest

pytestmark = pytest.mark.skipif(sys.platform == "win32", reason="POSIX executable shims and signals")

# ponytail: signal.SIGHUP has no Windows equivalent, so the (signum, status) list
# must be built conditionally rather than referenced directly in the parametrize
# decorator. A literal `signal.SIGHUP` there raises AttributeError at module import
# time on Windows, crashing pytest collection before the skipif marker can apply.
# Ceiling: POSIX-only; if Windows ever needs signal coverage, add a parallel table
# keyed off sys.platform using Windows-specific signals (e.g. CTRL_C_EVENT).
SIGNAL_EXIT_CASES = (
    [(signal.SIGHUP, 129), (signal.SIGINT, 130), (signal.SIGTERM, 143)]
    if sys.platform != "win32"
    else []
)


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


@pytest.mark.parametrize("shell", ["sh", "dash", "bash"])
@pytest.mark.parametrize("phase", ["full", "fallback", "pytest"])
@pytest.mark.parametrize(("signum", "status"), SIGNAL_EXIT_CASES)
def test_host_runner_signal_exits_and_cleans_fallback(repo_root, tmp_path, shell, phase, signum, status):
    _assert_host_runner_signal_exit(repo_root, tmp_path, shell, phase, signum, status)


@pytest.mark.parametrize("shell", ["sh", "dash", "bash"])
@pytest.mark.parametrize("phase", ["full", "fallback", "pytest"])
def test_host_runner_sigint_without_gnu_env_exits_and_cleans_fallback(repo_root, tmp_path, shell, phase):
    _assert_host_runner_signal_exit(
        repo_root, tmp_path, shell, phase, signal.SIGINT, 130, force_env_fallback=True,
    )


def _assert_host_runner_signal_exit(repo_root, tmp_path, shell, phase, signum, status,
                                    force_env_fallback=False):
    if shutil.which(shell) is None:
        pytest.skip(f"{shell} is not installed")
    shim = tmp_path / "python-shim"
    log = tmp_path / "calls.jsonl"
    child_pid_file = tmp_path / "child.pid"
    env_probe_file = tmp_path / "env-probe.json"
    if force_env_fallback:
        env_shim = tmp_path / "env"
        env_shim.write_text(f"#!{sys.executable}\n" + '''import json, os, sys
from pathlib import Path
Path(os.environ["ENV_PROBE_FILE"]).write_text(json.dumps(sys.argv[1:]))
# Emulate BSD/MSYS env rejecting the GNU-only option, even on a GNU host.
sys.exit(1)
''')
        env_shim.chmod(0o700)
    shim.write_text(f"#!{sys.executable}\n" + '''import json, os, signal, sys, time
from pathlib import Path
args = sys.argv[1:]
if args == ["-m", "pip", "--version"]:
    sys.exit(0)
phase = "pytest"
record = {"args": args, "sigint_ignored": signal.getsignal(signal.SIGINT) == signal.SIG_IGN}
if "install" in args:
    lock = Path(args[args.index("-r") + 1])
    phase = "full" if lock.name == "requirements-lock.txt" else "fallback"
    record["path"] = str(lock)
record["phase"] = phase
with open(os.environ["CALL_LOG"], "a") as stream:
    stream.write(json.dumps(record) + "\\n")
if phase == os.environ["SIGNAL_PHASE"]:
    Path(os.environ["CHILD_PID_FILE"]).write_text(str(os.getpid()))
    while True:
        time.sleep(1)
sys.exit(1 if phase == "full" else 0)
''')
    shim.chmod(0o700)
    process = subprocess.Popen(
        [shell, str(repo_root / "tools/test/test-host.sh"), "--skip-c"],
        cwd=tmp_path,
        env={**os.environ, "PYTHON_EXE": str(shim), "CALL_LOG": str(log),
             "SIGNAL_PHASE": phase, "CHILD_PID_FILE": str(child_pid_file), "TMPDIR": str(tmp_path),
             "ENV_PROBE_FILE": str(env_probe_file),
             "PATH": f"{shim.parent}:{os.environ.get('PATH', '')}"},
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
    )
    try:
        deadline = time.monotonic() + 5
        while not child_pid_file.exists() and time.monotonic() < deadline:
            if process.poll() is not None:
                break
            time.sleep(0.01)
        assert child_pid_file.exists(), "the test shim did not start the blocked child"
        child_pid = int(child_pid_file.read_text())
        os.kill(child_pid, 0)
        if force_env_fallback:
            assert json.loads(env_probe_file.read_text()) == ["--default-signal=INT", "true"]
            calls = [json.loads(line) for line in log.read_text().splitlines()]
            assert calls[-1]["sigint_ignored"], "the regression must exercise inherited SIGINT-ignore"
        # Signal only the wrapper: forwarding must stop/reap its direct child
        # within the timeout. This does not test descendant/process-group cleanup
        # or commands that also ignore SIGTERM; neither has a bounded guarantee.
        os.kill(process.pid, signum)
        stdout, stderr = process.communicate(timeout=5)
        assert process.returncode == status, stdout + stderr
        with pytest.raises(ProcessLookupError):
            os.kill(child_pid, 0)
        calls = [json.loads(line) for line in log.read_text().splitlines()]
        assert [call["phase"] for call in calls] == {
            "full": ["full"], "fallback": ["full", "fallback"],
            "pytest": ["full", "fallback", "pytest"],
        }[phase]
        if phase == "full":
            assert "retrying" not in stderr
        for call in calls:
            if call["phase"] == "fallback":
                assert not Path(call["path"]).exists()
    finally:
        # Kill the child first so a failed wrapper can reap it before being killed.
        if child_pid_file.exists():
            try:
                os.kill(int(child_pid_file.read_text()), signal.SIGKILL)
            except ProcessLookupError:
                pass
        if process.poll() is None:
            try:
                process.communicate(timeout=2)
            except subprocess.TimeoutExpired:
                process.kill()
                process.communicate(timeout=5)


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
             "INSTALL_STATUS": str(status), "TMPDIR": str(tmp_path),
             "PATH": f"{shim.parent}:{os.environ.get('PATH', '')}"},
        capture_output=True, text=True, timeout=10,
    )
    assert completed.returncode == status
    assert len(log.read_text().splitlines()) == 1
    assert "retrying" not in completed.stderr
