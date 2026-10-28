# PicoUart

[![PR Check](https://github.com/djwinter29-oss/PicoUart/actions/workflows/pr-check.yml/badge.svg?branch=main)](https://github.com/djwinter29-oss/PicoUart/actions/workflows/pr-check.yml)
[![Release](https://github.com/djwinter29-oss/PicoUart/actions/workflows/release.yml/badge.svg)](https://github.com/djwinter29-oss/PicoUart/actions/workflows/release.yml)
[![License: BSD 3-Clause](https://img.shields.io/badge/License-BSD%203--Clause-blue.svg)](LICENSE)
![Targets: RP2040 and RP2350](https://img.shields.io/badge/Targets-RP2040%20%7C%20RP2350-2674b8)

**Six UARTs. One Pico. One USB cable.** PicoUart turns a Raspberry Pi Pico or Pico 2 into six independent USB serial
ports, with host applications that monitor UART status and bridge health alongside your serial traffic. Built for
multi-device bring-up, test automation, and diagnosing failed test runs.

**Quick links:** [Download firmware](https://github.com/djwinter29-oss/PicoUart/releases) ·
[Hardware setup](docs/hardware-setup.md) ·
[Open browser dashboard](https://djwinter29-oss.github.io/PicoUart/) ·
[Python package](https://pypi.org/project/pico-uart/)

## Why PicoUart?

**A USB-serial adapter moves bytes. PicoUart also shows what the bridge saw.**

| | What you get |
| --- | --- |
| **Six serial ports, one cable** | Standard USB serial ports for your terminals, scripts, and test runners. No host app is needed for UART data. |
| **Visibility beside the data** | Per-UART health, traffic counters, RX overrun flags, and buffer peaks on a separate HID interface, apart from your serial ports. |
| **Test-automation friendly** | Save `pico-uart status --json` snapshots and `pico-uart monitor` logs next to each test run's results. |
| **Dashboards when you need them** | Watch all six UARTs live in the browser, local Python, or .NET app during bring-up and debugging. |

![WebHID dashboard with simulated device identity, board configuration, runtime status, and six-channel UART/USB traffic.](docs/assets/pico-uart-dashboard.png)

_Current WebHID layout with simulated telemetry; illustrative, not a live hardware result._

### When a test times out

Compare UART/USB traffic and RX dropped-byte counts before and after the run to help locate where traffic stopped.
PicoUart adds evidence, not automatic root-cause detection. Your runner still owns test sequencing and pass/fail.

## Before You Connect

- UART signals are **3.3 V logic**; do not connect 5 V or RS-232 signals directly.
- UART0-1 use hardware UARTs; UART2-5 use PIO and support **8N1 only**.
- RTS/CTS flow control is disabled by default.
- **Operating rates:** 460,800 baud per channel is a conservative six-port operating point supported by fixture testing.
  Six concurrent streams passed payload checks at configured rates up to 2 Mbaud on tested Pico artifacts; this does not
  establish sustained wire-rate throughput. See the [dated HIL results](docs/tests/records/README.md).
- **Shared bandwidth:** USB 2.0 full-speed's 12 Mbit/s is a signaling rate, not usable payload throughput. Higher-rate
  limits depend on combined traffic and firmware.
- **Overclocking:** A 200 MHz Pico profile has passed HIL testing, but stability is board-specific.
- Dashboards use HID for diagnostics and limited controls; serial applications use CDC for UART data and line settings.

## Get Started

1. **Get the firmware.** Download the **`pico`** `.uf2` for Raspberry Pi Pico (RP2040) or **`pico2`** `.uf2` for
   Raspberry Pi Pico 2 (RP2350) from [Releases](https://github.com/djwinter29-oss/PicoUart/releases).
   These images target non-wireless boards; see [Hardware Compatibility](#hardware-compatibility) for W variants.
2. **Flash over USB.** Hold **BOOTSEL** while plugging the board into USB, release it when the USB drive appears, then
   copy the `.uf2` file onto that drive. The board automatically reboots into PicoUart; no debug probe is needed.
   See [USB-drive flashing](docs/deployment/releasing.md#usb-drive-uf2) for upgrade and reset-button instructions.
3. **Connect the hardware.** Wire TX, RX, and ground using the [hardware setup](docs/hardware-setup.md), observing the
   3.3 V limit above.
4. **Open the serial ports.** Find the CDC port for each wired UART in the
   [port map](docs/hardware-setup.md#port-map), then [identify it on your host](docs/hardware-setup.md#find-the-serial-ports).
   Open it in your terminal or test script and set the baud rate and framing for the connected device.
5. **Inspect bridge health (optional).** Open the [browser dashboard](https://djwinter29-oss.github.io/PicoUart/) in
   desktop Chrome or Edge, click **Connect**, and select your board. No host app is needed for ordinary serial traffic.

If device access is denied, check OS permissions. Linux HID access uses the
[permissions setup](tools/README.md#linux-hid-permissions); CDC serial-port permissions are separate.

## Test Automation

The Python CLI reads HID status while your serial tools keep using the UARTs. Install it with Python 3.10 or newer in a
virtual environment. The monitoring-only example below records dropped-byte counts around a 60-second capture.
Run your existing serial test in a separate terminal during that capture; these commands do not start a test:

```sh
python -m pip install --pre pico-uart
pico-uart overruns > overruns-before.txt
pico-uart monitor --duration 60 > uart-traffic.log
pico-uart overruns > overruns-after.txt
```

`--pre` selects development builds; omit it for stable releases. Each monitor line includes all six channels.
Illustrative excerpt showing one channel:

```text
seq=412 cdc0 health=0x11[ready,cdc_open] uart_tx/rx=96/1520 cdc_tx/rx=1520/96 ring_peak=64
```

These are traffic statistics, not UART payloads; capture device messages with your serial tools. Each monitor line's
byte counts are deltas since the previous report, saturating at 65535. The `overruns` counts are cumulative since boot,
so compare before and after.

`monitor` exits non-zero if it receives no valid reports. A successful exit does **not** mean every UART is healthy:
your runner must evaluate health flags and counts. Only `status` supports `--json`.

<details>
<summary>Python API: save a status snapshot</summary>

Call this helper from your test setup or teardown:

```python
import json
from pathlib import Path
from pico_uart import PicoUartHid

def save_uart_status(path: Path) -> None:
    with PicoUartHid() as board:
        status = board.read_status(timeout_ms=1000)
    if status is None:
        raise RuntimeError("no PicoUart status report received")
    path.write_text(json.dumps(status, sort_keys=True))
```

See the [Python guide](host/python/README.md) for the full API.

</details>

## Dashboards

Use a dashboard for interactive bring-up and diagnosis. None is needed for serial traffic.

### Browser Dashboard

Launch [WebHID](https://djwinter29-oss.github.io/PicoUart/), click **Connect**, and select your board.
No Python or .NET installation is needed.

> [!IMPORTANT]
> **WebHID requires desktop Google Chrome or Microsoft Edge.** Safari and Firefox are not supported.
> Connect the board to the computer running the browser. Linux may need
> [HID permissions setup](tools/README.md#linux-hid-permissions).

See the [WebHID guide](host/webhid/README.md) for controls and connection troubleshooting.

### Local Python Dashboard

With the [pico-uart](https://pypi.org/project/pico-uart/) package installed as above, run `pico-uart web` and open
`http://127.0.0.1:5000/`. It does not require browser WebHID support. See the [Python guide](host/python/README.md) for
`pipx` and device permissions.

### Local .NET Dashboard

Download a self-contained host from [Releases](https://github.com/djwinter29-oss/PicoUart/releases), extract the whole
archive, and run `./PicoUart web` on Linux/macOS or `.\PicoUart.exe web` in Windows PowerShell. Open
`http://127.0.0.1:5000/`; no Python or .NET installation is needed, and browser WebHID support is not required.
See the [.NET host guide](host/dotnet/README.md) for supported downloads and device permissions.

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

<details>
<summary>Board photo credits</summary>

Reference boards, not PicoUart-specific assemblies. Pico photo by Misael Reséndiz
([source](https://commons.wikimedia.org/wiki/File:Raspberry_Pi_Pico.jpg),
[CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)); Pico 2 photo by SparkFun Electronics
([source](https://commons.wikimedia.org/wiki/File:DEV-26124-PICO-2-angle.jpg),
[CC BY 2.0](https://creativecommons.org/licenses/by/2.0/)).

</details>

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
| Understand USB and UART behavior | [CDC/HID overview](docs/design/usb/cdc-hid-overview.md), [architecture](docs/design/architecture.md) |
| Build or modify firmware | [Firmware build guide](firmware/build-and-config.md), [development and testing](docs/development/firmware-testing.md) |
| Develop host applications | [Host development](docs/development/host-tools.md), [host app distribution](docs/deployment/host-app-distribution.md) |
| Understand PR and release checks | [GitHub Actions: validation and releases](docs/development/github-actions.md) |
| Publish or test a release | [Release qualification](docs/deployment/releasing.md), [host publishing](docs/deployment/host-app-publishing.md) |
| Contribute | [Contributing](CONTRIBUTING.md), [Code of Conduct](CODE_OF_CONDUCT.md) |

Firmware is checked on a physical fixture ([test records](docs/tests/README.md)). Host tests do not replace hardware
qualification; results apply only to the exact artifacts and conditions recorded.

## License

[BSD 3-Clause](LICENSE).

The USB identity `cafe:4010` is for development/lab use, not commercial derivatives. See [SECURITY.md](SECURITY.md).
