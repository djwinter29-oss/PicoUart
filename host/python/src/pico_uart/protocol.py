"""Pure decoders and constants for the PicoUart HID report contract."""

import struct


REPORT_ID_STATUS = 1
REPORT_ID_BOARD_STATUS = 3
REPORT_ID_COMMAND = 4
REPORT_ID_OVERFLOW_COUNTS = 5

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
STATUS_HEADER_SIZE = 3
STATUS_CHANNEL_SIZE = 10
RESET_ARM_WINDOW_S = 2.0
BOARD_STATUS_FLAG_HID_RESET = 1 << 0
BOARD_STATUS_RESERVED0_KNOWN_FLAGS = BOARD_STATUS_FLAG_HID_RESET


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


def parse_overflow_counts(payload: bytes) -> list[int]:
    """Decode cumulative UART-to-USB dropped-byte counts for all channels."""
    if len(payload) != OVERFLOW_COUNTS_SIZE:
        raise RuntimeError(f"unexpected overflow-count report size {len(payload)}")
    version, *overflow_counts = struct.unpack("<B6I", payload)
    if version != STATUS_LAYOUT_VERSION:
        raise RuntimeError(f"unsupported overflow-count report version {version}")
    return overflow_counts


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
