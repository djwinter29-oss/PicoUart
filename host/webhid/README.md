# PicoUart WebHID Prototype

A browser dashboard with separate device identity and live runtime sections, plus six-channel health and
traffic. It talks directly to PicoUart through WebHID; there is no Python/.NET application backend. Its HTML, CSS,
favicon, and renderer live entirely in this folder, independently of the Python/.NET dashboard.
Device identity provides an **Identify board** action and LED output state; see
[dashboard controls](../../docs/development/host-tools.md#pythonnet-web-ui). Reset and WebSerial are not implemented.

## Run

Use desktop Chrome or Edge and an HTTPS site or localhost. Browser support and USB permissions vary by platform;
Firefox, Safari, and mobile browsers generally do not support WebHID. The page reports unavailable WebHID and keeps
Connect disabled when the API or secure context is missing.

For local development, serve `host/webhid` from the repository root:

```sh
tools/host/run-webhid.sh
```

Open `http://127.0.0.1:8000/`. The static server only delivers files; a hosted HTTPS deployment needs no local server
installation. Do not open the HTML directly from disk. To deploy, publish this folder as-is. No preparation step,
parent directory, or files from `host/web` are needed.

Use `--port N` to change the local port. Without a POSIX shell (for example native Windows), run
`python -m http.server 8000 --bind 127.0.0.1 --directory host/webhid` from the repository root instead.

Click Connect at the top-right of Device identity and select the PicoUart vendor HID interface. The same button
becomes Disconnect while connected. Device selection is explicitly user-initiated; the page
never reconnects automatically or requests a broader USB device filter. Metadata and runtime refresh every two seconds;
Disconnect closes the browser connection; unplugging clears the last sample.
Older firmware may supply only some diagnostics; unavailable fields remain `--` with an explicit read error.
Read failures leave
the device available for automatic polling or Disconnect, but clear stale metadata rather than inventing a clock or MCU.

Linux may require [HID permissions setup](../../tools/README.md#linux-hid-permissions) even after browser permission is
granted. Close competing HID clients when
diagnosing a busy device. Browser access is to USB devices on the browser's machine, not automatically to devices in a
remote VS Code workspace. The only write is the user-initiated LED toggle command; there is no firmware flashing or reset.

### No Compatible Device Found

WebHID does not request a password. Chrome's chooser lists eligible USB devices connected to the computer running
Chrome. Forwarding a page from a remote Linux/WSL/SSH workspace does not forward its USB devices.

If the Pico is attached to the remote machine, use the [Python/.NET dashboard](../dotnet/README.md) on forwarded port
5001 (or the server's configured port), or connect the Pico's USB data cable to the machine running Chrome. If it is
already local, check the USB data cable, device enumeration, browser site permissions, and OS device permissions.
Installing an OS access rule may require administrator approval in the OS; do not enter that password into the page.

## Contract And Tests

The client opens the PicoUart HID interface, displays diagnostics, and can toggle the board LED. The
[HID Report Reference](../../docs/design/usb/hid-report-reference.md) is the source for report layouts, versions, and
MCU compatibility. Keep browser-specific feature-report framing validation covered by the tests.
The same reference defines the board-derived Serial / UID, per-core service-load semantics, and RAM reservation
accounting; these are device values, not browser-session estimates.

Run the hardware-free Node tests with Node 22 or newer:

```sh
node --test host/webhid/tests/*.test.mjs
node --check host/webhid/js/app.mjs
```

Tests use mocked HID devices and do not qualify real browser/OS USB behavior. Validate device selection and
unplug/reconnect on the browser's machine before claiming hardware compatibility.