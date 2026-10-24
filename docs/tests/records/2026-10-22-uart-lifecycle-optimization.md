# UART Lifecycle Optimization Check

## Scope

Development validation on Pico/RP2040 serial `5303284748A07A1C`, measured at 125 MHz, using the fixed fixture with
RTS/CTS disabled. No wiring, clock profile, or RX snapshot-copy changes were made between variants. Firmware version
was `0.0.0`. Baseline source was `c71982775ee69fe396aabd83cbb81057a61d1866`; the candidate used that source with
the immediate-start, polling, and host setup changes in its recorded dirty worktree.

- [Baseline full HIL record](2026-10-22-202929Z-pico-125mhz-hil.md): 250 ms firmware debounce and fixed host setup waits.
- [Candidate full HIL record](2026-10-22-203712Z-pico-125mhz-hil.md): immediate worker startup and readiness-driven setup.

Both artifacts were flashed through probe `50543165611F6C9C`; OpenOCD reported `Verified OK`. Their paths and hashes
are authoritative in the linked HIL records. Both full fixture runs passed all four functional stages and all six
streams at 115200, 294400, and 396800 baud, for five seconds per rate.

## Reopen and Isolation

The same local profiling procedure ran at `20:30:14Z` for baseline and `20:37:21Z` for candidate:

1. Open CDC1 loopback at 115200 baud, and configure CDC5 at 230400 baud with DTR low.
2. Leave CDC5 closed for at least 1200 ms while a bounded readiness wait times out as expected. Require no current
   initialization/control error, assert DTR, and require the deferred 230400 baud settings to become ready.
3. Run an eight-second CDC1 stream of verified 128-byte blocks with two-second block timeouts.
4. While that stream runs, cycle CDC5 DTR low/high 20 times without an intentional closed delay. Measure from DTR
   assertion until the HID readiness wait returns; verify an eight-byte loopback payload after every open.
5. Compare before/after fault counts and traffic totals. Require no new faults, no counter regression, and successful
   unaffected CDC1 traffic. Restore TTY settings and close both ports after measurement.

| Measurement | Baseline | Candidate |
| --- | ---: | ---: |
| Reopen cycles verified | 20 | 20 |
| Minimum reopen-to-ready | 256.23 ms | 1.23 ms |
| Mean reopen-to-ready | 258.80 ms | 1.51 ms |
| Maximum reopen-to-ready | 260.02 ms | 2.10 ms |
| CDC1 verified bytes during isolation check | 85376 | 85504 |
| New init/control/RX/overrun faults | 0 | 0 |
| Traffic counters monotonic | Yes | Yes |
| Delayed closed configuration | PASS | PASS |

Mean observed reopen latency decreased by approximately 99.4%. This measures host-visible readiness including USB,
HID access, and host scheduling, not just peripheral initialization time. It is not a hard real-time upper bound.
Local raw samples and procedure are retained under `build/uart-review-profile-{baseline,candidate}.json` and
`build/uart-review-profile.py`; these ignored files are not distributed with the repository.

## Throughput and Polling

Summing the six reported stream rates at 396800 baud gives 225687.0 B/s baseline versus 225702.0 B/s candidate.
The approximately 0.007% difference is not evidence of a throughput improvement; tested throughput was effectively
unchanged, with clean health and no overruns. This sweep does not establish a rate ceiling or USB saturation.

Native lock-hook regressions verify that each idle lifecycle/control sweep now takes one lock per port instead of
two, and idle control sweeps do not advance telemetry sequence counters. Payload paths and snapshot copying retain
their existing synchronization. CPU telemetry was collected, but variant samples have different warm-up distributions;
no CPU-percentage improvement is claimed. More aggressive batching or RX-copy removal is not justified by this check.

## Limits

These are short development checks, not release qualification or soak testing. Pico 2 was compiled, not physically
tested. Startup/resource failure and GPIO cleanup assertions were exercised natively, not electrically fault-injected.
Generic external senders active during open and arbitrary wiring/noise conditions are not covered by this fixture.
Aggregate USB full-speed bandwidth remains a system limit that MCU overclocking alone cannot remove.