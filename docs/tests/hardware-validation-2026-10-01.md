## 2026-10-01 - pico - Nominal-clock functional and throughput validation

**Result:** `PARTIAL: verified payload runs PASS; post-test CDC5 RX overruns observed; development HIL, not release qualification`
**Firmware:** `0.0.0`, source commit `887c155d46591f2a10808fe4f7ec0cb646eb7663`
**Host-tool revision tested:** `cc121e8` (`first_receive_*` now records the first nonempty read)
**Board:** Raspberry Pi Pico / RP2040; BY25Q16ES 2 MiB; 125 MHz stock clock
**Test time:** `2026-10-01` (UTC)
**Wiring:** staged 4-link fixture: Debug Probe↔HW UART0; HW UART1↔PIO UART2; PIO UART3↔PIO UART4; CDC5 loopback
**SWD probe:** `50543165611F6C9C`; Pico USB serial: `5303284748A07A1C`
**Artifact:** `build/firmware-hardware-validation/pico_uart.elf`
**ELF SHA-256:** `6f470dd6b07125ac2f02b08f06fedd0b4ee8d4c84dea8096fabd481320858bbc`
**UF2 SHA-256:** `35743181ac7cbeeb11f461204176ebd781c5021785ca20ef643ef67fc7454fbe`
**Flash:** OpenOCD SWD; BY25Q16ES ID `0x154068` detected, programmed, verified (`Verified OK`), reset.

### Configuration and functional checks

- USB build verifier: 6 CDC + 1 HID; ARM artifacts valid.
- Four staged functional links at 115200 baud, 8N1, 1024-byte payloads: all pass in both directions / loopback as applicable.
- HID monitor in the functional runner: firmware `0.0.0`; health `cdc0=0x01`, `cdc1=0x01`, `cdc2`–`cdc5=0x21`; all overrun counts zero; monitor exit 0.
- Host suite: `/usr/bin/python3 -m pytest -o addopts='' host/python/tests -q` — `163 passed`.

### Isolated pair throughput

Stages 2 (HW↔PIO) and 3 (PIO↔PIO), one direction at a time and full duplex; 3 × 10 seconds per point, 1024-byte payloads, 8-second settle. Every direction passed at each baud rate.

| Baud | Single direction, verified bytes / 10 s | Full duplex, each direction / 10 s |
| ---: | ---: | ---: |
| 115200 | 113,664 (both stages) | 114,688 (both stages) |
| 230400 | 224,256–225,280 | 227,328 (both stages) |
| 460800 | 438,272–441,344 | 444,416 (both stages) |

### Concurrent streams

All seven configured streams passed 3/3 runs at each rate (10-second runs, 1024-byte payloads, 8-second settle): Debug Probe↔UART0, UART1↔UART2 (both directions), UART3↔UART4 (both directions), and UART5 loopback. UART0↔probe was held at 115200 baud; the tested main links were set to 115200, 230400, and 460800 baud. At 460800, UART1↔UART2 and UART3↔UART4 delivered 439,296–441,344 bytes per 10 seconds per direction; UART5 loopback delivered 440,320–441,344 bytes/10 seconds. No payload mismatch or timeout was reported. The concurrent benchmark recorded first-receive monotonic timestamps for all streams; the timing change was exercised, but no latency acceptance criterion was applied.

UART5 verified loopback evidence is included in the functional runner and all nine concurrent logs; no separate sustained isolated-loopback PASS is claimed.

### Evidence and limitations

Raw logs and artifact metadata: `docs/tests/raw/hardware-test-2026-10-01-cc121e8/`. The functional runner was invoked without metadata options, so its printed command labels show board/version/commit `unknown`; the exact flashed artifact and source revision are recorded above. Initial standalone `serial_bridge_test.py --loopback --flood-seconds 10` attempts exited 2 with serial write timeouts at all three rates (3 attempts each). These options were accepted by the tool; their root cause was not established. Later short loopback and concurrent verified-payload runs passed, but that does not erase these failures. This is one RP2040 at 125 MHz on one fixture, covering the listed rates only; it does not validate Pico 2/RP2350 or constitute release qualification. No rate above 460800 is claimed.


### Post-test health caveat

After the complete sequence, `/usr/bin/python3 host/python/src/pico_uart_hid.py overruns` reported `cdc0=0 cdc1=0 cdc2=0 cdc3=0 cdc4=0 cdc5=10984`. A subsequent two-second HID monitor reported CDC5 `0x61[ready,pio,rx_overrun]`, ring peak 4080; other ports remained ready. The counter was zero during the initial functional runner, but no before/after snapshots were taken around each flood/concurrent point. The responsible phase is therefore unknown. Payload passes cannot be described as an all-health-clean hardware PASS. No firmware fix or performance ceiling beyond the tested points is claimed.
