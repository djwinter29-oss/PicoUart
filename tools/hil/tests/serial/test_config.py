from __future__ import annotations

import importlib
import sys
from pathlib import Path

import pytest

HARDWARE_SRC = Path(__file__).resolve().parents[2] / "src"
pytestmark = pytest.mark.skipif(sys.platform == "win32", reason="Linux serial tools import termios/fcntl")


def _load_serial_config():
    if str(HARDWARE_SRC) not in sys.path:
        sys.path.insert(0, str(HARDWARE_SRC))
    module = importlib.import_module("hil_test_suite.serial.config")
    return importlib.reload(module)


def test_configure_port_sets_and_verifies_standard_baud(monkeypatch):
    serial_port = _load_serial_config()
    closes = []
    current = [0, 0, 0, 0, serial_port.termios.B9600, serial_port.termios.B9600, [0] * 32]
    monkeypatch.setattr(serial_port.os, "open", lambda *_args: 17)
    monkeypatch.setattr(serial_port.os, "close", closes.append)
    monkeypatch.setattr(serial_port.termios, "tcgetattr", lambda _fd: current.copy())
    monkeypatch.setattr(
        serial_port.termios, "tcsetattr", lambda _fd, _when, settings: current.__setitem__(slice(None), settings)
    )
    monkeypatch.setattr(serial_port.termios, "tcflush", lambda *_args: None)

    file_descriptor, original = serial_port.configure_port("/dev/fake", 115200)

    assert file_descriptor == 17
    assert original[4:6] == [serial_port.termios.B9600, serial_port.termios.B9600]
    assert current[4:6] == [serial_port.termios.B115200, serial_port.termios.B115200]
    assert closes == []


def test_configure_port_closes_if_readback_does_not_match(monkeypatch):
    serial_port = _load_serial_config()
    closes = []
    current = [0, 0, 0, 0, serial_port.termios.B9600, serial_port.termios.B9600, [0] * 32]
    monkeypatch.setattr(serial_port.os, "open", lambda *_args: 17)
    monkeypatch.setattr(serial_port.os, "close", closes.append)
    monkeypatch.setattr(serial_port.termios, "tcgetattr", lambda _fd: current.copy())
    monkeypatch.setattr(serial_port.termios, "tcsetattr", lambda *_args: None)
    monkeypatch.setattr(serial_port.termios, "tcflush", lambda *_args: None)

    with pytest.raises(OSError, match="requested 115200"):
        serial_port.configure_port("/dev/fake", 115200)

    assert closes == [17]


def test_configure_port_restores_settings_if_setup_fails(monkeypatch):
    serial_port = _load_serial_config()
    original = [0, 0, 0, 0, serial_port.termios.B9600, serial_port.termios.B9600, [0] * 32]
    current = [*original[:6], original[6][:]]
    set_calls = []
    closes = []

    def set_attributes(_fd, _when, settings):
        set_calls.append(settings)
        current[:] = [*settings[:6], settings[6][:]]

    monkeypatch.setattr(serial_port.os, "open", lambda *_args: 17)
    monkeypatch.setattr(serial_port.os, "close", closes.append)
    monkeypatch.setattr(serial_port.termios, "tcgetattr", lambda _fd: [*current[:6], current[6][:]])
    monkeypatch.setattr(serial_port.termios, "tcsetattr", set_attributes)
    monkeypatch.setattr(serial_port, "verify_line_speed", lambda *_args: None)
    monkeypatch.setattr(serial_port.termios, "tcflush", lambda *_args: (_ for _ in ()).throw(OSError("flush failed")))

    with pytest.raises(OSError, match="flush failed"):
        serial_port.configure_port("/dev/fake", 115200)

    assert current == original
    assert len(set_calls) == 2
    assert closes == [17]


def test_configure_port_sets_and_verifies_arbitrary_baud(monkeypatch):
    serial_port = _load_serial_config()
    current = {"input": 9600, "output": 9600, "cflag": 0}
    monkeypatch.setattr(serial_port.os, "open", lambda *_args: 17)
    monkeypatch.setattr(serial_port.os, "close", lambda *_args: None)
    monkeypatch.setattr(
        serial_port.termios,
        "tcgetattr",
        lambda _fd: [0, 0, 0, 0, serial_port.termios.B9600, serial_port.termios.B9600, [0] * 32],
    )
    monkeypatch.setattr(serial_port.termios, "tcsetattr", lambda *_args: None)
    monkeypatch.setattr(serial_port.termios, "tcflush", lambda *_args: None)

    def ioctl(_fd, request, raw, *_args):
        if request == serial_port.TCGETS2:
            raw[8:12] = current["cflag"].to_bytes(4, "little")
            raw[36:40] = current["input"].to_bytes(4, "little")
            raw[40:44] = current["output"].to_bytes(4, "little")
        elif request == serial_port.TCSETS2:
            current["cflag"] = int.from_bytes(raw[8:12], "little")
            current["input"] = int.from_bytes(raw[36:40], "little")
            current["output"] = int.from_bytes(raw[40:44], "little")
        else:
            raise AssertionError(f"unexpected ioctl request: {request:#x}")
        return 0

    monkeypatch.setattr(serial_port.fcntl, "ioctl", ioctl)

    serial_port.configure_port("/dev/fake", 123456, allow_arbitrary=True)

    assert current["input"] == current["output"] == 123456
    assert current["cflag"] & serial_port.BOTHER
