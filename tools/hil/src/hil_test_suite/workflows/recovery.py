"""Check stall, overflow, configuration and reopen isolation on the fixed fixture's CDC1/CDC5 loopbacks."""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import os
import select
import sys
import threading
import time
from datetime import datetime, timezone
from pathlib import Path

from pico_uart import PicoUartHid

from ..serial.config import configure_port, pico_uart_endpoint, set_port_line_coding
from ..serial.stress import close_ports, payload_for, read_exact, run_stream, write_all
from ..support.health import collect_hid_health, health_is_clean


def build_parser(add_help: bool = True) -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__, add_help=add_help)
    parser.add_argument("--serial", required=True, help="Exact PicoUart USB serial")
    parser.add_argument("--cdc1", required=True, help="HW UART1 loopback endpoint")
    parser.add_argument("--cdc5", required=True, help="PIO UART5 loopback endpoint")
    parser.add_argument("--artifact", required=True, type=Path, help="Previously flashed ELF (hashed, not flashed)")
    parser.add_argument("--output", required=True, type=Path, help="New JSON evidence file; never overwritten")
    parser.add_argument("--mode", choices=("all", "stall", "overflow", "reopen", "churn"), default="all")
    return parser


def require_health(snapshot: dict, *, allow_overflow: bool = False) -> None:
    """Only an explicitly expected CDC1 drop counter may differ from clean health."""
    checked = copy.deepcopy(snapshot)
    if allow_overflow and not checked.get("error") and 1 in checked.get("diagnostics", {}):
        checked["overruns"][1] = 0
        checked["diagnostics"][1]["rx_overrun_count"] = 0
    if not health_is_clean(checked):
        raise RuntimeError(f"unexpected HID health: {snapshot}")


def drain_backlog(descriptor: int, limit: int) -> bytes:
    """Drain device-side backlog to 200 ms quiescence, with a hard size and time limit."""
    received = bytearray()
    deadline = time.monotonic() + 3
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("device backlog did not become quiescent")
        if not select.select([descriptor], [], [], min(0.2, remaining))[0]:
            if time.monotonic() >= deadline:
                raise TimeoutError("device backlog did not become quiescent")
            return bytes(received)
        try:
            chunk = os.read(descriptor, 4096)
        except BlockingIOError:
            continue
        if not chunk:
            raise OSError("serial endpoint disconnected while draining backlog")
        received.extend(chunk)
        if len(received) > limit:
            raise RuntimeError("received more backlog than was transmitted")


def exchange(descriptor: int, sequence: int) -> None:
    payload = payload_for("fresh", sequence, 128)
    write_all(descriptor, payload, time.monotonic() + 3)
    read_exact(descriptor, payload, time.monotonic() + 3)


def sibling_traffic(descriptor: int, duration: float) -> dict:
    results, timing = {}, {}
    run_stream("sibling", descriptor, descriptor, duration, 128, 3.0, threading.Barrier(1), results, timing)
    if results["sibling"][1] is not None or results["sibling"][0] <= 0:
        raise RuntimeError(f"sibling payload verification failed: {results}")
    return {"verified_bytes": results["sibling"][0], "timing": timing["sibling"]}


def run_checks(arguments: argparse.Namespace, evidence: dict, ports: list) -> None:
    # Identify both endpoints before opening either; never configure an unrelated serial device.
    for channel in (1, 5):
        path = getattr(arguments, f"cdc{channel}")
        if pico_uart_endpoint(path) != (arguments.serial, channel):
            raise ValueError(f"{path} is not CDC{channel} on PicoUart {arguments.serial}")
    evidence["before"] = collect_hid_health(arguments.serial)
    require_health(evidence["before"])
    if any(state != "closed" for state in evidence["before"]["channels"].values()):
        raise RuntimeError("all six channels must be closed before recovery checks")
    with PicoUartHid(serial_number=arguments.serial) as board:
        evidence["hardware"] = board.read_hardware_info()
        evidence["configuration"] = board.read_configuration()
        evidence["firmware_version"] = board.read_firmware_version()

    ports.append(configure_port(arguments.cdc1, 115200))
    ports.append(configure_port(arguments.cdc5, 115200))
    primary, sibling = ports[0][0], ports[1][0]
    checks = evidence["checks"] = {}
    if arguments.mode in ("all", "churn"):
        results, timing = {}, {}
        checks["churn"] = {"changes": 0, "results": results, "timing": timing}
        worker = threading.Thread(target=run_stream, args=(
            "sibling", primary, primary, 5.0, 128, 3.0, threading.Barrier(1), results, timing,
        ))
        worker.start()
        try:
            with PicoUartHid(serial_number=arguments.serial) as board:
                for iteration in range(12):
                    rate = (115200, 230400, 460800)[iteration % 3]
                    set_port_line_coding(sibling, rate)
                    board.wait_channels_ready({5: rate})
                    exchange(sibling, iteration)
                    checks["churn"]["changes"] += 1
                set_port_line_coding(sibling, 115200)
                board.wait_channels_ready({5: 115200})
        finally:
            # run_stream uses nonblocking I/O and bounded phase deadlines. Join before closing its descriptor.
            worker.join()
        if results["sibling"][1] is not None or results["sibling"][0] <= 0:
            raise RuntimeError(f"configuration isolation failed: {results}")
    if arguments.mode in ("all", "reopen"):
        checks["reopen"] = {"cycles": 0, "fresh_bytes_per_cycle": 128}
        for iteration in range(5):
            write_all(sibling, payload_for("old", iteration, 256), time.monotonic() + 3)
            old = ports.pop()
            error = close_ports([old])
            if error:
                raise error
            ports.append(configure_port(arguments.cdc5, 115200))
            sibling = ports[1][0]
            exchange(sibling, iteration + 100)
            exchange(primary, iteration + 200)
            checks["reopen"]["cycles"] += 1
    if arguments.mode in ("all", "stall"):
        payload = payload_for("stall", 0, 2048)
        write_all(primary, payload, time.monotonic() + 3)
        checks["stall"] = sibling_traffic(sibling, 0.25)
        read_exact(primary, payload, time.monotonic() + 3)
        checks["stall"]["health"] = collect_hid_health(arguments.serial)
        require_health(checks["stall"]["health"])
    # Catch faults from reopen/churn before permitting the intentional overflow.
    evidence["pre_overflow"] = collect_hid_health(arguments.serial)
    require_health(evidence["pre_overflow"])
    if arguments.mode in ("all", "overflow"):
        payload = payload_for("overflow", 0, 16384)
        write_all(primary, payload, time.monotonic() + 3)
        checks["overflow"] = sibling_traffic(sibling, 2.0)
        retained = drain_backlog(primary, len(payload))
        snapshot = collect_hid_health(arguments.serial)
        checks["overflow"].update({"retained_bytes": len(retained), "health": snapshot})
        require_health(snapshot, allow_overflow=True)
        drops = snapshot["overruns"][1]
        if drops <= 0 or drops + len(retained) != len(payload):
            raise RuntimeError(f"overflow accounting mismatch: {drops} dropped, {len(retained)} retained")
        # Discarded bytes may occur at multiple points; retained bytes must remain an ordered subsequence.
        position = 0
        for value in retained:
            position = payload.find(bytes([value]), position)
            if position < 0:
                raise RuntimeError("retained overflow payload is not an ordered subsequence")
            position += 1
        exchange(primary, 300)
        exchange(sibling, 301)
    evidence["after"] = collect_hid_health(arguments.serial)
    require_health(evidence["after"], allow_overflow=arguments.mode in ("all", "overflow"))
    if "overflow" in checks and evidence["after"]["overruns"][1] != checks["overflow"]["health"]["overruns"][1]:
        raise RuntimeError("additional RX drops after overflow recovery")


def main(arguments: argparse.Namespace | None = None) -> int:
    arguments = arguments or build_parser().parse_args()
    evidence = {
        "result": "FAIL", "started_utc": datetime.now(timezone.utc).isoformat(), "serial": arguments.serial,
        "mode": arguments.mode, "artifact": str(arguments.artifact),
        "artifact_sha256": hashlib.sha256(arguments.artifact.read_bytes()).hexdigest(),
    }
    ports = []
    # Reserve the evidence path before touching hardware and retain partial evidence on failure.
    with arguments.output.open("x") as output:
        try:
            run_checks(arguments, evidence, ports)
            evidence["result"] = "PASS"
        except (OSError, RuntimeError, ValueError) as error:
            evidence["error"] = str(error)
        finally:
            cleanup_error = close_ports(ports)
            if cleanup_error:
                evidence["result"] = "FAIL"
                evidence["cleanup_error"] = str(cleanup_error)
            json.dump(evidence, output, indent=2)
            output.write("\n")
    print(json.dumps(evidence, indent=2), file=sys.stdout if evidence["result"] == "PASS" else sys.stderr)
    return 0 if evidence["result"] == "PASS" else 1
