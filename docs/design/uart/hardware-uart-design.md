# Hardware UART Design

UART0 and UART1 use the RP2040/RP2350 PL011 peripherals. Core 1 services the registers, DMA, and flow-control GPIO. The
backend never calls TinyUSB; its rings are its only link to the USB side.

## Resources

On startup the backend checks its pin configuration, claims one RX and one TX DMA channel, configures the PL011, and
starts circular RX DMA. It keeps the channels until it stops. If any step fails, everything claimed so far is released.

## RX Path

Circular DMA copies received bytes into the port's aligned RX ring. When a transfer count runs out, the DMA interrupt
re-arms it, and the worker's poll is a fallback. The backend publishes DMA progress as the ring's producer cursor.

## TX Path

When the UART is idle and no line-coding change is blocking TX, the worker sends the next contiguous TX-ring span by
DMA. The span is committed only after the transfer completes. Each span is capped by a wire-time budget, so one busy
port cannot monopolize the worker.

DMA completion places bytes in the PL011 FIFO; a safe line-coding change waits until the UART reports idle.

## Line-Coding Changes

The backend validates the requested data bits, parity, and stop bits. It also checks that the PL011 divider can reach
the baud rate within tolerance.

Once the old TX bytes have drained and the UART is idle, the backend:

1. pauses RX DMA and records its exact progress,
2. applies the new format, and
3. restarts RX DMA at the live position, so unread data survives.

If applying fails, the backend restores the previous format. If even that fails, the port is marked `INIT_FAILED` rather
than left running on a stopped UART.

## Flow Control

Hardware RTS/CTS is opt-in in the board configuration and off by default:

- CTS uses the PL011's active-low hardware flow control.
- RTS is a GPIO driven from RX-ring occupancy with hysteresis. It deasserts at the high watermark and reasserts at the
  low watermark.

Assigning the pins alone does not make the link lossless; test the flow-control variant before relying on it.

## Errors and Limits

- Receive errors left over from bring-up are cleared after startup. Later framing and receive-status errors appear in
  backend statistics and HID health.
- While a single RX DMA transfer is being reloaded, there is a short window in which the PL011 FIFO can overflow.
  Bytes lost there set a sticky flag and are counted as one `rx_error_count` event, not as a known number of dropped
  bytes. Closing that window would need double-buffered RX DMA.
- RX can also overflow if the peer ignores RTS or the host does not drain CDC fast enough.
