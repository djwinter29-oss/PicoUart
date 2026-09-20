# Performance Test Results

Newest result first. Add future results below the heading and keep the
reusable template at the bottom.

## 2026-09-20 - Pico - Full Hardware Matrix

**Result:** `PASS`
**Firmware:** `0.0.0`, latest working tree
**OpenOCD:** upstream `0.12.0+dev-g853c016`
**Flash:** Boya BY25Q16ES, `0x00154068`, 2048 KiB
**Wiring:** Self-test stages 1-4
**RTS/CTS:** disabled

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

The generic concurrent benchmark was not used for the full fixture because it
hardcodes UART2 <-> UART3. The topology-correct staged tests were used instead.

---

## Template

### YYYY-MM-DD - BOARD - TEST TYPE

**Result:** `PASS` / `FAIL` / `PARTIAL`
**Firmware:**
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
