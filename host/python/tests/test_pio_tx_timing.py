"""Check that both PIO TX programs emit exact 8-clock 8N1 frames on one pin path."""

from __future__ import annotations

import re
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
PIO_PATH = REPO_ROOT / "firmware" / "src" / "uart" / "pio" / "uart.pio"

CLOCKS_PER_BIT = 8


def _program_body(text: str, name: str) -> str:
    start = text.index(f".program {name}")
    rest = text[start:]
    next_program = rest.find("\n.program ", 1)
    return rest if next_program < 0 else rest[:next_program]


def _parse(body: str) -> tuple[dict[str, int], list[dict]]:
    labels: dict[str, int] = {}
    instructions: list[dict] = []
    for raw in body.splitlines():
        line = raw.split(";", 1)[0].strip()
        if not line or line.startswith("."):
            continue
        if line.endswith(":"):
            labels[line[:-1]] = len(instructions)
            continue
        delay = 0
        match = re.search(r"\[(\d+)\]\s*$", line)
        if match:
            delay = int(match.group(1))
            line = line[: match.start()].strip()
        if re.search(r"\bside\b", line):
            raise AssertionError(f"TX bit must not use side-set: {line}")
        parts = line.replace(",", " ").split()
        instructions.append({"op": parts[0], "args": parts[1:], "delay": delay})
    return labels, instructions


def _mov_level(args: list[str]) -> int:
    source = args[-1]
    if source == "null":
        return 0
    if source == "!null":
        return 1
    raise AssertionError(f"unexpected mov source {source}")


def _simulate(program: str, payload: bytes) -> list[int]:
    labels, instructions = _parse(_program_body(PIO_PATH.read_text(), program))
    pc = 0
    x = 0
    osr = 0
    pin = 1
    levels: list[int] = []
    byte_index = 0
    for _ in range(10000):
        insn = instructions[pc]
        jump = None
        op = insn["op"]
        if op == "pull":
            if byte_index >= len(payload):
                levels.extend([pin] * (1 + insn["delay"]))
                break
            osr = payload[byte_index]
            byte_index += 1
        elif op == "set":
            x = int(insn["args"][-1])
        elif op == "mov":
            pin = _mov_level(insn["args"])
        elif op == "out":
            pin = osr & 1
            osr >>= 1
        elif op == "jmp" and insn["args"][0] == "x--":
            if x != 0:
                x -= 1
                jump = insn["args"][1]
        elif op == "jmp":
            jump = insn["args"][0]
        elif op == "wait":
            pass
        else:
            raise AssertionError(f"unhandled PIO op {op}")
        levels.extend([pin] * (1 + insn["delay"]))
        if jump is not None:
            pc = labels[jump]
        else:
            pc = (pc + 1) % len(instructions)
    return levels


def _assert_frame(program: str, byte: int) -> None:
    levels = _simulate(program, bytes((byte, byte)))
    start = levels.index(0)
    frame = levels[start:]
    assert frame[:CLOCKS_PER_BIT] == [0] * CLOCKS_PER_BIT
    for bit_index in range(8):
        bit = (byte >> bit_index) & 1
        chunk = frame[CLOCKS_PER_BIT * (bit_index + 1) : CLOCKS_PER_BIT * (bit_index + 2)]
        assert chunk == [bit] * CLOCKS_PER_BIT, (program, byte, bit_index, chunk)
    stop = frame[CLOCKS_PER_BIT * 9 : CLOCKS_PER_BIT * 10]
    assert stop == [1] * CLOCKS_PER_BIT
    assert frame[CLOCKS_PER_BIT * 10] == 0


def test_tx_frames_are_ten_bit_times_on_one_pin_path():
    for program in ("pio_uart_tx", "pio_uart_tx_cts"):
        for byte in (0x00, 0xFF, 0x55, 0x01, 0x80, 0xA5):
            _assert_frame(program, byte)
