"""Offline configuration provisioner checks: no OpenOCD or physical target is contacted."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import shutil
import struct
import subprocess
import zlib

import pytest


TOOL = Path(__file__).resolve().parents[2] / "tools/firmware/config.py"
spec = importlib.util.spec_from_file_location("flash_config_under_test", TOOL)
assert spec is not None and spec.loader is not None
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


@pytest.fixture
def build_dir(tmp_path):
    (tmp_path / "config-layout.json").write_text(json.dumps({
        "version": 1,
        "flash_size": 2097152,
        "sector_offset": 2093056,
        "sector_size": 4096,
        "target_family": "rp2040",
    }))
    return tmp_path


def arguments(build_dir, *command):
    return ["--build-dir", str(build_dir), "--probe-serial", "explicit-probe", *command]


@pytest.mark.parametrize("profile,number", [("default", 0), ("150mhz", 1), ("200mhz", 2),
                                          ("250mhz", 3), ("280mhz", 4)])
def test_exact_wire_contract(profile, number):
    sector = tool.encode_sector(profile)
    assert len(sector) == 4096
    assert sector[:12] == b"PUCF\x01\x00\x20\x00\x24\x00\x00\x00"
    assert sector[16:32] == bytes(16)
    assert sector[32:36] == struct.pack("<I", number)
    assert sector[36:] == b"\xff" * 4060
    assert struct.unpack_from("<I", sector, 12)[0] == zlib.crc32(sector[:12] + sector[16:36])
    family = "rp2350" if number == 4 else "rp2040"
    assert tool.decode_sector(sector, family)["clock_profile"] == profile


@pytest.mark.parametrize("offset", [0, 4, 6, 8, 12, 16, 32, 36, 4095])
def test_corruption_is_rejected(offset):
    sector = bytearray(tool.encode_sector("default"))
    sector[offset] ^= 1
    with pytest.raises(ValueError):
        tool.decode_sector(sector, "rp2040")


@pytest.mark.parametrize("profile", ["150mhz", "200mhz", "250mhz"])
@pytest.mark.parametrize("family", ["rp2040", "rp2350"])
def test_common_profiles_require_acknowledgement_and_decode_on_both_targets(profile, family):
    with pytest.raises(ValueError, match="unsafe-overclock"):
        tool.check_profile(profile, family, False)
    tool.check_profile(profile, family, True)
    assert tool.decode_sector(tool.encode_sector(profile), family)["clock_profile"] == profile


def test_unknown_and_wrong_family_profiles_rejected():
    sector = bytearray(tool.encode_sector("default"))
    struct.pack_into("<I", sector, 32, 5)
    struct.pack_into("<I", sector, 12, zlib.crc32(sector[:12] + sector[16:36]))
    with pytest.raises(ValueError, match="unknown"):
        tool.decode_sector(sector, "rp2040")
    with pytest.raises(ValueError, match="not supported"):
        tool.decode_sector(tool.encode_sector("280mhz"), "rp2040")
    with pytest.raises(ValueError, match="complete"):
        tool.decode_sector(b"\xff" * 36, "rp2040")


@pytest.mark.parametrize("key,value", [
    ("version", 2), ("version", True), ("flash_size", 1048576), ("flash_size", "2097152"),
    ("sector_offset", 0), ("sector_size", 256), ("target_family", "pico"), ("flash_size", None),
])
def test_bad_layout_never_contacts_probe(build_dir, monkeypatch, key, value):
    layout_file = build_dir / "config-layout.json"
    layout = json.loads(layout_file.read_text())
    layout[key] = value
    layout_file.write_text(json.dumps(layout))
    monkeypatch.setattr(tool.subprocess, "run", lambda *a, **kw: pytest.fail("probe contacted"))
    assert tool.main(arguments(build_dir, "erase")) == 1


@pytest.mark.parametrize("contents", ["[]", "{broken", "{}"])
def test_malformed_layout(build_dir, contents):
    (build_dir / "config-layout.json").write_text(contents)
    with pytest.raises(ValueError):
        tool.load_layout(build_dir)


def test_missing_layout(build_dir, monkeypatch):
    (build_dir / "config-layout.json").unlink()
    monkeypatch.setattr(tool.subprocess, "run", lambda *a, **kw: pytest.fail("probe contacted"))
    assert tool.main(arguments(build_dir, "read")) == 1


@pytest.mark.parametrize("profile,unsafe", [("250mhz", False), ("280mhz", False), ("280mhz", True),
                                          ("200mhz", False), ("150mhz", False)])
def test_unsafe_or_wrong_family_never_contacts_probe(build_dir, monkeypatch, profile, unsafe):
    monkeypatch.setattr(tool.subprocess, "run", lambda *a, **kw: pytest.fail("probe contacted"))
    command = ["set-clock", profile] + (["--unsafe-overclock"] if unsafe else [])
    assert tool.main(arguments(build_dir, *command)) == 1


@pytest.mark.parametrize("command,expected", [
    (["set-clock", "default"], "default"),
    (["set-clock", "250mhz", "--unsafe-overclock"], "250mhz"),
    (["erase"], "default"),
    (["read"], "default"),
])
def test_successful_operations(build_dir, monkeypatch, capsys, command, expected):
    calls = []

    def openocd(argv, **kwargs):
        calls.append(argv)
        assert kwargs == {"capture_output": True, "text": True, "check": False}
        assert argv[0] == "chosen-openocd"
        assert argv[argv.index("-f") + 1] == "interface/cmsis-dap.cfg"
        assert "target/rp2040.cfg" in argv
        assert "adapter serial \"explicit-probe\"" in argv
        assert "adapter speed 1000" in argv
        if len(calls) == 1:
            script = argv[-1]
            assert script.index("targets rp2040.core0\nhalt") < script.index("flash probe 0")
            assert script.index("targets rp2040.core1\nhalt") < script.index("flash probe 0")
            assert script.index("flash probe 0") < script.index("array set bank [lindex [flash list] 0]")
            assert "$bank(size) != 2097152" in script
            assert "$bank(base) != 268435456" in script
            if command[0] == "read":
                assert "erase_address" not in script and "write_image" not in script
                assert "pre_shutdown_commands" in script
                assert "resume" in script
            else:
                assert script.index("array set bank") < script.index("flash erase_address 0x101ff000 4096")
                assert "reset" not in script
            images = list(build_dir.glob(".config-*.page.bin"))
            assert len(images) == 1 and images[0].is_absolute()
            page = images[0].read_bytes()
            assert len(page) == 256
            if command[0] == "set-clock":
                assert "flash write_image" in script
                assert page == tool.encode_sector(expected)[:256]
            else:
                assert "flash write_image" not in script
            sector = tool.encode_sector(expected) if command[0] == "set-clock" else b"\xff" * 4096
            next(build_dir.glob(".config-*.readback.bin")).write_bytes(sector)
        else:
            assert argv[-1] == "init; reset run; shutdown"
        return subprocess.CompletedProcess(argv, 0, stdout="", stderr="")

    monkeypatch.setattr(tool.subprocess, "run", openocd)
    options = ["--openocd-exe", "chosen-openocd", "--adapter-speed-khz", "1000"]
    assert tool.main([*options, *arguments(build_dir, *command)]) == 0
    metadata = json.loads(capsys.readouterr().out)
    assert metadata["clock_profile"] == expected
    assert set(metadata) <= {"target_family", "sector_offset", "clock_profile", "state", "version"}
    assert len(calls) == (1 if command[0] == "read" else 2)
    assert not list(build_dir.glob(".config-*"))


def test_rp2350_layout_and_target_names(build_dir):
    layout = tool.load_layout(build_dir)
    layout.update(target_family="rp2350", flash_size=4194304, sector_offset=4190208)
    (build_dir / "config-layout.json").write_text(json.dumps(layout))
    assert tool.load_layout(build_dir) == layout
    tool.check_profile("280mhz", "rp2350", True)
    tool.check_profile("250mhz", "rp2350", True)
    with pytest.raises(ValueError, match="not supported"):
        tool.check_profile("280mhz", "rp2040", True)
    script = tool.operation_script(layout, "erase", build_dir / "page", build_dir / "readback")
    assert "targets rp2350.cm0\nhalt" in script and "targets rp2350.cm1\nhalt" in script
    assert "flash erase_address 0x103ff000 4096" in script


@pytest.mark.parametrize("failure", ["openocd", "missing-exe", "interrupt", "crc", "short", "different"])
def test_failed_mutation_never_resets(build_dir, monkeypatch, capsys, failure):
    calls = []

    def fail(argv, **kwargs):
        calls.append(argv)
        if failure == "openocd":
            return subprocess.CompletedProcess(argv, 1, stdout="", stderr="bank mismatch")
        if failure == "missing-exe":
            raise FileNotFoundError("no OpenOCD")
        if failure == "interrupt":
            raise KeyboardInterrupt
        data = bytearray(tool.encode_sector("default"))
        if failure == "crc":
            data[12] ^= 1
        elif failure == "short":
            data = data[:128]
        elif failure == "different":
            data = tool.encode_sector("250mhz")
        next(build_dir.glob(".config-*.readback.bin")).write_bytes(data)
        return subprocess.CompletedProcess(argv, 0, stdout="", stderr="")

    monkeypatch.setattr(tool.subprocess, "run", fail)
    assert tool.main(arguments(build_dir, "set-clock", "default")) == (130 if failure == "interrupt" else 1)
    assert len(calls) == 1
    assert "reset run" not in calls[0][-1]
    assert not list(build_dir.glob(".config-*"))
    assert "Error:" in capsys.readouterr().err


@pytest.mark.parametrize("options", [
    ["--build-dir", "build", "read"],
    ["--probe-serial", "serial", "read"],
    ["--build-dir", "build", "--probe-serial", "serial", "--adapter-speed-khz", "0", "read"],
])
def test_required_selection_and_positive_speed(options):
    with pytest.raises(SystemExit) as exc:
        tool.main(options)
    assert exc.value.code == 2


@pytest.mark.parametrize("serial", ["", " ", "nul\x00serial"])
def test_empty_or_invalid_probe_serial(build_dir, monkeypatch, serial):
    monkeypatch.setattr(tool.subprocess, "run", lambda *a, **kw: pytest.fail("probe contacted"))
    assert tool.main(["--build-dir", str(build_dir), "--probe-serial", serial, "erase"]) == 1


@pytest.mark.parametrize("value", ['a"; error injected; #', r"C:\path\{x}\[cmd]$var", "a\nb\r\t\\", "{unbalanced"])
def test_tcl_values_round_trip_without_substitution(value):
    tclsh = shutil.which("tclsh")
    if not tclsh:
        pytest.skip("offline Tcl interpreter unavailable")
    script = f"set value {tool.tcl_quote(value)}\nbinary scan $value H* encoded\nputs $encoded\n"
    result = subprocess.run([tclsh], input=script, capture_output=True, text=True, check=True)
    assert not result.stderr
    assert bytes.fromhex(result.stdout.strip()).decode() == value


@pytest.mark.parametrize("command,wrong_size,fail_at", [
    ("erase", False, ""), ("erase", True, ""), ("erase", False, "write"),
    ("read", True, ""), ("read", False, "dump"), ("read", False, "halt"),
])
def test_offline_tcl_safety_paths(build_dir, command, wrong_size, fail_at):
    """Execute the generated Tcl only against stubs, proving bank checks gate destructive calls."""
    tclsh = shutil.which("tclsh")
    if not tclsh:
        pytest.skip("offline Tcl interpreter unavailable")
    layout = tool.load_layout(build_dir)
    script = tool.operation_script(layout, command, build_dir / "page", build_dir / "readback")
    prelude = """
set current ""
set halted0 0
set halted1 0
set pre_shutdown_commands {}
proc init {} {}
proc targets {name} { global current; set current $name }
proc halt {} {
    global current halted0 halted1 fail_at
    if {$current eq "rp2040.core0"} {set halted0 1} else {set halted1 1}
    if {$fail_at eq "halt"} {error "injected halt failure"}
}
proc wait_halt {timeout} {}
proc rp2040.core0 {command} { global halted0; if {$halted0} {return halted}; return running }
proc rp2040.core1 {command} { global halted1; if {$halted1} {return halted}; return running }
proc flash {command args} {
    global halted0 halted1 size fail_at
    if {!$halted0 || !$halted1} {error "both cores must be halted"}
    if {$command eq "list"} {return [list [list name rp2040.flash base 268435456 size $size]]}
    puts "FLASH $command $args"
    if {$command eq "erase_address" && $fail_at eq "write"} {error "interrupted mutation"}
}
proc dump_image {args} {
    global fail_at
    if {$fail_at eq "dump"} {error "interrupted dump"}
    puts DUMP
}
proc resume {} {
    global current halted0 halted1
    puts "RESUME $current"
    if {$current eq "rp2040.core0"} {set halted0 0} else {set halted1 0}
}
proc echo {message} { puts $message }
proc shutdown {args} {
    global pre_shutdown_commands
    foreach command $pre_shutdown_commands {uplevel #0 $command}
    puts "SHUTDOWN $args"
    if {$args eq "error"} {exit 1}
    exit 0
}
"""
    settings = f"set size {1048576 if wrong_size else 2097152}\nset fail_at {tool.tcl_quote(fail_at)}\n"
    result = subprocess.run([tclsh], input=prelude + settings + script, capture_output=True, text=True)
    assert not result.stderr
    assert result.returncode == (1 if wrong_size or fail_at else 0)
    if wrong_size:
        assert "FLASH erase_address" not in result.stdout
    if command == "read":
        assert "RESUME rp2040.core0" in result.stdout
        assert "RESUME rp2040.core1" in result.stdout
        assert "FLASH erase_address" not in result.stdout
    else:
        assert "RESUME" not in result.stdout
    assert "reset" not in script
