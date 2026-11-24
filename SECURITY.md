# Security

## Supported Versions

PicoUart is pre-1.0; only the latest [GitHub release](https://github.com/djwinter29-oss/PicoUart/releases) is
supported. Firmware, the [`pico-uart`](https://pypi.org/project/pico-uart/) package, and the .NET host share a
[release version](docs/deployment/releasing.md#versioning).

PyPI development uploads and content served at the official
[Direct Dashboard](https://djwinter29-oss.github.io/PicoUart/) URL, including test deployments, are in scope for
reports but are not supported releases. Check the dashboard's displayed version before use.

## Trust Model and Safe Use

The host treats USB device data as untrusted. PicoUart does not authenticate USB users or provide encrypted transport
or secure boot; board controls protect against mistakes, not an authorized local user with device access.

- Any process allowed to open a CDC port can read or write UART traffic. Any process allowed to open HID can read
  diagnostics and control the board. Restrict access to trusted accounts. On Linux,
  [HID access](tools/README.md#linux-hid-permissions) is granted by udev; serial access is usually controlled
  separately by groups such as `dialout`. On Windows and macOS, the logged-in user's processes typically have HID
  access.
- Python and .NET dashboards bind to loopback, check the Host, and require a session CSRF token for changes. CSRF
  blocks other websites from issuing browser actions, not local processes. Local processes can read diagnostic
  identifiers and error messages; these are not secrets.
- The Direct Dashboard requires a user-selected WebHID device and uses a restrictive CSP. The current GitHub Pages
  deployment has no CSP `frame-ancestors` directive or `X-Frame-Options` header. Open it directly rather than inside
  another page, and report clickjacking that can trigger board actions.
- UART data is forwarded without inspection. The reserved flash sector stores only the CRC-protected clock profile.
  The USB identity is Raspberry Pi-allocated `2e8a:116b`; derivatives need their own
  [VID/PID](docs/deployment/releasing.md#usb-identity).
- Release SHA-256 manifests can detect accidental corruption but do not prove authenticity. Release workflows pin
  GitHub Actions to commit SHAs and PyPI uses trusted publishing, but a compromised source or workflow can still
  publish untrustworthy artifacts.

## Vulnerability Reporting Scope

Report firmware or host bugs involving malformed USB/HID data, unsafe board controls, dashboard access-control
bypasses, script injection, clickjacking that can trigger controls, or release workflows publishing artifacts other
than those built from reviewed source. Host-side handling of malicious device data is in scope even when an attacker
needs physical USB access.

Out of scope:

- Attacks requiring physical modification or SWD access to the board.
- Intentional use of interfaces by a local account already allowed to access them, and diagnostic identifiers exposed
  to such local processes.
- Damage from an acknowledged overclock or out-of-spec wiring.
- Services deliberately exposed beyond loopback through user-configured proxies, tunnels, or port forwarding; accidental
  exposure caused by PicoUart remains in scope.
- UART payload content and upstream dependency issues not made exploitable by PicoUart.

## Report a Vulnerability

Use the repository's **Security** tab to report privately; do not post exploit details publicly. If private reporting is
unavailable, open a minimal issue requesting a private channel without technical details. Include the affected
component, version, board target, reproduction steps, and observed impact. Do not test equipment without authorization.

We aim to acknowledge reports within five business days. Coordinate public disclosure with maintainers after a fix or
mitigation is available; reporter credit is by consent.
