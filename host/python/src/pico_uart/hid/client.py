# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""Resource-owning Python API for PicoUart's version-1 HID reports."""

import math
import threading
import time
from collections.abc import Callable
from copy import deepcopy
from typing import TypeVar

from .gate import DeviceGate
from .input_tracker import InputTracker
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
    BoardStatus,
    ChannelDiagnostic,
    ChannelFeatureSample,
    ChannelSample,
    Configuration,
    ControlResult,
    DeviceIdentity,
    HardwareInfo,
    RuntimeTelemetry,
    StatusSnapshot,
    Telemetry,
)

COMMAND_TIMEOUT_S = 3.0
COMMAND_POLL_INTERVAL_S = 0.05
BAUD_READY_TOLERANCE = 0.03
_TransferResult = TypeVar("_TransferResult")


class ControlOutcomeUnconfirmed(RuntimeError):
    """A submitted board-control request may have executed, but its result was not confirmed."""


class ControlRequestRejected(RuntimeError):
    """Firmware returned a result confirming that a board-control request was not accepted."""


class _TransferNotStarted(RuntimeError):
    """A HID transfer was refused before its device operation could run."""


def _require_channel_index(index: int) -> None:
    if type(index) is not int or not 0 <= index < UART_CHANNEL_COUNT:
        raise ValueError("channel index must be 0 through 5")


class PicoUartHid:
    """Own a PicoUart HID connection and expose decoded reports and board controls.

    Methods are thread-safe: one reentrant lock serializes device access. Feature transfers are bounded by
    ``COMMAND_TIMEOUT_S``; a timed-out transfer leaves its outcome unconfirmed and is never retried, and
    further device I/O fails until it returns. ``close()`` defers closing the handle until it returns.
    """

    def __init__(
        self,
        serial_number: str | None = None,
        device_path: str | None = None,
        device: HidDevice | None = None,
    ) -> None:
        if device is not None and (serial_number is not None or device_path is not None):
            raise ValueError("an injected device cannot be combined with a device selector")
        self._gate = DeviceGate(device if device is not None else open_device(serial_number, device_path))
        self._lock = threading.RLock()
        self._device_info: DeviceIdentity | None = None
        self._configuration: Configuration | None = None
        self._inputs = InputTracker()
        self._channels = self._inputs.channels

    def _require_open(self) -> HidDevice:
        if self._gate.closing:
            raise RuntimeError("PicoUart HID connection is closed")
        return self._gate.device

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
        try:
            self._gate.begin_transfer()
        except RuntimeError as error:
            raise _TransferNotStarted(str(error)) from error
        completed = threading.Event()
        result: list[_TransferResult] = []
        failure: list[Exception] = []

        def run() -> None:
            try:
                result.append(operation())
            except Exception as error:  # The worker must relay any operation failure to its caller.
                failure.append(error)
            finally:
                self._gate.end_transfer(completed)

        worker = threading.Thread(target=run, name="pico-uart-hid-transfer", daemon=True)
        try:
            worker.start()
        except RuntimeError as error:
            self._gate.abort_transfer()
            raise _TransferNotStarted(str(error)) from error
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
        device = self._gate.device
        return self._bounded_transfer(lambda: device.send_feature_report(report))

    def read_identity(self) -> DeviceIdentity:
        """Read static device identity, firmware, capabilities, and memory information."""
        with self._lock:
            return DeviceIdentity(**self._read_device_info())

    def read_board_status(self) -> BoardStatus:
        """Return static board identity/capabilities together with the current temperature."""
        info = self.read_identity()
        return {
            **info,
            "temperature_celsius": self.read_telemetry()["temperature_celsius"],
            "hid_led_enabled": info["hid_led_available"],
        }

    def read_board_temperature(self) -> float:
        """Read the internal RP2 sensor estimate in degrees Celsius."""
        return float(self.read_telemetry()["temperature_celsius"])

    def read_firmware_version(self) -> str:
        """Read the firmware semantic version string."""
        return str(self.read_identity()["firmware_version"])

    def read_hardware_info(self) -> HardwareInfo:
        """Read MCU identity and the current system clock."""
        info = self.read_identity()
        telemetry = self.read_telemetry()
        return {"mcu": info["mcu"], "mcu_id": info["mcu_id"], "system_clock_hz": telemetry["system_clock_hz"]}

    def read_configuration(self, *, deadline: float | None = None) -> Configuration:
        """Read immutable boot metadata independently of the action result."""
        if deadline is not None and not math.isfinite(deadline):
            raise ValueError("deadline must be finite")
        if deadline is None:
            self._lock.acquire()
        else:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not self._lock.acquire(timeout=min(remaining, threading.TIMEOUT_MAX)):
                raise TimeoutError("configuration read timed out waiting for HID lock")
        try:
            if deadline is not None and time.monotonic() >= deadline:
                raise TimeoutError("configuration read timed out waiting for HID lock")
            if self._configuration is None:
                self._configuration = parse_configuration(
                    self._read_feature(REPORT_ID_CONFIGURATION, CONFIGURATION_SIZE, deadline),
                    self._read_device_info(deadline),
                )
            configuration = deepcopy(self._configuration)
            if deadline is not None and time.monotonic() >= deadline:
                raise TimeoutError("configuration read timed out")
            return configuration
        finally:
            self._lock.release()

    def read_configuration_save(self) -> ControlResult:
        """Read the latest device-global action result."""
        return self.read_control()

    def read_control(self) -> ControlResult:
        """Read the latest device-global board-control result."""
        with self._lock:
            return parse_control(self._read_feature(REPORT_ID_CONTROL, CONTROL_SIZE))

    def read_telemetry(self) -> Telemetry:
        """Read live LED, temperature, uptime, service-load, and clock values."""
        with self._lock:
            return parse_telemetry(self._read_feature(REPORT_ID_TELEMETRY, TELEMETRY_SIZE))

    def read_runtime(self) -> RuntimeTelemetry:
        """Read live service-load and runtime telemetry."""
        return self.read_telemetry()["runtime"]

    def read_overflow_counts(self) -> list[int]:
        """Read cumulative UART RX dropped-byte counts for every channel."""
        return [int(item["rx_overrun_count"]) for item in self.read_channel_diagnostics()]

    def read_channel_diagnostics(self) -> list[ChannelDiagnostic]:
        """Read non-consuming diagnostics for every channel."""
        return [self.read_channel_diagnostic(index) for index in range(UART_CHANNEL_COUNT)]

    def read_channel_diagnostic(self, index: int) -> ChannelDiagnostic:
        """Read one channel's non-consuming combined Feature snapshot."""
        _require_channel_index(index)
        with self._lock:
            configuration = self.read_configuration()
            payload = self._read_feature(REPORT_ID_DIAGNOSTIC_BASE + index, DIAGNOSTIC_SIZE)
            return {"id": index, **parse_diagnostic(payload, configuration)}

    def read_channel(self, index: int) -> ChannelFeatureSample:
        """Read a non-consuming snapshot; never merge it into Input traffic accounting."""
        _require_channel_index(index)
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
        if deadline is None:
            self._lock.acquire()
        else:
            remaining_s = deadline - time.monotonic()
            if remaining_s <= 0 or not self._lock.acquire(
                timeout=min(remaining_s, threading.TIMEOUT_MAX)
            ):
                return None
        try:
            self._read_device_info(deadline)
            configuration = self.read_configuration(deadline=deadline)
            # ponytail: hidapi treats zero as an infinite wait; one millisecond is the smallest portable poll.
            input_timeout_ms = max(1, timeout_ms)
            if deadline is not None:
                remaining_ms = int((deadline - time.monotonic()) * 1000)
                if remaining_ms <= 0:
                    return None
                input_timeout_ms = min(max(1, timeout_ms), remaining_ms)
            if input_timeout_ms <= 0:
                return None
            device = self._require_open()
            self._gate.begin_transfer()
            try:
                report = device.read(CHANNEL_SIZE + 1, input_timeout_ms)
            finally:
                self._gate.end_transfer()
            if deadline is not None and time.monotonic() >= deadline:
                return None
            if not report:
                return None
            if len(report) != CHANNEL_SIZE + 1 or not REPORT_ID_CHANNEL_BASE <= report[0] < (
                REPORT_ID_CHANNEL_BASE + UART_CHANNEL_COUNT
            ):
                raise RuntimeError("unexpected channel Input report ID or size")
            channel_id = report[0] - REPORT_ID_CHANNEL_BASE
            self._inputs.apply(
                channel_id,
                parse_channel(bytes(report[1:]), channel_id, self._read_device_info(), configuration),
                time.monotonic(),
            )
            return self._inputs.snapshot()
        finally:
            self._lock.release()

    def wait_channels_ready(
        self, channels: dict[int, int], timeout_s: float = 3.0
    ) -> dict[int, ChannelFeatureSample]:
        """Wait for selected open channels to apply raw 8N1 at the requested baud (within `BAUD_READY_TOLERANCE`).

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
                if channel_ready(sample, baud):
                    samples[index] = {"id": index, **sample}
            if len(samples) == len(channels):
                return samples
            time.sleep(min(0.05, max(0.0, deadline - time.monotonic())))

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
        try:
            written = self._send_feature(report)
        except _TransferNotStarted:
            raise
        except (OSError, RuntimeError) as error:
            raise ControlOutcomeUnconfirmed(
                "board-control command submission failed; outcome is unconfirmed"
            ) from error
        if written != len(report):
            raise ControlOutcomeUnconfirmed(
                f"HID control write was incomplete: wrote {written} bytes; outcome is unconfirmed"
            )

        deadline = time.monotonic() + COMMAND_TIMEOUT_S
        while True:
            if time.monotonic() >= deadline:
                raise ControlOutcomeUnconfirmed("board-control operation timed out; outcome is unconfirmed")
            try:
                result = parse_control(self._read_feature(REPORT_ID_CONTROL, CONTROL_SIZE, deadline))
            except (OSError, RuntimeError) as error:
                raise ControlOutcomeUnconfirmed(
                    "board-control result could not be confirmed; outcome is unconfirmed"
                ) from error
            if result["action"] != command:
                raise ControlOutcomeUnconfirmed(
                    "board-control result does not match this request; outcome is unconfirmed"
                )
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
                raise ControlRequestRejected(f"{errors[status]} (error {result['error_reason']})")
            if status not in (STATUS_IDLE, STATUS_PENDING):
                raise RuntimeError("firmware returned an invalid board-control status")
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise ControlOutcomeUnconfirmed("board-control operation timed out; outcome is unconfirmed")
            time.sleep(min(COMMAND_POLL_INTERVAL_S, remaining))

    def close(self) -> bool:
        """Close the device or report that an active feature transfer is delaying closure."""
        with self._lock:
            return self._gate.close()

    def wait_closed(self, timeout_s: float | None = None) -> bool:
        """Wait for a deferred device close; return false when the timeout expires."""
        if timeout_s is not None and (not math.isfinite(timeout_s) or timeout_s < 0):
            raise ValueError("timeout_s must be finite and non-negative")
        return self._gate.wait_closed(timeout_s)

    def __enter__(self) -> "PicoUartHid":
        self._require_open()
        return self

    def __exit__(self, *_: object) -> None:
        self.close()


def channel_ready(sample: ChannelSample, baud: int) -> bool:
    """Return whether a channel is ready, raw 8N1, and within tolerance of the requested baud rate."""
    return bool(
        sample["state"] == "ready" and sample["baud_rate"]
        and abs(sample["baud_rate"] - baud) <= baud * BAUD_READY_TOLERANCE
        and (sample["data_bits"], sample["parity"], sample["stop_bits"]) == (8, "none", 1)
    )


def read_board_status(device: HidDevice) -> BoardStatus:
    """Read static identity/capabilities and current temperature from an already opened device."""
    return PicoUartHid(device=device).read_board_status()


def read_board_temperature(device: HidDevice) -> float:
    """Read the internal RP2 sensor estimate in degrees Celsius."""
    return PicoUartHid(device=device).read_board_temperature()


def read_firmware_version(device: HidDevice) -> str:
    """Read the firmware semantic version string."""
    return PicoUartHid(device=device).read_firmware_version()


def read_configuration(device: HidDevice) -> Configuration:
    """Read immutable boot metadata."""
    return PicoUartHid(device=device).read_configuration()


def read_hardware_info(device: HidDevice) -> HardwareInfo:
    """Read MCU identity and the current system clock."""
    return PicoUartHid(device=device).read_hardware_info()


def read_telemetry(device: HidDevice) -> Telemetry:
    """Read live LED, temperature, uptime, service-load, and clock values."""
    return PicoUartHid(device=device).read_telemetry()


def read_overflow_counts(device: HidDevice) -> list[int]:
    """Read cumulative UART RX dropped-byte counts."""
    return PicoUartHid(device=device).read_overflow_counts()


def reset_board(device: HidDevice) -> None:
    """Request one confirmed board reboot."""
    PicoUartHid(device=device).reset_board()
