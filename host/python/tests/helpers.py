# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""Shared selected-layout payload builders and fake HID device."""

import struct

from pico_uart import protocol as hid


def device_info_bytes(*, product_id=1, serial=bytes.fromhex("0123456789ABCDEF"),
                      firmware=(1, 2, 3), mcu=hid.MCU_RP2040, board=1, hardware_mask=3,
                      capabilities=7, profile_mask=0x1F, ram_total=270336, ram_reserved=65536):
    return struct.pack("<BH8s8BII", 1, product_id, serial, *firmware, mcu, board, hardware_mask,
                       capabilities, profile_mask, ram_total, ram_reserved)


def configuration_bytes(*, stored=255, count=6, reset_cause=2, capacities=(4096, 4096, 2048, 2048),
                        pins=None):
    pins = pins or [(0, 1, 3, 2), (4, 5, 7, 6), (8, 9, 255, 255),
                    (12, 13, 255, 255), (16, 17, 255, 255), (20, 21, 255, 255)]
    return bytes((1, stored, count, *(pin for channel in pins for pin in channel), reset_cause)) + struct.pack(
        "<4H", *capacities)


def control_bytes(*, action=0, status=0, error_reason=0):
    return bytes((1, action, status, error_reason))


def telemetry_bytes(*, led=1, temperature_c=25.30, uptime_ms=1234,
                    cores=(2500, 0xFFFF), clock_hz=250000000):
    return struct.pack("<BBhQHHI", 1, led, int(round(temperature_c * 100)), uptime_ms, *cores, clock_hz)


def channel_bytes(*, sequence=1, interval_ms=500, state=2, measurement=1,
                  traffic=(10, 20, 30, 40), baud=115200, framing=(8, 1, 0)):
    return struct.pack("<BBBIBBBHH4I", 1, state, measurement, baud, *framing, sequence, interval_ms, *traffic)


def diagnostic_bytes(*, measurement=1, age=0, reason=0, dropped=0, errors=0, timestamp=0,
                     peaks=(0, 0, 0, 0)):
    return struct.pack("<4B3I4H", 1, measurement, age, reason, dropped, errors, timestamp, *peaks)


class FakeHidDevice:
    """Minimal stand-in for HID Feature and Input report operations."""

    def __init__(self, reports, inputs=None):
        self.reports = {
            hid.REPORT_ID_DEVICE_INFO: device_info_bytes(),
            hid.REPORT_ID_CONFIGURATION: configuration_bytes(),
            **reports,
        }
        self.inputs = list(inputs or [])
        self.writes = []
        self.closed = False

    def get_feature_report(self, report_id, size):
        payload = self.reports[report_id]
        assert size >= len(payload) + 1
        return [report_id, *payload]

    def send_feature_report(self, report):
        self.writes.append(report)
        return len(report)

    def read(self, size, timeout_ms):
        if not self.inputs:
            return []
        report_id, payload = self.inputs.pop(0)
        assert size == len(payload) + 1
        return [report_id, *payload]

    def close(self):
        self.closed = True
