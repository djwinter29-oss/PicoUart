# Security

## Report a Vulnerability

Please report suspected vulnerabilities privately through the repository's
**Security** tab using **Report a vulnerability**. Do not publish exploit details
in an issue or pull request.

If private reporting is unavailable, open a minimal issue asking maintainers for
a private reporting channel; do not include technical details until one is
provided.

## Device Security Notes

- The USB identity `cafe:4010` is an unallocated development/lab identity. Do
  not use it for commercial products; collisions and driver-association
  conflicts are possible. See the [release guide](docs/releasing.md) before
  distributing derivative devices.
- Remote HID reset is disabled by default. In builds that enable it, any local
  process with permission to open the HID device can reset the board. Restrict
  operating-system access to the HID device node to trusted users. Serial-port
  group membership (such as `dialout` on Linux) does not itself grant or restrict
  HID access.
- The HID interface is a local USB control/diagnostic interface, not a network
  service. The host tool checks whether firmware advertises reset capability but
  cannot authenticate other local processes with access to the same HID device.
