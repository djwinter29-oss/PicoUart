"""Persistent sysclk saves validate the target, acknowledgement, and device readback."""

import struct
from unittest.mock import patch

import pytest

from pico_uart.client import PicoUartHid
from pico_uart.protocol import parse_configuration, parse_configuration_save
from pico_uart.web.app import create_app


class ConfigDevice:
    def __init__(self, mcu=1, status=2, written=9, result_profile=None, initial_status=0):
        self.mcu = mcu
        self.status = status
        self.written = written
        self.result_profile = result_profile
        self.initial_status = initial_status
        self.writes = []

    def get_feature_report(self, report_id, length):
        if report_id == 6:
            return [6, *struct.pack("<BBI", 1, self.mcu, 125000000)]
        assert report_id == 17 and length == 9
        profile = self.result_profile if self.result_profile is not None else self.writes[-1][5] if self.writes else 0
        return [17, *struct.pack("<BBHI", 1, self.status if self.writes else self.initial_status, 0, profile)]

    def send_feature_report(self, report):
        self.writes.append(report)
        return self.written


@pytest.mark.parametrize("mcu,profile,ack", [(1, 0, False), (1, 1, True), (2, 0, False), (2, 2, True)])
def test_save_supported_profile_has_explicit_payload(mcu, profile, ack):
    device = ConfigDevice(mcu=mcu)
    result = PicoUartHid(device=device).save_configuration(profile, ack)
    assert result == {"status": 2, "profile": profile}
    assert device.writes == [[17, 1, int(ack), 0, 0, profile, 0, 0, 0]]
    assert parse_configuration(struct.pack("<BBBBII", 1, 1, 0, 0, 0, 0))["stored_profile"] == 0


@pytest.mark.parametrize("mcu,profile,ack", [
    (1, 1, False), (2, 2, False), (1, 2, True), (2, 1, True),
    (99, 0, False), (1, True, True), (1, 3, True), (1, 0, 1),
])
def test_invalid_request_never_writes(mcu, profile, ack):
    device = ConfigDevice(mcu=mcu)
    with pytest.raises(ValueError):
        PicoUartHid(device=device).save_configuration(profile, ack)
    assert device.writes == []


@pytest.mark.parametrize("status,message", [
    (3, "rejected"), (4, "close all CDC ports"), (5, "verification failed"),
])
def test_save_errors_are_explicit(status, message):
    with pytest.raises(RuntimeError, match=message):
        PicoUartHid(device=ConfigDevice(status=status)).save_configuration(0)


def test_partial_write_mismatch_and_timeout_are_not_success():
    with pytest.raises(RuntimeError, match="incomplete"):
        PicoUartHid(device=ConfigDevice(written=8)).save_configuration(0)
    with pytest.raises(RuntimeError, match="does not match"):
        PicoUartHid(device=ConfigDevice(result_profile=1)).save_configuration(0)
    with patch("pico_uart.client.time.monotonic", side_effect=[0, 1, 4]), patch("pico_uart.client.time.sleep"):
        with pytest.raises(RuntimeError, match="timed out"):
            PicoUartHid(device=ConfigDevice(status=1)).save_configuration(0)


def test_pending_save_is_not_replaced():
    device = ConfigDevice(initial_status=1)
    with pytest.raises(RuntimeError, match="already pending"):
        PicoUartHid(device=device).save_configuration(0)
    assert device.writes == []


@pytest.mark.parametrize("payload", [
    b"", bytes(8), struct.pack("<BBHI", 2, 0, 0, 0), struct.pack("<BBHI", 1, 6, 0, 0),
    struct.pack("<BBHI", 1, 0, 1, 0), struct.pack("<BBHI", 1, 0, 0, 3),
    struct.pack("<BBHI", 1, 2, 0, 0xFFFFFFFF),
])
def test_save_report_rejects_malformed_payload(payload):
    with pytest.raises(RuntimeError):
        parse_configuration_save(payload)


def test_configuration_endpoint_requires_csrf_and_validated_json():
    class Dashboard:
        calls = []

        def save_configuration(self, board, profile, unsafe):
            self.calls.append((board, profile, unsafe))

    dashboard = Dashboard()
    client = create_app(dashboard).test_client()
    assert client.post("/api/configuration", json={"profile": 0, "unsafe_overclock": False}).status_code == 403
    client.get("/")
    with client.session_transaction() as session:
        token = session["csrf_token"]
    headers = {"X-CSRF-Token": token}
    for body in ({}, [], {"profile": True, "unsafe_overclock": False}, {"profile": 0, "unsafe_overclock": 1},
                 {"profile": 3, "unsafe_overclock": True},
                 {"profile": 0, "unsafe_overclock": False, "extra": 1}):
        assert client.post("/api/configuration", json=body, headers=headers).status_code == 400
    assert dashboard.calls == []
    response = client.post("/api/configuration?board=chosen", json={"profile": 0, "unsafe_overclock": False},
                           headers=headers)
    assert response.status_code == 200 and response.json == {"ok": True}
    assert dashboard.calls == [("chosen", 0, False)]


@pytest.mark.parametrize("error,status,message", [
    (ValueError("bad profile"), 400, "invalid request"),
    (RuntimeError("busy"), 409, "request could not be completed"),
    (OSError("disconnected"), 503, "service temporarily unavailable"),
])
def test_configuration_endpoint_sanitizes_errors(error, status, message):
    class Dashboard:
        def save_configuration(self, *args):
            raise error

    client = create_app(Dashboard()).test_client()
    client.get("/")
    with client.session_transaction() as session:
        token = session["csrf_token"]
    response = client.post("/api/configuration", json={"profile": 0, "unsafe_overclock": False},
                           headers={"X-CSRF-Token": token})
    assert response.status_code == status
    assert response.json == {"error": message}
