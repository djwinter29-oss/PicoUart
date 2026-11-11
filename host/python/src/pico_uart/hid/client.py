# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""Resource-owning Python API for PicoUart's version-1 HID reports."""

import math
import threading
import time
from collections.abc import Callable
from copy import deepcopy
from datetime import datetime, timezone
from typing import TypeVar

from .protocol import (
    CHANNEL_SIZE,
    COMMAND_LED_OFF,
    COMMAND_LED_ON,
    COMMAND_REBOOT,
    COMMAND_SAVE_CONFIGURATION,
    CONFIGURATION_SIZE,
    CONTROL_SIZE,
    DEVICE_INFO_SIZE,
    DIAGNOSTIC_SIZE,
    MAX_U32,
    REPORT_ID_CHANNEL_BASE,
    REPORT_ID_CONFIGURATION,
    REPORT_ID_CONTROL,
    REPORT_ID_DEVICE_INFO,
    REPORT_ID_DIAGNOSTIC_BASE,
    REPORT_ID_TELEMETRY,
    STATUS_BUSY,
    STATUS_COMPLETED,
    STATUS_FAILED,
    STATUS_IDLE,
    STATUS_INVALID,
    STATUS_PENDING,
    STATUS_UNSUPPORTED,
    TELEMETRY_SIZE,
    UART_CHANNEL_COUNT,
    parse_channel,
    parse_configuration,
    parse_control,
    parse_device_info,
    parse_diagnostic,
    parse_telemetry,
    require_payload,
)
from .transport import HidDevice, open_device
from .types import (
    ChannelDiagnostic,
    ChannelFeatureSample,
    ChannelSample,
    ChannelSnapshot,
    ChannelTotals,
    ChannelTraffic,
    Configuration,
    ControlResult,
    DeviceIdentity,
    RuntimeTelemetry,
    StatusSnapshot,
    Telemetry,
)

COMMAND_TIMEOUT_S = 3.0
COMMAND_POLL_INTERVAL_S = 0.05
_TransferResult = TypeVar("_TransferResult")


def read_feature(device: HidDevice, report_id: int, payload_size: int) -> bytes:
    """Read a fixed-size feature report, excluding its HID report ID."""
    return require_payload(device.get_feature_report(report_id, payload_size + 1), report_id, payload_size)


def _empty_channel(channel_id: int) -> ChannelSnapshot:
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


class PicoUartHid:
    """Own a PicoUart HID connection and expose decoded reports and board controls."""

    def __init__(
        self,
        serial_number: str | None = None,
        device_path: str | None = None,
        device: HidDevice | None = None,
    ) -> None:
        if device is not None and (serial_number is not None or device_path is not None):
            raise ValueError("an injected device cannot be combined with a device selector")
        self._device = device if device is not None else open_device(serial_number, device_path)
        self._lock = threading.RLock()
        self._transfer_state_lock = threading.Lock()
        self._closed = False
        self._device_closed = False
        self._device_close_complete = threading.Event()
        self._deferred_close_error: OSError | None = None
        self._device_info: DeviceIdentity | None = None
        self._configuration: Configuration | None = None
        self._transfer_in_flight = False
        self._channels = [_empty_channel(index) for index in range(UART_CHANNEL_COUNT)]
        self._last_counters: list[dict[str, int] | None] = [None] * UART_CHANNEL_COUNT
        self._input_seen = [False] * UART_CHANNEL_COUNT
        self._last_input_at: list[float | None] = [None] * UART_CHANNEL_COUNT
        self._ignored_gap = [False] * UART_CHANNEL_COUNT

    def _require_open(self) -> HidDevice:
        if self._closed:
            raise RuntimeError("PicoUart HID connection is closed")
        return self._device

    def _read_device_info(self, deadline: float | None = None) -> DeviceIdentity:
        if self._device_info is None:
            timeout_s = COMMAND_TIMEOUT_S if deadline is None else min(
                COMMAND_TIMEOUT_S, deadline - time.monotonic()
            )
            if timeout_s <= 0:
                raise RuntimeError("HID feature transfer timed out; outcome is unconfirmed")
            report = self._bounded_transfer(
                lambda: self._require_open().get_feature_report(REPORT_ID_DEVICE_INFO, DEVICE_INFO_SIZE + 1),
                timeout_s,
            )
            if report is None:
                raise RuntimeError("invalid identity report ID")
            self._device_info = parse_device_info(
                require_payload(report, REPORT_ID_DEVICE_INFO, DEVICE_INFO_SIZE)
            )
        return self._device_info

    def _bounded_transfer(
        self,
        operation: Callable[[], _TransferResult],
        timeout_s: float = COMMAND_TIMEOUT_S,
    ) -> _TransferResult:
        """Bound synchronous hidapi feature I/O and quarantine a call that returns late."""
        with self._transfer_state_lock:
            if self._transfer_in_flight:
                raise RuntimeError("a previous HID feature transfer is still in flight")
            self._transfer_in_flight = True
        completed = threading.Event()
        result: list[_TransferResult] = []
        failure: list[Exception] = []

        def run() -> None:
            try:
                result.append(operation())
            except Exception as error:
                failure.append(error)
            finally:
                with self._transfer_state_lock:
                    self._transfer_in_flight = False
                    if self._closed and not self._device_closed:
                        self._close_device_after_transfer()
                    completed.set()

        worker = threading.Thread(target=run, name="pico-uart-hid-transfer", daemon=True)
        try:
            worker.start()
        except RuntimeError:
            with self._transfer_state_lock:
                self._transfer_in_flight = False
            raise
        if not completed.wait(max(0.0, timeout_s)):
            raise RuntimeError("HID feature transfer timed out; outcome is unconfirmed")
        if failure:
            raise failure[0]
        return result[0]

    def _read_feature(self, report_id: int, payload_size: int, deadline: float | None = None) -> bytes:
        self._read_device_info(deadline)
        remaining = COMMAND_TIMEOUT_S if deadline is None else min(COMMAND_TIMEOUT_S, deadline - time.monotonic())
        if remaining <= 0:
            raise RuntimeError("board-control operation timed out; outcome is unconfirmed")
        report = self._bounded_transfer(
            lambda: self._require_open().get_feature_report(report_id, payload_size + 1),
            remaining,
        )
        return require_payload(report, report_id, payload_size)

    def _send_feature(self, report: list[int]) -> int:
        return self._bounded_transfer(lambda: self._require_open().send_feature_report(report))

    def read_identity(self) -> DeviceIdentity:
        """Read static device identity, firmware, capabilities, and memory information."""
        with self._lock:
            return DeviceIdentity(**self._read_device_info())

    def read_board_status(self) -> dict[str, object]:
        """Return static board identity/capabilities together with the current temperature."""
        info = self.read_identity()
        return {
            **info,
            "temperature_celsius": self.read_telemetry()["temperature_celsius"],
            "hid_led_enabled": info["hid_led_available"],
        }

    def read_board_temperature(self) -> float:
        return float(self.read_telemetry()["temperature_celsius"])

    def read_firmware_version(self) -> str:
        return str(self.read_identity()["firmware_version"])

    def read_hardware_info(self) -> dict[str, object]:
        info = self.read_identity()
        telemetry = self.read_telemetry()
        return {"mcu": info["mcu"], "mcu_id": info["mcu_id"], "system_clock_hz": telemetry["system_clock_hz"]}

    def read_configuration(self, *, deadline: float | None = None) -> Configuration:
        """Read immutable boot metadata independently of the action result."""
        if deadline is not None and not math.isfinite(deadline):
            raise ValueError("deadline must be finite")
        with self._lock:
            if self._configuration is None:
                self._configuration = parse_configuration(
                    self._read_feature(REPORT_ID_CONFIGURATION, CONFIGURATION_SIZE, deadline),
                    self._read_device_info(deadline),
                )
            return deepcopy(self._configuration)

    def read_configuration_save(self) -> ControlResult:
        """Read the latest device-global action result."""
        return self.read_control()

    def read_control(self) -> ControlResult:
        with self._lock:
            return parse_control(self._read_feature(REPORT_ID_CONTROL, CONTROL_SIZE))

    def read_telemetry(self) -> Telemetry:
        """Read live LED, temperature, uptime, service-load, and clock values."""
        with self._lock:
            telemetry = parse_telemetry(self._read_feature(REPORT_ID_TELEMETRY, TELEMETRY_SIZE))
            return telemetry

    def read_runtime(self) -> RuntimeTelemetry:
        return self.read_telemetry()["runtime"]

    def read_overflow_counts(self) -> list[int]:
        return [int(item["rx_overrun_count"]) for item in self.read_channel_diagnostics()]

    def read_channel_diagnostics(self) -> list[ChannelDiagnostic]:
        return [self.read_channel_diagnostic(index) for index in range(UART_CHANNEL_COUNT)]

    def read_channel_diagnostic(self, index: int) -> ChannelDiagnostic:
        """Read one channel's non-consuming combined Feature snapshot."""
        if type(index) is not int or not 0 <= index < UART_CHANNEL_COUNT:
            raise ValueError("channel index must be 0 through 5")
        with self._lock:
            payload = self._read_feature(REPORT_ID_DIAGNOSTIC_BASE + index, DIAGNOSTIC_SIZE)
            configuration = self.read_configuration()
            return {"id": index, **parse_diagnostic(payload, configuration)}

    def read_channel(self, index: int) -> ChannelFeatureSample:
        """Read a non-consuming snapshot; never merge it into Input traffic accounting."""
        if type(index) is not int or not 0 <= index < UART_CHANNEL_COUNT:
            raise ValueError("channel index must be 0 through 5")
        with self._lock:
            configuration = self.read_configuration()
            return {
                "id": index,
                **parse_channel(
                    self._read_feature(REPORT_ID_CHANNEL_BASE + index, CHANNEL_SIZE),
                    index,
                    self._read_device_info(),
                    configuration,
                ),
            }

    def read_status(self, timeout_ms: int = 250, *, deadline: float | None = None) -> StatusSnapshot | None:
        """Read one Input report; the optional absolute deadline includes metadata and Input reads."""
        if timeout_ms < 0:
            raise ValueError("timeout_ms must not be negative")
        if deadline is not None and not math.isfinite(deadline):
            raise ValueError("deadline must be finite")
        with self._lock:
            self._read_device_info(deadline)
            configuration = self.read_configuration(deadline=deadline)
            input_timeout_ms = timeout_ms
            if deadline is not None:
                input_timeout_ms = min(timeout_ms, max(0, int((deadline - time.monotonic()) * 1000)))
            report = self._require_open().read(CHANNEL_SIZE + 1, input_timeout_ms)
            if not report:
                return None
            if len(report) != CHANNEL_SIZE + 1 or not REPORT_ID_CHANNEL_BASE <= report[0] < (
                REPORT_ID_CHANNEL_BASE + UART_CHANNEL_COUNT
            ):
                raise RuntimeError("unexpected channel Input report ID or size")
            channel_id = report[0] - REPORT_ID_CHANNEL_BASE
            self._apply_input(
                channel_id,
                parse_channel(bytes(report[1:]), channel_id, self._read_device_info(), configuration),
                time.monotonic(),
            )
            return {"channels": deepcopy(self._channels)}

    def wait_channels_ready(
        self, channels: dict[int, int], timeout_s: float = 3.0
    ) -> dict[int, ChannelFeatureSample]:
        """Wait for selected open channels to apply raw 8N1 at the requested baud (within 3%).

        Call after opening/configuring every endpoint in a group. Historical faults do not fail this wait;
        current initialization/configuration errors do. HID transport errors propagate to the caller.
        """
        if not channels or any(type(index) is not int or not 0 <= index < UART_CHANNEL_COUNT
                               or type(baud) is not int or baud <= 0 for index, baud in channels.items()):
            raise ValueError("channels must map indices 0 through 5 to positive integer baud rates")
        if not math.isfinite(timeout_s) or timeout_s <= 0:
            raise ValueError("timeout_s must be finite and positive")
        deadline = time.monotonic() + timeout_s
        while True:
            samples: dict[int, ChannelFeatureSample] = {}
            for index, baud in channels.items():
                if time.monotonic() >= deadline:
                    raise TimeoutError(f"CDC readiness timed out for channels {list(channels)}")
                try:
                    remaining = deadline - time.monotonic()
                    if remaining <= 0 or not self._lock.acquire(timeout=remaining):
                        raise TimeoutError("CDC readiness timed out waiting for HID lock")
                    try:
                        identity = self._read_device_info(deadline)
                        configuration = self.read_configuration(deadline=deadline)
                        sample = parse_channel(
                            self._read_feature(REPORT_ID_CHANNEL_BASE + index, CHANNEL_SIZE, deadline),
                            index,
                            identity,
                            configuration,
                        )
                    finally:
                        self._lock.release()
                except RuntimeError as error:
                    if time.monotonic() >= deadline:
                        raise TimeoutError("CDC readiness HID read timed out") from error
                    raise
                if time.monotonic() >= deadline:
                    raise TimeoutError("CDC readiness HID read timed out")
                if sample["state"] == "error":
                    raise OSError(f"CDC{index} configuration failed")
                if (sample["state"] == "ready" and sample["baud_rate"]
                        and abs(sample["baud_rate"] - baud) <= baud * 0.03
                        and (sample["data_bits"], sample["parity"], sample["stop_bits"]) == (8, "none", 1)):
                    samples[index] = {"id": index, **sample}
            if len(samples) == len(channels):
                return samples
            time.sleep(min(0.05, max(0.0, deadline - time.monotonic())))

    def _apply_input(self, channel_id: int, sample: ChannelSample, now: float) -> None:
        channel = self._channels[channel_id]
        previous_at = self._last_input_at[channel_id]
        previous_sequence = channel["sequence"]
        first = not self._input_seen[channel_id]
        sequence_delta = (
            (sample["sequence"] - previous_sequence) & 0xFFFF
            if not first and previous_sequence is not None else None
        )
        stale = not first and (previous_at is None or now - previous_at > 3.0)
        if stale:
            channel["traffic_incomplete"] = True
            if not self._ignored_gap[channel_id]:
                channel["traffic_gaps"] += 1
            first = True
        elif sequence_delta == 0:
            return
        elif sequence_delta is not None and sequence_delta > 0x8000:
            channel["traffic_incomplete"] = True
            if not self._ignored_gap[channel_id]:
                channel["traffic_gaps"] += 1
                self._ignored_gap[channel_id] = True
            return
        gap = not first and sequence_delta != 1
        interval = sample["interval_ms"]
        invalid_interval = interval in (0, 65535)
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
        def traffic_delta(field: str, value: int) -> int | None:
            if previous_counters is None or not clean:
                return None
            if not field.startswith("cdc_") and not (
                sample["uart_measurement_state"] == 1 and previous_measurement == 1
            ):
                return None
            return (value - previous_counters[field]) & MAX_U32

        traffic: ChannelTraffic = {
            "uart_tx": traffic_delta("uart_tx_bytes", counters["uart_tx_bytes"]),
            "uart_rx": traffic_delta("uart_rx_bytes", counters["uart_rx_bytes"]),
            "usb_tx": traffic_delta("cdc_tx_bytes", counters["cdc_tx_bytes"]),
            "usb_rx": traffic_delta("cdc_rx_bytes", counters["cdc_rx_bytes"]),
        }
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

    def read_led(self) -> dict[str, bool | None]:
        """Read the current LED state; unavailable hardware is reported as unavailable."""
        with self._lock:
            info = self._read_device_info()
            if not info["hid_led_available"]:
                return {"manual_on": None, "output_on": None}
            state = bool(self.read_telemetry()["led"])
            return {"manual_on": state, "output_on": state}

    def toggle_led(self) -> None:
        """Set the LED opposite its observed state using an explicit On/Off command."""
        with self._lock:
            info = self._read_device_info()
            if not info["hid_led_available"]:
                raise RuntimeError("identification LED is unavailable")
            current = bool(self.read_telemetry()["led"])
            self._execute_command(COMMAND_LED_OFF if current else COMMAND_LED_ON)
            self.read_telemetry()

    def reset_board(self) -> None:
        """Request one explicitly confirmed reboot; never retry an unconfirmed outcome."""
        with self._lock:
            if not self._read_device_info()["hid_reset_enabled"]:
                raise RuntimeError("firmware HID reset is disabled")
            self._execute_command(COMMAND_REBOOT, flags=1)

    def save_configuration(self, profile: int, unsafe_overclock: bool = False) -> ControlResult:
        """Save an advertised clock profile and return the verified command result."""
        if type(profile) is not int or not 0 <= profile <= 4 or type(unsafe_overclock) is not bool:
            raise ValueError("invalid clock profile or overclock acknowledgement")
        if profile and not unsafe_overclock:
            raise ValueError("non-default clocks require explicit unsafe overclock acknowledgement")
        with self._lock:
            info = self._read_device_info()
            if not info["configuration_save_available"]:
                raise RuntimeError("firmware does not support configuration save")
            if not int(info["clock_profile_mask"]) & (1 << profile):
                raise ValueError("clock profile is not supported on this target")
            return self._execute_command(
                COMMAND_SAVE_CONFIGURATION, flags=1 | (2 if profile else 0), profile=profile
            )

    def _execute_command(
        self, command: int, *, flags: int = 0, profile: int = 0
    ) -> ControlResult:
        self._require_open()
        self._read_device_info()
        latest = parse_control(self._read_feature(REPORT_ID_CONTROL, CONTROL_SIZE))
        if latest["status"] == STATUS_PENDING:
            raise RuntimeError("another board-control operation is pending")
        if latest["status"] == STATUS_COMPLETED and latest["action"] in (
            COMMAND_REBOOT, COMMAND_SAVE_CONFIGURATION
        ):
            raise RuntimeError("a rebooting operation is already scheduled; reconnect to inspect the result")
        payload = bytes((1, command, flags, profile))
        report = [REPORT_ID_CONTROL, *payload]
        written = self._send_feature(report)
        if written != len(report):
            raise RuntimeError(f"HID control write was incomplete: wrote {written} bytes")

        deadline = time.monotonic() + COMMAND_TIMEOUT_S
        while True:
            if time.monotonic() >= deadline:
                raise RuntimeError("board-control operation timed out; outcome is unconfirmed")
            result = parse_control(self._read_feature(REPORT_ID_CONTROL, CONTROL_SIZE, deadline))
            if result["action"] != command:
                raise RuntimeError("board-control result does not match this request; outcome is unconfirmed")
            status = int(result["status"])
            if status == STATUS_COMPLETED:
                return result
            errors = {
                STATUS_INVALID: "firmware rejected the board-control request",
                STATUS_UNSUPPORTED: "firmware does not support this board-control request",
                STATUS_BUSY: "close all CDC ports and stop UART traffic before saving configuration",
                STATUS_FAILED: "board-control operation failed",
            }
            if status in errors:
                raise RuntimeError(f"{errors[status]} (error {result['error_reason']})")
            if status not in (STATUS_IDLE, STATUS_PENDING):
                raise RuntimeError("firmware returned an invalid board-control status")
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise RuntimeError("board-control operation timed out; outcome is unconfirmed")
            time.sleep(min(COMMAND_POLL_INTERVAL_S, remaining))

    def close(self) -> bool:
        """Close the device or report that an active feature transfer is delaying closure."""
        with self._lock:
            if not self._closed:
                self._closed = True
                with self._transfer_state_lock:
                    if not self._transfer_in_flight:
                        self._device_closed = True
                        try:
                            self._device.close()
                        except OSError as error:
                            self._deferred_close_error = error
                            raise
                        finally:
                            self._device_close_complete.set()
            with self._transfer_state_lock:
                if self._deferred_close_error is not None:
                    raise self._deferred_close_error
            return self._device_close_complete.is_set()

    def wait_closed(self, timeout_s: float | None = None) -> bool:
        """Wait for a deferred device close; return false when the timeout expires."""
        if timeout_s is not None and (not math.isfinite(timeout_s) or timeout_s < 0):
            raise ValueError("timeout_s must be finite and non-negative")
        if not self._device_close_complete.wait(timeout_s):
            return False
        with self._transfer_state_lock:
            if self._deferred_close_error is not None:
                raise self._deferred_close_error
        return True

    def _close_device_after_transfer(self) -> None:
        if self._device_closed:
            return
        self._device_closed = True
        try:
            self._device.close()
        except OSError as error:
            self._deferred_close_error = error
        finally:
            self._device_close_complete.set()

    def __enter__(self) -> "PicoUartHid":
        self._require_open()
        return self

    def __exit__(self, *_: object) -> None:
        self.close()


def read_board_status(device: HidDevice) -> dict[str, object]:
    """Read static identity/capabilities and current temperature."""
    info = parse_device_info(read_feature(device, REPORT_ID_DEVICE_INFO, DEVICE_INFO_SIZE))
    telemetry = parse_telemetry(read_feature(device, REPORT_ID_TELEMETRY, TELEMETRY_SIZE))
    temperature = telemetry["temperature_celsius"]
    return {**info, "temperature_celsius": temperature, "hid_led_enabled": info["hid_led_available"]}


def read_board_temperature(device: HidDevice) -> float:
    """Read the internal RP2 sensor estimate in degrees Celsius."""
    parse_device_info(read_feature(device, REPORT_ID_DEVICE_INFO, DEVICE_INFO_SIZE))
    return float(parse_telemetry(read_feature(device, REPORT_ID_TELEMETRY, TELEMETRY_SIZE))["temperature_celsius"])


def read_firmware_version(device: HidDevice) -> str:
    info = parse_device_info(read_feature(device, REPORT_ID_DEVICE_INFO, DEVICE_INFO_SIZE))
    return str(info["firmware_version"])


def read_configuration(device: HidDevice) -> Configuration:
    return PicoUartHid(device=device).read_configuration()


def read_hardware_info(device: HidDevice) -> dict[str, object]:
    info = parse_device_info(read_feature(device, REPORT_ID_DEVICE_INFO, DEVICE_INFO_SIZE))
    telemetry = parse_telemetry(read_feature(device, REPORT_ID_TELEMETRY, TELEMETRY_SIZE))
    return {"mcu": info["mcu"], "mcu_id": info["mcu_id"], "system_clock_hz": telemetry["system_clock_hz"]}


def read_telemetry(device: HidDevice) -> Telemetry:
    parse_device_info(read_feature(device, REPORT_ID_DEVICE_INFO, DEVICE_INFO_SIZE))
    return parse_telemetry(read_feature(device, REPORT_ID_TELEMETRY, TELEMETRY_SIZE))


def read_overflow_counts(device: HidDevice) -> list[int]:
    return PicoUartHid(device=device).read_overflow_counts()


def reset_board(device: HidDevice) -> None:
    PicoUartHid(device=device).reset_board()
