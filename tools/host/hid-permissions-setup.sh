#!/usr/bin/env sh
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
RULE_TARGET="/etc/udev/rules.d/70-picouart-hid.rules"

case "${1:-}" in
    -h|--help)
        cat <<'EOF'
Usage: tools/host/hid-permissions-setup.sh

Install the PicoUart Linux USB/HID udev rules. May prompt for sudo.
Grants read/write access to the invoking user's primary group, plus desktop uaccess.
Run from the account that will use the host application; no group membership changes.
EOF
        exit 0
        ;;
    "")
        ;;
    *)
        echo "Unknown argument: $1; use --help." >&2
        exit 2
        ;;
esac
if [ "$#" -ne 0 ]; then
    echo "Unexpected arguments; use --help." >&2
    exit 2
fi

if [ "$(uname -s)" != "Linux" ]; then
    echo "PicoUart HID udev permissions are only needed on Linux."
    exit 0
fi
if ! command -v udevadm >/dev/null 2>&1; then
    echo "udevadm is required to configure PicoUart HID permissions." >&2
    exit 1
fi
if [ "$(id -u)" -ne 0 ]; then
    if ! command -v sudo >/dev/null 2>&1; then
        echo "sudo is required to install the PicoUart HID udev rules." >&2
        exit 1
    fi
    exec sudo "$SCRIPT_DIR/hid-permissions-setup.sh" "$@"
fi
if [ -z "${SUDO_USER:-}" ] || [ "$SUDO_USER" = root ]; then
    echo "Run this helper from the non-root account that needs HID access (it will invoke sudo)." >&2
    exit 1
fi

ACCESS_GROUP=$(id -g "$SUDO_USER")
USB_RULE="SUBSYSTEM==\"usb\", ENV{DEVTYPE}==\"usb_device\", ATTR{idVendor}==\"cafe\", ATTR{idProduct}==\"4010\", GROUP=\"$ACCESS_GROUP\", MODE=\"0660\", TAG+=\"uaccess\""
HIDRAW_RULE="SUBSYSTEM==\"hidraw\", KERNEL==\"hidraw*\", ATTRS{idVendor}==\"cafe\", ATTRS{idProduct}==\"4010\", GROUP=\"$ACCESS_GROUP\", MODE=\"0660\", TAG+=\"uaccess\""
RULE_CONTENT=$(printf '%s\n%s' "$USB_RULE" "$HIDRAW_RULE")

if [ -L "$RULE_TARGET" ]; then
    echo "Refusing to replace symlink $RULE_TARGET; review the rule manually." >&2
    exit 1
fi
if [ -e "$RULE_TARGET" ] && [ "$(cat "$RULE_TARGET")" != "$RULE_CONTENT" ]; then
    echo "Existing udev rule differs; refusing to overwrite $RULE_TARGET." >&2
    echo "Review and merge the rule manually." >&2
    exit 1
fi
if [ ! -e "$RULE_TARGET" ]; then
    TEMP_RULE=$(mktemp)
    trap 'rm -f "$TEMP_RULE"' 0 HUP INT TERM
    printf '%s\n' "$RULE_CONTENT" > "$TEMP_RULE"
    install -m 0644 "$TEMP_RULE" "$RULE_TARGET"
fi

udevadm control --reload-rules
udevadm trigger --subsystem-match=usb --attr-match=idVendor=cafe --attr-match=idProduct=4010
udevadm trigger --subsystem-match=hidraw
udevadm settle
echo "PicoUart HID permissions applied for $SUDO_USER (primary group $ACCESS_GROUP)."
echo "Reconnect the board if access is still denied, then run tools/host/hid-permissions-check.sh."
