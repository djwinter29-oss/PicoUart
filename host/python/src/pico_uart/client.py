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
    COMMAND_ARM_REBOOT,
    COMMAND_LED_OFF,
    COMMAND_LED_ON,
    COMMAND_REBOOT,
    COMMAND_SAVE_CONFIGURATION,
    CONTROL_SIZE,
    DEVICE_INFO_SIZE,
    MAX_U32,
    REPORT_ID_CHANNEL_BASE,
    REPORT_ID_CONTROL,
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
    decode_health,
    parse_channel,
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
        "health": None,
        "health_labels": [],
        "state": "unknown",
        "cdc_open": False,
        "ring_high_watermark": 0,
        "traffic": {field: 0 for field in TRAFFIC_FIELDS},
        "totals": {field: 0 for field in TRAFFIC_FIELDS},
        "interval_ms": None,
        "baud_rate": None,
        "data_bits": None,
        "stop_bits": None,
        "parity": None,
        "tx_pin": None,
        "rx_pin": None,
        "faults": {name: {"count": 0} for name in ("init_failed", "control_error", "rx_overrun", "rx_error")},
        "last_fault_uptime_s": None,
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
        self._next_request_id = 1
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
            self._device_info = parse_device_info(
                self._read_feature(REPORT_ID_DEVICE_INFO, DEVICE_INFO_SIZE)
            )
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
        """Read boot configuration and latest command-result metadata."""
        result = self.read_control()
        return {key: result[key] for key in ("source", "reason", "stored_profile", "active_profile")}

    def read_configuration_save(self) -> dict[str, object]:
        """Read report 2's latest command result using the legacy API method name."""
        result = self.read_control()
        return {
            "status": result["status"],
            "profile": result["requested_profile"],
            "command": result["command"],
            "request_id": result["request_id"],
            "error_reason": result["error_reason"],
        }

    def read_control(self) -> dict[str, object]:
        with self._lock:
            return parse_control(self._read_feature(REPORT_ID_CONTROL, CONTROL_SIZE))

    def read_telemetry(self) -> dict[str, object]:
        """Read live LED, temperature, uptime, service-load, RAM, and clock values."""
        with self._lock:
            telemetry = parse_telemetry(self._read_feature(REPORT_ID_TELEMETRY, TELEMETRY_SIZE))
            if self._device_info is not None:
                total = int(self._device_info["ram_total_bytes"])
                reserved = int(telemetry["runtime"]["ram_reserved_bytes"])
                if reserved > total:
                    raise RuntimeError("telemetry RAM reservation exceeds total RAM")
                telemetry["runtime"]["ram_total_bytes"] = total
            return telemetry

    def read_runtime(self) -> dict[str, object]:
        return self.read_telemetry()["runtime"]

    def read_overflow_counts(self) -> list[int]:
        return [int(item["faults"]["rx_overrun"]["count"]) for item in self.read_channel_diagnostics()]

    def read_channel_diagnostics(self) -> list[dict[str, object]]:
        return [self.read_channel_diagnostic(index) for index in range(UART_CHANNEL_COUNT)]

    def read_channel_diagnostic(self, index: int) -> dict[str, object]:
        """Read one channel's non-consuming combined Feature snapshot."""
        if type(index) is not int or not 0 <= index < UART_CHANNEL_COUNT:
            raise ValueError("channel index must be 0 through 5")
        with self._lock:
            payload = self._read_feature(REPORT_ID_CHANNEL_BASE + index, CHANNEL_SIZE)
            return {"id": index, **parse_channel(payload)}

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
                health = sample["health"]
                if health & 0x06:
                    raise OSError(f"CDC{index} configuration failed: {sample['health_labels']}")
                if (health & 0x19 == 0x11 and sample["baud_rate"]
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
        invalid_interval = interval in (0, MAX_U32)
        counters = {field: int(sample[source]) for field, source in TRAFFIC_FIELDS.items()}
        previous_counters = self._last_counters[channel_id]
        if gap or invalid_interval:
            channel["traffic_incomplete"] = True
        if gap:
            channel["traffic_gaps"] += 1
        traffic = {
            field: (value - previous_counters[field]) & MAX_U32 if previous_counters is not None else 0
            for field, value in counters.items()
        }
        channel["traffic"] = traffic
        channel["totals"] = counters
        if not first and not gap and not invalid_interval:
            channel["observed_interval_ms"] += interval
        labels = decode_health(int(sample["health"]))
        channel.update({
            "updated_at": datetime.now(timezone.utc).isoformat(timespec="milliseconds"),
            "sequence": sample["sequence"],
            "interval_ms": interval,
            "health": sample["health"],
            "health_labels": labels,
            "state": (
                "attention" if {"init_failed", "control_error", "rx_overrun", "rx_error"}.intersection(labels)
                else "ready" if "ready" in labels else "initializing"
            ),
            "cdc_open": "cdc_open" in labels,
            "ring_high_watermark": sample["ring_high_watermark"],
            "baud_rate": sample["baud_rate"],
            "data_bits": sample["data_bits"],
            "stop_bits": sample["stop_bits"],
            "parity": sample["parity"],
            "tx_pin": sample["tx_pin"],
            "rx_pin": sample["rx_pin"],
            "faults": sample["faults"],
            "last_fault_uptime_s": sample["last_fault_uptime_s"],
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
        """Run the capability-gated arm and reboot command sequence."""
        with self._lock:
            if not self._read_device_info()["hid_reset_enabled"]:
                raise RuntimeError("firmware HID reset is disabled")
            self._execute_command(COMMAND_ARM_REBOOT)
            self._execute_command(COMMAND_REBOOT)

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
                COMMAND_SAVE_CONFIGURATION, flags=int(unsafe_overclock), profile=profile
            )

    def _allocate_request_id(self, latest_id: int) -> int:
        candidate = self._next_request_id
        if candidate == latest_id:
            candidate += 1
        if candidate > MAX_U32 or candidate == MAX_U32:
            raise RuntimeError("control request ID space exhausted; reconnect before issuing another command")
        self._next_request_id = candidate + 1
        return candidate

    def _execute_command(
        self, command: int, *, flags: int = 0, profile: int = 0
    ) -> dict[str, object]:
        self._require_open()
        self._read_device_info()
        latest = parse_control(self._read_feature(REPORT_ID_CONTROL, CONTROL_SIZE))
        if latest["status"] == STATUS_PENDING:
            raise RuntimeError("another board-control operation is pending")
        if latest["status"] == STATUS_COMPLETED and latest["command"] in (
            COMMAND_REBOOT, COMMAND_SAVE_CONFIGURATION
        ):
            raise RuntimeError("a rebooting operation is already scheduled; reconnect to inspect the result")
        request_id = self._allocate_request_id(int(latest["request_id"]))
        payload = bytes((1, command, flags, 0)) + request_id.to_bytes(4, "little") + bytes(8)
        payload += profile.to_bytes(4, "little") if command == COMMAND_SAVE_CONFIGURATION else bytes(4)
        payload += bytes(4)
        report = [REPORT_ID_CONTROL, *payload]
        written = self._send_feature(report)
        if written != len(report):
            raise RuntimeError(f"HID control write was incomplete: wrote {written} bytes")

        deadline = time.monotonic() + COMMAND_TIMEOUT_S
        while True:
            if time.monotonic() >= deadline:
                raise RuntimeError("board-control operation timed out; outcome is unconfirmed")
            result = parse_control(self._read_feature(REPORT_ID_CONTROL, CONTROL_SIZE, deadline))
            if result["request_id"] != request_id or result["command"] != command:
                raise RuntimeError("board-control result does not match this request; outcome is unconfirmed")
            if result["flags"] != flags:
                raise RuntimeError("board-control result flags do not match this request")
            if (
                command == COMMAND_SAVE_CONFIGURATION
                and result["status"] != STATUS_UNSUPPORTED
                and result["requested_profile"] != profile
            ):
                raise RuntimeError("configuration result does not match the requested profile")
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
    if int(telemetry["runtime"]["ram_reserved_bytes"]) > int(info["ram_total_bytes"]):
        raise RuntimeError("telemetry RAM reservation exceeds total RAM")
    temperature = telemetry["temperature_celsius"]
    return {**info, "temperature_celsius": temperature, "hid_led_enabled": info["hid_led_available"]}


def read_board_temperature(device: Any) -> float:
    """Read the internal RP2 sensor estimate in degrees Celsius."""
    return float(parse_telemetry(read_feature(device, REPORT_ID_TELEMETRY, TELEMETRY_SIZE))["temperature_celsius"])


def read_firmware_version(device: Any) -> str:
    info = parse_device_info(read_feature(device, REPORT_ID_DEVICE_INFO, DEVICE_INFO_SIZE))
    return str(info["firmware_version"])


def read_configuration(device: Any) -> dict[str, object]:
    result = parse_control(read_feature(device, REPORT_ID_CONTROL, CONTROL_SIZE))
    return {key: result[key] for key in ("source", "reason", "stored_profile", "active_profile")}


def read_hardware_info(device: Any) -> dict[str, object]:
    info = parse_device_info(read_feature(device, REPORT_ID_DEVICE_INFO, DEVICE_INFO_SIZE))
    telemetry = parse_telemetry(read_feature(device, REPORT_ID_TELEMETRY, TELEMETRY_SIZE))
    if int(telemetry["runtime"]["ram_reserved_bytes"]) > int(info["ram_total_bytes"]):
        raise RuntimeError("telemetry RAM reservation exceeds total RAM")
    return {"mcu": info["mcu"], "mcu_id": info["mcu_id"], "system_clock_hz": telemetry["system_clock_hz"]}


def read_telemetry(device: Any) -> dict[str, object]:
    telemetry = parse_telemetry(read_feature(device, REPORT_ID_TELEMETRY, TELEMETRY_SIZE))
    total = int(read_board_status(device)["ram_total_bytes"])
    if int(telemetry["runtime"]["ram_reserved_bytes"]) > total:
        raise RuntimeError("telemetry RAM reservation exceeds total RAM")
    telemetry["runtime"]["ram_total_bytes"] = total
    return telemetry


def read_overflow_counts(device: Any) -> list[int]:
    counts = []
    for index in range(UART_CHANNEL_COUNT):
        channel = parse_channel(read_feature(device, REPORT_ID_CHANNEL_BASE + index, CHANNEL_SIZE))
        counts.append(int(channel["faults"]["rx_overrun"]["count"]))
    return counts


def reset_board(device: Any) -> None:
    PicoUartHid(device=device).reset_board()
