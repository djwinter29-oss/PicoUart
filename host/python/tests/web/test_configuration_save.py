# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""Report-20 configuration actions validate capabilities and observe results."""

from unittest.mock import patch

import pytest

from pico_uart.client import PicoUartHid
from helpers import control_bytes, device_info_bytes
from pico_uart.web.app import create_app
from pico_uart.protocol import REPORT_ID_CONTROL, REPORT_ID_DEVICE_INFO


class ConfigDevice:
    def __init__(self, *, mcu=1, status=2, written=None, result_profile=None, initial_status=0, capabilities=7):
        self.mcu = mcu
        self.status = status
        self.written = written
        self.result_profile = result_profile
        self.initial_status = initial_status
        self.capabilities = capabilities
        self.writes = []

    def get_feature_report(self, report_id, length):
        if report_id == REPORT_ID_DEVICE_INFO:
            return [report_id, *device_info_bytes(
                mcu=self.mcu,
                capabilities=self.capabilities,
                profile_mask=0x1F if self.mcu == 2 else 0x0F,
            )]
        assert report_id == REPORT_ID_CONTROL and length == 5
        if not self.writes:
            return [report_id, *control_bytes(
                action=4 if self.initial_status else 0,
                status=self.initial_status,
            )]
        command = self.writes[-1][2]
        error_reason = {3: 3, 4: 5, 5: 6, 6: 9}.get(self.status, 0)
        return [report_id, *control_bytes(
            action=self.result_profile if self.result_profile is not None else command,
            status=self.status,
            error_reason=error_reason,
        )]

    def send_feature_report(self, report):
        self.writes.append(report)
        return len(report) if self.written is None else self.written

    def close(self):
        pass


@pytest.mark.parametrize("mcu,profile,ack", [(1, 0, False), (1, 3, True), (2, 0, False), (2, 4, True),
                                          (1, 1, True), (1, 2, True), (2, 1, True), (2, 2, True), (2, 3, True)])
def test_save_supported_profile_has_explicit_payload(mcu, profile, ack):
    device = ConfigDevice(mcu=mcu)
    result = PicoUartHid(device=device).save_configuration(profile, ack)
    assert result["status"] == 2 and result["action"] == 4
    report = device.writes[0]
    assert report == [20, 1, 4, 3 if profile else 1, profile]


@pytest.mark.parametrize("mcu,profile,ack", [
    (1, 1, False), (2, 2, False), (1, 4, True),
    (1, True, True), (1, 5, True), (1, 0, 1), (1, 3, False), (2, 4, False),
])
def test_invalid_request_never_writes(mcu, profile, ack):
    device = ConfigDevice(mcu=mcu)
    with pytest.raises(ValueError):
        PicoUartHid(device=device).save_configuration(profile, ack)
    assert device.writes == []


@pytest.mark.parametrize("status,message", [
    (3, "rejected"), (5, "close all CDC ports"), (6, "operation failed"),
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
    with pytest.raises(RuntimeError, match="operation is pending"):
        PicoUartHid(device=device).save_configuration(0)
    assert device.writes == []


@pytest.mark.parametrize("payload", [
    b"", bytes(24), bytes((2,)) + bytes(23), bytes((1, 0, 0, 7)) + bytes(20),
    control_bytes(status=3, error_reason=5),
])
def test_save_report_rejects_malformed_payload(payload):
    with pytest.raises(RuntimeError):
        from pico_uart.protocol import parse_control

        parse_control(payload)


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
    response = client.post("/api/configuration", data="{", content_type="application/json", headers=headers)
    assert response.status_code == 400
    assert response.json == {"error": "malformed JSON body"}
    response = client.post("/api/configuration", data="{}", content_type="text/plain", headers=headers)
    assert response.status_code == 415
    assert response.json == {"error": "configuration request must be JSON"}
    for body in ({}, [], {"profile": True, "unsafe_overclock": False}, {"profile": 0, "unsafe_overclock": 1},
                 {"profile": 5, "unsafe_overclock": True},
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
