"""Execute the PIO RX majority-vote tree checked into uart.pio."""

from __future__ import annotations

import re
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
PIO_PATH = REPO_ROOT / "firmware" / "src" / "uart" / "pio" / "uart.pio"
LINE_CODING_PATH = REPO_ROOT / "firmware" / "src" / "uart" / "line_coding.h"


def _program_body(text: str) -> str:
    start = text.index(".program pio_uart_rx")
    rest = text[start:]
    next_program = rest.find("\n.program ", 1)
    return rest if next_program < 0 else rest[:next_program]


def _parse(body: str) -> tuple[dict[str, int], list[tuple[str, int]]]:
    labels: dict[str, int] = {}
    instructions: list[tuple[str, int]] = []
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
        instructions.append((line, delay))
    return labels, instructions


class _VoteMachine:
    def __init__(self, labels: dict[str, int], instructions: list[tuple[str, int]], samples: list[int]):
        self._labels = labels
        self._instructions = instructions
        self._samples = list(samples)
        self.pc = labels["bitloop"]
        self.x = 8
        self.y = 1
        self.isr_bits: list[int] = []
        self.cycles = 0
        self.sample_cycles: list[int] = []
        self.framing_error = False
        self.pushed = False

    def run_until_bit_resolves(self) -> int:
        start = self.pc
        seen_return = False
        for _ in range(32):
            if seen_return:
                break
            text, delay = self._instructions[self.pc]
            started_at = self.cycles
            self.cycles += 1 + delay
            self.pc += 1
            parts = text.replace(",", " ").split()
            op = parts[0]
            if op == "jmp" and parts[1] == "pin":
                self.sample_cycles.append(started_at)
            if op == "jmp":
                seen_return = self._jump(parts[1:])
            elif op == "in":
                bit = 0 if parts[1] == "null" else self.y & 1
                self.isr_bits.append(bit)
            elif op == "irq":
                self.framing_error = True
            elif op == "push":
                self.pushed = True
            elif op == "set":
                target = parts[1]
                value = int(parts[2])
                if target == "x":
                    self.x = value
                elif target == "y":
                    self.y = value
            elif op in {"wait", "mov"}:
                pass
            else:
                raise AssertionError(f"unhandled PIO op {text}")
            if self.framing_error or self.pushed or (self.pc == start and op == "jmp"):
                seen_return = True
        if not seen_return:
            raise AssertionError("vote tree did not resolve")
        return self.cycles

    def _jump(self, args: list[str]) -> bool:
        if args[0] == "pin":
            sample = self._samples.pop(0)
            if sample:
                self.pc = self._labels[args[1]]
            return False
        if args[0] == "!x":
            if self.x == 0:
                self.pc = self._labels[args[1]]
            return False
        if args[0] == "x--":
            if self.x != 0:
                self.x -= 1
                self.pc = self._labels[args[1]]
                return True
            return False
        self.pc = self._labels[args[0]]
        return False


def _majority(samples: tuple[int, int, int]) -> int:
    return 1 if sum(samples) >= 2 else 0


def test_rx_clocks_per_bit_matches_the_line_coding_contract():
    header = LINE_CODING_PATH.read_text()
    assert re.search(r"#define UART_LINE_CODING_PIO_RX_CLOCKS_PER_BIT 16u", header)
    assert re.search(r"#define UART_LINE_CODING_PIO_CLOCKS_PER_BIT 16u", header)


def test_vote_tree_follows_majority_in_16_cycles():
    labels, instructions = _parse(_program_body(PIO_PATH.read_text()))
    assert "bitloop" in labels
    assert "vote0" in labels
    assert "vote1" in labels

    for samples in ((a, b, c) for a in (0, 1) for b in (0, 1) for c in (0, 1)):
        machine = _VoteMachine(labels, instructions, list(samples))
        machine.x = 8
        cycles = machine.run_until_bit_resolves()
        assert cycles == 16
        assert machine.isr_bits == [_majority(samples)]
        assert not machine.framing_error
        if samples[0] == samples[1]:
            assert machine.sample_cycles == [0, 2]
        else:
            assert machine.sample_cycles == [0, 2, 4]


def test_stop_bit_vote_reports_framing_only_when_majority_is_zero():
    labels, instructions = _parse(_program_body(PIO_PATH.read_text()))

    for samples in ((a, b, c) for a in (0, 1) for b in (0, 1) for c in (0, 1)):
        machine = _VoteMachine(labels, instructions, list(samples))
        machine.x = 0
        machine.run_until_bit_resolves()
        if samples[0] == samples[1]:
            assert machine.sample_cycles == [0, 2]
        else:
            assert machine.sample_cycles == [0, 2, 4]
        if _majority(samples) == 0:
            assert machine.framing_error
            assert not machine.pushed
        else:
            assert machine.pushed
            assert not machine.framing_error
