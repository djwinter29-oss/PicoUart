# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""Typed result contracts for HID channel status and diagnostics."""

from typing import TypedDict


class DeviceIdentity(TypedDict):
    """Validated static board identity and capabilities."""

    product_id: int
    product_name: str
    serial_number: str
    firmware_version: str
    firmware_major: int
    firmware_minor: int
    firmware_patch: int
    mcu: str
    mcu_id: int
    board_model: str
    board_model_id: int
    hardware_channel_mask: int
    capabilities: int
    clock_profile_mask: int
    ram_total_bytes: int
    ram_reserved_bytes: int
    hid_led_available: bool
    hid_reset_enabled: bool
    configuration_save_available: bool


class UartPins(TypedDict):
    """GPIO assignment for one UART channel."""

    tx_pin: int | None
    rx_pin: int | None
    rts_pin: int | None
    cts_pin: int | None


class Configuration(TypedDict):
    """Validated boot-time board configuration and buffer capacities."""

    stored_profile: int | None
    channel_count: int
    channel_pins: list[UartPins]
    reset_cause: int
    uart_tx_ring_capacity_bytes: int
    uart_rx_ring_capacity_bytes: int
    cdc_tx_fifo_capacity_bytes: int
    cdc_rx_fifo_capacity_bytes: int


class ControlResult(TypedDict):
    """Validated result of the latest device-global control action."""

    action: int
    status: int
    error_reason: int
    error: str


class RuntimeTelemetry(TypedDict):
    """Live runtime counters and service-load estimates."""

    uptime_ms: int
    service_load_basis_points: list[int | None]
    system_clock_hz: int


class Telemetry(TypedDict):
    """Validated live board telemetry."""

    led: bool
    temperature_celsius: float
    runtime: RuntimeTelemetry
    system_clock_hz: int


class ChannelSample(TypedDict):
    """One decoded channel Feature or Input report."""

    channel_state: int
    state: str
    uart_measurement_state: int
    cdc_open: bool
    baud_rate: int
    data_bits: int | None
    stop_bits: int | None
    parity: str | None
    sequence: int
    interval_ms: int
    uart_tx_bytes: int
    uart_rx_bytes: int
    cdc_tx_bytes: int
    cdc_rx_bytes: int


class ChannelFeatureSample(ChannelSample):
    """One channel Feature snapshot with its channel index."""

    id: int


class ChannelTraffic(TypedDict):
    """Counter deltas observed between consecutive Input reports."""

    uart_tx: int | None
    uart_rx: int | None
    usb_tx: int | None
    usb_rx: int | None


class ChannelTotals(TypedDict):
    """Cumulative counters from the latest Input report."""

    uart_tx: int
    uart_rx: int
    usb_tx: int
    usb_rx: int


class ChannelSnapshot(TypedDict):
    """Per-channel status exposed by ``PicoUartHid.read_status``."""

    id: int
    updated_at: str | None
    sequence: int | None
    observed_interval_ms: int
    traffic_gaps: int
    traffic_incomplete: bool
    state: str
    channel_state: int | None
    uart_measurement_state: int | None
    cdc_open: bool
    traffic: ChannelTraffic
    totals: ChannelTotals
    interval_ms: int | None
    baud_rate: int | None
    data_bits: int | None
    stop_bits: int | None
    parity: str | None


class StatusSnapshot(TypedDict):
    """Latest host-side status for all six UART channels."""

    channels: list[ChannelSnapshot]


class DiagnosticSample(TypedDict):
    """Decoded diagnostics from one channel report."""

    uart_measurement_state: int
    uart_sample_age_100ms: int
    last_control_error_reason: int
    rx_overrun_count: int
    rx_error_count: int
    last_fault_uptime_s: int
    uart_tx_ring_peak_bytes: int
    uart_rx_ring_peak_bytes: int
    cdc_tx_fifo_peak_bytes: int
    cdc_rx_fifo_peak_bytes: int


class ChannelDiagnostic(DiagnosticSample):
    """Channel diagnostics with their channel index."""

    id: int
