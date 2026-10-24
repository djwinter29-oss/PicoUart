"""Tests for target-specific linked firmware memory budget checks."""

import importlib.util
import json
from pathlib import Path

import pytest

CHECKER = Path(__file__).resolve().parents[2] / "tools/release/check-firmware-budget.py"
spec = importlib.util.spec_from_file_location("firmware_budget", CHECKER)
assert spec is not None and spec.loader is not None
firmware_budget = importlib.util.module_from_spec(spec)
spec.loader.exec_module(firmware_budget)


def make_build(tmp_path, ram_size=1024, image_size=1024, flash_size=8192):
    (tmp_path / "pico_uart.elf").write_bytes(b"elf")
    (tmp_path / "pico_uart.bin").write_bytes(bytes(image_size))
    (tmp_path / "config-layout.json").write_text(
        json.dumps({"flash_size": flash_size, "sector_size": 4096}),
        encoding="utf-8",
    )
    size_tool = tmp_path / "size-tool.py"
    size_tool.write_text(
        "print('section size addr')\n"
        f"print('.bss 0x{ram_size:x} 0x20000000')\n",
        encoding="utf-8",
    )
    return size_tool


def test_parser_counts_sram_and_reserved_linker_sections_only():
    output = """
section size addr
.text 0x1000 0x10000000
.ram_vector_table 0x100 0x20000000
.data 0x200 0x20000100
.bss 0x300 0x20001000
.stack_dummy 0x800 0x20040000
.debug_info 0x5000 0x0
"""
    assert firmware_budget.parse_size_output(output, 264 * 1024) == 0x100 + 0x200 + 0x300 + 0x800


def test_parser_rejects_sram_section_outside_physical_capacity():
    with pytest.raises(ValueError, match="extends beyond"):
        firmware_budget.parse_size_output(".bss 0x2000 0x20041000", 264 * 1024)


@pytest.mark.parametrize(
    ("board", "expected"),
    [
        ("pico", (264 * 1024, 192 * 1024)),
        ("pico_w", (264 * 1024, 192 * 1024)),
        ("pico2", (520 * 1024, 384 * 1024)),
        ("pico2_w", (520 * 1024, 384 * 1024)),
    ],
)
def test_budget_covers_each_declared_board(board, expected):
    assert firmware_budget.BOARD_LIMITS[board] == expected


def test_check_fails_when_required_artifact_is_missing(tmp_path):
    with pytest.raises(ValueError, match="missing build artifact"):
        firmware_budget.check_build(tmp_path, "pico", "arm-none-eabi-size")


def test_check_accepts_memory_at_budget_and_valid_flash_image(tmp_path):
    size_tool = make_build(tmp_path, ram_size=192 * 1024, image_size=4096)
    assert firmware_budget.check_build(tmp_path, "pico", str(size_tool)) == (
        192 * 1024,
        192 * 1024,
        4096,
        4096,
    )


def test_check_rejects_sram_over_budget(tmp_path):
    size_tool = make_build(tmp_path, ram_size=193 * 1024)
    with pytest.raises(ValueError, match="SRAM sections reserve"):
        firmware_budget.check_build(tmp_path, "pico", str(size_tool))


def test_check_rejects_flash_image_over_budget(tmp_path):
    size_tool = make_build(tmp_path, image_size=4097)
    with pytest.raises(ValueError, match="flash image uses"):
        firmware_budget.check_build(tmp_path, "pico", str(size_tool))
