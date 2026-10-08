"""Regression tests for shell validation coverage and interruption policy."""

import json
import os
from pathlib import Path
import shlex
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
    [(signal.SIGHUP, 129), (signal.SIGINT, 130), (signal.SIGTERM, 143)] if sys.platform != "win32" else []
)


def test_host_smoke_uses_selected_python_without_hil_venv(repo_root, tmp_path):
    script_path = tmp_path / "tools/validation/smoke-host-tools.sh"
    script_path.parent.mkdir(parents=True)
    shutil.copyfile(repo_root / "tools/validation/smoke-host-tools.sh", script_path)
    (tmp_path / "sitecustomize.py").write_text("import sys\nsys.modules['serial'] = None\n", encoding="utf-8")
    for source_path in ("host/python/src", "tools/hil/src"):
        destination = tmp_path / source_path
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.symlink_to(repo_root / source_path, target_is_directory=True)

    completed = subprocess.run(
        ["sh", str(script_path)],
        cwd=tmp_path,
        env={**os.environ, "PYTHON_EXE": sys.executable, "PYTHONPATH": str(tmp_path)},
        capture_output=True,
        text=True,
    )

    assert not (tmp_path / "tools/hil/.venv").exists()
    assert completed.returncode == 0, completed.stdout + completed.stderr
    assert "hidapi OK" in completed.stdout


def test_static_analysis_covers_every_firmware_source_from_any_cwd(repo_root, tmp_path):
    shim = tmp_path / "cppcheck"
    log = tmp_path / "cppcheck.json"
    shim.write_text(
        f"#!{sys.executable}\n"
        + """import json, os, sys
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
"""
    )
    shim.chmod(0o700)
    env = {
        **os.environ,
        "PATH": str(tmp_path) + os.pathsep + os.environ["PATH"],
        "ANALYSIS_LOG": str(log),
        "ANALYSIS_STATUS": "0",
    }
    command = ["sh", str(repo_root / "tools/validation/static-analyze.sh")]
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
        repo_root,
        tmp_path,
        shell,
        phase,
        signal.SIGINT,
        130,
        force_env_fallback=True,
    )


def _assert_host_runner_signal_exit(repo_root, tmp_path, shell, phase, signum, status, force_env_fallback=False):
    if shutil.which(shell) is None:
        pytest.skip(f"{shell} is not installed")
    shim = tmp_path / "python-shim"
    log = tmp_path / "calls.jsonl"
    child_pid_file = tmp_path / "child.pid"
    env_probe_file = tmp_path / "env-probe.json"
    if force_env_fallback:
        env_shim = tmp_path / "env"
        env_shim.write_text(
            f"#!{sys.executable}\n"
            + """import json, os, sys
from pathlib import Path
Path(os.environ["ENV_PROBE_FILE"]).write_text(json.dumps(sys.argv[1:]))
# Emulate BSD/MSYS env rejecting the GNU-only option, even on a GNU host.
sys.exit(1)
"""
        )
        env_shim.chmod(0o700)
    shim.write_text(
        f"#!{sys.executable}\n"
        + """import json, os, signal, sys, time
from pathlib import Path
args = sys.argv[1:]
if args == ["-m", "pip", "--version"]:
    sys.exit(0)
if args and args[0].endswith("filter_lock_exclude.py"):
    os.execv(sys.executable, [sys.executable, *args])
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
"""
    )
    shim.chmod(0o700)
    process = subprocess.Popen(
        [shell, str(repo_root / "tools/validation/run-host-tests.sh"), "--skip-c"],
        cwd=tmp_path,
        env={
            **os.environ,
            "PYTHON_EXE": str(shim),
            "CALL_LOG": str(log),
            "SIGNAL_PHASE": phase,
            "CHILD_PID_FILE": str(child_pid_file),
            "TMPDIR": str(tmp_path),
            "ENV_PROBE_FILE": str(env_probe_file),
            "PATH": f"{shim.parent}:{os.environ.get('PATH', '')}",
        },
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
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
            "full": ["full"],
            "fallback": ["full", "fallback"],
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


@pytest.mark.parametrize(
    ("phase", "signal_name", "status"),
    [("pre_launch", "TERM", 143), ("pre_publication", "HUP", 129)],
)
def test_host_runner_launch_race_reaps_child(repo_root, tmp_path, phase, signal_name, status):
    """Exercise deferred signal forwarding in both child-launch handoff windows.

        * "pre_launch": the TERM handler runs after LAUNCH_IN_PROGRESS=1, before
            the command is backgrounded (no child or '$!' yet).
        * "pre_publication": the HUP handler runs after '&' but before
            CURRENT_CHILD_PID is published. OS-delivered signals are tested by the
            separate signal-forwarding cases below.

    In both windows forward_signal must defer the signal (PENDING_SIGNAL_NAME)
    rather than read '$!' directly, since in the pre_launch window '$!' may be
    stale/empty. run_interruptible must still launch the command, publish
    CURRENT_CHILD_PID, and then forward the deferred signal to the real child,
    which must be reaped rather than leaked as an orphan.

    Deterministic injection happens here, via a temporary instrumented copy
    of run-host-tests.sh built by string replacement only; the production script
    carries no test-controlled bypass that would let an environment variable
    make it run arbitrary caller-supplied command args. The copy is instead
    made to invoke the real run_interruptible/forward_signal code early,
    before its normal argument-parsing loop, by replacing that loop's marker
    line with a direct call.
    """
    original = (repo_root / "tools/validation/run-host-tests.sh").read_text()
    child_pid_file = tmp_path / "child.pid"

    # Always record the real child PID right after it is backgrounded (this is
    # the only point where it is recorded, regardless of phase), so that
    # either injection site can be verified to have actually launched and
    # later reaped the same child.
    publish_marker = '        "$@" &\n    fi\n    CURRENT_CHILD_PID=$!\n'
    assert original.count(publish_marker) == 1, "publication marker not found; run-host-tests.sh changed shape"
    publish_injected = f'        "$@" &\n    fi\n    echo "$!" > {shlex.quote(str(child_pid_file))}\n'
    if phase == "pre_publication":
        # Inject the trap handler at the exact race window after '$!' names
        # the child but before CURRENT_CHILD_PID is published.
        publish_injected += f"    forward_signal {signal_name} {status}\n"
    publish_injected += "    CURRENT_CHILD_PID=$!\n"
    instrumented = original.replace(publish_marker, publish_injected)

    if phase == "pre_launch":
        # Inject the trap handler before the command is backgrounded, when no
        # child or '$!' exists yet.
        launch_marker = "    LAUNCH_IN_PROGRESS=1\n"
        assert instrumented.count(launch_marker) == 1, "launch marker not found; run-host-tests.sh changed shape"
        instrumented = instrumented.replace(
            launch_marker,
            launch_marker + f"    forward_signal {signal_name} {status}\n",
        )

    # Invoke the real run_interruptible directly, ahead of the normal
    # argument-parsing loop, instead of relying on any selftest bypass in the
    # production script.
    loop_marker = 'while [ "$#" -gt 0 ]; do'
    assert instrumented.count(loop_marker) == 1, (
        "argument-parsing loop marker not found; run-host-tests.sh changed shape"
    )
    loop_injected = f'run_interruptible true\nexit "$?"\n{loop_marker}'
    instrumented = instrumented.replace(loop_marker, loop_injected)

    instrumented_path = tmp_path / "test-host-instrumented.sh"
    instrumented_path.write_text(instrumented)
    instrumented_path.chmod(0o700)

    completed = subprocess.run(
        ["sh", str(instrumented_path)],
        env={**os.environ},
        capture_output=True,
        text=True,
        timeout=10,
    )
    assert completed.returncode == status, completed.stdout + completed.stderr
    # The command must still be launched in both windows (deferred signals do
    # not skip the launch), and its PID is recorded exactly once, at the real
    # publication site, never from a stale/empty '$!' read during injection.
    assert child_pid_file.exists(), "the command was never launched"
    child_pid = int(child_pid_file.read_text())
    with pytest.raises(ProcessLookupError):
        os.kill(child_pid, 0)


@pytest.mark.parametrize("status", [129, 130, 143])
def test_interrupted_initial_pip_is_not_retried(repo_root, tmp_path, status):
    shim = tmp_path / "python-shim"
    log = tmp_path / "calls.txt"
    shim.write_text(
        f"#!{sys.executable}\n"
        + """import os, sys
from pathlib import Path
args = sys.argv[1:]
if args == ["-m", "pip", "--version"]:
    sys.exit(0)
with open(os.environ["CALL_LOG"], "a") as stream:
    stream.write(" ".join(args) + "\\n")
sys.exit(int(os.environ["INSTALL_STATUS"]))
"""
    )
    shim.chmod(0o700)
    completed = subprocess.run(
        ["sh", str(repo_root / "tools/validation/run-host-tests.sh"), "--skip-c"],
        cwd=repo_root,
        env={
            **os.environ,
            "PYTHON_EXE": str(shim),
            "CALL_LOG": str(log),
            "INSTALL_STATUS": str(status),
            "TMPDIR": str(tmp_path),
            "PATH": f"{shim.parent}:{os.environ.get('PATH', '')}",
        },
        capture_output=True,
        text=True,
        timeout=10,
    )
    assert completed.returncode == status
    assert len(log.read_text().splitlines()) == 1
    assert "retrying" not in completed.stderr
