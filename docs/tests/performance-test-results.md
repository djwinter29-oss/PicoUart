# Performance Test Results

Add each new result directly below this heading so the newest result remains
at the top. Keep the date in ISO format (`YYYY-MM-DD`) and do not overwrite
older entries.

## 2026-09-20T11:18:18+00:00 - pico - HID Verification

**Result:** `PARTIAL`
**Tester:** local
**Firmware version:** `0.0.0`
**Firmware commit:** `working-tree`
**Board:** `pico`
**Test date/time:** `2026-09-20T11:18:18+00:00`
**Wiring:** Self-test stages 1-4
**RTS/CTS:** `disabled`

#### HID Results

- HID status monitor: valid v15 reports received through `/dev/hidraw7`
- HID firmware version: `0.0.0`
- RX overrun counts: `cdc0=0 cdc1=0 cdc2=0 cdc3=0 cdc4=0 cdc5=0`
- Health flags: `cdc1` reports `rx_error`; other channels report no RX error

#### Notes

The host HID issue was fixed by resolving hidapi's Linux interface identifier
(`1-3:1.12`) to `/dev/hidraw7` and using a hidraw fallback when the installed
libusb-backed hidapi cannot open that interface. HID verification now works.
The `cdc1 rx_error` flag needs separate hardware investigation before marking
the full bring-up as `PASS`.

---

## 2026-09-20T11:05:00+00:00 - pico - Initial Functional Test Verification

**Result:** `PARTIAL`
**Tester:** local
**Firmware version:** unknown
**Firmware commit:** `working-tree`
**Board:** `pico`
**Test date/time:** `2026-09-20T11:05:00+00:00`
**Wiring:** Self-test stages 1-4
**RTS/CTS:** `disabled`

#### Serial Functional Results

All four stages passed at 115200 baud:

- Debug Probe to HW UART0: bidirectional pass
- HW UART1 to PIO UART2: bidirectional pass
- PIO UART3 to PIO UART4: bidirectional pass
- PIO UART5 loopback: pass

#### HID Health Check

Not verified. hidapi enumerated the PicoUart HID interface but could not open
the device path (`1-3:1.12` / `/dev/hidraw7`) in this environment.

#### Notes

Serial access succeeded through the `dialout` group. Complete the HID health
check before marking this initial test as a full `PASS`.

---

## 2026-09-20T11:03:08+00:00 - pico - Functional Test

**Result:** `FAIL`
**Tester:** local
**Firmware version:** unknown
**Firmware commit:** `working-tree`
**Board:** `pico`
**Test date/time:** `2026-09-20T11:03:08+00:00`
**Wiring:** Self-test stages 1-4
**RTS/CTS:** `disabled`

#### Stage Results

| Stage | Exit code | Result |
| --- | ---: | --- |
| Debug Probe to HW UART0 | 2 | FAIL |

#### Command Output

```text
[Debug Probe to HW UART0] exit=2
Serial setup failed: [Errno 13] Permission denied: '/dev/serial/by-id/usb-PicoUart_PicoUart_CDC+HID_PIO_8N1_5303284748A07A1C-if00'

```

---

## 2026-09-20T11:04:13+00:00 - pico - Functional Test

**Result:** `PASS`
**Tester:** local
**Firmware version:** unknown
**Firmware commit:** `working-tree`
**Board:** `pico`
**Test date/time:** `2026-09-20T11:04:13+00:00`
**Wiring:** Self-test stages 1-4
**RTS/CTS:** `disabled`

#### Stage Results

| Stage | Exit code | Result |
| --- | ---: | --- |
| Debug Probe to HW UART0 | 0 | PASS |
| HW UART1 to PIO UART2 | 0 | PASS |
| PIO UART3 to PIO UART4 | 0 | PASS |
| PIO UART5 loopback | 0 | PASS |

#### Command Output

```text
[Debug Probe to HW UART0] exit=0
Testing stage1-debug-probe-hw-uart at 115200 baud
PASS pico-to-peer: 117 bytes
PASS peer-to-pico: 117 bytes

[HW UART1 to PIO UART2] exit=0
Testing stage2-hw-to-pio at 115200 baud
PASS pico-to-peer: 117 bytes
PASS peer-to-pico: 117 bytes

[PIO UART3 to PIO UART4] exit=0
Testing stage3-pio-to-pio at 115200 baud
PASS pico-to-peer: 117 bytes
PASS peer-to-pico: 117 bytes

[PIO UART5 loopback] exit=0
Testing stage4-pio-loopback loopback at 115200 baud
PASS pico-loopback: 118 bytes

```

---

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