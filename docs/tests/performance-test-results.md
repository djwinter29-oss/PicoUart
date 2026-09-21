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

## 2026-09-20T19:51:12+00:00 - pico - Functional Test

**Result:** `PASS`
**Firmware:** 0.0.0-dev, `850c8ff`
**Qualification:** development HIL; functional phase only
**Board:** `pico`
**Test date/time:** `2026-09-20T19:51:12+00:00`
**Wiring:** Self-test stages 1-4
**RTS/CTS:** disabled

### Configuration

- Baud rate: 115200
- Payload: 64 bytes
- Artifact: not supplied
- Artifact SHA-256: `not supplied`
- HID firmware version: `0.0.0`

### Results

| Link | Result |
| --- | --- |
| Debug Probe to HW UART0 | PASS |
| HW UART1 to PIO UART2 | PASS |
| PIO UART3 to PIO UART4 | PASS |
| PIO UART5 loopback | PASS |

### Health

- Before: health [cdc0=0x01, cdc1=0x01, cdc2=0x21, cdc3=0x21, cdc4=0x21, cdc5=0x21]; overruns [cdc0=0, cdc1=0, cdc2=0, cdc3=0, cdc4=0, cdc5=0]; firmware=0.0.0
- After: health [cdc0=0x01, cdc1=0x01, cdc2=0x21, cdc3=0x21, cdc4=0x21, cdc5=0x21]; overruns [cdc0=0, cdc1=0, cdc2=0, cdc3=0, cdc4=0, cdc5=0]; firmware=0.0.0
- Raw log: /home/home/repo/PicoUart/docs/tests/raw/hardware-test-2026-09-20T19_51_12_00_00.log

### Notes (review-requested HIL for fix/address-review-findings, commit 850c8ff)

- Flashed with `/usr/local/bin/openocd-upstream` (0.12.0+dev-g8056a09) over
  CMSIS-DAP/SWD at 1000 kHz adapter speed. That build's flash driver was
  renamed from `rp2040_flash` to `rp2xxx` upstream and it ships without its
  own scripts directory, so the distro `/usr/share/openocd/scripts` target
  file was used with the flash-bank driver name substituted; programming
  correctly identified `RP2040 rev 2, QSPI Flash bsemi by25q16es id = 0x154068
  size = 2048 KiB` and reported `** Verified OK **`.
- Board re-enumerated as `cafe:4010` with all 6 CDC ports (`ttyACM1`-`ttyACM6`)
  plus the HID interface immediately after reset.
- `pico_uart_hid.py version` -> `0.0.0`; `temperature` -> `26.67 C`;
  `overruns` -> all 0; `monitor` ran cleanly with no `rx_error`/`control_error`
  bits set throughout the run.
- `pico_uart_hid.py toggle-led` returned success (HID ack); this environment
  has no camera, so LED visibility was not machine-observable and is not
  claimed as verified here.
- `sudo dmesg` was cleared immediately before flashing and again immediately
  before this functional run; captured logs contain no USB `-71` errors and
  no unexpected disconnect/reset events for the PicoUart or Debug Probe
  devices across the whole flash + test sequence.

---

## 2026-09-20T13:59:50+00:00 - pico - Hardware Test

**Result:** `PASS`
**Firmware:** 0.0.0, `working-tree`
**Qualification:** development HIL; not release qualification
**Board:** `pico`
**Test date/time:** `2026-09-20T13:59:50+00:00`
**Wiring:** Self-test stages 1-4 and performance fixture
**RTS/CTS:** disabled
**Artifact:** /home/home/repo/PicoUart/build/firmware-pico/pico_uart.elf
**Artifact SHA-256:** `bc1941b0a4d82a06e74744008109915203138df4e0034a43280a0fe6d31610be`

### Results

- Functional test: PASS
- Performance test: PASS

### Health

- RX overflows: check with `pico_uart_hid.py overruns`
- HID errors: check with `pico_uart_hid.py monitor`
- Raw log: /home/home/repo/PicoUart/docs/tests/raw/hardware-test-2026-09-20T13_59_50_00_00.log

---

## 2026-09-20 - Pico - Full Hardware Matrix

**Result:** `PASS`
**Firmware:** `0.0.0`, latest working tree
**Qualification:** local development HIL; not a release qualification record
**Firmware commit:** `1150ed4`
**Test date/time:** `2026-09-20`
**OpenOCD:** upstream `0.12.0+dev-g853c016`
**Flash:** Boya BY25Q16ES, `0x00154068`, 2048 KiB
**Wiring:** Self-test stages 1-4
**RTS/CTS:** disabled

**Artifacts:** `build/firmware-pico/pico_uart.elf`
**ELF SHA256:** `9a417879968ae8703ce2c69541f4fd9feecefec94602ba2134c4f925cdfa3e00`
**UF2 SHA256:** `91cae04c45f6a02c3b7c9908666fd5b30aebe5db4d03a496783e1b6fd1786fbf`

### Test Configuration

- Payload: 1024 bytes
- Baud rates: 9600 through 1000000 baud
- Settle interval: 0.5 seconds after line-coding changes

### Results

- Debug Probe <-> HW UART0: PASS at all rates
- HW UART1 <-> PIO UART2: PASS at all rates
- PIO UART3 <-> PIO UART4: PASS at all rates
- PIO UART5 loopback: PASS at all rates
- RX overflow counts: zero on CDC0 through CDC5
- HID health: all channels clean

The topology-aware benchmark uses the staged HW1-to-PIO2 and PIO3-to-PIO4
peers when `--uart1` and `--uart4` are supplied. The result above records the
topology-correct staged matrix.

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
