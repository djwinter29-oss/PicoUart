# Performance Test Results

Newest result first. Add future results below the heading and keep the
reusable template at the bottom.

Result entries are development evidence unless they explicitly say
`release qualification` and include the exact flashed artifact hash for each
required board.

## 2026-09-22T17:59:53+00:00 - pico - Hardware Test

**Result:** `PASS`
**Firmware:** 0.0.0, `2c1a98e299615f9d74869547ee766e2ffc9a163f`
**Board:** `pico`
**Test date/time:** `2026-09-22T17:59:53+00:00`
**Wiring:** Self-test stages 1-4 and performance fixture
**RTS/CTS:** disabled
**Artifact:** /home/home/repo/PicoUart/build/firmware-pico-current/pico_uart.elf
**Artifact SHA-256:** `0ea0b23ce4f621621c70ba3fb56464138aa02955abc55ba2ae8ce644a822f116`
**UF2 SHA-256:** `fcc31471a22693bf16c5d86fad746d59a4fc348bc98845349a5c8a39b2982b36`
**Flash:** local OpenOCD `0.12.0+dev-g46d9b60`, verified at 1000 kHz

### Results

- Functional test: PASS
- Performance test: PASS
- Stage 1 Debug Probe to HW UART0: PASS both directions, 117 bytes each
- Stage 2 HW UART1 to PIO UART2: PASS both directions, 117 bytes each
- Stage 3 PIO UART3 to PIO UART4: PASS both directions, 117 bytes each
- Stage 4 PIO UART5 loopback: PASS, 118 bytes
- Concurrent 115200 baud: PASS, all 7 streams verified
- Concurrent throughput: 113664 bytes on UART0 directions; 114688 bytes on
  each staged UART/PIO stream at approximately 11.3 kB/s

### Health

- RX overflows: 0 on CDC0 through CDC5
- HID health: ready on all ports; PIO flags on CDC2 through CDC5; no
  `control_error`, `rx_overrun`, or `rx_error`
- HID version: `0.0.0`
- Raw log: /home/home/repo/PicoUart/docs/tests/raw/hardware-test-2026-09-22T17_59_53_00_00.log

---

## 2026-09-20T20:28:00+00:00 - pico - Full Matrix Performance + Functional

**Result:** `PASS`
**Firmware:** `0.0.0`, `accdb59`
**Qualification:** development HIL; not release qualification
**Board:** `pico`
**Test date/time:** `2026-09-20T20:28:00+00:00`
**Wiring:** Self-test stages 1-4 (full staged fixture)
**RTS/CTS:** disabled

### Configuration

- Payload: 1024 bytes (individual), 1024 bytes (concurrent)
- Baud rates individual: 115200, 460800, 921600, 1000000
- Concurrent: 115200, 10 s, all 4 links / 7 streams
- UART0 fix at 115200 for concurrent runs

### Results

All individual-link sequences were run with `tools/hardware/serial_bridge_test.py` at each
baud rate in sequence. The concurrent run used `tools/hardware/serial_stress_benchmark.py`
with all four fixture links active (`--uart1 --uart1-peer --uart4 --uart4-peer`).

| Link | 115200 | 460800 | 921600 | 1000000 | Concurrent 115200 |
| --- | :-: | :-: | :-: | :-: | :-: |
| Debug Probe ↔ HW UART0 | PASS | PASS | PASS | PASS | PASS (2 streams) |
| HW UART1 ↔ PIO UART2 | PASS | PASS | PASS | PASS | PASS (2 streams) |
| PIO UART3 ↔ PIO UART4 | PASS | PASS | PASS | PASS | PASS (2 streams) |
| PIO UART5 loopback | PASS | PASS | PASS | PASS | PASS (1 stream) |

### Health

- **Before:** all overruns 0; firmware `0.0.0`
- **After:** all overruns 0; firmware `0.0.0`
- USB: no disconnects, no `-71`, no unexpected errors in dmesg
- **Note:** `tools/hardware/serial_stress_benchmark.py` without `--uart1/--uart4` forces a
  UART2↔UART3 link that is not wired in the staged fixture. The earlier observed
  "failure" on that link was expected because those pins have no jumper — it is
  not a regression. The tool path with full `--uart1/--uart1-peer/--uart4/--uart4-peer`
  produces the correct cross-wired topology and passes cleanly at 115200 concurrent.

---

## Template

### YYYY-MM-DD - BOARD - TEST TYPE

**Result:** `PASS` / `FAIL` / `PARTIAL`
**Firmware:**
**Qualification:** development HIL / release qualification / partial
**Board:** `pico` / `pico2`
**Firmware commit:**
**Test date/time:**
**Wiring:**
**RTS/CTS:**

### Configuration

- Payload:
- Baud rates:
- Duration:
- Settle interval:

### Results

| Link | Result | Notes |
| --- | --- | --- |
| Debug Probe <-> HW UART0 |  |  |
| HW UART1 <-> PIO UART2 |  |  |
| PIO UART3 <-> PIO UART4 |  |  |
| PIO UART5 loopback |  |  |

### Health

- RX overflows:
- HID errors:
- USB resets/disconnects:
- Raw log:
