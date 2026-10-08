"""The optional hidapi fallback must preserve other hashed requirements."""

import json
import os
import subprocess
import sys

import pytest

FILTER_SCRIPT = "tools/validation/filter_lock_exclude.py"


def _run_filter(repo_root, lock_text: str, exclude: str) -> str:
    script_path = repo_root / FILTER_SCRIPT
    result = subprocess.run(
        [sys.executable, str(script_path), "--exclude", exclude, "-"],
        input=lock_text, text=True, capture_output=True, check=True,
    )
    return result.stdout


def test_python_filter_is_used_by_host_test_runner(repo_root):
    assert (repo_root / FILTER_SCRIPT).is_file()
    script = (repo_root / "tools/validation/run-host-tests.sh").read_text()
    assert "filter_lock_exclude.py" in script


def test_hidapi_filter_drops_hidapi_and_its_only_dependent_setuptools(repo_root):
    lock = (repo_root / "host/python/requirements-lock.txt").read_text()
    filtered = _run_filter(repo_root, lock, "hidapi")

    assert "\nhidapi==" not in ("\n" + filtered)
    assert "\nsetuptools==" not in ("\n" + filtered)
    # via-hidapi comment must not survive as a dangling reference either.
    assert "# via hidapi" not in filtered

    original_count = lock.count("# via")
    filtered_count = filtered.count("# via")
    assert filtered_count == original_count - 2  # hidapi + setuptools removed

    # Every other locked package (and its hashes) must be preserved verbatim.
    for package in ("pytest==8.3.5", "iniconfig==2.3.0", "pluggy==1.6.0",
                     "packaging==26.3", "exceptiongroup==1.3.1",
                     "tomli==2.4.1", "typing-extensions==4.16.0",
                     "colorama==0.4.6"):
        assert package in filtered
        assert package in lock  # sanity: package really exists upstream


@pytest.mark.parametrize("hashes", [True, False])
def test_filter_preserves_package_with_a_still_kept_parent(repo_root, hashes):
    """A package pulled in by *both* an excluded and a kept parent must survive."""
    hash_line = "    --hash=sha256:deadbeef\n" if hashes else ""
    lock = (
        "# preamble\n"
        "hidapi==0.14.0 \\\n"
        "    --hash=sha256:aaaa\n"
        "    # via -r host/python/requirements.txt\n"
        "shared-dep==1.0.0 \\\n"
        f"{hash_line}"
        "    # via hidapi\n"
        "    # via pytest\n"
        "pytest==8.3.5 \\\n"
        "    --hash=sha256:bbbb\n"
        "    # via -r host/python/requirements-dev.txt\n"
    )

    filtered = _run_filter(repo_root, lock, "hidapi")

    assert "hidapi==" not in filtered
    assert "shared-dep==1.0.0" in filtered  # kept: pytest still needs it
    assert "pytest==8.3.5" in filtered
    assert "# via hidapi" not in filtered
    assert "# via pytest" in filtered


def test_filter_drops_only_transitively_hidapi_only_packages(repo_root):
    """A package solely required by the excluded package must be dropped too."""
    lock = (
        "# preamble\n"
        "hidapi==0.14.0 \\\n"
        "    --hash=sha256:aaaa\n"
        "    # via -r host/python/requirements.txt\n"
        "setuptools==84.0.0 \\\n"
        "    --hash=sha256:bbbb\n"
        "    # via hidapi\n"
        "pytest==8.3.5 \\\n"
        "    --hash=sha256:cccc\n"
        "    # via -r host/python/requirements-dev.txt\n"
    )

    filtered = _run_filter(repo_root, lock, "hidapi")

    assert "hidapi==" not in filtered
    assert "setuptools==" not in filtered
    assert "pytest==8.3.5" in filtered


@pytest.mark.parametrize("fallback_fails", [False, True])
def test_shell_fallback_preserves_failure_and_cleans_lock(repo_root, tmp_path, fallback_fails):
    """Exercise the shell policy, not pip: any first failure retries once."""
    shim = tmp_path / "python-shim"
    log = tmp_path / "calls.jsonl"
    shim.write_text(f"#!{sys.executable}\n" + '''import json, os, sys
from pathlib import Path
args = sys.argv[1:]
if args == ["-m", "pip", "--version"]:
    sys.exit(0)
if args and args[0].endswith("filter_lock_exclude.py"):
    os.execv(sys.executable, [sys.executable, *args])
record = {"args": args}
if "-r" in args:
    lock = Path(args[args.index("-r") + 1])
    record.update(path=str(lock), text=lock.read_text())
with open(os.environ["CALL_LOG"], "a") as stream:
    stream.write(json.dumps(record) + "\\n")
if "install" in args:
    if lock.name == "requirements-lock.txt":
        print("unrelated package/network failure", file=sys.stderr)
        sys.exit(1)
    sys.exit(int(os.environ["FALLBACK_FAILS"]))
sys.exit(0)
''')
    shim.chmod(0o700)
    result = subprocess.run(
        ["sh", str(repo_root / "tools/validation/run-host-tests.sh"), "--skip-c"],
        env={**os.environ, "PYTHON_EXE": str(shim), "CALL_LOG": str(log),
             "FALLBACK_FAILS": str(int(fallback_fails)), "TMPDIR": str(tmp_path)},
        capture_output=True, text=True,
    )
    calls = [json.loads(line) for line in log.read_text().splitlines()]
    installs = [call for call in calls if "install" in call["args"]]
    assert len(installs) == 2
    assert all("--require-hashes" in call["args"] for call in installs)
    filtered = installs[1]
    assert "hidapi==" not in filtered["text"]
    assert "setuptools==" not in filtered["text"]
    assert not os.path.exists(filtered["path"])
    assert result.returncode == int(fallback_fails)
    assert any("pytest" in call["args"] for call in calls) is not fallback_fails
    assert "unrelated package/network failure" in result.stderr
