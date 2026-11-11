# PicoUart

[![PR Check](https://github.com/djwinter29-oss/PicoUart/actions/workflows/pr-check.yml/badge.svg?branch=main)](https://github.com/djwinter29-oss/PicoUart/actions/workflows/pr-check.yml)
[![Release](https://github.com/djwinter29-oss/PicoUart/actions/workflows/release.yml/badge.svg)](https://github.com/djwinter29-oss/PicoUart/actions/workflows/release.yml)
[![License: BSD 3-Clause](https://img.shields.io/badge/License-BSD%203--Clause-blue.svg)](LICENSE)
![Targets: RP2040 and RP2350](https://img.shields.io/badge/Targets-RP2040%20%7C%20RP2350-2674b8)

**Six serial ports. One Pico. Diagnostics built in.**

PicoUart turns a stock Raspberry Pi Pico or Pico 2 into a six-port USB-to-UART bridge for bring-up benches and test
racks. Replace a tangle of single-port adapters with one board and one USB cable, keep your existing terminals and
scripts, and inspect bridge health without closing your serial connections.

**Quick links:** [Download firmware](https://github.com/djwinter29-oss/PicoUart/releases) ·
[Hardware setup](docs/hardware-setup.md) ·
[Open Direct Dashboard](https://djwinter29-oss.github.io/PicoUart/) ·
[Python package](https://pypi.org/project/pico-uart/) ·
[Measured performance](docs/tests/records/README.md#evidence-index)

## Why PicoUart?

A silent serial log is ambiguous: did the device stop talking, did the bridge drop bytes, or did the host stop
reading? A typical USB-serial adapter can't tell you which. PicoUart counts traffic on both sides of the bridge, per
channel, to help narrow down where traffic stopped. These counters provide evidence, not automatic root-cause
detection; your test runner still decides pass or fail.

| | What you get |
| --- | --- |
| **Six ports, one cable** | Six independent standard USB serial ports from a single Pico. No host app needed for data. |
| **Independent diagnostics** | UART/USB traffic counters, RX dropped-byte counts, and buffer peaks over separate USB HID. |
| **Evidence for CI** | Save JSON status snapshots and timed traffic logs next to your test results. |
| **Direct Dashboard** | Inspect bridge health without installing a host application. |

One host, six UART devices (logical connections, not a wiring diagram):

```text
Computer -- USB -- Pico / Pico 2 +- UART0 -- Device A
                                 |- UART1 -- Device B
                                 |- UART2 -- Device C
                                 |- UART3 -- Device D
                                 |- UART4 -- Device E
                                 `- UART5 -- Device F
```

![Direct Dashboard with simulated device identity, board configuration, runtime status, and six-channel UART/USB traffic.](docs/assets/pico-uart-dashboard.png)

_Direct Dashboard layout with simulated telemetry; illustrative, not a live hardware result._

## Before You Connect

- **Electrical limits:** use 3.3 V UART logic and a shared ground. Never connect 5 V or RS-232 signals directly.
- **Framing:** UART0-1 use hardware UARTs; UART2-5 use PIO and support **8N1 only**.
- **Flow control:** RTS/CTS is disabled by default.
- **Throughput:** all six channels share USB full-speed bandwidth. A configured baud rate is not a guarantee of
  sustained throughput; compare board, clock, workload, and duration in the
  [measured-performance evidence index](docs/tests/records/README.md#evidence-index).
- **Clock settings:** non-default profiles require explicit overclock acknowledgement. Stability is board-specific;
  results apply only to the tested artifacts and conditions.

Serial applications use USB CDC for UART data, baud rate, and framing. Dashboards use the separate HID interface for
diagnostics and limited board controls.

## Get Started

1. **Download firmware** from [Releases](https://github.com/djwinter29-oss/PicoUart/releases): choose the **`pico`**
   `.uf2` for Pico (RP2040) or **`pico2`** `.uf2` for Pico 2 (RP2350). These images target non-wireless boards; see
   [Hardware Compatibility](#hardware-compatibility) for W variants.
2. **Flash the board** using the [USB-drive flashing guide](docs/deployment/releasing.md#usb-drive-uf2).
   You need a USB cable, not a debug probe.
3. **Wire a UART device:** connect TX to the device's RX, RX to its TX, and ground to ground. Follow the
   [pin map and wiring rules](docs/hardware-setup.md) and observe the 3.3 V limit.
4. **Open the matching serial port** in your terminal or test script. Use the
   [port identification guide](docs/hardware-setup.md#find-the-serial-ports), then set baud rate and framing for your
   device.
5. **Check diagnostics (optional):** choose a [dashboard](#dashboards), connect, and select your board.

For each UART device, the signal connections are:

```text
PicoUart TX  -->  Device RX
PicoUart RX  <--  Device TX
PicoUart GND ---  Device GND
```

Use the [port map](docs/hardware-setup.md#port-map) to find the pins for your chosen UART.

**Setup checkpoint:** the host should list six PicoUart serial ports. That confirms USB enumeration, not UART wiring.
Confirm communication by exchanging expected data with your connected device at its required baud rate and framing.

If the serial port cannot be opened, check its OS permissions. Dashboard/HID access is separate; see
[dashboard permissions](#dashboards).

## Test Automation

The optional Python CLI reads diagnostics while your serial tools use the UARTs. With Python 3.10 or newer, follow the
[installation guide](host/python/README.md#quick-start), then record a status snapshot or a timed traffic log:

```sh
pico-uart status --json > uart-status.json
pico-uart monitor --duration 60 > uart-traffic.log
pico-uart overruns > uart-dropped-bytes.txt
```

These commands collect diagnostics; they do not start a serial test or capture UART payloads. Run your serial test
separately during monitoring. To measure dropped bytes for a run, record `overruns` before and after it and account for
any board reset.

A successful command does **not** prove that every channel is healthy or that reporting was continuous. Your runner
must evaluate the returned states and counters. Use one HID monitor per physical board; separate processes do not
share input reports.

See the [Python guide](host/python/README.md) for the API, output semantics, and multi-board selection.

## Dashboards

Choose the interface that fits your workflow. All three show bridge diagnostics; none is required for serial traffic.

| Dashboard | Choose it when | Connect the board to |
| --- | --- | --- |
| [Direct](#direct-dashboard) | You want browser-only access. | The computer running Chrome/Edge. |
| [Python](#local-python-dashboard) | You also want the Python API or CLI. | The computer running `pico-uart dashboard`. |
| [.NET](#local-net-dashboard) | You prefer a self-contained executable. | The computer running `PicoUart web`. |

For Python and .NET, the host application accesses USB; the browser displays its web interface without WebHID.
On Linux, run [HID permissions setup](tools/README.md#linux-hid-permissions) on the computer accessing USB, not
necessarily the computer displaying the dashboard. CDC serial-port permissions are separate.

<a id="browser-dashboard"></a>

### Direct Dashboard

Open the [Direct Dashboard](https://djwinter29-oss.github.io/PicoUart/), click **Connect**, and select your board.
Connect your browser directly to PicoUart over USB; no host application installation is needed.

> [!IMPORTANT]
> **WebHID requires desktop Google Chrome or Microsoft Edge.** Safari and Firefox are not supported.

See the [Direct Dashboard guide](host/direct/README.md) for controls and connection troubleshooting.

### Local Python Dashboard

Install [pico-uart](https://pypi.org/project/pico-uart/) with its dashboard extra, run `pico-uart dashboard`, and
open <http://127.0.0.1:5000/>.
See the [Python guide](host/python/README.md) for installation and connection troubleshooting.

### Local .NET Dashboard

Download a self-contained host from [Releases](https://github.com/djwinter29-oss/PicoUart/releases), extract the whole
archive, and run `./PicoUart web` on Linux/macOS or `.\PicoUart.exe web` in Windows PowerShell. Open
<http://127.0.0.1:5000/>; no Python or .NET installation is required.
See the [.NET host guide](host/dotnet/README.md) for supported downloads and connection troubleshooting.

## Hardware Compatibility

The prebuilt **`pico`** and **`pico2`** firmware images target the standard non-wireless boards:

<table align="center">
    <thead>
        <tr>
            <th scope="col" align="center">Raspberry Pi Pico</th>
            <th scope="col" align="center">Raspberry Pi Pico 2</th>
        </tr>
    </thead>
    <tbody>
        <tr>
            <td align="center">
                <img src="docs/assets/raspberry-pi-pico.jpg" alt="Raspberry Pi Pico reference board" width="240" />
            </td>
            <td align="center">
                <img src="docs/assets/raspberry-pi-pico-2.jpg" alt="Raspberry Pi Pico 2 reference board" width="240" />
            </td>
        </tr>
        <tr>
            <td align="center">RP2040 · firmware: <strong><code>pico</code></strong></td>
            <td align="center">RP2350 · firmware: <strong><code>pico2</code></strong></td>
        </tr>
    </tbody>
</table>

Reference boards, not PicoUart-specific assemblies. See [photo credits](#photo-credits).

**Pico W and Pico 2 W:** use the matching `pico_w` or `pico2_w`
[board-specific build](firmware/build-and-config.md#build) for USB/UART operation. Wi-Fi and Bluetooth are not enabled.
See the [wireless-board notes](docs/hardware-setup.md#wireless-boards) for the LED limitation and qualification scope.

**PicoUART6 carrier:** the default UART TX/RX pinout matches
[8086net/pico-sexa-uart-bridge](https://github.com/8086net/pico-sexa-uart-bridge) and the PicoUART6 hardware sold on
[Lectronz](https://lectronz.com/products/picouart6) and [The Pi Hut](https://thepihut.com/products/picouart6).
Carrier-board LEDs and USB-to-I2C are not supported; see the
[compatibility notes](docs/hardware-setup.md#picouart6-carrier-compatibility) before using it.

## Documentation

| Need | Guide |
| --- | --- |
| Connect UART devices | [Hardware setup](docs/hardware-setup.md) |
| Use the CLI, API, or dashboards | [Python](host/python/README.md), [.NET](host/dotnet/README.md), [Direct](host/direct/README.md) |
| Understand USB and UART behavior | [CDC/HID overview](docs/design/usb/cdc-hid-overview.md), [architecture](docs/design/architecture.md) |
| Build or modify firmware | [Firmware build guide](firmware/build-and-config.md), [development and testing](docs/development/firmware-testing.md) |
| Develop host applications | [Host development](docs/development/host-tools.md), [host app distribution](docs/deployment/host-app-distribution.md) |
| Understand PR and release checks | [GitHub Actions: validation and releases](docs/development/github-actions.md) |
| Publish or test a release | [Release qualification](docs/deployment/releasing.md), [host publishing](docs/deployment/host-app-publishing.md) |
| Review validation and hardware evidence | [Test guide](docs/tests/README.md), [dated HIL records](docs/tests/records/README.md) |
| Contribute | [Contributing](CONTRIBUTING.md), [Code of Conduct](CODE_OF_CONDUCT.md) |

Host tests do not replace physical hardware qualification; see the [test guide](docs/tests/README.md) for validation
scope and release gates.

## License

[BSD 3-Clause](LICENSE).

## Photo Credits

Pico photo by Misael Reséndiz
([source](https://commons.wikimedia.org/wiki/File:Raspberry_Pi_Pico.jpg),
[CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)); Pico 2 photo by SparkFun Electronics
([source](https://commons.wikimedia.org/wiki/File:DEV-26124-PICO-2-angle.jpg),
[CC BY 2.0](https://creativecommons.org/licenses/by/2.0/)).
