"""Shared helpers for PicoUart host pytest suite."""

from __future__ import annotations

import struct

import pico_uart_hid as hid


def board_status_bytes(
    layout_version: int | None = None,
    temperature_c: float = 25.30,
    major: int = 1,
    minor: int = 2,
    patch: int = 3,
    reserved0: int = 0,
    reserved1: int = 0,
) -> bytes:
    if layout_version is None:
        layout_version = hid.BOARD_STATUS_LAYOUT_VERSION
    centi = int(round(temperature_c * 100.0))
    return struct.pack(
        "<BBhBBBB", layout_version, reserved0, centi, major, minor, patch, reserved1
    )


def overflow_counts_bytes(layout_version: int | None = None) -> bytes:
    if layout_version is None:
        layout_version = hid.STATUS_LAYOUT_VERSION
    return struct.pack("<B6I", layout_version, 1, 2, 3, 4, 5, 6)


def status_report_bytes(sequence: int = 7, health0: int = 0x11) -> bytes:
    header = struct.pack("<BBB", hid.STATUS_SIGNATURE, hid.STATUS_LAYOUT_VERSION, sequence)
    channels = b""
    for index in range(hid.UART_CHANNEL_COUNT):
        health = health0 if index == 0 else 0x01
        channels += struct.pack(
            "<BB4H", health, 2, 10 + index, 20 + index, 30 + index, 40 + index
        )
    assert len(header) + len(channels) == hid.STATUS_SIZE
    return header + channels


class FakeHidDevice:
    """Minimal stand-in for hidapi feature-report reads."""

    def __init__(self, payload: bytes, report_id: int = hid.REPORT_ID_BOARD_STATUS) -> None:
        self._payload = payload
        self._report_id = report_id

    def get_feature_report(self, report_id: int, size: int) -> list[int]:
        assert report_id == self._report_id
        assert size == len(self._payload) + 1
        return [report_id, *self._payload]
