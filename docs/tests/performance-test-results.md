# Performance Test Results

Newest result first. Add future results below the heading and keep the
reusable template at the bottom.

Result entries are development evidence unless they explicitly say
`release qualification` and include the exact flashed artifact hash for each
required board.

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

All individual-link sequences were run with `tools/serial_bridge_test.py` at each
baud rate in sequence. The concurrent run used `tools/serial_stress_benchmark.py`
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
- **Note:** `serial_stress_benchmark.py` without `--uart1/--uart4` forces a
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
