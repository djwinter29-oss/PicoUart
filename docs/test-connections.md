# Test Connections

This setup verifies PicoUart channels using a Raspberry Pi Debug Probe and
direct TX-to-RX loopback jumpers.

## UART0 To Debug Probe

PicoUart UART0 is exposed to the USB host as CDC0 and uses the hardware UART on
GP0 and GP1.

| PicoUart signal | Pico GPIO | Debug Probe UART signal |
| --- | --- | --- |
| TX | GP0 | RX |
| RX | GP1 | TX |
| GND | GND | GND |

Cross TX and RX. Do not connect a signal labelled TX to another TX signal.

## Direct Pico Cross-Connection And Loopback

Cross-connect UART2 and UART3 to test two independent PIO UART channels in both
directions. Keep UART5 self-looped to test its own complete
CDC-to-UART-TX-to-UART-RX-to-CDC path without a second UART device.

| Test | USB CDC | Jumper wiring |
| --- | --- | --- |
| UART2 to UART3 cross-connection | CDC2 and CDC3 | GP8 to GP13; GP12 to GP9 |
| UART5 loopback | CDC5 | GP20 to GP21 |

Do not connect these GPIOs to power or ground while the test jumpers are fitted.

## Run The Loopback Tests

Flash the board, connect the USB device port, and identify the six CDC devices.
The devices are normally ordered CDC0 through CDC5, but `/dev/ttyACM` numbering
can change when other USB serial devices are attached.

```sh
ls -l /dev/ttyACM*
```

Run the existing marker test. Replace each placeholder with the device
corresponding to that CDC interface.

```sh
# UART0 through the Debug Probe UART connection.
python3 tools/linux/serial_bridge_test.py \
   --pico-port <cdc0> --peer-port /dev/ttyACM0 \
   --label uart0-debug-probe

# UART2 and UART3 cross-connection.
python3 tools/linux/serial_bridge_test.py \
   --pico-port <cdc2> --peer-port <cdc3> \
   --label uart2-uart3-cross

# UART5 direct loopback.
python3 tools/linux/serial_bridge_test.py \
   --pico-port <cdc5> --loopback --label uart5-gp20-gp21
```

UART0 and the UART2-to-UART3 cross-connection must each print both
`PASS pico-to-peer` and `PASS peer-to-pico`. The UART5 loopback must print
`PASS pico-loopback`.

## Concurrent Performance Benchmark

The standard functional tests above use 115200 baud, which is sufficient for
bring-up. Use the performance benchmark after they pass to apply load to all
available test links simultaneously.

For each PIO rate, the benchmark runs these streams concurrently:

- UART0 CDC0 to the Debug Probe and Debug Probe to CDC0 at 115200 baud.
- UART2 CDC2 to UART3 CDC3 and UART3 CDC3 to UART2 CDC2 at the selected rate.
- UART5 CDC5 through the GP20-to-GP21 loopback at the selected rate.

Each stream sends deterministic 1024-byte blocks for 10 seconds by default,
checks every returned byte, and reports verified bytes plus measured throughput.
The default rate list is 9600, 19200, 38400, 57600, 115200, 230400, 460800, and
921600 baud.

```sh
python3 tools/linux/serial_stress_benchmark.py \
  --uart0-pico <cdc0> --uart0-peer /dev/ttyACM0 \
  --uart2 <cdc2> --uart3 <cdc3> --uart5 <cdc5>
```

Use `--rates` to retry one or more PIO rates and `--duration` to use a longer
load window. For example, apply a 30-second run at 460800 and 921600 baud:

```sh
python3 tools/linux/serial_stress_benchmark.py \
  --uart0-pico <cdc0> --uart0-peer /dev/ttyACM0 \
  --uart2 <cdc2> --uart3 <cdc3> --uart5 <cdc5> \
  --rates 460800,921600 --duration 30
```

The Debug Probe peer remains at its proven 115200 baud rate throughout this
benchmark. This keeps external-peer limitations from masking the PIO UART rate
results.

## Bring-Up Check

1. Flash PicoUart and connect its USB device port to the host.
2. Confirm that all six CDC devices enumerate.
3. Fit GP8-to-GP13 and GP12-to-GP9 for the UART2-to-UART3 cross-connection,
   plus GP20-to-GP21 for the UART5 loopback.
4. Run the UART0 Debug Probe test, the UART2-to-UART3 cross-connection test,
   and the UART5 loopback test.
5. Run the concurrent performance benchmark when the 115200 baud tests pass.
6. Remove the test jumpers before connecting external UART targets.

The current firmware enables RTS/CTS on hardware UART0/UART1. Debug Probe and
PIO loopback tests only need TX, RX, and GND; unconnected CTS still allows TX
because the pin is pulled down. Cross-connect RTS/CTS when validating flow
control against a peer that supports it. PIO UART RTS/CTS pins remain reserved
only.