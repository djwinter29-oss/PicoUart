"""Resource-owning Python API for PicoUart's version-1 HID reports."""

from copy import deepcopy
from datetime import datetime, timezone
import threading
import time
import math
from collections.abc import Callable
from typing import Any

from .protocol import (
    CHANNEL_SIZE,
    COMMAND_LED_OFF,
    COMMAND_LED_ON,
    COMMAND_REBOOT,
    COMMAND_SAVE_CONFIGURATION,
    CONTROL_SIZE,
    CONFIGURATION_SIZE,
    DIAGNOSTIC_SIZE,
    DEVICE_INFO_SIZE,
    MAX_U32,
    REPORT_ID_CHANNEL_BASE,
    REPORT_ID_CONTROL,
    REPORT_ID_CONFIGURATION,
    REPORT_ID_DIAGNOSTIC_BASE,
    REPORT_ID_DEVICE_INFO,
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
    parse_diagnostic,
    parse_control,
    parse_device_info,
    parse_telemetry,
    require_payload,
)
from .transport import open_device


TRAFFIC_FIELDS = {
    "uart_tx": "uart_tx_bytes",
    "uart_rx": "uart_rx_bytes",
    "usb_tx": "cdc_tx_bytes",
    "usb_rx": "cdc_rx_bytes",
}
COMMAND_TIMEOUT_S = 3.0
COMMAND_POLL_INTERVAL_S = 0.05


def read_feature(device: Any, report_id: int, payload_size: int) -> bytes:
    """Read a fixed-size feature report, excluding its HID report ID."""
    return require_payload(device.get_feature_report(report_id, payload_size + 1), report_id, payload_size)


def _empty_channel(channel_id: int) -> dict[str, Any]:
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
        "traffic": {field: None for field in TRAFFIC_FIELDS},
        "totals": {field: 0 for field in TRAFFIC_FIELDS},
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
        device: Any | None = None,
    ) -> None:
        if device is not None and (serial_number is not None or device_path is not None):
            raise ValueError("an injected device cannot be combined with a device selector")
        self._device = device if device is not None else open_device(serial_number, device_path)
        self._lock = threading.RLock()
        self._closed = False
        self._device_info: dict[str, object] | None = None
        self._configuration: dict[str, object] | None = None
        self._transfer_in_flight = False
        self._channels = [_empty_channel(index) for index in range(UART_CHANNEL_COUNT)]
        self._last_counters: list[dict[str, int] | None] = [None] * UART_CHANNEL_COUNT
        self._input_seen = [False] * UART_CHANNEL_COUNT
        self._last_input_at: list[float | None] = [None] * UART_CHANNEL_COUNT
        self._ignored_gap = [False] * UART_CHANNEL_COUNT

    def _require_open(self) -> Any:
        if self._closed:
            raise RuntimeError("PicoUart HID connection is closed")
        return self._device

    def _read_device_info(self) -> dict[str, object]:
        if self._device_info is None:
            report = self._bounded_transfer(
                lambda: self._require_open().get_feature_report(REPORT_ID_DEVICE_INFO, 65), COMMAND_TIMEOUT_S
            )
            if not report or report[0] != REPORT_ID_DEVICE_INFO:
                raise RuntimeError("invalid identity report ID")
            self._device_info = parse_device_info(bytes(report[1:]))
        return self._device_info

    def _bounded_transfer(self, operation: Callable[[], Any], timeout_s: float = COMMAND_TIMEOUT_S) -> Any:
        """Bound synchronous hidapi feature I/O and quarantine a call that returns late."""
        if self._transfer_in_flight:
            raise RuntimeError("a previous HID feature transfer is still in flight")
        completed = threading.Event()
        result: list[Any] = []
        failure: list[Exception] = []

        def run() -> None:
            try:
                result.append(operation())
            except Exception as error:
                failure.append(error)
            finally:
                completed.set()

        worker = threading.Thread(target=run, name="pico-uart-hid-transfer", daemon=True)
        worker.start()
        if not completed.wait(max(0.0, timeout_s)):
            self._transfer_in_flight = True
            raise RuntimeError("HID feature transfer timed out; outcome is unconfirmed")
        if failure:
            raise failure[0]
        return result[0]

    def _read_feature(self, report_id: int, payload_size: int, deadline: float | None = None) -> bytes:
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

    def read_identity(self) -> dict[str, object]:
        """Read static device identity, firmware, capabilities, and memory information."""
        with self._lock:
            return dict(self._read_device_info())

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

    def read_configuration(self) -> dict[str, object]:
        """Read immutable boot metadata independently of the action result."""
        with self._lock:
            if self._configuration is None:
                self._configuration = parse_configuration(
                    self._read_feature(REPORT_ID_CONFIGURATION, CONFIGURATION_SIZE), self._read_device_info())
            return deepcopy(self._configuration)

    def read_configuration_save(self) -> dict[str, object]:
        """Read the latest device-global action result."""
        return self.read_control()

    def read_control(self) -> dict[str, object]:
        with self._lock:
            return parse_control(self._read_feature(REPORT_ID_CONTROL, CONTROL_SIZE))

    def read_telemetry(self) -> dict[str, object]:
        """Read live LED, temperature, uptime, service-load, and clock values."""
        with self._lock:
            telemetry = parse_telemetry(self._read_feature(REPORT_ID_TELEMETRY, TELEMETRY_SIZE))
            return telemetry

    def read_runtime(self) -> dict[str, object]:
        return self.read_telemetry()["runtime"]

    def read_overflow_counts(self) -> list[int]:
        return [int(item["rx_overrun_count"]) for item in self.read_channel_diagnostics()]

    def read_channel_diagnostics(self) -> list[dict[str, object]]:
        return [self.read_channel_diagnostic(index) for index in range(UART_CHANNEL_COUNT)]

    def read_channel_diagnostic(self, index: int) -> dict[str, object]:
        """Read one channel's non-consuming combined Feature snapshot."""
        if type(index) is not int or not 0 <= index < UART_CHANNEL_COUNT:
            raise ValueError("channel index must be 0 through 5")
        with self._lock:
            payload = self._read_feature(REPORT_ID_DIAGNOSTIC_BASE + index, DIAGNOSTIC_SIZE)
            return {"id": index, **parse_diagnostic(payload, self._configuration)}

    def read_channel(self, index: int) -> dict[str, object]:
        """Read a non-consuming snapshot; never merge it into Input traffic accounting."""
        if type(index) is not int or not 0 <= index < UART_CHANNEL_COUNT:
            raise ValueError("channel index must be 0 through 5")
        with self._lock:
            return {"id": index, **parse_channel(self._read_feature(REPORT_ID_CHANNEL_BASE + index, CHANNEL_SIZE))}

    def read_status(self, timeout_ms: int = 250) -> dict[str, object] | None:
        """Read one Input report, derive its counter increments, and return the latest channel snapshot."""
        if timeout_ms < 0:
            raise ValueError("timeout_ms must not be negative")
        with self._lock:
            report = self._require_open().read(CHANNEL_SIZE + 1, timeout_ms)
            if not report:
                return None
            if len(report) != CHANNEL_SIZE + 1 or not REPORT_ID_CHANNEL_BASE <= report[0] < (
                REPORT_ID_CHANNEL_BASE + UART_CHANNEL_COUNT
            ):
                raise RuntimeError("unexpected channel Input report ID or size")
            channel_id = report[0] - REPORT_ID_CHANNEL_BASE
            self._apply_input(channel_id, parse_channel(bytes(report[1:])), time.monotonic())
            return {"channels": deepcopy(self._channels)}

    def wait_channels_ready(self, channels: dict[int, int], timeout_s: float = 3.0) -> dict[int, dict[str, object]]:
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
            samples = {}
            for index, baud in channels.items():
                if time.monotonic() >= deadline:
                    raise TimeoutError(f"CDC readiness timed out for channels {list(channels)}")
                try:
                    remaining = deadline - time.monotonic()
                    if remaining <= 0 or not self._lock.acquire(timeout=remaining):
                        raise TimeoutError("CDC readiness timed out waiting for HID lock")
                    try:
                        sample = parse_channel(self._read_feature(REPORT_ID_CHANNEL_BASE + index, CHANNEL_SIZE, deadline))
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

    def _apply_input(self, channel_id: int, sample: dict[str, object], now: float) -> None:
        channel = self._channels[channel_id]
        previous_at = self._last_input_at[channel_id]
        previous_sequence = channel["sequence"]
        first = not self._input_seen[channel_id]
        sequence_delta = None if first else (int(sample["sequence"]) - int(previous_sequence)) & 0xFFFF
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
        interval = int(sample["interval_ms"])
        invalid_interval = interval in (0, 65535)
        counters = {field: int(sample[source]) for field, source in TRAFFIC_FIELDS.items()}
        previous_counters = self._last_counters[channel_id]
        if gap or invalid_interval:
            channel["traffic_incomplete"] = True
        if gap:
            channel["traffic_gaps"] += 1
        previous_measurement = channel["uart_measurement_state"]
        clean = not first and not gap and not invalid_interval
        traffic = {
            field: ((value - previous_counters[field]) & MAX_U32)
            if clean and (field.startswith("usb") or
                          (sample["uart_measurement_state"] == 1 and previous_measurement == 1)) else None
            for field, value in counters.items()
        }
        channel["traffic"] = traffic
        channel["totals"] = counters
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

    def save_configuration(self, profile: int, unsafe_overclock: bool = False) -> dict[str, object]:
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
    ) -> dict[str, object]:
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

    def close(self) -> None:
        with self._lock:
            if not self._closed:
                self._device.close()
                self._closed = True

    def __enter__(self) -> "PicoUartHid":
        self._require_open()
        return self

    def __exit__(self, *_: object) -> None:
        self.close()


def read_board_status(device: Any) -> dict[str, object]:
    """Read static identity/capabilities and current temperature."""
    info = parse_device_info(read_feature(device, REPORT_ID_DEVICE_INFO, DEVICE_INFO_SIZE))
    telemetry = parse_telemetry(read_feature(device, REPORT_ID_TELEMETRY, TELEMETRY_SIZE))
    temperature = telemetry["temperature_celsius"]
    return {**info, "temperature_celsius": temperature, "hid_led_enabled": info["hid_led_available"]}


def read_board_temperature(device: Any) -> float:
    """Read the internal RP2 sensor estimate in degrees Celsius."""
    return float(parse_telemetry(read_feature(device, REPORT_ID_TELEMETRY, TELEMETRY_SIZE))["temperature_celsius"])


def read_firmware_version(device: Any) -> str:
    info = parse_device_info(read_feature(device, REPORT_ID_DEVICE_INFO, DEVICE_INFO_SIZE))
    return str(info["firmware_version"])


def read_configuration(device: Any) -> dict[str, object]:
    return PicoUartHid(device=device).read_configuration()


def read_hardware_info(device: Any) -> dict[str, object]:
    info = parse_device_info(read_feature(device, REPORT_ID_DEVICE_INFO, DEVICE_INFO_SIZE))
    telemetry = parse_telemetry(read_feature(device, REPORT_ID_TELEMETRY, TELEMETRY_SIZE))
    return {"mcu": info["mcu"], "mcu_id": info["mcu_id"], "system_clock_hz": telemetry["system_clock_hz"]}


def read_telemetry(device: Any) -> dict[str, object]:
    return parse_telemetry(read_feature(device, REPORT_ID_TELEMETRY, TELEMETRY_SIZE))


def read_overflow_counts(device: Any) -> list[int]:
    return PicoUartHid(device=device).read_overflow_counts()


def reset_board(device: Any) -> None:
    PicoUartHid(device=device).reset_board()
