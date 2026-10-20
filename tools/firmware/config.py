#!/usr/bin/env python3
"""Provision the build-declared, final flash configuration sector over CMSIS-DAP SWD."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib


FLASH_BASE = 0x10000000
SECTOR_SIZE = 4096
PAGE_SIZE = 256
RECORD_SIZE = 36
PROFILES = {"default": 0, "250mhz": 1, "280mhz": 2}


def load_layout(build_dir: Path) -> dict:
    """Require explicit build metadata; never infer capacity from the board name."""
    layout = json.loads((build_dir / "config-layout.json").read_text(encoding="utf-8"))
    if not isinstance(layout, dict):
        raise ValueError("config-layout.json must contain an object")
    for key in ("version", "flash_size", "sector_offset", "sector_size"):
        if type(layout.get(key)) is not int:
            raise ValueError(f"config-layout.json: {key} must be an integer")
    if (
        layout["version"] != 1
        or layout["flash_size"] not in (2097152, 4194304)
        or layout["sector_size"] != SECTOR_SIZE
        or layout["sector_offset"] != layout["flash_size"] - SECTOR_SIZE
        or layout.get("target_family") not in ("rp2040", "rp2350")
    ):
        raise ValueError("unsupported or inconsistent config-layout.json")
    return layout


def check_profile(profile: str, family: str, unsafe: bool) -> None:
    if profile != "default":
        if profile != {"rp2040": "250mhz", "rp2350": "280mhz"}[family]:
            raise ValueError(f"{profile} is not supported on {family}")
        if not unsafe:
            raise ValueError("non-default clocks require --unsafe-overclock")


def encode_sector(profile: str) -> bytes:
    """PUCF v1: fixed LE header, reserved zeros, clock enum, then erased space (not KV storage)."""
    record = bytearray(struct.pack("<4sHHII16sI", b"PUCF", 1, 32, RECORD_SIZE, 0, bytes(16), PROFILES[profile]))
    struct.pack_into("<I", record, 12, zlib.crc32(record[:12] + record[16:]))
    return bytes(record) + b"\xff" * (SECTOR_SIZE - RECORD_SIZE)


def decode_sector(sector: bytes, family: str) -> dict:
    """Validate the entire sector, returning metadata only, never reserved/raw contents."""
    if len(sector) != SECTOR_SIZE:
        raise ValueError("readback is not one complete 4096-byte sector")
    if sector == b"\xff" * SECTOR_SIZE:
        return {"state": "erased", "clock_profile": "default"}
    magic, version, header, used, crc, reserved, profile = struct.unpack("<4sHHII16sI", sector[:RECORD_SIZE])
    if (magic, version, header, used) != (b"PUCF", 1, 32, RECORD_SIZE):
        raise ValueError("invalid PUCF configuration header")
    if crc != zlib.crc32(sector[:12] + sector[16:RECORD_SIZE]):
        raise ValueError("configuration CRC-32 mismatch")
    if reserved != bytes(16) or sector[RECORD_SIZE:] != b"\xff" * (SECTOR_SIZE - RECORD_SIZE):
        raise ValueError("configuration reserved space is not zero/erased")
    names = {value: name for name, value in PROFILES.items()}
    if profile not in names:
        raise ValueError("unknown configuration clock profile")
    check_profile(names[profile], family, unsafe=True)
    return {"state": "valid", "version": version, "clock_profile": names[profile]}


def tcl_quote(value: str) -> str:
    """Quote one Tcl word, including substitution characters and Windows path separators."""
    if "\x00" in value:
        raise ValueError("NUL is not allowed in Tcl arguments")
    escaped = value.replace("\\", "\\\\")
    for char in ('"', "$", "[", "]"):
        escaped = escaped.replace(char, "\\" + char)
    return '"' + escaped.replace("\r", "\\r").replace("\n", "\\n").replace("\t", "\\t") + '"'


def operation_script(layout: dict, command: str, image: Path, readback: Path) -> str:
    family = layout["target_family"]
    cores = [f"{family}.{name}" for name in (("core0", "core1") if family == "rp2040" else ("cm0", "cm1"))]
    address = FLASH_BASE + layout["sector_offset"]
    lines = ["init"]
    if command == "read":
        for index, core in enumerate(cores):
            lines.append(f"set was_running_{index} [expr {{[{core} curstate] eq \"running\"}}]")
            lines.append(
                f"lappend pre_shutdown_commands {{if {{$was_running_{index} && "
                f'[{core} curstate] eq "halted"}} {{targets {core}; resume}}}}'
            )
    lines.append("set failed [catch {")
    for core in cores:
        lines.extend([f"targets {core}", "halt", "wait_halt 5000"])
    lines.extend([
        f"targets {cores[0]}",
        "flash probe 0",
        "array set bank [lindex [flash list] 0]",
        f'if {{$bank(base) != {FLASH_BASE} || $bank(size) != {layout["flash_size"]}}} {{',
        '    error "probed flash bank does not match config-layout.json"',
        "}",
    ])
    if command != "read":
        lines.append(f"flash erase_address {address:#x} {SECTOR_SIZE}")
        if command == "set-clock":
            lines.append(f"flash write_image {tcl_quote(str(image))} {address:#x} bin")
    lines.extend([f"dump_image {tcl_quote(str(readback))} {address:#x} {SECTOR_SIZE}", "} failure]"])
    if command == "read":
        # Restore only cores that were running, even if probing/dumping/halting failed.
        for index, core in enumerate(cores):
            lines.append(f"if {{$was_running_{index}}} {{")
            lines.append(f"    if {{[catch {{targets {core}; resume}}]}} {{ set failed 1 }}")
            lines.append("}")
    lines.extend(['if {$failed} { echo "Configuration SWD operation failed: $failure"; shutdown error }', "shutdown"])
    return "\n".join(lines)


def run_openocd(args: argparse.Namespace, family: str, script: str) -> None:
    argv = [
        args.openocd_exe,
        "-c", "gdb_port disabled; tcl_port disabled; telnet_port disabled",
        "-f", "interface/cmsis-dap.cfg",
        "-c", f"adapter serial {tcl_quote(args.probe_serial)}",
        "-f", f"target/{family}.cfg",
        "-c", f"adapter speed {args.adapter_speed_khz}",
        "-c", script,
    ]
    result = subprocess.run(argv, capture_output=True, text=True, check=False)
    if result.returncode:
        raise ValueError(f"OpenOCD failed ({result.returncode}):\n{result.stdout}{result.stderr}")


def provision(args: argparse.Namespace) -> dict:
    build_dir = Path(args.build_dir).resolve()
    layout = load_layout(build_dir)
    family = layout["target_family"]
    if not args.probe_serial.strip():
        raise ValueError("--probe-serial must not be empty")
    tcl_quote(args.probe_serial)
    if args.command == "set-clock":
        check_profile(args.profile, family, args.unsafe_overclock)
    expected = encode_sector(args.profile) if args.command == "set-clock" else b"\xff" * SECTOR_SIZE
    paths = []
    try:
        # Keep private scratch files in the selected build directory, never the system temp directory.
        for suffix in (".page.bin", ".readback.bin"):
            fd, name = tempfile.mkstemp(prefix=".config-", suffix=suffix, dir=build_dir)
            os.close(fd)
            paths.append(Path(name))
        image, readback = paths
        image.write_bytes(expected[:PAGE_SIZE])
        run_openocd(args, family, operation_script(layout, args.command, image, readback))
        actual = readback.read_bytes()
        metadata = decode_sector(actual, family)
        if args.command != "read":
            if actual != expected:
                raise ValueError("configuration readback differs from requested sector")
            # Only reset after Python has verified all bytes, the layout, and the CRC.
            run_openocd(args, family, "init; reset run; shutdown")
        return {"target_family": family, "sector_offset": layout["sector_offset"], **metadata}
    finally:
        for path in paths:
            path.unlink(missing_ok=True)


def positive_integer(value: str) -> int:
    number = int(value)
    if number <= 0:
        raise argparse.ArgumentTypeError("must be a positive integer")
    return number


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, help="directory containing generated config-layout.json")
    parser.add_argument("--probe-serial", required=True, help="explicit CMSIS-DAP probe serial")
    parser.add_argument("--openocd-exe", default="openocd")
    parser.add_argument("--adapter-speed-khz", type=positive_integer, default=5000)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("read", help="read and validate configuration metadata (no raw output)")
    clock = commands.add_parser("set-clock", help="replace the configuration with a clock profile")
    clock.add_argument("profile", choices=PROFILES)
    clock.add_argument("--unsafe-overclock", action="store_true")
    commands.add_parser("erase", help="erase only the final configuration sector")
    args = parser.parse_args(argv)
    try:
        print(json.dumps(provision(args), sort_keys=True))
        return 0
    except (OSError, ValueError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
    except KeyboardInterrupt:
        print("Error: configuration operation interrupted.", file=sys.stderr)
        if args.command != "read":
            print("Target may remain halted; no recovery reset was requested.", file=sys.stderr)
        return 130
    if args.command != "read":
        print("Mutation did not complete; target may remain halted. No recovery reset was requested.", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
