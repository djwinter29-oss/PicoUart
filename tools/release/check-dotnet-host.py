#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""Smoke-test an extracted .NET host without discovering or controlling physical boards."""

from __future__ import annotations

import argparse
import http.client
import json
import os
import re
import signal
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def request(port: int, path: str, method: str = "GET", headers: dict[str, str] | None = None):
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=2)
    try:
        connection.request(method, path, headers=headers or {})
        response = connection.getresponse()
        return response.status, dict(response.getheaders()), response.read()
    finally:
        connection.close()


def check_host(directory: Path) -> None:
    directory = directory.resolve()
    executable = directory / ("PicoUart.exe" if os.name == "nt" else "PicoUart")
    subprocess.run([str(executable), "--help"], cwd=directory, check=True, timeout=10)
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    with tempfile.TemporaryFile(mode="w+", encoding="utf-8") as log:
        process = subprocess.Popen(
            [str(executable), "web", "--port", str(port)],
            cwd=directory,
            stdout=log,
            stderr=subprocess.STDOUT,
            creationflags=subprocess.CREATE_NEW_PROCESS_GROUP if os.name == "nt" else 0,
        )
        succeeded = False
        try:
            deadline = time.monotonic() + 15
            while process.poll() is None and time.monotonic() < deadline:
                try:
                    status, headers, page = request(port, "/")
                    break
                except (OSError, http.client.HTTPException):
                    time.sleep(0.05)
            else:
                raise RuntimeError("extracted .NET dashboard did not start")
            assert status == 200, f"dashboard returned HTTP {status}"
            assert b"<title>PicoUart Diagnostics | .NET</title>" in page
            token = re.search(rb'name="csrf-token" content="([A-F0-9]{64})"', page)
            assert token is not None, "missing dashboard CSRF token"
            cookie = headers["Set-Cookie"].split(";", 1)[0]
            assert cookie.startswith(f"PicoUart.DotNet.Session.{port}=")
            assert "httponly" in headers["Set-Cookie"].lower()
            assert "samesite=strict" in headers["Set-Cookie"].lower()
            assert "no-store" in {directive.strip() for directive in headers["Cache-Control"].split(",")}
            for path in ("css/dashboard.css", "js/dashboard.js", "favicon.svg"):
                status, _, payload = request(port, f"/{path}")
                assert status == 200 and payload == (directory / "web" / path).read_bytes(), path
            status, _, payload = request(port, "/api/status")
            assert status == 200
            snapshot = json.loads(payload)
            assert snapshot["connected"] is False
            assert len(snapshot["channels"]) == 6 and snapshot["overflow_counts"] == [None] * 6
            path = "/api/actions/unknown"
            assert request(port, path, "POST")[0] == 403
            assert request(port, path, "POST", {"Cookie": cookie, "X-CSRF-Token": "invalid"})[0] == 403
            authenticated = {"Cookie": cookie, "X-CSRF-Token": token.group(1).decode("ascii")}
            assert request(port, path, "POST", authenticated)[0] == 404
            stop_signal = signal.CTRL_BREAK_EVENT if os.name == "nt" else signal.SIGINT
            process.send_signal(stop_signal)
            assert process.wait(timeout=10) == 0, "dashboard did not shut down cleanly"
            succeeded = True
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
            if not succeeded:
                log.seek(0)
                sys.stderr.write(log.read())
    print("Extracted .NET host: CLI, dashboard, assets, session/CSRF, and graceful shutdown passed")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, help="Extracted host directory containing PicoUart and web/")
    check_host(parser.parse_args().directory)


if __name__ == "__main__":
    main()
