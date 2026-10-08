"""HID device discovery and platform-specific opening helpers."""

import os
from pathlib import Path
import select
from typing import Any

try:
    import fcntl
except ImportError:  # pragma: no cover - only used by the Linux hidraw fallback
    fcntl = None

try:
    import hid
except ImportError as error:
    hid = None
    _HID_IMPORT_ERROR = error
else:
    _HID_IMPORT_ERROR = None


VENDOR_ID = 0xCAFE  # Keep in sync with firmware/src/config/usb_identity.h
PRODUCT_ID = 0x4010  # Development placeholder; see SECURITY.md
USAGE_PAGE = 0xFF00
USAGE = 0x0001
PRODUCT_STRING = "PicoUart CDC+HID PIO 8N1"
HID_INTERFACE_NUMBER = 12


def require_hid() -> None:
    """Raise a clear error if hidapi is unavailable, only when opening a device."""
    if hid is None:
        raise RuntimeError("Missing dependency: install host/python/requirements.txt") from _HID_IMPORT_ERROR


class _LinuxHidrawDevice:
    """Small hidraw adapter used when the installed hidapi uses libusb."""

    def __init__(self, path: str) -> None:
        self._file_descriptor = os.open(path, os.O_RDWR | os.O_NONBLOCK)

    def close(self) -> None:
        os.close(self._file_descriptor)

    def read(self, size: int, timeout_ms: int) -> list[int]:
        readable, _, _ = select.select([self._file_descriptor], [], [], timeout_ms / 1000.0)
        if not readable:
            return []
        return list(os.read(self._file_descriptor, size))

    def get_feature_report(self, report_id: int, size: int) -> list[int]:
        if fcntl is None:
            raise OSError("hidraw fallback requires fcntl")
        report = bytearray(size)
        report[0] = report_id
        bytes_read = fcntl.ioctl(
            self._file_descriptor,
            _hidraw_feature_ioctl(0x07, size),
            report,
            True,
        )
        return list(report[:bytes_read])

    def send_feature_report(self, report: list[int]) -> int:
        if fcntl is None:
            raise OSError("hidraw fallback requires fcntl")
        payload = bytearray(report)
        return fcntl.ioctl(
            self._file_descriptor,
            _hidraw_feature_ioctl(0x06, len(payload)),
            payload,
            True,
        )


def _hidraw_feature_ioctl(command: int, size: int) -> int:
    """Build HIDIOCGFEATURE/HIDIOCSFEATURE for a report payload size."""
    ioc_write = 1
    ioc_read = 2
    ioc_nr_bits = 8
    ioc_type_bits = 8
    ioc_size_bits = 14
    ioc_nr_shift = 0
    ioc_type_shift = ioc_nr_shift + ioc_nr_bits
    ioc_size_shift = ioc_type_shift + ioc_type_bits
    ioc_dir_shift = ioc_size_shift + ioc_size_bits
    direction = ioc_read | ioc_write
    if command == 0x07:
        direction = ioc_read | ioc_write
    elif command == 0x06:
        direction = ioc_read | ioc_write
    return (
        (direction << ioc_dir_shift)
        | (ord("H") << ioc_type_shift)
        | (command << ioc_nr_shift)
        | (size << ioc_size_shift)
    )


def _resolve_hidraw_path(enumerated_path: str) -> str | None:
    """Resolve a Linux hidapi interface path to its corresponding hidraw node."""
    if os.name != "posix" or not enumerated_path or enumerated_path.startswith("/dev/"):
        return None

    for device_link in Path("/sys/class/hidraw").glob("hidraw*/device"):
        try:
            device_parts = os.path.realpath(device_link).split(os.sep)
        except OSError:
            continue
        if enumerated_path in device_parts:
            return f"/dev/{device_link.parent.name}"
    return None


def _open_hidraw_device(path: str) -> Any:
    """Open a Linux hidraw node with the standard file-descriptor API."""
    if os.name != "posix":
        return None
    return _LinuxHidrawDevice(path)


def open_enumerated_device(device_info: dict[str, Any]) -> Any:
    """Open one enumerated HID device, resolving Linux interface paths when needed."""
    require_hid()
    device = hid.device()
    path = device_info.get("path", b"")

    try:
        device.open_path(path)
    except OSError as error:
        original_path = path.decode("utf-8", errors="replace") if isinstance(path, bytes) else path
        resolved_path = _resolve_hidraw_path(original_path)
        if resolved_path is not None:
            try:
                device.open_path(os.fsencode(resolved_path))
                return device
            except OSError:
                try:
                    return _open_hidraw_device(resolved_path)
                except OSError:
                    pass
        raise RuntimeError(
            f"failed to open PicoUart HID interface at {original_path}; check hidraw permissions"
        ) from error

    return device


def _device_path_matches(device_info: dict[str, Any], requested_path: str) -> bool:
    """Return whether a hidapi path matches a command-line path string."""
    path = device_info.get("path", b"")
    if isinstance(path, bytes):
        path = path.decode("utf-8", errors="surrogateescape")
    return path == requested_path


def open_device(serial_number: str | None = None, device_path: str | None = None) -> Any:
    """Open PicoUart's vendor-defined HID collection."""
    require_hid()
    if serial_number is not None and device_path is not None:
        raise RuntimeError("--serial and --device-path cannot be used together")

    devices = hid.enumerate(VENDOR_ID, PRODUCT_ID)
    exact_matches = [
        device_info
        for device_info in devices
        if device_info.get("usage_page") == USAGE_PAGE and device_info.get("usage") == USAGE
    ]
    if serial_number is not None:
        exact_matches = [
            device_info for device_info in exact_matches if device_info.get("serial_number") == serial_number
        ]
    if device_path is not None:
        exact_matches = [device_info for device_info in exact_matches if _device_path_matches(device_info, device_path)]
    if len(exact_matches) == 1:
        return open_enumerated_device(exact_matches[0])
    if len(exact_matches) > 1:
        raise RuntimeError("multiple PicoUart HID interfaces matched the expected usage")

    # Some hidapi backends omit usage metadata. Trust PicoUart's exact product
    # string, or an empty product string only when the interface number is
    # known to be the vendor HID interface. Keep the interface constraint for
    # every fallback match.
    fallback_matches = [
        device_info
        for device_info in devices
        if not device_info.get("usage_page")
        and not device_info.get("usage")
        and (
            device_info.get("product_string") == PRODUCT_STRING
            or (not device_info.get("product_string") and device_info.get("interface_number") == HID_INTERFACE_NUMBER)
        )
        and device_info.get("interface_number") in (None, -1, HID_INTERFACE_NUMBER)
    ]
    if serial_number is not None:
        fallback_matches = [
            device_info for device_info in fallback_matches if device_info.get("serial_number") == serial_number
        ]
    if device_path is not None:
        fallback_matches = [
            device_info for device_info in fallback_matches if _device_path_matches(device_info, device_path)
        ]
    if len(fallback_matches) == 1:
        return open_enumerated_device(fallback_matches[0])
    if len(fallback_matches) > 1:
        raise RuntimeError("multiple PicoUart HID interfaces lacked usage metadata")

    raise RuntimeError("PicoUart HID interface not found with the expected usage metadata")
