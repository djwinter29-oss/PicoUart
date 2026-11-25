# Dashboard Behavior and Presentation

This guide covers the shared Python/.NET frontend in `host/web/src` and the separate Direct Dashboard in
`host/direct/src`. For endpoint behavior, see the Python [route tests](../../host/python/tests/dashboard/test_app.py),
[configuration tests](../../host/python/tests/dashboard/test_configuration_save.py), and
[published .NET contract tests](../../tests/tooling/test_dotnet_host.py). For Python API, CLI, or server implementation,
see [Host Tools Development](host-tools.md).

## Hosted Dashboards

### Frontend

Python and .NET serve the same frontend, written as native ES modules with no bundler. Keep it backend-neutral and do
not create a second hosted frontend. If a module's cache-busting version changes, update every importer and the HTML
entry. The Python server sets correct MIME types for JS and CSS.

### Version Display

The top bar shows the backend name and **App v**, the host version. Python reports `pico_uart.__version__` (from
`hatch-vcs`), and .NET reports its assembly informational version (from MinVer). `PICO_UART_HOST_VERSION` overrides
the displayed value only.

### Hosted Service Behavior

The Python and .NET apps share one HID reader per selected board and serve cached snapshots to each browser page.
Boards are found at startup and on **Rescan**; unplugged boards do not trigger a switch to another device. Selection is
page-local, so separate tabs can watch different boards.

Both servers listen on loopback. Write requests require a session CSRF header.

| Endpoint | Purpose |
| --- | --- |
| `GET /api/boards` | Board list and default selection; .NET also returns per-device discovery errors. |
| `GET /api/status?board=ID` | Cached board snapshot, or an empty six-channel snapshot without a board ID. |
| `GET /api/diagnostics?board=ID` | Refreshes channel diagnostics (reports 30-35). |
| `POST /api/actions/{toggle-led,reset}?board=ID` | Board actions. |
| `POST /api/configuration?board=ID` | Save a clock profile and reboot. |

Snapshots keep identity, runtime, configuration, and LED state separate, each with its own error. Channel data includes
applied settings, totals, per-input byte increments, traffic gaps, and incomplete intervals. Overflow counts are
unavailable when diagnostics cannot be read.

The LED action sends the opposite of the observed state; an unknown state disables it. A timed-out control may still
have run, so the hosts surface an unconfirmed outcome rather than retrying it.

### Boot Configuration

A save request takes exactly `{"profile": 0-4, "unsafe_overclock": bool}` in a body of at most 1 KiB. It succeeds only
after firmware reports Completed. After a timeout or disconnect, the backends keep a `configuration_save_unconfirmed`
warning across reconnects and page reloads. Reading report 20 cannot identify which request produced its result. The
[HID save rules](../design/usb/hid-report-reference.md#save-and-reboot-safety) define the board-side checks.

### Channel Presentation

The wire contract lives in the [HID reference](../design/usb/hid-report-reference.md). These labels and display rules
belong to host applications:

- Show an unknown product as `Unknown product (0xNNNN)` and disable its product-specific controls.
- Show the UID as **Serial / UID**.
- Show RAM as **Firmware RAM reserved / total**.
- Show the stored clock profile and the running clock separately. Show peak percentages only when report 2 supplies
  valid capacities.
- Show temperature as an estimate in Celsius, normally to one decimal place. Label service load by core and mark the
  `65535` sentinel as warming up or unavailable. Uptime is meaningful only for the same device within one boot.
- Treat a channel with no Input report for three seconds as stale and show host-side Unknown. A read error invalidates
  only the values that read would refresh; keep static reports 1 and 2. Disable LED controls when the telemetry read
  fails. Show an Error state as needing attention and read diagnostics for its reason.
- Label `rx_overrun_count` as **Known RX bytes dropped**, `rx_error_count` as **RX controller error observations**,
  and `last_control_error_reason` as **Last configuration error**.
- Label ring and FIFO peaks **Observed peak since boot**. When the port is stopped, show its baud rate as **Last
  applied**. For update rates and stale reports, follow the
  [HID polling rules](../design/usb/hid-report-reference.md#polling-and-validation).

Lifecycle badges are Closed, Starting, Ready, Configuring, Error, and host-side Unknown. Fault history and the last
configuration error remain visible beside the badge.

## Direct Dashboard

The Direct Dashboard is a standalone WebHID app with its own assets and renderer; it has no server-side code. If a save
times out or disconnects, it remembers the unconfirmed result by board UID. A fresh report 2 read and explicit
acknowledgement are required before another save. WebHID calls cannot be canceled: keep reconnect disabled until an
open or close settles, close a late successful open, and never start an overlapping write.

```sh
tools/host/run-direct.sh                           # serves http://127.0.0.1:8000/ (--port N)
python tools/host/direct_site.py serve --port 8000 # without a POSIX shell
```

The launcher serves a staged copy with a version derived from Git tags. For manual WebHID testing, use desktop Chrome
or Edge and serve the page from HTTPS or localhost; do not open the HTML straight from disk.

CI runs a browser smoke test against a simulated device:

```sh
python -m pip install playwright==1.63.0
python -m playwright install --with-deps chromium
python host/direct/tests/browser_smoke.py
```

The simulated test does not verify real browser and OS USB access. Deployment to Pages is described in
[Host App Distribution and Publishing](../deployment/host-app-publishing.md#direct-dashboard-pages).

## Frontend Tests

Requires Node 22+. Run the frontend tests with:

```sh
node --test host/web/tests/*.test.mjs host/direct/tests/*.test.mjs
```
