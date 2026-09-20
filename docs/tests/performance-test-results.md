# Performance Test Results

Add each new result directly below this heading so the newest result remains
at the top. Keep the date in ISO format (`YYYY-MM-DD`) and do not overwrite
older entries.

## Template

### YYYY-MM-DD - BOARD - TEST TYPE

**Result:** `PASS` / `FAIL` / `PARTIAL`  
**Tester:**  
**Firmware version:**  
**Firmware commit:**  
**Build artifact:**  
**Board:** `pico` / `pico2`  
**Host OS:**  
**Python version:**  
**Test date/time:**  
**Wiring:** See [Self-Test Setup](self-test-setup.md), stage(s):  
**RTS/CTS:** `disabled` / `enabled`  

#### HIL Artifact Identity

Record the exact packaged artifacts used for this run. These hashes are the
binding between the physical test and the release files; never record a hash
from a separate local rebuild.

| Board | Artifact | SHA256 |
| --- | --- | --- |
| pico | `pico_uart-vX.Y.Z-pico.elf` |  |
| pico2 | `pico_uart-vX.Y.Z-pico2.elf` |  |

#### Test Configuration

| Setting | Value |
| --- | --- |
| Baud rate(s) |  |
| Duration |  |
| Payload size |  |
| Concurrent links |  |
| Tool command |  |

#### Results

| Link | Direction | Result | Verified bytes | Throughput | RX errors | Overrun delta |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| Debug Probe to HW UART0 | Both |  |  |  |  |  |
| HW UART1 to PIO UART2 | Both |  |  |  |  |  |
| PIO UART3 to PIO UART4 | Both |  |  |  |  |  |
| PIO UART5 loopback | Loopback |  |  |  |  |  |

#### HID Status

```text
Before:

After:
```

#### Notes

- USB enumeration:
- Board resets or disconnects:
- Failures or skipped stages:
- Raw log location:

---

## Example Dummy Entry

### 2026-09-20 - Pico 2 - Baseline Template

**Result:** `NOT RUN`  
**Tester:** Example  
**Firmware version:** `0.0.0`  
**Firmware commit:** `<commit>`  
**Build artifact:** `build/firmware-pico2/pico_uart.uf2`  
**Board:** `pico2`  
**Host OS:** `<OS>`  
**Python version:** `<version>`  
**Test date/time:** `2026-09-20T00:00:00Z`  
**Wiring:** Self-test stages 1-4  
**RTS/CTS:** `disabled`  

#### HIL Artifact Identity

| Board | Artifact | SHA256 |
| --- | --- | --- |
| pico2 | `build/firmware-pico2/pico_uart.elf` | `<record hash>` |

#### Test Configuration

| Setting | Value |
| --- | --- |
| Baud rate(s) | 115200, 460800, 921600, 1000000 |
| Duration | 10 seconds per run |
| Payload size | `<tool default>` |
| Concurrent links | `<record included links>` |
| Tool command | `<paste command>` |

#### Results

| Link | Direction | Result | Verified bytes | Throughput | RX errors | Overrun delta |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| Debug Probe to HW UART0 | Both | NOT RUN | - | - | - | - |
| HW UART1 to PIO UART2 | Both | NOT RUN | - | - | - | - |
| PIO UART3 to PIO UART4 | Both | NOT RUN | - | - | - | - |
| PIO UART5 loopback | Loopback | NOT RUN | - | - | - | - |

#### Notes

This is a placeholder entry. Replace it with measured values when the first
physical performance run is complete.