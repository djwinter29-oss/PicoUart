from __future__ import annotations

import importlib
import struct
import sys
from pathlib import Path

import pytest

HARDWARE_SRC = Path(__file__).resolve().parents[2] / "src"
pytestmark = pytest.mark.skipif(sys.platform != "linux", reason="Serial tools require Linux termios2")


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


def test_configure_port_waits_after_open_and_line_coding(monkeypatch):
    serial_port = _load_serial_config()
    events = []
    current = [0, 0, 0, 0, serial_port.termios.B9600, serial_port.termios.B9600, [0] * 32]
    monkeypatch.setattr(serial_port.os, "open", lambda *_args: events.append("open") or 17)
    monkeypatch.setattr(serial_port.os, "close", lambda *_args: None)
    monkeypatch.setattr(serial_port.termios, "tcgetattr", lambda _fd: current.copy())
    monkeypatch.setattr(
        serial_port.termios,
        "tcsetattr",
        lambda _fd, _when, settings: (events.append("line coding"), current.__setitem__(slice(None), settings)),
    )
    monkeypatch.setattr(serial_port, "verify_line_speed", lambda *_args: events.append("verify"))
    monkeypatch.setattr(serial_port, "time", type("Clock", (), {"sleep": lambda seconds: events.append(seconds)}))
    monkeypatch.setattr(serial_port.termios, "tcflush", lambda *_args: events.append("flush"))

    serial_port.configure_port("/dev/fake", 115200)

    assert events == [
        "open",
        serial_port.PORT_OPEN_SETTLE_SECONDS,
        "line coding",
        "verify",
        serial_port.LINE_CODING_SETTLE_SECONDS,
        "flush",
    ]


def test_deassert_dtr_clears_modem_bit(monkeypatch):
    serial_port = _load_serial_config()
    calls = []
    monkeypatch.setattr(serial_port.fcntl, "ioctl", lambda *arguments: calls.append(arguments))

    serial_port.deassert_dtr(17)

    assert calls == [(17, serial_port.termios.TIOCMBIC, struct.pack("I", serial_port.termios.TIOCM_DTR))]


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
    current = {"input": 9600, "output": 9600, "cflag": 0, "iflag": 0, "oflag": 0, "lflag": 0, "cc": bytearray(32)}
    original_termios2 = bytearray(44)
    original_termios2[8:12] = (serial_port.termios.CS8 | serial_port.termios.CREAD | serial_port.termios.CLOCAL).to_bytes(4, "little")
    original_termios2[36:40] = (9600).to_bytes(4, "little")
    original_termios2[40:44] = (9600).to_bytes(4, "little")
    applied_termios2 = bytearray(original_termios2)
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
            raw[:] = applied_termios2
        elif request == serial_port.TCSETS2:
            applied_termios2[:] = raw
            current["iflag"] = int.from_bytes(raw[0:4], "little")
            current["oflag"] = int.from_bytes(raw[4:8], "little")
            current["cflag"] = int.from_bytes(raw[8:12], "little")
            current["lflag"] = int.from_bytes(raw[12:16], "little")
            current["cc"] = bytearray(raw[17:49])
            current["input"] = int.from_bytes(raw[36:40], "little")
            current["output"] = int.from_bytes(raw[40:44], "little")
            if current["input"] == current["output"] == 123456:
                raw[36:40] = (123456).to_bytes(4, "little")
                raw[40:44] = (123456).to_bytes(4, "little")
                applied_termios2[:] = raw
        else:
            raise AssertionError(f"unexpected ioctl request: {request:#x}")
        return 0

    monkeypatch.setattr(serial_port.fcntl, "ioctl", ioctl)

    descriptor, saved = serial_port.configure_port("/dev/fake", 123456, allow_arbitrary=True)

    assert descriptor == 17
    assert saved[1] == original_termios2
    assert current["input"] == current["output"] == 123456
    assert current["cflag"] & serial_port.BOTHER
    assert current["iflag"] == current["oflag"] == current["lflag"] == 0
    assert current["cflag"] & serial_port.termios.CS8
    assert current["cc"][serial_port.termios.VMIN] == current["cc"][serial_port.termios.VTIME] == 0


def test_verify_line_speed_rejects_readback_at_fifty_percent_deviation(monkeypatch):
    serial_port = _load_serial_config()
    settings = [0, 0, 0, 0, serial_port.termios.B460800, serial_port.termios.B460800, [0] * 32]
    monkeypatch.setattr(serial_port.termios, "tcgetattr", lambda _fd: settings)

    with pytest.raises(OSError, match="deviation must be less than 50%"):
        serial_port.verify_line_speed(17, 921600)


def test_verify_line_speed_accepts_readback_within_fifty_percent(monkeypatch):
    serial_port = _load_serial_config()
    settings = [0, 0, 0, 0, serial_port.termios.B921600, serial_port.termios.B921600, [0] * 32]
    monkeypatch.setattr(serial_port.termios, "tcgetattr", lambda _fd: settings)

    serial_port.verify_line_speed(17, 1000000)
