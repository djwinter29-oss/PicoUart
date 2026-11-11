# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""Pure decoders for PicoUart HID report layouts."""

import struct

from .types import (
    ChannelSample,
    Configuration,
    ControlResult,
    DeviceIdentity,
    DiagnosticSample,
    RuntimeTelemetry,
    Telemetry,
    UartPins,
)

REPORT_ID_DEVICE_INFO = 1
REPORT_ID_CONFIGURATION = 2
REPORT_ID_TELEMETRY = 3
REPORT_ID_CHANNEL_BASE = 10
REPORT_ID_CONTROL = 20
REPORT_ID_DIAGNOSTIC_BASE = 30
UART_CHANNEL_COUNT = 6
DEVICE_INFO_SIZE = 27
CONFIGURATION_SIZE = 36
TELEMETRY_SIZE = 20
CHANNEL_SIZE = 30
CONTROL_SIZE = 4
DIAGNOSTIC_SIZE = 24
LAYOUT_VERSION = 1
MCU_RP2040 = 1
MCU_RP2350 = 2
COMMAND_LED_OFF = 1
COMMAND_LED_ON = 2
COMMAND_REBOOT = 3
COMMAND_SAVE_CONFIGURATION = 4
STATUS_IDLE = 0
STATUS_PENDING = 1
STATUS_COMPLETED = 2
STATUS_INVALID = 3
STATUS_UNSUPPORTED = 4
STATUS_BUSY = 5
STATUS_FAILED = 6
MAX_U32 = 0xFFFFFFFF
CHANNEL_STATES = ("closed", "starting", "ready", "configuring", "error")
ERROR_REASONS = (
    "none", "malformed_payload", "invalid_profile", "missing_overclock_acknowledgement",
    "missing_reboot_authorization", "unsupported", "cdc_session_open", "flash_operation_failed",
    "flash_verification_failed", "execution_failed",
)


def _layout(payload: bytes, size: int, name: str) -> None:
    if len(payload) != size or payload[0] != LAYOUT_VERSION:
        raise RuntimeError(f"unsupported {name} report size or version")


def parse_device_info(payload: bytes) -> DeviceIdentity:
    """Select a product/layout from the discovery prefix before interpreting its fields."""
    if len(payload) < 3:
        raise RuntimeError("identity discovery prefix is truncated")
    product = int.from_bytes(payload[1:3], "little")
    if product != 1:
        raise RuntimeError(f"Unknown product (0x{product:04X}); product-specific operations are unavailable")
    _layout(payload, DEVICE_INFO_SIZE, "device-information")
    major, minor, patch, mcu, board, hardware_mask, capabilities, profiles = payload[11:19]
    total, reserved = struct.unpack_from("<II", payload, 19)
    if capabilities & 0xF8 or profiles & 0xE0 or not profiles & 1 or not total or reserved > total:
        raise RuntimeError("invalid static device information")
    return {
        "product_id": product,
        "product_name": "PicoUart",
        "serial_number": payload[3:11].hex().upper(),
        "firmware_version": f"{major}.{minor}.{patch}",
        "firmware_major": major, "firmware_minor": minor, "firmware_patch": patch,
        "mcu": {1: "RP2040", 2: "RP2350"}.get(mcu, f"Unknown MCU ({mcu})"),
        "mcu_id": mcu,
        "board_model": {0: "Unknown", 1: "Pico", 2: "Pico W", 3: "Pico 2", 4: "Pico 2 W"}.get(
            board, f"Unknown board ({board})"),
        "board_model_id": board,
        "hardware_channel_mask": hardware_mask,
        "capabilities": capabilities,
        "clock_profile_mask": profiles,
        "ram_total_bytes": total,
        "ram_reserved_bytes": reserved,
        "hid_led_available": bool(capabilities & 1),
        "hid_reset_enabled": bool(capabilities & 2),
        "configuration_save_available": bool(capabilities & 4),
    }


def parse_configuration(payload: bytes, identity: DeviceIdentity | None = None) -> Configuration:
    """Decode immutable boot metadata and validate topology only with a valid channel count."""
    _layout(payload, CONFIGURATION_SIZE, "configuration")
    stored, count = payload[1:3]
    if stored not in (0, 1, 2, 3, 4, 255) or count != UART_CHANNEL_COUNT:
        raise RuntimeError("invalid stored profile or channel count")
    if identity is not None and identity["hardware_channel_mask"] >> count:
        raise RuntimeError("hardware channel mask exceeds channel count")
    pins: list[UartPins] = []
    for offset in range(3, 27, 4):
        tx_pin, rx_pin, rts_pin, cts_pin = payload[offset:offset + 4]
        values = (tx_pin, rx_pin, rts_pin, cts_pin)
        if any(value > 47 and value != 255 for value in values):
            raise RuntimeError("invalid UART GPIO")
        pins.append({
            "tx_pin": None if tx_pin == 255 else tx_pin,
            "rx_pin": None if rx_pin == 255 else rx_pin,
            "rts_pin": None if rts_pin == 255 else rts_pin,
            "cts_pin": None if cts_pin == 255 else cts_pin,
        })
    tx_capacity, rx_capacity, cdc_tx_capacity, cdc_rx_capacity = struct.unpack_from("<4H", payload, 28)
    capacities = (tx_capacity, rx_capacity, cdc_tx_capacity, cdc_rx_capacity)
    if not all(capacities):
        raise RuntimeError("buffer capacities must be nonzero")
    return {
        "stored_profile": None if stored == 255 else stored,
        "channel_count": count,
        "channel_pins": pins,
        "reset_cause": payload[27],
        "uart_tx_ring_capacity_bytes": tx_capacity,
        "uart_rx_ring_capacity_bytes": rx_capacity,
        "cdc_tx_fifo_capacity_bytes": cdc_tx_capacity,
        "cdc_rx_fifo_capacity_bytes": cdc_rx_capacity,
    }


def parse_control(payload: bytes) -> ControlResult:
    """Validate a device-global action result, without claiming request correlation."""
    _layout(payload, CONTROL_SIZE, "control")
    _, action, status, error = payload
    allowed = ({0}, {0}, {0}, {1, 2, 3, 4}, {5}, {6}, {7, 8, 9})
    if status >= len(allowed) or error not in allowed[status]:
        raise RuntimeError("invalid control status/error pairing")
    if status == STATUS_IDLE:
        if action != 0:
            raise RuntimeError("idle result must have action zero")
    elif status == STATUS_FAILED and action != COMMAND_SAVE_CONFIGURATION or action not in (1, 2, 3, 4) and (
        action != 0 or (status, error) not in ((STATUS_INVALID, 1), (STATUS_UNSUPPORTED, 5))
    ):
        raise RuntimeError("invalid control action")
    return {"action": action, "status": status, "error_reason": error, "error": ERROR_REASONS[error]}


def parse_telemetry(payload: bytes) -> Telemetry:
    """Decode live board telemetry; unknown service load remains unavailable."""
    _layout(payload, TELEMETRY_SIZE, "telemetry")
    _, led, temperature, uptime, core0, core1, clock = struct.unpack("<BBhQHHI", payload)
    if led & ~1 or any(value > 10000 and value != 65535 for value in (core0, core1)) or not clock:
        raise RuntimeError("invalid telemetry values")
    runtime: RuntimeTelemetry = {
        "uptime_ms": uptime,
        "service_load_basis_points": [None if value == 65535 else value for value in (core0, core1)],
        "system_clock_hz": clock,
    }
    return {
        "led": bool(led),
        "temperature_celsius": temperature / 100.0,
        "runtime": runtime,
        "system_clock_hz": clock,
    }


def parse_channel(
    payload: bytes,
    channel_id: int | None = None,
    identity: DeviceIdentity | None = None,
    configuration: Configuration | None = None,
) -> ChannelSample:
    """Decode independent lifecycle, measurement validity, applied settings, and traffic totals."""
    _layout(payload, CHANNEL_SIZE, "channel")
    _, state, measurement, baud, data_bits, stop_bits, parity, sequence, interval, *counters = struct.unpack(
        "<BBBIBBBHH4I", payload)
    if state >= len(CHANNEL_STATES) or measurement > 2:
        raise RuntimeError("invalid channel state or UART measurement state")
    if baud and (not 50 <= baud <= 3_000_000
                 or not 5 <= data_bits <= 8 or stop_bits not in (1, 2) or parity > 2):
        raise RuntimeError("invalid applied UART framing")
    if not baud and any((data_bits, stop_bits, parity)):
        raise RuntimeError("unavailable UART framing must be zero")
    if channel_id is not None and (type(channel_id) is not int or not 0 <= channel_id < UART_CHANNEL_COUNT):
        raise ValueError("channel index must be 0 through 5")
    if channel_id is not None and configuration is not None and channel_id >= configuration["channel_count"]:
        raise RuntimeError("channel index exceeds advertised channel count")
    if baud and channel_id is not None and identity is not None and configuration is not None:
        hardware_mask = identity["hardware_channel_mask"]
        if not hardware_mask & (1 << channel_id) and (data_bits, stop_bits, parity) != (8, 1, 0):
            raise RuntimeError("PIO UART channels only support 8N1 framing")
    if measurement == 0 and any(counters[:2]):
        raise RuntimeError("unavailable UART counters must be zero placeholders")
    return {
        "channel_state": state, "state": CHANNEL_STATES[state],
        "uart_measurement_state": measurement, "cdc_open": state != 0,
        "baud_rate": baud, "data_bits": data_bits if baud else None,
        "stop_bits": stop_bits if baud else None,
        "parity": ("none", "odd", "even")[parity] if baud else None,
        "sequence": sequence, "interval_ms": interval,
        "uart_tx_bytes": counters[0],
        "uart_rx_bytes": counters[1],
        "cdc_tx_bytes": counters[2],
        "cdc_rx_bytes": counters[3],
    }


def parse_diagnostic(payload: bytes, configuration: Configuration | None = None) -> DiagnosticSample:
    """Decode one independent diagnostic capture and optionally check peaks against capacities."""
    _layout(payload, DIAGNOSTIC_SIZE, "diagnostic")
    _, measurement, age, reason, dropped, errors, timestamp, *peaks = struct.unpack("<4B3I4H", payload)
    if measurement > 2 or reason > 5 or (measurement == 1 and age != 0):
        raise RuntimeError("invalid diagnostic measurement state, age, or reason")
    if measurement == 0 and (age != 255 or dropped or errors or any(peaks[:2])):
        raise RuntimeError("unavailable UART diagnostics must have zero placeholders and unknown age")
    if not (dropped or errors or reason) and timestamp:
        raise RuntimeError("fault timestamp requires recorded history")
    result: DiagnosticSample = {
        "uart_measurement_state": measurement, "uart_sample_age_100ms": age,
        "last_control_error_reason": reason, "rx_overrun_count": dropped, "rx_error_count": errors,
        "last_fault_uptime_s": timestamp,
        "uart_tx_ring_peak_bytes": peaks[0],
        "uart_rx_ring_peak_bytes": peaks[1],
        "cdc_tx_fifo_peak_bytes": peaks[2],
        "cdc_rx_fifo_peak_bytes": peaks[3],
    }
    if configuration is not None:
        capacity_checks = (
            ("uart_tx_ring_peak_bytes", result["uart_tx_ring_peak_bytes"],
             configuration["uart_tx_ring_capacity_bytes"]),
            ("uart_rx_ring_peak_bytes", result["uart_rx_ring_peak_bytes"],
             configuration["uart_rx_ring_capacity_bytes"]),
            ("cdc_tx_fifo_peak_bytes", result["cdc_tx_fifo_peak_bytes"],
             configuration["cdc_tx_fifo_capacity_bytes"]),
            ("cdc_rx_fifo_peak_bytes", result["cdc_rx_fifo_peak_bytes"],
             configuration["cdc_rx_fifo_capacity_bytes"]),
        )
        for peak, value, capacity in capacity_checks:
            if value > capacity:
                raise RuntimeError(f"{peak} exceeds advertised capacity")
    return result


def require_payload(report: list[int] | None, report_id: int, payload_size: int) -> bytes:
    """Validate a report-ID-prefixed HID report and return its payload."""
    if report is None:
        raise RuntimeError(
            f"unexpected report {report_id}: expected {payload_size + 1} bytes with report ID prefix, "
            "received no report"
        )
    if len(report) != payload_size + 1 or report[0] != report_id:
        raise RuntimeError(
            f"unexpected report {report_id}: expected {payload_size + 1} bytes with report ID prefix, "
            f"received {len(report)} bytes"
        )
    return bytes(report[1:])
