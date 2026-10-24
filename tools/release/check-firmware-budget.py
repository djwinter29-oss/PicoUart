#!/usr/bin/env python3
"""Check linked SRAM reservations and flash image size for PicoUart firmware."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys

SRAM_BASE = 0x20000000
BOARD_LIMITS = {
    "pico": (264 * 1024, 192 * 1024),
    "pico_w": (264 * 1024, 192 * 1024),
    "pico2": (520 * 1024, 384 * 1024),
    "pico2_w": (520 * 1024, 384 * 1024),
}


def parse_size_output(output: str, sram_bytes: int) -> int:
    """Sum allocated ELF sections located in SRAM, including linker reserves."""
    sram_end = SRAM_BASE + sram_bytes
    total = 0
    found = False
    for line in output.splitlines():
        fields = line.split()
        if len(fields) != 3:
            continue
        try:
            size, address = (int(value, 0) for value in fields[1:])
        except ValueError:
            continue
        if size == 0 or not SRAM_BASE <= address < sram_end:
            continue
        if address + size > sram_end:
            raise ValueError(f"ELF section {fields[0]} extends beyond target SRAM")
        total += size
        found = True
    if not found:
        raise ValueError("size output contains no allocated SRAM sections")
    return total


def check_build(build_dir: Path, board: str, size_tool: str) -> tuple[int, int, int, int]:
    if board not in BOARD_LIMITS:
        raise ValueError(f"unsupported board for memory budget: {board}")
    sram_bytes, sram_budget = BOARD_LIMITS[board]
    elf = build_dir / "pico_uart.elf"
    image = build_dir / "pico_uart.bin"
    layout_path = build_dir / "config-layout.json"
    for artifact in (elf, image, layout_path):
        if not artifact.is_file():
            raise ValueError(f"missing build artifact: {artifact}")

    size_path = Path(size_tool)
    command = [sys.executable, str(size_path)] if size_path.suffix.lower() == ".py" else [size_tool]
    command.extend(("-A", "-x", str(elf)))
    try:
        output = subprocess.run(command, check=True, capture_output=True, text=True).stdout
    except FileNotFoundError as error:
        raise ValueError(f"size tool not found: {size_tool}") from error
    except subprocess.CalledProcessError as error:
        detail = error.stderr.strip() or error.stdout.strip() or str(error)
        raise ValueError(f"size tool failed: {detail}") from error
    ram_used = parse_size_output(output, sram_bytes)

    layout = json.loads(layout_path.read_text(encoding="utf-8"))
    if not isinstance(layout, dict):
        raise ValueError(f"invalid flash layout in {layout_path}")
    flash_bytes = layout.get("flash_size")
    sector_bytes = layout.get("sector_size")
    if (not isinstance(flash_bytes, int) or not isinstance(sector_bytes, int) or
            flash_bytes <= sector_bytes or sector_bytes <= 0):
        raise ValueError(f"invalid flash layout in {layout_path}")
    flash_used = image.stat().st_size
    flash_budget = flash_bytes - sector_bytes
    if flash_used > flash_budget:
        raise ValueError(f"flash image uses {flash_used} bytes; budget is {flash_budget}")
    if ram_used > sram_budget:
        raise ValueError(f"SRAM sections reserve {ram_used} bytes; budget is {sram_budget}")
    return ram_used, sram_budget, flash_used, flash_budget


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--board", choices=BOARD_LIMITS, required=True)
    parser.add_argument("--size-tool", default=shutil.which("arm-none-eabi-size") or "arm-none-eabi-size")
    args = parser.parse_args()
    try:
        ram_used, ram_budget, flash_used, flash_budget = check_build(args.build_dir, args.board, args.size_tool)
    except (OSError, ValueError) as error:
        print(f"firmware budget check failed: {error}", file=sys.stderr)
        return 1
    print(f"SRAM sections: {ram_used}/{ram_budget} bytes; unallocated margin: {BOARD_LIMITS[args.board][0] - ram_used}")
    print(f"Flash image:   {flash_used}/{flash_budget} bytes before reserved configuration sector")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
