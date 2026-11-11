# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""Typed contracts for dashboard board discovery and snapshots."""

from typing import Protocol, TypedDict

from ..hid.types import (
    ChannelDiagnostic,
    ChannelSnapshot,
    Configuration,
    ControlResult,
    DeviceIdentity,
    RuntimeTelemetry,
)


class BoardSummary(TypedDict):
    """A discovered board that can be selected by the dashboard."""

    id: str
    product_name: str
    serial_number: str | None
    device_path: str


class BoardList(TypedDict):
    """Current board discovery results and the preferred selection."""

    boards: list[BoardSummary]
    default_board_id: str | None


class BoardIdentity(DeviceIdentity):
    """Static board identity; the temperature is None while telemetry is unavailable (cf. `hid.types.BoardStatus`)."""

    temperature_celsius: float | None


class HardwareSnapshot(TypedDict):
    """Hardware identity; the clock is None while telemetry is unavailable (cf. `hid.types.HardwareInfo`)."""

    mcu: str
    mcu_id: int
    system_clock_hz: int | None


class LedSnapshot(TypedDict):
    """Manual and physical identification LED state."""

    manual_on: bool | None
    output_on: bool | None


class BoardSnapshot(TypedDict):
    """Data returned by dashboard status and diagnostics endpoints."""

    connected: bool
    refresh_interval_ms: int
    error: str | None
    metadata_error: str | None
    hardware_error: str | None
    hardware: HardwareSnapshot | None
    configuration: Configuration | None
    configuration_error: str | None
    configuration_save: ControlResult | None
    configuration_save_error: str | None
    identity: DeviceIdentity | None
    identity_error: str | None
    runtime: RuntimeTelemetry | None
    runtime_error: str | None
    led: LedSnapshot | None
    led_error: str | None
    updated_at: str | None
    traffic_incomplete: bool
    traffic_gaps: int
    channel_diagnostics: list[ChannelDiagnostic] | None
    diagnostics_error: str | None
    board: BoardIdentity | None
    overflow_counts: list[int | None]
    channels: list[ChannelSnapshot]


class DashboardService(Protocol):
    """Operations required by the Flask routes."""

    def snapshot(self, board_id: str | None = None) -> BoardSnapshot: ...

    def boards(self) -> BoardList: ...

    def read_diagnostics(self, board_id: str | None) -> BoardSnapshot: ...

    def save_configuration(self, board_id: str | None, profile: int, unsafe_overclock: bool) -> None: ...

    def toggle_led(self, board_id: str | None) -> None: ...

    def reset_board(self, board_id: str | None) -> None: ...
