# CDC/UART Session and Tuning Check

## Scope and Artifacts

Development check on Pico/RP2040 serial `5303284748A07A1C`, live clock 125 MHz, firmware `0.0.0`, probe
`50543165611F6C9C`, fixed six-port fixture, RTS/CTS disabled. Source baseline was
`0c7e70568c4ebe24eeeb752b89a39cec6c0c8309` plus the session/startup changes in the dirty worktree. Each artifact
was flashed with the repository loader and OpenOCD reported `Verified OK`; clock/identity were checked after enumeration.

| Variant | Artifact under build/ | ELF SHA-256 |
| --- | --- | --- |
| Profiled defaults | firmware-session-profile-default/pico_uart.elf | acbd7863c9ecad4f17fff9cd37544191d72aae265662ea8229c06d98b4f5f7eb |
| Profiled bridge budget 512 | firmware-session-profile-budget512/pico_uart.elf | fa57b098a9d302ca31180325d5d610a648612cda8cbbe62bf2930c372ae10a8d |
| Profiled DMA threshold 32 | firmware-session-profile-dma32/pico_uart.elf | 24fd97db5ef825a8974186934e7e6fb95e7305f6857bf83d3ee4d95906960bc2 |
| Uninstrumented defaults | firmware-uart-session-final-pico/pico_uart.elf | 3956a072f703c55b5ce7cc2fa179831258059e84e196ca2ec5f3db358b683807 |

The [final full HIL record](2026-10-22-213102Z-pico-125mhz-hil.md) is the authority for uninstrumented functional and
performance results: all four functional stages and all six concurrent streams passed at 115200, 396800, and 460800
baud, ten seconds per rate, with zero RX overruns and clean per-rate health. It does not establish a baud ceiling.

## Controlled Tuning

All profiling variants enabled `PICO_UART_PROFILE_TIMING=1`. Default bridge budget was 1024 bytes and default PIO TX DMA
threshold was 64 bytes. Each candidate changed only the value named above; board, clock, wiring, payload, and duration
were unchanged. Six verified streams ran five seconds per rate with 1024-byte blocks and three-second block timeouts.
Counters were zeroed through non-halting SWD before each rate and read through OpenOCD `read_memory` afterward.

| Variant | Baud | Verified bytes, six streams | Max verified block, ms | Core-1 max pass, us | Max port service gap, us |
| --- | ---: | ---: | ---: | ---: | ---: |
| Defaults | 396800 | 1127424 | 31.45 | 163 | 213 |
| Budget 512 | 396800 | 1127424 | 31.66 | 164 | 201 |
| DMA 32 | 396800 | 1125376 | 32.53 | 170 | 220 |
| Defaults | 460800 | 1293312 | 27.45 | 162 | 203 |
| Budget 512 | 460800 | 1291264 | 28.17 | 159 | 202 |
| DMA 32 | 460800 | 1293312 | 29.67 | 164 | 214 |

All runs verified data with no new fault-counter increments. Within each run, per-stream verified totals differed by at
most one 1024-byte block; this is not proof of a starvation-free upper bound. Reported cumulative RX ring peaks were
960 bytes on every port. Core-0 observed maximum passes were 580-683 us across the runs. The timing counters are
boot/interval maxima from intrusive profiling builds, not guaranteed worst-case execution times. Block latency includes
host scheduling and USB, and a final in-flight block can finish beyond nominal duration.

No candidate demonstrated a clear improvement: throughput counts were effectively unchanged and maximum block latency
was slightly higher for both candidates. Keep production defaults at bridge budget 1024 and DMA threshold 64.
The first profiling attempt completed one traffic rate but failed to parse this OpenOCD build's empty `mdw` output;
it is not counted. The corrected procedure used structured `read_memory`. One inter-command HID discovery failure
before a candidate run required identity rechecking; no comparison traffic ran during that failed query.

## Session Boundaries on Uninstrumented Firmware

The normal image was flashed before this separate bounded procedure:

1. Configure CDC0 TX at 230400 baud and queue a repeating marker before opening CDC2 RX. Stage CDC2 at 230400 with
   DTR low, then assert DTR and require applied 8N1 through HID. Receive a complete marker from the already-running
   sender. Result: PASS; CDC2 reported 230400 8N1 and all init/control/RX/overrun fault counts remained zero.
2. Queue 1024 bytes on CDC1 loopback, issue Linux USB device reset (`USBDEVFS_RESET`) on the USB device verified by
   serial number, close old handles, and require every channel to return to closed/not-ready state. Result: PASS;
   closed health was `[0x08, 0x08, 0x28, 0x28, 0x28, 0x28]`.
3. Reopen CDC1 after reset and verify a fresh 26-byte payload exactly. Result: PASS.

The native integrated target additionally injects a failed 8O2 start followed by reopen without a new line-coding
request. Real CDC/driver/control code must reseed the previous applied 8N1 settings and publish matching metadata.
Hardware resources were not electrically fault-injected.

## Evidence and Limits

Local procedures/samples are retained in ignored `build/uart-session-tune.py`, `build/uart-session-*.json`, and
`build/uart-session-boundaries.py`. The final uninstrumented ELF contains no profiling counter symbols. Normal builds
compile both Pico and Pico 2, but only this RP2040 was physically tested. These short checks are not soak or release
qualification. Marker synchronization does not guarantee arbitrary first-byte recovery for every continuously active
sender, wiring/noise condition, or host driver. RX snapshot copying, memory barriers, per-port bounds, and the 6 CDC
to 6 UART design remain intact.