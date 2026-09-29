"""Execute the PIO RX majority-vote tree checked into uart.pio."""

from __future__ import annotations

import re
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
PIO_PATH = REPO_ROOT / "firmware" / "src" / "uart" / "pio" / "uart.pio"
LINE_CODING_PATH = REPO_ROOT / "firmware" / "src" / "uart" / "line_coding.h"


def _program_body(text: str, name: str = "pio_uart_rx") -> str:
    start = text.index(f".program {name}")
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
            elif op in {"wait", "mov", "nop"}:
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


def test_plain_tx_is_10_bit_8n1():
    text = PIO_PATH.read_text()
    _, tx = _parse(_program_body(text, "pio_uart_tx"))
    _, cts = _parse(_program_body(text, "pio_uart_tx_cts"))
    _, rx = _parse(_program_body(text, "pio_uart_rx"))

    assert [item[0].split()[0] for item in tx] == ["pull", "set", "out", "jmp"]
    assert tx[0][0].split()[:3] == ["pull", "side", "1"]
    assert tx[0][1] == 7  # stop bit is 8 clocks (1 + delay 7)
    assert tx[1][0].split()[:3] == ["set", "x,", "7"]
    assert tx[3][1] == 6  # data-bit hold stays 8 clocks (1 + delay 6)
    assert len(cts) == 5
    assert len(rx) == 24
    # Shipped board loads plain TX + RX (28/32). The extra RX nop is the
    # preamble delay past the 31-cycle field. CTS + plain TX + RX is 33/32.
    assert len(tx) + len(rx) == 28
    assert len(tx) + len(cts) + len(rx) == 33


def test_rx_clocks_per_bit_matches_the_line_coding_contract():
    header = LINE_CODING_PATH.read_text()
    assert re.search(r"#define UART_LINE_CODING_PIO_RX_CLOCKS_PER_BIT 32u", header)
    assert re.search(r"#define UART_LINE_CODING_PIO_CLOCKS_PER_BIT 8u", header)


def _rx_clocks_per_bit() -> int:
    header = LINE_CODING_PATH.read_text()
    match = re.search(r"#define UART_LINE_CODING_PIO_RX_CLOCKS_PER_BIT (\d+)u", header)
    assert match is not None
    return int(match.group(1))


def test_vote_tree_follows_majority_across_one_bit():
    labels, instructions = _parse(_program_body(PIO_PATH.read_text()))
    clocks = _rx_clocks_per_bit()
    gap = clocks // 8
    assert "bitloop" in labels
    assert "vote0" in labels
    assert "vote1" in labels

    for samples in ((a, b, c) for a in (0, 1) for b in (0, 1) for c in (0, 1)):
        machine = _VoteMachine(labels, instructions, list(samples))
        machine.x = 8
        cycles = machine.run_until_bit_resolves()
        assert cycles == clocks
        assert machine.isr_bits == [_majority(samples)]
        assert not machine.framing_error
        if samples[0] == samples[1]:
            assert machine.sample_cycles == [0, gap]
        else:
            assert machine.sample_cycles == [0, gap, gap * 2]


def test_stop_bit_vote_reports_framing_only_when_majority_is_zero():
    labels, instructions = _parse(_program_body(PIO_PATH.read_text()))
    clocks = _rx_clocks_per_bit()
    gap = clocks // 8

    for samples in ((a, b, c) for a in (0, 1) for b in (0, 1) for c in (0, 1)):
        machine = _VoteMachine(labels, instructions, list(samples))
        machine.x = 0
        cycles = machine.run_until_bit_resolves()
        if samples[0] == samples[1]:
            assert machine.sample_cycles == [0, gap]
        else:
            assert machine.sample_cycles == [0, gap, gap * 2]
        if _majority(samples) == 0:
            assert machine.framing_error
            assert not machine.pushed
        else:
            assert machine.pushed
            assert not machine.framing_error
            # bitloop is `gap` clocks before centre. The next start is half a
            # bit plus that same gap later. `cycles` is when `wait 0 pin` runs.
            next_start = (clocks // 2) + gap
            assert next_start - cycles == 9
