"""Shared payload builders and fake HID device for host tests."""

from __future__ import annotations

import struct

from pico_uart import protocol as hid


def device_info_bytes(
    *,
    product: bytes = b"PicoUart",
    serial: bytes = b"0123456789ABCDEF",
    firmware: tuple[int, int, int] = (1, 2, 3),
    mcu: int = hid.MCU_RP2040,
    capabilities: int = 7,
    profile_mask: int = 0x0F,
    ram_total: int = 270336,
) -> bytes:
    return (
        bytes((1,))
        + product.ljust(32, b"\0")
        + serial
        + bytes((*firmware, mcu, 6, 3, 0x3C))
        + capabilities.to_bytes(2, "little")
        + bytes((profile_mask,))
        + ram_total.to_bytes(4, "little")
    )


def control_bytes(
    *,
    command: int = 0,
    flags: int = 0,
    status: int = 0,
    request_id: int = 0,
    stored: int = 0xFFFFFFFF,
    active: int = 0,
    requested: int = 0xFFFFFFFF,
    source: int = 0,
    reason: int = 1,
    error_reason: int = 0,
    reserved: int = 0,
) -> bytes:
    return struct.pack(
        "<BBBBIIII4B", 1, command, flags, status, request_id, stored, active,
        requested, source, reason, error_reason, reserved,
    )


def telemetry_bytes(
    *,
    led: int = 1,
    temperature_c: float = 25.30,
    uptime_ms: int = 1234,
    cores: tuple[int, int] = (2500, 0xFFFF),
    reserved_ram: int = 65536,
    clock_hz: int = 250000000,
) -> bytes:
    return struct.pack(
        "<BBhQHHII", 1, led, int(round(temperature_c * 100)), uptime_ms,
        *cores, reserved_ram, clock_hz,
    )


def channel_bytes(
    *,
    sequence: int = 1,
    interval_ms: int = 500,
    health: int = 0x11,
    ring_blocks: int = 2,
    traffic: tuple[int, int, int, int] = (10, 20, 30, 40),
    baud: int = 115200,
    framing: tuple[int, int, int] = (8, 1, 0),
    pins: tuple[int, int] = (0, 1),
    faults: tuple[int, int, int, int] = (0, 0, 0, 0),
    last_fault_uptime_s: int = 0,
) -> bytes:
    return (
        bytes((1,))
        + sequence.to_bytes(2, "little")
        + interval_ms.to_bytes(4, "little")
        + bytes((health, ring_blocks))
        + struct.pack("<4I", *traffic)
        + baud.to_bytes(4, "little")
        + bytes((*framing, *pins))
        + struct.pack("<4I", *faults)
        + last_fault_uptime_s.to_bytes(4, "little")
    )


class FakeHidDevice:
    """Minimal stand-in for HID Feature and Input report operations."""

    def __init__(self, reports: dict[int, bytes], inputs: list[tuple[int, bytes]] | None = None):
        self.reports = reports
        self.inputs = list(inputs or [])
        self.writes: list[list[int]] = []
        self.closed = False

    def get_feature_report(self, report_id: int, size: int) -> list[int]:
        payload = self.reports[report_id]
        assert size == len(payload) + 1
        return [report_id, *payload]

    def send_feature_report(self, report: list[int]) -> int:
        self.writes.append(report)
        return len(report)

    def read(self, size: int, timeout_ms: int) -> list[int]:
        if not self.inputs:
            return []
        report_id, payload = self.inputs.pop(0)
        assert size == len(payload) + 1
        return [report_id, *payload]

    def close(self) -> None:
        self.closed = True
