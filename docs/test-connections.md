# Test Connections

This setup verifies PicoUart channels using a Raspberry Pi Debug Probe and
selected direct TX-to-RX loopback jumpers.

## UART0 To Debug Probe

PicoUart UART0 is exposed to the USB host as CDC0 and uses the hardware UART on
GP0 and GP1.

| PicoUart signal | Pico GPIO | Debug Probe UART signal |
| --- | --- | --- |
| TX | GP0 | RX |
| RX | GP1 | TX |
| GND | GND | GND |

Cross TX and RX. Do not connect a signal labelled TX to another TX signal.
UART0 is tested only through the external Debug Probe peer. It is not part of a
direct Pico loopback.

## Direct Pico Cross-Connection And Loopback

Cross-connect UART2 and UART3 to test two independent PIO UART channels in both
directions. Add loopbacks only on the channels you want to exercise.

The current bench wiring used for bring-up keeps these links fitted:

- UART0 to the Debug Probe UART
- UART2 to UART3 cross-connection
- UART5 loopback

UART1 and UART4 loopbacks are optional and are not fitted on this bench.

| Test | USB CDC | Jumper wiring |
| --- | --- | --- |
| UART1 loopback (optional) | CDC1 | GP4 to GP5 |
| UART2 to UART3 cross-connection | CDC2 and CDC3 | GP8 to GP13; GP12 to GP9 |
| UART4 loopback (optional) | CDC4 | GP16 to GP17 |
| UART5 loopback | CDC5 | GP20 to GP21 |

Do not connect these GPIOs to power or ground while the test jumpers are fitted.

## Run The Loopback Tests

Flash the board, connect the USB device port, and identify the six CDC devices.
The devices are normally ordered CDC0 through CDC5, but `/dev/ttyACM` numbering
can change when other USB serial devices are attached.

```sh
ls -l /dev/serial/by-id
```

Prefer stable `/dev/serial/by-id/...` paths. Do not assume `/dev/ttyACM*`
numbering is stable when other USB serial devices are attached.

```sh
# UART0 through the Debug Probe UART connection.
python3 tools/linux/serial_bridge_test.py \
   --pico-port /dev/serial/by-id/<pico-uart-cdc0> \
   --peer-port /dev/serial/by-id/<debug-probe-uart> \
   --label uart0-debug-probe

# UART2 and UART3 cross-connection.
python3 tools/linux/serial_bridge_test.py \
  --pico-port /dev/serial/by-id/<pico-uart-cdc2> \
  --peer-port /dev/serial/by-id/<pico-uart-cdc3> \
  --label uart2-uart3-cross

# Optional UART1 / UART4 loopbacks. Skip these when those jumpers are not fitted.
python3 tools/linux/serial_bridge_test.py \
  --pico-port /dev/serial/by-id/<pico-uart-cdc1> \
  --loopback --label uart1-gp4-gp5
python3 tools/linux/serial_bridge_test.py \
  --pico-port /dev/serial/by-id/<pico-uart-cdc4> \
  --loopback --label uart4-gp16-gp17

# UART5 direct loopback.
python3 tools/linux/serial_bridge_test.py \
  --pico-port /dev/serial/by-id/<pico-uart-cdc5> \
  --loopback --label uart5-gp20-gp21
```

UART0 and the UART2-to-UART3 cross-connection must each print both
`PASS pico-to-peer` and `PASS peer-to-pico`. Each loopback must print
`PASS pico-loopback`.

## Concurrent Performance Benchmark

The standard functional tests above use 115200 baud, which is sufficient for
bring-up. Use the performance benchmark after they pass to apply load to all
available test links simultaneously. The current bench has also been validated
at `1,000,000` baud on UART0, UART2↔UART3, and UART5.

For each selected rate, the benchmark runs these streams concurrently:

- UART0 CDC0 to the Debug Probe and Debug Probe to CDC0 at 115200 baud.
- UART2 CDC2 to UART3 CDC3 and UART3 CDC3 to UART2 CDC2 at the selected rate.
- UART5 CDC5 through the GP20-to-GP21 loopback at the selected rate.
- Optional UART1 / UART4 loopbacks when `--uart1` / `--uart4` are passed.

Each stream sends deterministic 1024-byte blocks for 10 seconds by default,
checks every returned byte, and reports verified bytes plus measured throughput.
The default rate list is 9600, 19200, 38400, 57600, 115200, 230400, 460800,
921600, and 1000000 baud.

```sh
python3 tools/linux/serial_stress_benchmark.py \
  --uart0-pico /dev/serial/by-id/<pico-uart-cdc0> \
  --uart0-peer /dev/serial/by-id/<debug-probe-uart> \
  --uart2 /dev/serial/by-id/<pico-uart-cdc2> \
  --uart3 /dev/serial/by-id/<pico-uart-cdc3> \
  --uart5 /dev/serial/by-id/<pico-uart-cdc5>
# Optional when jumpers are fitted:
#   --uart1 /dev/serial/by-id/<pico-uart-cdc1> \
#   --uart4 /dev/serial/by-id/<pico-uart-cdc4>
```

Use `--rates` to retry one or more rates and `--duration` to use a longer
load window. For example, apply a 30-second run at 460800 and 921600 baud:

```sh
python3 tools/linux/serial_stress_benchmark.py \
  --uart0-pico /dev/serial/by-id/<pico-uart-cdc0> \
  --uart0-peer /dev/serial/by-id/<debug-probe-uart> \
  --uart2 /dev/serial/by-id/<pico-uart-cdc2> \
  --uart3 /dev/serial/by-id/<pico-uart-cdc3> \
  --uart5 /dev/serial/by-id/<pico-uart-cdc5> \
  --rates 460800,921600 --duration 30
# Optional: --uart1 / --uart4 when those loopbacks are wired
```

When the Debug Probe path needs extra margin, keep UART0 at 115200 with the
default benchmark settings. On the current bench, `--uart0-baud 1000000` is
also valid and has passed repeated smoke tests plus 120-second concurrent soaks.

## Bring-Up Check

1. Flash PicoUart and connect its USB device port to the host.
2. Confirm that all six CDC devices enumerate.
3. Fit GP8-to-GP13 and GP12-to-GP9 (UART2↔UART3) plus GP20-to-GP21 (UART5).
4. Optionally fit GP4-to-GP5 (UART1) and GP16-to-GP17 (UART4) when those
   loopbacks are part of the bench.
5. Run the UART0 Debug Probe test, the UART2-to-UART3 cross-connection test,
   and the UART5 loopback. Run the UART1 / UART4 loopbacks only when those
   jumpers are fitted.
6. Run the concurrent performance benchmark when the 115200 baud tests pass.
   Pass `--uart1` / `--uart4` only when those optional loopbacks are wired.
7. Remove the test jumpers before connecting external UART targets.

The current firmware leaves RTS/CTS disabled by default on hardware UART0 and
UART1. Debug Probe and PIO loopback tests therefore only need TX, RX, and GND.
Cross-connect RTS/CTS only when validating explicit hardware flow control with a
peer that supports it. PIO UART RTS/CTS pins are docs-reserved only (not
GPIO-owned by firmware).
