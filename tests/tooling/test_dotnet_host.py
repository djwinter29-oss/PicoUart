"""Published .NET host contract checks; skipped when no SDK is available."""

import http.client
import html
import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import threading
import time

import pytest

from pico_uart.web.monitor import empty_snapshot


ROOT = Path(__file__).resolve().parents[2]
DOTNET = os.environ.get("DOTNET_EXE") or shutil.which("dotnet")
pytestmark = pytest.mark.skipif(not DOTNET, reason=".NET SDK not available; set DOTNET_EXE to enable")


@pytest.fixture(scope="module")
def published_host(tmp_path_factory, packaged_version):
    output = tmp_path_factory.mktemp("dotnet-host")
    result = subprocess.run(
        [DOTNET, "publish", str(ROOT / "host/dotnet/PicoUart"), "-o", str(output)]
        + (["-p:Version=" + packaged_version] if packaged_version != "0.0.0-debug" else []),
        capture_output=True,
        text=True,
        timeout=120,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    return output


@pytest.fixture(scope="module")
def server(published_host, host_version):
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    with (published_host / "server.log").open("w+") as log:
        process = subprocess.Popen(
            [DOTNET, str(published_host / "PicoUart.dll"), "web", "--port", str(port)],
            cwd=published_host,
            env={**os.environ, "PICO_UART_HOST_VERSION": host_version},
            stdout=log,
            stderr=subprocess.STDOUT,
        )
        try:
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline and process.poll() is None:
                try:
                    status, _, _ = request(port, "/api/status")
                    if status == 200:
                        break
                except OSError:
                    pass
                threading.Event().wait(0.05)
            else:
                log.seek(0)
                pytest.fail(f".NET dashboard did not start: {log.read()}")
            yield port
        finally:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)


def request(port, path, method="GET", headers=None):
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=2)
    try:
        connection.request(method, path, headers=headers or {})
        response = connection.getresponse()
        return response.status, dict(response.getheaders()), response.read()
    finally:
        connection.close()


def session(port):
    status, headers, page = request(port, "/")
    assert status == 200
    token = re.search(rb'name="csrf-token" content="([^"]+)"', page).group(1).decode()
    return token, headers["Set-Cookie"].split(";", 1)[0], headers, page


@pytest.fixture(scope="module", params=["", "1.2.3", "<script>"])
def host_version(request):
    return request.param


@pytest.fixture(scope="module", params=["0.0.0-debug", "1.2.3"])
def packaged_version(request):
    return request.param


def test_published_dotnet_serves_the_canonical_frontend(server, host_version, packaged_version):
    token, _, headers, page = session(server)
    template = (ROOT / "host/web/index.html").read_bytes()
    expected_version = html.escape(host_version or packaged_version).encode()
    assert page == template.replace(b"{{ csrf_token }}", token.encode()).replace(
        b"{{ dashboard_type }}", b".NET").replace(b"{{ host_version }}", expected_version)
    assert b"<title>PicoUart Diagnostics | .NET</title>" in page
    assert b'rel="icon" type="image/svg+xml" href="/favicon.svg"' in page
    assert b'id="mcu"' in page
    assert b'id="system-clock"' in page
    assert "httponly" in headers["Set-Cookie"].lower()
    assert "samesite=strict" in headers["Set-Cookie"].lower()
    assert headers["X-Content-Type-Options"] == "nosniff"
    assert headers["X-Frame-Options"] == "DENY"
    assert "frame-ancestors 'none'" in headers["Content-Security-Policy"]
    for path in ("css/dashboard.css", "js/dashboard.js", "favicon.svg"):
        status, asset_headers, payload = request(server, f"/{path}")
        assert status == 200
        assert payload == (ROOT / "host/web" / path).read_bytes()
        if path == "favicon.svg":
            assert asset_headers["Content-Type"].split(";", 1)[0] == "image/svg+xml"
    for path in ("index.html", "css/index.html", "js/index.html", "css/../index.html", "js/../index.html"):
        assert request(server, f"/{path}")[0] == 404


def test_dotnet_refresh_interval_matches_python_default(server):
    status, _, payload = request(server, "/api/status")
    assert status == 200
    assert json.loads(payload)["refresh_interval_ms"] == empty_snapshot()["refresh_interval_ms"] == 500


def test_dotnet_status_matches_python_dashboard_shape(server):
    status, headers, payload = request(server, "/api/status")
    assert status == 200
    assert headers["Cache-Control"] == "no-store"
    snapshot = json.loads(payload)
    expected = empty_snapshot()
    assert snapshot.keys() == expected.keys()
    assert len(snapshot["channels"]) == len(snapshot["overflow_counts"]) == 6
    assert snapshot["overflow_counts"] == expected["overflow_counts"]
    assert snapshot["channels"] == expected["channels"]


def test_dotnet_startup_prints_dashboard_url_without_framework_info_logs(server, published_host):
    log = (published_host / "server.log").read_text()
    assert f"PicoUart dashboard: http://127.0.0.1:{server}/ (Ctrl+C to stop)" in log
    assert "info: Microsoft." not in log


def test_dotnet_board_catalog_and_invalid_selection(server):
    status, _, payload = request(server, "/api/boards")
    assert status == 200
    listing = json.loads(payload)
    assert listing.keys() == {"boards", "default_board_id"}
    for board in listing["boards"]:
        assert board.keys() == {"id", "product_name", "serial_number", "device_path"}
    assert listing["default_board_id"] is None or listing["default_board_id"] in {
        board["id"] for board in listing["boards"]
    }
    token, cookie, _, _ = session(server)
    headers = {"Cookie": cookie, "X-CSRF-Token": token}
    assert request(server, "/api/status?board=unknown")[0] == 409
    assert request(server, "/api/actions/toggle-led?board=unknown", "POST", headers)[0] == 409
    assert request(server, "/api/actions/reset", "POST", headers)[0] == 409
    assert request(server, "/api/status")[0] == 200


def test_dotnet_controls_reject_bad_hosts_and_csrf(server):
    token, cookie, _, _ = session(server)
    path = "/api/actions/toggle-led"
    assert request(server, path, "POST")[0] == 403
    assert request(server, path, "POST", {"X-CSRF-Token": token})[0] == 403
    assert request(server, path, "POST", {"Cookie": cookie, "X-CSRF-Token": "wrong"})[0] == 403
    other_token, other_cookie, _, _ = session(server)
    assert other_token != token
    assert request(server, path, "POST", {"Cookie": other_cookie, "X-CSRF-Token": token})[0] == 403
    headers = {"Cookie": cookie, "X-CSRF-Token": token}
    assert request(server, "/api/actions/unknown", "POST", headers)[0] == 404
    headers["Host"] = "attacker.example"
    assert request(server, path, "POST", headers)[0] == 400
    assert request(server, "/", headers={"Host": "attacker.example"})[0] == 400


@pytest.mark.parametrize("arguments", [
    ["web", "--port", "0"],
    ["monitor", "--duration", "NaN"],
    ["--serial", "a", "--device-path", "b", "status"],
    ["unknown"],
])
def test_dotnet_cli_rejects_invalid_arguments(published_host, arguments):
    result = subprocess.run(
        [DOTNET, str(published_host / "PicoUart.dll"), *arguments],
        capture_output=True,
        text=True,
        timeout=10,
    )
    assert result.returncode == 1
    assert result.stderr.strip()