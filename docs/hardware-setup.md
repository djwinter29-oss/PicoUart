# Hardware Setup

How to wire UART devices to a PicoUart board and find its serial ports. The pin assignments come from
[uart_config.c](../firmware/src/config/uart_config.c).

## Supported Boards

| Board | MCU | Firmware image |
| --- | --- | --- |
| Raspberry Pi Pico | RP2040 | `pico` |
| Raspberry Pi Pico W | RP2040 | `pico` |
| Raspberry Pi Pico 2 | RP2350 | `pico2` |
| Raspberry Pi Pico 2 W | RP2350 | `pico2` |

<img src="assets/raspberry-pi-pico.jpg" alt="Raspberry Pi Pico" width="240" />
<img src="assets/raspberry-pi-pico-2.jpg" alt="Raspberry Pi Pico 2" width="240" />

W boards have no separate image. They run the `pico` or `pico2` image on the same pins and report Pico or Pico 2 over
HID. Wi-Fi and Bluetooth are unused, and LED identification does nothing because the W LED is wired through the
wireless chip. Release HIL testing covers Pico and Pico 2 only.

## Installing Firmware

For first setup or an update, download the image listed above from
[GitHub Releases](https://github.com/djwinter29-oss/PicoUart/releases). Hold **BOOTSEL** while connecting the USB
cable, then copy the `.uf2` file to the `RPI-RP2` or `RP2350` drive. The board reboots when the copy finishes. On
Linux, configure [HID permissions](#linux-hid-permissions) before checking the version. With the Python host
installed, run `pico-uart version` ([CLI guide](../host/python/README.md#command-line)) before resuming UART traffic.

The [HIL evidence index](tests/records/README.md#evidence-index) lists which firmware builds passed hardware tests.

## Port Map

The host sees six USB CDC ACM ports. Each CDC port maps to one target-side UART:

| USB CDC | UART  | Backend        | TX   | RX   | Optional RTS | Optional CTS |
| ------- | ----- | -------------- | ---- | ---- | ------------ | ------------ |
| CDC0    | UART0 | Hardware UART0 | GP0  | GP1  | GP3          | GP2          |
| CDC1    | UART1 | Hardware UART1 | GP4  | GP5  | GP7          | GP6          |
| CDC2    | UART2 | PIO UART       | GP8  | GP9  | GP10         | GP11         |
| CDC3    | UART3 | PIO UART       | GP12 | GP13 | GP14         | GP15         |
| CDC4    | UART4 | PIO UART       | GP16 | GP17 | GP18         | GP19         |
| CDC5    | UART5 | PIO UART       | GP20 | GP21 | GP22         | GP26         |

Normal wiring needs only TX, RX, and ground. The RTS/CTS pins are reserved, but flow control is off in the default
firmware; enabling it is a per-port build setting listed under
[firmware configuration](../firmware/build-and-config.md#configuration).

## Wiring Rules

- Connect PicoUart TX to the peer RX.
- Connect PicoUart RX to the peer TX.
- Share ground between PicoUart and every UART peer.
- Connect RTS to the peer's CTS, and CTS to the peer's RTS, only when flow control is enabled and tested.
- Use 3.3 V UART logic. Never connect 5 V or RS-232 levels directly to RP2040 or RP2350 GPIOs.

For the fixed loopback wiring used in hardware tests, see [HIL fixture setup](tests/hil-fixture-setup.md).

## Power

Do not power six external targets from the Pico unless the supply is rated for their combined current. For a carrier
with target-power output, check its documented current limit and protection before connecting devices.

## Find the Serial Ports

Host port numbers such as `/dev/ttyACM*` or `COMx` depend on enumeration order, so identify each CDC port by its USB
interface instead. CDCn uses USB interface number 2×n and the interface name below, from the
[USB descriptors](../firmware/src/usb/usb_descriptors.c):

| USB CDC | Interface number | Linux `by-id` suffix | Interface name |
| ------- | ---------------- | -------------------- | -------------- |
| CDC0    | 0                | `-if00`              | `CDC0 HW`      |
| CDC1    | 2                | `-if02`              | `CDC1 HW`      |
| CDC2    | 4                | `-if04`              | `CDC2 PIO 8N1` |
| CDC3    | 6                | `-if06`              | `CDC3 PIO 8N1` |
| CDC4    | 8                | `-if08`              | `CDC4 PIO 8N1` |
| CDC5    | 10               | `-if0a`              | `CDC5 PIO 8N1` |

On Linux, use the stable paths from `ls -l /dev/serial/by-id/`; the suffix is the hexadecimal interface number. On
other systems, match the interface number or name shown by the OS device tools. With several boards attached, the
board's unique USB serial number in the device name distinguishes them.

After wiring a peer, confirm all six CDC ports enumerate. Open the peer's CDC port with DTR asserted; that opens its
UART. Exchange a known byte pattern with the peer using your serial tool before relying on the connection. Seeing the
ports alone confirms USB enumeration, not UART wiring.

Example observed Linux entries (board serial redacted; `/dev/ttyACM*` assignments vary):

```text
usb-djwinter_PicoUart_<board-serial>-if00 -> ../../ttyACM2
usb-djwinter_PicoUart_<board-serial>-if02 -> ../../ttyACM3
usb-djwinter_PicoUart_<board-serial>-if04 -> ../../ttyACM4
usb-djwinter_PicoUart_<board-serial>-if06 -> ../../ttyACM5
usb-djwinter_PicoUart_<board-serial>-if08 -> ../../ttyACM6
usb-djwinter_PicoUart_<board-serial>-if0a -> ../../ttyACM7
```

## Linux HID Permissions

Linux may restrict access to the board's HID interface. Add a udev rule for the primary group of the account that will
run the host application. Find that group with `id -gn`, then replace both instances of `YOUR_GROUP` below:

```text
SUBSYSTEM=="usb", ENV{DEVTYPE}=="usb_device", ATTR{idVendor}=="2e8a", ATTR{idProduct}=="116b", GROUP="YOUR_GROUP", MODE="0660", TAG+="uaccess"
SUBSYSTEM=="hidraw", KERNEL=="hidraw*", ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="116b", GROUP="YOUR_GROUP", MODE="0660", TAG+="uaccess"
```

Save the lines as `/etc/udev/rules.d/70-picouart-hid.rules`, then reload the rules and reconnect the board:

```sh
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=usb --attr-match=idVendor=2e8a --attr-match=idProduct=116b
sudo udevadm trigger --subsystem-match=hidraw
sudo udevadm settle
```

The rule grants read/write access to the selected group and the active desktop user. It does not change CDC serial-port
permissions. For repository setup commands, see [tools/README.md](../tools/README.md#linux-hid-permissions).

## PicoUART6 Carrier Compatibility

With flow control disabled, PicoUart's six UART TX/RX pairs match the published pinout of
[8086net/pico-sexa-uart-bridge](https://github.com/8086net/pico-sexa-uart-bridge). This provides UART TX/RX pin
compatibility with the PicoUART6 carrier sold on [Lectronz](https://lectronz.com/products/picouart6) and
[The Pi Hut](https://thepihut.com/products/picouart6). The carrier's extra features are not supported:

- The carrier's UART activity, I2C activity, and GPIO-controlled power LEDs are not driven by PicoUart. The Pico's own
  identification LED is separate.
- The STEMMA QT/Qwiic connector has no firmware I2C support: PicoUart does not expose a USB-to-I2C interface and is not
  compatible with the original firmware's `i2c-tiny-usb` functionality.
- Keep RTS/CTS disabled on this carrier. PicoUart's optional flow-control assignments overlap the carrier's LED and I2C
  wiring; enabling them requires a separately reviewed pin map and hardware setup.

## GPIO Availability Notes

- This allocation targets Pico-class boards that expose GP0-GP22 and GP26-GP28 on headers.
- GP23 and GP24 are not used because they are not generally available on standard Pico headers.
- GP25 is reserved for the selected board's default LED when `PICO_DEFAULT_LED_PIN` is defined.
- GP26 is used as UART5 CTS when PIO flow control is enabled. In that configuration it is not available as an ADC input.
- GP27 and GP28 remain free for future use.

## Photo Credits

Pico photo by Misael Reséndiz
([source](https://commons.wikimedia.org/wiki/File:Raspberry_Pi_Pico.jpg),
[CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)); Pico 2 photo by SparkFun Electronics
([source](https://commons.wikimedia.org/wiki/File:DEV-26124-PICO-2-angle.jpg),
[CC BY 2.0](https://creativecommons.org/licenses/by/2.0/)).
