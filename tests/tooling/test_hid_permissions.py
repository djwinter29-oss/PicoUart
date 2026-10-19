"""Linux permission helpers exercised with fake devices, sysfs and udev, never /etc."""

import json
import os
from pathlib import Path
import subprocess
import sys

import pytest


pytestmark = pytest.mark.skipif(sys.platform == "win32", reason="POSIX permission helpers")


@pytest.fixture
def permissions(repo_root, tmp_path):
    root = tmp_path / "permission fixture"
    scripts = root / "tools/host"
    scripts.mkdir(parents=True)
    dev = root / "dev"
    sysfs = root / "sys"
    rule = root / "rules/70-picouart-hid.rules"
    rule.parent.mkdir()
    for name in ("check", "setup"):
        source = (repo_root / f"tools/host/hid-permissions-{name}.sh").read_text()
        source = source.replace("/dev/hidraw*", f'"{dev}/hidraw"*')
        source = source.replace("/dev/bus/usb/*/*", f'"{dev}/bus/usb"/*/*')
        source = source.replace('sys_path="/sys$device_path"', f'sys_path="{sysfs}$device_path"')
        source = source.replace('RULE_TARGET="/etc/udev/rules.d/70-picouart-hid.rules"', f'RULE_TARGET="{rule}"')
        target = scripts / f"hid-permissions-{name}.sh"
        target.write_text(source)
        target.chmod(0o700)

    bin_dir = root / "bin"
    bin_dir.mkdir()
    commands = {
        "uname": '#!/bin/sh\necho "${FAKE_OS:-Linux}"\n',
        "id": '#!/bin/sh\ncase "$1" in -u) echo "${FAKE_UID:-0}";; -g) echo 4321;; *) exit 2;; esac\n',
        "sudo": f"#!{sys.executable}\n"
        "import json, os, sys\n"
        'with open(os.environ["SUDO_LOG"], "w") as log: json.dump(sys.argv[1:], log)\n',
        "udevadm": f"#!{sys.executable}\n"
        "import json, os, sys\n"
        "args = sys.argv[1:]\n"
        'with open(os.environ["UDEV_LOG"], "a") as log: log.write(json.dumps(args) + "\\n")\n'
        'if args[:2] == ["info", "--query=path"]:\n'
        '    print(json.loads(os.environ["DEVICE_PATHS"])[args[2].split("=", 1)[1]])\n'
        'sys.exit(int(os.environ.get("UDEV_STATUS", "0")))\n',
    }
    for name, content in commands.items():
        target = bin_dir / name
        target.write_text(content)
        target.chmod(0o700)
    env = {
        **os.environ, "PATH": f"{bin_dir}:{os.environ['PATH']}", "SUDO_USER": "tester",
        "UDEV_LOG": str(root / "udev.jsonl"), "SUDO_LOG": str(root / "sudo.json"),
        "DEVICE_PATHS": "{}",
    }
    return scripts, dev, sysfs, rule, env


def invoke(permissions, name, *args, **overrides):
    scripts, _, _, _, env = permissions
    return subprocess.run(
        [str(scripts / f"hid-permissions-{name}.sh"), *args], env={**env, **overrides},
        capture_output=True, text=True, timeout=10,
    )


def add_device(permissions, product="4010", vendor="cafe"):
    _, dev, sysfs, _, env = permissions
    parent = sysfs / "devices/usb/target"
    child = parent / "interface/hidraw/hidraw0"
    child.mkdir(parents=True)
    (parent / "idVendor").write_text(vendor + "\n")
    (parent / "idProduct").write_text(product + "\n")
    hidraw = dev / "hidraw0"
    usb = dev / "bus/usb/001/002"
    usb.parent.mkdir(parents=True)
    hidraw.touch()
    usb.touch()
    env["DEVICE_PATHS"] = json.dumps({str(hidraw): "/devices/usb/target/interface/hidraw/hidraw0",
                                    str(usb): "/devices/usb/target"})
    return hidraw, usb


@pytest.mark.parametrize("name", ["check", "setup"])
def test_help_and_non_linux_need_no_privilege_or_udev(permissions, name):
    result = invoke(permissions, name, "--help")
    assert result.returncode == 0
    assert "Usage:" in result.stdout
    assert invoke(permissions, name, FAKE_OS="Darwin").returncode == 0
    assert not permissions[3].exists()
    assert not Path(permissions[4]["UDEV_LOG"]).exists()
    assert invoke(permissions, name, "--unknown").returncode == 2
    assert invoke(permissions, name, "", "unexpected").returncode == 2


def test_no_device_or_accessible_device_does_not_block(permissions):
    assert invoke(permissions, "check").returncode == 0
    add_device(permissions)
    assert invoke(permissions, "check").returncode == 0


@pytest.mark.skipif(sys.platform != "win32" and os.geteuid() == 0, reason="root bypasses file access bits")
@pytest.mark.parametrize("node", [0, 1], ids=["hidraw", "usb"])
def test_denied_matching_nodes_are_reported_including_hidraw_ancestor(permissions, node):
    nodes = add_device(permissions)
    nodes[node].chmod(0o400)
    result = invoke(permissions, "check")
    assert result.returncode == 1
    assert str(nodes[node]) in result.stderr
    assert "hid-permissions-setup.sh" in result.stderr


@pytest.mark.parametrize("vendor,product", [("cafe", "4011"), ("2e8a", "000c"), ("1234", "4010")])
def test_unrelated_devices_are_ignored(permissions, vendor, product):
    for node in add_device(permissions, product, vendor):
        node.chmod(0o000)
    assert invoke(permissions, "check").returncode == 0


def test_failed_device_inspection_is_not_silently_ignored(permissions):
    add_device(permissions)
    result = invoke(permissions, "check", UDEV_STATUS="7")
    assert result.returncode == 1
    assert "Cannot inspect" in result.stderr


def test_setup_installs_scoped_rules_idempotently_and_reloads(permissions):
    result = invoke(permissions, "setup")
    assert result.returncode == 0, result.stderr
    rule = permissions[3]
    content = rule.read_text()
    assert len(content.splitlines()) == 2
    assert 'ENV{DEVTYPE}=="usb_device"' in content
    assert 'SUBSYSTEM=="hidraw"' in content
    assert content.count('"cafe"') == content.count('"4010"') == 2
    assert content.count('GROUP="4321"') == content.count('MODE="0660"') == 2
    assert content.count('TAG+="uaccess"') == 2
    assert rule.stat().st_mode & 0o777 == 0o644
    mtime = rule.stat().st_mtime_ns
    assert invoke(permissions, "setup").returncode == 0
    assert rule.stat().st_mtime_ns == mtime
    commands = [json.loads(line) for line in Path(permissions[4]["UDEV_LOG"]).read_text().splitlines()]
    assert commands[:4] == [
        ["control", "--reload-rules"],
        ["trigger", "--subsystem-match=usb", "--attr-match=idVendor=cafe", "--attr-match=idProduct=4010"],
        ["trigger", "--subsystem-match=hidraw"], ["settle"],
    ]


def test_setup_preserves_custom_rules_and_symlinks(permissions):
    rule = permissions[3]
    rule.write_text("custom rule\n")
    assert invoke(permissions, "setup").returncode == 1
    assert rule.read_text() == "custom rule\n"
    rule.unlink()
    destination = rule.parent / "missing"
    rule.symlink_to(destination)
    assert invoke(permissions, "setup").returncode == 1
    assert rule.is_symlink()
    assert not destination.exists()
    assert not Path(permissions[4]["UDEV_LOG"]).exists()


def test_setup_escalates_only_when_needed_and_requires_an_invoking_account(permissions):
    assert invoke(permissions, "setup", FAKE_UID="1001").returncode == 0
    assert json.loads(Path(permissions[4]["SUDO_LOG"]).read_text()) == [
        str(permissions[0] / "hid-permissions-setup.sh"),
    ]
    for user in ("", "root"):
        result = invoke(permissions, "setup", SUDO_USER=user)
        assert result.returncode == 1
        assert "non-root account" in result.stderr
    assert not permissions[3].exists()


def test_setup_propagates_udev_failure(permissions):
    result = invoke(permissions, "setup", UDEV_STATUS="9")
    assert result.returncode == 9
    assert "permissions applied" not in result.stdout
