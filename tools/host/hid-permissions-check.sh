#!/usr/bin/env sh
set -eu

case "${1:-}" in
    -h|--help)
        printf 'Usage: tools/host/hid-permissions-check.sh\nCheck access to connected PicoUart Linux USB/HID device nodes.\n'
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
    exit 0
fi
if ! command -v udevadm >/dev/null 2>&1; then
    echo "Skipping PicoUart permission check: udevadm is unavailable." >&2
    exit 0
fi

for device_node in /dev/hidraw* /dev/bus/usb/*/*; do
    [ -e "$device_node" ] || continue
    if ! device_path=$(udevadm info --query=path --name="$device_node"); then
        [ -e "$device_node" ] || continue
        echo "Cannot inspect device permissions for $device_node." >&2
        exit 1
    fi
    sys_path="/sys$device_path"
    # hidraw nodes need their USB ancestor; vendor/product properties may be absent.
    while [ -n "$sys_path" ] && [ "$sys_path" != / ]; do
        if [ -f "$sys_path/idVendor" ] && [ -f "$sys_path/idProduct" ]; then
            read -r vendor < "$sys_path/idVendor"
            read -r product < "$sys_path/idProduct"
            if [ "$vendor" = cafe ] && [ "$product" = 4010 ]; then
                if [ ! -r "$device_node" ] || [ ! -w "$device_node" ]; then
                    echo "No read/write permission for PicoUart device node: $device_node" >&2
                    echo "Run tools/host/hid-permissions-setup.sh, then retry or reconnect the board." >&2
                    exit 1
                fi
                break
            fi
        fi
        sys_path=${sys_path%/*}
    done
done
