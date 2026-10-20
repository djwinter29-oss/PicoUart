"""Pure decoders and constants for the PicoUart HID report contract."""

import struct


REPORT_ID_STATUS = 1
REPORT_ID_BOARD_STATUS = 3
REPORT_ID_COMMAND = 4
REPORT_ID_OVERFLOW_COUNTS = 5
REPORT_ID_HARDWARE_INFO = 6
REPORT_ID_IDENTITY = 7
REPORT_ID_RUNTIME = 8
REPORT_ID_LED = 9
LED_SIZE = 2
IDENTITY_SIZE = 49
RUNTIME_SIZE = 21
HARDWARE_INFO_SIZE = 6
HARDWARE_INFO_LAYOUT_VERSION = 1
MCU_RP2040 = 1
MCU_RP2350 = 2

COMMAND_TOGGLE_LED = 1
COMMAND_RESET_BOARD = 2
COMMAND_ARM_RESET = 3

STATUS_SIZE = 63
BOARD_STATUS_SIZE = 8
OVERFLOW_COUNTS_SIZE = 25
BOARD_STATUS_LAYOUT_VERSION = 15
STATUS_LAYOUT_VERSION = 15
STATUS_SIGNATURE = ord("P")
UART_CHANNEL_COUNT = 6
REPORT_ID_CHANNEL_BASE = 10
CHANNEL_DIAGNOSTICS_SIZE = 59
FAULT_LABELS = ("init_failed", "control_error", "rx_overrun", "rx_error")
STATUS_HEADER_SIZE = 3
STATUS_CHANNEL_SIZE = 10
RESET_ARM_WINDOW_S = 2.0
BOARD_STATUS_FLAG_HID_RESET = 1 << 0
BOARD_STATUS_RESERVED0_KNOWN_FLAGS = BOARD_STATUS_FLAG_HID_RESET


def parse_channel_diagnostics(payload: bytes) -> dict[str, object]:
    """Decode applied settings and sampled fault history; times are device boot uptime in milliseconds."""
    if len(payload) != CHANNEL_DIAGNOSTICS_SIZE or payload[0] != 1:
        raise RuntimeError("unsupported channel diagnostics size or version")
    _, health, baud, data, stop, parity, tx, rx = struct.unpack_from("<BBIBBBBB", payload)
    if baud and (not 5 <= data <= 8 or stop not in (1, 2) or parity > 2):
        raise RuntimeError("invalid applied UART framing")
    if any(pin > 47 and pin != 255 for pin in (tx, rx)):
        raise RuntimeError("invalid UART GPIO")
    faults = {}
    for index, name in enumerate(FAULT_LABELS):
        count, last_seen = struct.unpack_from("<IQ", payload, 11 + index * 12)
        if last_seen > 2**53 - 1 or (not count and last_seen):
            raise RuntimeError("invalid channel fault timestamp")
        faults[name] = {"count": count, "last_seen_ms": last_seen if count else None}
    return {"health": health, "baud_rate": baud, "data_bits": data, "stop_bits": stop,
            "parity": "NOE"[parity] if baud else None,
            "tx_pin": None if tx == 255 else tx, "rx_pin": None if rx == 255 else rx, "faults": faults}


def parse_led(payload: bytes) -> dict[str, bool | None]:
    """Decode the manual state and merged physical LED output."""
    if len(payload) != LED_SIZE or payload[0] != 1 or payload[1] & ~7:
        raise RuntimeError("unsupported LED report size, version, or flags")
    flags = payload[1]
    if not flags & 1 and flags != 0 or flags & 2 and not flags & 4:
        raise RuntimeError("inconsistent LED state")
    return {"manual_on": bool(flags & 2) if flags & 1 else None,
            "output_on": bool(flags & 4) if flags & 1 else None}


def require_payload(report: list[int], report_id: int, payload_size: int) -> bytes:
    """Validate a report-ID-prefixed HID feature report and return its payload."""
    if len(report) != payload_size + 1 or report[0] != report_id:
        raise RuntimeError(
            f"unexpected report {report_id}: expected {payload_size + 1} bytes with report ID prefix, "
            f"received {len(report)} bytes"
        )
    return bytes(report[1:])


def parse_board_status(payload: bytes) -> dict[str, object]:
    """Decode temperature, firmware version, and supported board capabilities."""
    if len(payload) != BOARD_STATUS_SIZE:
        raise RuntimeError(f"unexpected board-status report size {len(payload)}")
    version, reserved0, centidegrees, major, minor, patch, reserved1 = struct.unpack("<BBhBBBB", payload)
    if version != BOARD_STATUS_LAYOUT_VERSION:
        raise RuntimeError(f"unsupported board-status report version {version}")
    if (reserved0 & ~BOARD_STATUS_RESERVED0_KNOWN_FLAGS) != 0 or reserved1 != 0:
        raise RuntimeError("unsupported board-status report with unknown reserved fields")
    return {
        "temperature_celsius": centidegrees / 100.0,
        "firmware_version": f"{major}.{minor}.{patch}",
        "firmware_major": major,
        "firmware_minor": minor,
        "firmware_patch": patch,
        "hid_reset_enabled": bool(reserved0 & BOARD_STATUS_FLAG_HID_RESET),
    }


def parse_hardware_info(payload: bytes) -> dict[str, object]:
    """Decode the MCU identity and SDK-reported system clock in Hz."""
    if len(payload) != HARDWARE_INFO_SIZE:
        raise RuntimeError(f"unexpected hardware-info report size {len(payload)}")
    version, mcu, system_clock_hz = struct.unpack("<BBI", payload)
    if version != HARDWARE_INFO_LAYOUT_VERSION:
        raise RuntimeError(f"unsupported hardware-info report version {version}")
    if system_clock_hz == 0:
        raise RuntimeError("invalid hardware-info system clock")
    model = {MCU_RP2040: "RP2040", MCU_RP2350: "RP2350"}.get(mcu, f"Unknown MCU ({mcu})")
    return {"mcu": model, "mcu_id": mcu, "system_clock_hz": system_clock_hz}


def parse_overflow_counts(payload: bytes) -> list[int]:
    """Decode cumulative UART-to-USB dropped-byte counts for all channels."""
    if len(payload) != OVERFLOW_COUNTS_SIZE:
        raise RuntimeError(f"unexpected overflow-count report size {len(payload)}")
    version, *overflow_counts = struct.unpack("<B6I", payload)
    if version != STATUS_LAYOUT_VERSION:
        raise RuntimeError(f"unsupported overflow-count report version {version}")
    return overflow_counts


def parse_identity(payload: bytes) -> dict[str, object]:
    """Decode the product and SDK board UID used as the USB serial."""
    if len(payload) != IDENTITY_SIZE or payload[0] != 1:
        raise RuntimeError("unsupported identity report size or version")
    product_bytes = payload[1:33]
    product, separator, padding = product_bytes.partition(b"\0")
    serial = payload[33:49]
    if (
        not product or not separator or any(padding)
        or any(byte < 32 or byte > 126 for byte in product)
        or any(byte not in b"0123456789abcdefABCDEF" for byte in serial)
    ):
        raise RuntimeError("invalid HID product or serial identity")
    return {"product_name": product.decode("ascii"), "serial_number": serial.decode("ascii")}


def parse_runtime(payload: bytes) -> dict[str, object]:
    """Decode uptime, per-core measured service load, and reserved SRAM."""
    if len(payload) != RUNTIME_SIZE or payload[0] not in (1, 2):
        raise RuntimeError("unsupported runtime report size or version")
    _, uptime, core0, core1, reserved, total = struct.unpack("<BQHHII", payload)
    if any(value > 10000 and value != 0xffff for value in (core0, core1)) or not total or reserved > total:
        raise RuntimeError("invalid runtime CPU or RAM values")
    return {
        "uptime_ms": uptime,
        "cpu_usage_basis_points": [None if payload[0] == 1 or value == 0xffff else value for value in (core0, core1)],
        "ram_reserved_bytes": reserved,
        "ram_total_bytes": total,
    }


def parse_status(payload: bytes) -> dict[str, object]:
    """Decode the 63-byte periodic monitor report (layout v15)."""
    if len(payload) != STATUS_SIZE:
        raise RuntimeError(f"unexpected status report size {len(payload)}")

    signature0, version, sequence = payload[:3]
    if signature0 != STATUS_SIGNATURE:
        raise RuntimeError("received a status report with an invalid signature")
    if version != STATUS_LAYOUT_VERSION:
        raise RuntimeError(f"unsupported status report version {version}")

    channels = [
        struct.unpack_from("<BB4H", payload, STATUS_HEADER_SIZE + index * STATUS_CHANNEL_SIZE)
        for index in range(UART_CHANNEL_COUNT)
    ]
    return {
        "sequence": sequence,
        "channels": [
            {
                "id": index,
                "health": channel[0],
                "ring_high_watermark": channel[1] * 16,
                "controller_tx_bytes": channel[2],
                "controller_rx_bytes": channel[3],
                "cdc_tx_bytes": channel[4],
                "cdc_rx_bytes": channel[5],
            }
            for index, channel in enumerate(channels)
        ],
    }


def decode_health(health: int) -> list[str]:
    """Decode compact HID channel health bits into short labels."""
    labels = []
    if health & (1 << 0):
        labels.append("ready")
    if health & (1 << 1):
        labels.append("init_failed")
    if health & (1 << 2):
        labels.append("control_error")
    if health & (1 << 3):
        labels.append("control_pending")
    if health & (1 << 4):
        labels.append("cdc_open")
    if health & (1 << 5):
        labels.append("pio")
    if health & (1 << 6):
        labels.append("rx_overrun")
    if health & (1 << 7):
        labels.append("rx_error")
    return labels
