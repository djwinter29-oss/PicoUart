# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""Per-channel Input report accounting: sequence gaps, staleness, and counter deltas."""

from copy import deepcopy
from datetime import datetime, timezone

from .protocol import MAX_U32, UART_CHANNEL_COUNT, UNAVAILABLE_U16
from .types import ChannelSample, ChannelSnapshot, ChannelTotals, ChannelTraffic, StatusSnapshot

STALE_INPUT_S = 3.0  # Input reports arrive about every 0.5-1 s; a longer silence restarts accounting


def empty_channel(channel_id: int) -> ChannelSnapshot:
    return {
        "id": channel_id,
        "updated_at": None,
        "sequence": None,
        "observed_interval_ms": 0,
        "traffic_gaps": 0,
        "traffic_incomplete": False,
        "state": "unknown",
        "channel_state": None,
        "uart_measurement_state": None,
        "cdc_open": False,
        "traffic": {"uart_tx": None, "uart_rx": None, "usb_tx": None, "usb_rx": None},
        "totals": {"uart_tx": 0, "uart_rx": 0, "usb_tx": 0, "usb_rx": 0},
        "interval_ms": None,
        "baud_rate": None,
        "data_bits": None,
        "stop_bits": None,
        "parity": None,
    }


def _traffic_deltas(
    previous: dict[str, int] | None, current: dict[str, int], clean: bool, counted_uart: bool
) -> ChannelTraffic:
    """Per-report wrap-safe byte deltas; None whenever continuity or UART measurement is not established."""

    def delta(field: str, uart: bool) -> int | None:
        if previous is None or not clean or (uart and not counted_uart):
            return None
        return (current[field] - previous[field]) & MAX_U32

    return {
        "uart_tx": delta("uart_tx_bytes", True),
        "uart_rx": delta("uart_rx_bytes", True),
        "usb_tx": delta("cdc_tx_bytes", False),
        "usb_rx": delta("cdc_rx_bytes", False),
    }


class InputTracker:
    """Fold Input reports into per-channel snapshots; callers provide any needed locking."""

    def __init__(self) -> None:
        self.channels = [empty_channel(index) for index in range(UART_CHANNEL_COUNT)]
        self._last_counters: list[dict[str, int] | None] = [None] * UART_CHANNEL_COUNT
        self._input_seen = [False] * UART_CHANNEL_COUNT
        self._last_input_at: list[float | None] = [None] * UART_CHANNEL_COUNT
        self._ignored_gap = [False] * UART_CHANNEL_COUNT

    def snapshot(self) -> StatusSnapshot:
        """Return a copy of all channel snapshots."""
        return {"channels": deepcopy(self.channels)}

    def _classify(self, channel_id: int, sample: ChannelSample, now: float) -> tuple[bool, int | None] | None:
        """Return (restarts_accounting, sequence_delta), or None when the report must not be accounted."""
        channel = self.channels[channel_id]
        previous_at = self._last_input_at[channel_id]
        previous_sequence = channel["sequence"]
        first = not self._input_seen[channel_id]
        sequence_delta = (
            (sample["sequence"] - previous_sequence) & 0xFFFF
            if not first and previous_sequence is not None else None
        )
        if not first and (previous_at is None or now - previous_at > STALE_INPUT_S):
            channel["traffic_incomplete"] = True
            if not self._ignored_gap[channel_id]:
                channel["traffic_gaps"] += 1
            return True, sequence_delta
        if sequence_delta == 0:
            return None
        if sequence_delta is not None and sequence_delta > 0x8000:
            channel["traffic_incomplete"] = True
            if not self._ignored_gap[channel_id]:
                channel["traffic_gaps"] += 1
                self._ignored_gap[channel_id] = True
            return None
        return first, sequence_delta

    def apply(self, channel_id: int, sample: ChannelSample, now: float) -> None:
        """Fold one Input report into the channel snapshot, counting gaps and per-report byte deltas."""
        decision = self._classify(channel_id, sample, now)
        if decision is None:
            return
        first, sequence_delta = decision
        channel = self.channels[channel_id]
        gap = not first and sequence_delta != 1
        interval = sample["interval_ms"]
        invalid_interval = interval in (0, UNAVAILABLE_U16)
        counters = {
            "uart_tx_bytes": sample["uart_tx_bytes"],
            "uart_rx_bytes": sample["uart_rx_bytes"],
            "cdc_tx_bytes": sample["cdc_tx_bytes"],
            "cdc_rx_bytes": sample["cdc_rx_bytes"],
        }
        previous_counters = self._last_counters[channel_id]
        if gap or invalid_interval:
            channel["traffic_incomplete"] = True
        if gap:
            channel["traffic_gaps"] += 1
        previous_measurement = channel["uart_measurement_state"]
        clean = not first and not gap and not invalid_interval
        traffic = _traffic_deltas(
            previous_counters, counters, clean,
            counted_uart=sample["uart_measurement_state"] == 1 and previous_measurement == 1,
        )
        totals: ChannelTotals = {
            "uart_tx": counters["uart_tx_bytes"],
            "uart_rx": counters["uart_rx_bytes"],
            "usb_tx": counters["cdc_tx_bytes"],
            "usb_rx": counters["cdc_rx_bytes"],
        }
        channel["traffic"] = traffic
        channel["totals"] = totals
        if not first and not gap and not invalid_interval:
            channel["observed_interval_ms"] += interval
        channel.update({
            "updated_at": datetime.now(timezone.utc).isoformat(timespec="milliseconds"),
            "sequence": sample["sequence"],
            "interval_ms": interval,
            "state": sample["state"],
            "channel_state": sample["channel_state"],
            "uart_measurement_state": sample["uart_measurement_state"],
            "cdc_open": sample["cdc_open"],
            "baud_rate": sample["baud_rate"],
            "data_bits": sample["data_bits"],
            "stop_bits": sample["stop_bits"],
            "parity": sample["parity"],
        })
        self._input_seen[channel_id] = True
        self._last_counters[channel_id] = counters
        self._last_input_at[channel_id] = now
        self._ignored_gap[channel_id] = False
