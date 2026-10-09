# PicoUart WebHID Prototype

A read-only browser dashboard for MCU identity, clock, firmware version, temperature, overflows, and six-channel health
and traffic. It talks directly to PicoUart through WebHID; there is no Python/.NET application backend. Its HTML, CSS,
favicon, and renderer live entirely in this folder, independently of the Python/.NET dashboard. Board controls and
WebSerial are not implemented here.

## Run

Use desktop Chrome or Edge and an HTTPS site or localhost. Browser support and USB permissions vary by platform;
Firefox, Safari, and mobile browsers generally do not support WebHID. The page reports unavailable WebHID and keeps
Connect disabled when the API or secure context is missing.

For local development, serve `host/webhid` from the repository root:

```sh
python3 -m http.server 8000 --bind 127.0.0.1 --directory host/webhid
```

Open `http://127.0.0.1:8000/`. The static server only delivers files; a hosted HTTPS deployment needs no local server
installation. Do not open the HTML directly from disk. To deploy, publish this folder as-is. No preparation step,
parent directory, or files from `host/web` are needed.

Click Connect and select the PicoUart vendor HID interface. Device selection is explicitly user-initiated; the page
never reconnects automatically or requests a broader USB device filter. Refresh reads all metadata again. Disconnect
closes the browser connection; unplugging clears the last sample. Report 6 supplies hardware info; older firmware may
still supply the other diagnostics. Read failures leave
the device available for Refresh/Disconnect, but clear stale metadata rather than inventing a clock or MCU.

Linux may require hidraw permissions even after browser permission is granted. Close competing HID clients when
diagnosing a busy device. Browser access is to USB devices on the browser's machine, not automatically to devices in a
remote VS Code workspace. No firmware flashing or write/control reports are performed by this page.

### No Compatible Device Found

WebHID does not request a password. Chrome's chooser lists eligible USB devices connected to the computer running
Chrome. Forwarding a page from a remote Linux/WSL/SSH workspace does not forward its USB devices.

If the Pico is attached to the remote machine, use the [Python/.NET dashboard](../dotnet/README.md) on forwarded port
5001 (or the server's configured port), or connect the Pico's USB data cable to the machine running Chrome. If it is
already local, check the USB data cable, device enumeration, browser site permissions, and OS device permissions.
Installing an OS access rule may require administrator approval in the OS; do not enter that password into the page.

## Contract And Tests

The client filters `cafe:4010` with usage page `0xFF00`, usage `1`. It reads feature reports 3, 5, and 6 and listens for
input report 1. Input reports exclude the report-ID prefix; their ID is supplied separately by the browser event.
Chromium returns numbered feature reports with their report-ID prefix; the client validates it and allows zero padding
from fixed-length platform buffers. Unknown MCU IDs retain their raw ID and clock, matching Python/.NET behavior.
Unsupported versions, malformed reports, nonzero padding, and zero clocks are rejected.

Run the hardware-free Node tests with Node 22 or newer:

```sh
node --test host/webhid/tests/*.test.mjs
node --check host/webhid/js/app.mjs
```

Tests cover report framing, signedness/byte order, unknown IDs, chooser cancellation, permission/open failures,
read recovery, unplug/close races, reconnect, and live sequence/counter accounting. They use mocked HID devices and do
not qualify real browser/OS USB
behavior; validate native selection and unplug on the browser's machine before claiming hardware compatibility.