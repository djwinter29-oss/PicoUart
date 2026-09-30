"""The optional hidapi fallback must preserve other hashed requirements."""

import re
import subprocess

import pytest


@pytest.mark.parametrize("keep_via", [True, False])
def test_hidapi_filter_preserves_other_packages_and_hashes(repo_root, keep_via):
    script = (repo_root / "tools/test/test-host.sh").read_text()
    program = re.search(r"awk '\n(.*?)\n\s*' \"\$LOCK_FILE\"", script, re.S)
    assert program is not None
    lock = (repo_root / "host/python/requirements-lock.txt").read_text()
    hid_block = re.search(r"^hidapi==.*?(?=^[A-Za-z0-9_.-]+==|\Z)", lock, re.M | re.S)
    assert hid_block is not None
    expected = lock[:hid_block.start()] + lock[hid_block.end():]
    if not keep_via:
        block = re.sub(r"^    # via.*\n", "", hid_block.group(), flags=re.M)
        lock = lock[:hid_block.start()] + block + lock[hid_block.end():]
    result = subprocess.run(["awk", program.group(1)], input=lock, text=True,
                            capture_output=True, check=True)
    assert result.stdout == expected
