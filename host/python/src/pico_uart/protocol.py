"""Pure decoders and constants for the version-1 PicoUart HID contract."""

import struct


REPORT_ID_DEVICE_INFO = 1
REPORT_ID_CONTROL = 2
REPORT_ID_TELEMETRY = 3
REPORT_ID_CHANNEL_BASE = 10
UART_CHANNEL_COUNT = 6
DEVICE_INFO_SIZE = 63
CONTROL_SIZE = 24
TELEMETRY_SIZE = 24
CHANNEL_SIZE = 54
LAYOUT_VERSION = 1

MCU_RP2040 = 1
MCU_RP2350 = 2

COMMAND_LED_OFF = 1
COMMAND_LED_ON = 2
COMMAND_ARM_REBOOT = 3
COMMAND_REBOOT = 4
COMMAND_SAVE_CONFIGURATION = 5

STATUS_IDLE = 0
STATUS_PENDING = 1
STATUS_COMPLETED = 2
STATUS_INVALID = 3
STATUS_UNSUPPORTED = 4
STATUS_BUSY = 5
STATUS_FAILED = 6

FAULT_LABELS = ("init_failed", "control_error", "rx_overrun", "rx_error")
CONFIGURATION_SOURCES = ("defaults", "stored", "recovery")
CONFIGURATION_REASONS = (
    "none", "erased", "invalid_header", "crc_failed", "unsupported_version",
    "invalid_profile", "watchdog_recovery", "clock_unavailable",
)
ERROR_REASONS = (
    "none", "malformed_payload", "invalid_profile", "missing_overclock_acknowledgement",
    "missing_reboot_authorization", "unsupported", "cdc_session_open", "flash_operation_failed",
    "flash_verification_failed", "execution_failed",
)
HEALTH_NAMES = (
    "ready", "init_failed", "control_error", "control_pending",
    "cdc_open", "pio", "rx_overrun", "rx_error",
)
MAX_U32 = 0xFFFFFFFF


def _profile(value: int, *, unavailable: bool = False) -> int | None:
    if value == MAX_U32 and unavailable:
        return None
    if value not in (0, 1, 2, 3, 4):
        raise RuntimeError("invalid clock profile")
    return value


def parse_device_info(payload: bytes) -> dict[str, object]:
    """Decode static product, firmware, MCU, channel, capability, and RAM data."""
    if len(payload) != DEVICE_INFO_SIZE or payload[0] != LAYOUT_VERSION:
        raise RuntimeError("unsupported device-information report size or version")
    product_bytes = payload[1:33]
    product, separator, padding = product_bytes.partition(b"\0")
    serial = payload[33:49]
    if (
        not product or not separator or any(padding)
        or any(byte < 32 or byte > 126 for byte in product)
        or any(byte not in b"0123456789abcdefABCDEF" for byte in serial)
    ):
        raise RuntimeError("invalid HID product or serial identity")
    major, minor, patch, mcu, count, hardware_mask, pio_mask = payload[49:56]
    capabilities = int.from_bytes(payload[56:58], "little")
    profile_mask = payload[58]
    total_ram = int.from_bytes(payload[59:63], "little")
    if (
        count != UART_CHANNEL_COUNT
        or hardware_mask & pio_mask
        or (hardware_mask | pio_mask) != (1 << UART_CHANNEL_COUNT) - 1
        or capabilities & 0xFFF8
        or profile_mask & 0xE0
        or not profile_mask & 1
        or not total_ram
    ):
        raise RuntimeError("invalid static device information")
    model = {MCU_RP2040: "RP2040", MCU_RP2350: "RP2350"}.get(mcu, f"Unknown MCU ({mcu})")
    return {
        "product_name": product.decode("ascii"),
        "serial_number": serial.decode("ascii"),
        "firmware_version": f"{major}.{minor}.{patch}",
        "firmware_major": major,
        "firmware_minor": minor,
        "firmware_patch": patch,
        "mcu": model,
        "mcu_id": mcu,
        "channel_count": count,
        "hardware_channel_mask": hardware_mask,
        "pio_channel_mask": pio_mask,
        "capabilities": capabilities,
        "clock_profile_mask": profile_mask,
        "ram_total_bytes": total_ram,
        "hid_led_available": bool(capabilities & 1),
        "hid_reset_enabled": bool(capabilities & 2),
        "configuration_save_available": bool(capabilities & 4),
    }


def parse_control(payload: bytes) -> dict[str, object]:
    """Decode the board command result and boot-configuration snapshot."""
    if len(payload) != CONTROL_SIZE or payload[0] != LAYOUT_VERSION:
        raise RuntimeError("unsupported control report size or version")
    (
        _, command, flags, status, request_id, stored, active, requested,
        source, reason, error_reason, reserved,
    ) = struct.unpack("<BBBBIIII4B", payload)
    if reserved or status > STATUS_FAILED or source >= len(CONFIGURATION_SOURCES):
        raise RuntimeError("invalid control status, source, or reserved field")
    if reason >= len(CONFIGURATION_REASONS) or error_reason >= len(ERROR_REASONS):
        raise RuntimeError("invalid control reason")
    allowed_errors = {
        STATUS_IDLE: {0},
        STATUS_PENDING: {0},
        STATUS_COMPLETED: {0},
        STATUS_INVALID: {1, 2, 3, 4},
        STATUS_UNSUPPORTED: {5},
        STATUS_BUSY: {6},
        STATUS_FAILED: {7, 8, 9},
    }
    if error_reason not in allowed_errors[status]:
        raise RuntimeError("invalid control status/error pairing")
    stored_profile = _profile(stored, unavailable=True)
    active_profile = _profile(active)
    requested_profile = _profile(requested, unavailable=True)
    known_commands = {
        COMMAND_LED_OFF, COMMAND_LED_ON, COMMAND_ARM_REBOOT, COMMAND_REBOOT, COMMAND_SAVE_CONFIGURATION,
    }
    if flags & 0xFE and (status != STATUS_INVALID or error_reason != 1):
        raise RuntimeError("invalid echoed control flags")
    if command not in known_commands and status not in (STATUS_IDLE,):
        if (status, error_reason) not in ((STATUS_INVALID, 1), (STATUS_UNSUPPORTED, 5)):
            raise RuntimeError("unknown command has an invalid rejection result")
    if status in (STATUS_PENDING, STATUS_COMPLETED) and request_id == 0:
        raise RuntimeError("accepted control result has a zero request ID")
    if status == STATUS_IDLE and (command or flags or request_id or requested_profile is not None):
        raise RuntimeError("idle control result contains command data")
    if status in (STATUS_PENDING, STATUS_COMPLETED) and command != COMMAND_SAVE_CONFIGURATION:
        if flags or requested_profile is not None:
            raise RuntimeError("non-save command result has save-only fields")
    if source == 1 and (reason != 0 or stored_profile is None or active_profile != stored_profile):
        raise RuntimeError("inconsistent stored configuration")
    if source == 2 and reason == 0:
        raise RuntimeError("inconsistent recovery configuration")
    if source == 0 and stored_profile is not None:
        raise RuntimeError("defaults configuration reports stored profile")
    return {
        "command": command,
        "flags": flags,
        "status": status,
        "request_id": request_id,
        "stored_profile": stored_profile,
        "active_profile": active_profile,
        "requested_profile": requested_profile,
        "source": CONFIGURATION_SOURCES[source],
        "reason": CONFIGURATION_REASONS[reason],
        "error_reason": error_reason,
        "error": ERROR_REASONS[error_reason],
    }


def parse_telemetry(payload: bytes) -> dict[str, object]:
    """Decode live board telemetry from Feature report 3."""
    if len(payload) != TELEMETRY_SIZE or payload[0] != LAYOUT_VERSION:
        raise RuntimeError("unsupported telemetry report size or version")
    led_flags = payload[1]
    temperature = int.from_bytes(payload[2:4], "little", signed=True)
    uptime = int.from_bytes(payload[4:12], "little")
    core0, core1 = struct.unpack_from("<HH", payload, 12)
    reserved, clock_hz = struct.unpack_from("<II", payload, 16)
    if (
        led_flags & ~1
        or any(value > 10000 and value != 0xFFFF for value in (core0, core1))
        or not clock_hz
    ):
        raise RuntimeError("invalid telemetry values")
    return {
        "led": bool(led_flags & 1),
        "temperature_celsius": temperature / 100.0,
        "runtime": {
            "uptime_ms": uptime,
            "cpu_usage_basis_points": [None if value == 0xFFFF else value for value in (core0, core1)],
            "ram_reserved_bytes": reserved,
            "system_clock_hz": clock_hz,
        },
        "system_clock_hz": clock_hz,
    }


def parse_channel(payload: bytes) -> dict[str, object]:
    """Decode a combined traffic, health, applied-settings, and fault snapshot."""
    if len(payload) != CHANNEL_SIZE or payload[0] != LAYOUT_VERSION:
        raise RuntimeError("unsupported channel report size or version")
    sequence = int.from_bytes(payload[1:3], "little")
    interval_ms = int.from_bytes(payload[3:7], "little")
    health, ring_blocks = payload[7:9]
    counters = struct.unpack_from("<4I", payload, 9)
    baud = int.from_bytes(payload[25:29], "little")
    data_bits, stop_bits, parity, tx_pin, rx_pin = payload[29:34]
    fault_counts = struct.unpack_from("<4I", payload, 34)
    last_fault_uptime_s = int.from_bytes(payload[50:54], "little")
    if baud and (not 5 <= data_bits <= 8 or stop_bits not in (1, 2) or parity > 2):
        raise RuntimeError("invalid applied UART framing")
    if any((data_bits, stop_bits, parity)) and not baud:
        raise RuntimeError("unavailable UART framing must be zero")
    if any(pin > 47 and pin != 255 for pin in (tx_pin, rx_pin)):
        raise RuntimeError("invalid UART GPIO")
    if (not any(fault_counts) and last_fault_uptime_s) or (fault_counts[2] and not health & 0x40) or (
        fault_counts[3] and not health & 0x80
    ):
        raise RuntimeError("inconsistent channel fault history")
    return {
        "sequence": sequence,
        "interval_ms": interval_ms,
        "health": health,
        "health_labels": [name for bit, name in enumerate(HEALTH_NAMES) if health & (1 << bit)],
        "ring_high_watermark": ring_blocks * 16,
        "controller_tx_bytes": counters[0],
        "controller_rx_bytes": counters[1],
        "cdc_tx_bytes": counters[2],
        "cdc_rx_bytes": counters[3],
        "baud_rate": baud,
        "data_bits": data_bits if baud else None,
        "stop_bits": stop_bits if baud else None,
        "parity": ("none", "odd", "even")[parity] if baud else None,
        "tx_pin": None if tx_pin == 255 else tx_pin,
        "rx_pin": None if rx_pin == 255 else rx_pin,
        "faults": {
            name: {"count": count}
            for name, count in zip(FAULT_LABELS, fault_counts, strict=True)
        },
        "last_fault_uptime_s": last_fault_uptime_s,
    }


def decode_health(health: int) -> list[str]:
    """Decode compact channel health bits into short labels."""
    return [name for bit, name in enumerate(HEALTH_NAMES) if health & (1 << bit)]


def require_payload(report: list[int], report_id: int, payload_size: int) -> bytes:
    """Validate a report-ID-prefixed HID report and return its payload."""
    if len(report) != payload_size + 1 or report[0] != report_id:
        raise RuntimeError(
            f"unexpected report {report_id}: expected {payload_size + 1} bytes with report ID prefix, "
            f"received {len(report)} bytes"
        )
    return bytes(report[1:])
