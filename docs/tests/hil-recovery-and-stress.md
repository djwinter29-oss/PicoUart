# HIL Recovery and Stress Plan

This guide covers recovery and stress procedures beyond the baseline
[HIL test plan](hil-fixture-test-plan.md). Follow the
[hardware authorization rules](README.md#hardware-authorization) and test only the exact flashed image.

## Automated Loopback Recovery

The `recovery` command uses the CDC1 and CDC5 loopbacks. Close all CDC sessions first. The command checks that both
endpoints belong to the given USB serial, and it never resets or flashes the board.

```sh
tools/hil/.venv/bin/pico-uart-hil recovery \
  --serial "<usb-serial>" --cdc1 "/dev/serial/by-id/<cdc1>" --cdc5 "/dev/serial/by-id/<cdc5>" \
  --artifact /path/to/flashed/pico_uart.elf --output build/recovery.json
```

The default `--mode all` runs four checks; `--mode churn|reopen|stall|overflow` runs one:

- Configuration churn, with payloads checked on a sibling port
- Five close/reopen cycles, each with a distinct payload
- A 2048-byte read stall of at least 250 ms
- A deliberate 16384-byte overflow

To pass, all payloads must verify, with no unexpected faults on any port. In the overflow check, only CDC1 may count
drops. The retained bytes plus the drops must equal the input, the retained bytes must stay in order, and fresh
payloads must pass after the backlog drains. The overflow leaves loss history behind, so reflash before any clean-health
test. The output file is never overwritten. Use an outer timeout for unattended runs.

## Startup Observation

Record each cold power-on, SWD reset, watchdog recovery, or USB reset as its own observation:

```sh
tools/hil/.venv/bin/pico-uart-hil startup \
  --serial "<usb-serial>" --event swd-reset --artifact /path/to/flashed/pico_uart.elf \
  --timeout 20 --output build/startup-swd-01.json
```

The command does not trigger the event; you do. For a reset, start it right after the reset; for power-on, start it
before applying power. `--event` is a label. The command passes when the board reports a valid clock and all six
channels stay closed for 12 seconds (`--stable-seconds`), with uptime advancing in step with host time.

For startup acceptance, add `--cdc1 "/dev/serial/by-id/<cdc1>"`. This transmits UART data: it opens CDC1 on its
loopback, waits for readiness, and verifies a 154-byte payload. Without it, the check only proves the board
enumerates. Repeat each event type, keep failures as well as passes, and never lengthen firmware sleeps to hide an
unexplained reset.

## Manual Cases

These cases need a fixture source or authorization beyond the runners. Record the board, artifact hash, rate, payload
or burst size, stall duration, affected channels, and HID samples. If a case cannot be set up, report it as not tested.

### All Six Channels Loaded

Run the concurrent fixture. Collect service load, block latency, ring peaks, and faults. Use timing instrumentation for
service gaps, then repeat acceptance on uninstrumented firmware. Every stream must verify without new loss or errors at
the qualified load. USB-paced traffic does not establish wire saturation.

### Host Drain Stalled

Use a bounded, independently paced source. Pause reads on one RX channel while others continue; resume within the
capacity budget, then test a separate intentional-overflow interval. Within budget, bytes must arrive in order without
loss. Beyond budget, drops must be counted and traffic must recover, with no new faults on other channels.

### TX Backpressure

With a qualified flow-control fixture, pause a peer using supported CTS while host OUT writes fill the channel, then
resume it. Host writes must block or time out rather than lose bytes. Admitted bytes must stay ordered while other
channels continue.

### Close and Reopen During Traffic

Close one channel while other streams run. Reopen with staged settings, wait for HID readiness, then send a fresh
marker. Old-session payloads must not enter the new stream; settings must match and other channels must keep passing.
Queued bytes discarded on close need not be delivered.

### Session Loss with Pending Control

Keep a supported line-coding request pending and confirm it through HID; unplug the board or issue a targeted USB bus
reset. Repeat with all six channels active. Old handles must terminate or time out. After remount, old sessions must be
closed, settings and a fresh marker must verify, and canceled work must not cause a late timeout. A USB reset stops
every stream.

### Concurrent Configuration

Repeatedly change supported settings on other channels while an independently paced channel carries verified traffic.
Change both ends of crossed links. Record the maximum unaffected-port service gap and both cores' maximum pass times.
Each change must complete or time out explicitly, with no new faults on unaffected traffic. A mismatched crossed pair
is not a bridge failure.

## Known Gaps

Open hardware coverage:

- The fixture cannot test input that keeps arriving while the host has stopped reading. Every byte starts as a host
  write, so [Host Drain Stalled](#host-drain-stalled) needs an independently paced UART source.
- RTS/CTS has not been tested; there is no flow-control fixture yet, so [TX Backpressure](#tx-backpressure) is open.
- Pico 2 has much thinner coverage than Pico. Check the [evidence index](records/README.md#evidence-index) before
  relying on it; a Pico result does not carry over.
- Sustained DMA wrap/re-arm endurance with the normal transfer count remains untested. A shortened-countdown attempt
  produced invalid results; no safe injection procedure is validated yet.
- Watchdog recovery has one development observation, but no repeatable trigger procedure is documented. See the
  [review record](records/2026-10-23-uart-cdc-review-validation.md#configuration-isolation-and-recovery).
- Cold-power-on, watchdog, and USB-reset startup remain unqualified; see
  [Startup Observation](#startup-observation).
