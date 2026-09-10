# Portable smoke test

This firmware exercises the portable UIAPIR backend on Arduino Uno-compatible
boards, Arduino Pro Mini boards, and ESP32 boards. It checks initialization with caller-provided storage,
invalid pin rejection, shutdown and restart, NEC, extended NEC, NEC repeat,
AEHA, Sony SIRC, and RAW transmission. It then waits for a remote control,
captures the signal through `available()` and `learn()`, prints the decoded
format, and replays the captured frame.

## Wiring

Use an IR LED through a transistor or MOSFET. Do not drive a high-current LED
directly from a GPIO. Connect the output of a demodulating 38 kHz IR receiver to
the RX pin and connect all grounds.

| Board | RX | TX |
|---|---:|---:|
| Arduino Uno | D2 | D6 |
| Arduino Pro Mini (ATmega328P, 8 or 16 MHz) | D3 | A0 |
| ESP32 Dev Module | GPIO27 | GPIO26 |

The pins can be overridden at compile time with `UIAPIR_TEST_RX_PIN` and
`UIAPIR_TEST_TX_PIN`. The RX pin must support an external interrupt. On an
ATmega328P Pro Mini, use D2 or D3; A3 cannot be used for RX and makes
`begin()` return `false`.

Open the serial monitor at 115200 baud. On startup, point the IR LED at another
receiver or a logic analyzer to inspect the six transmission checks. After the
summary appears, press a commercial remote button toward the connected receiver.
The firmware prints the detected protocol and replays the captured signal after
250 ms.

Compilation alone verifies API and board-core compatibility. A complete manual
pass requires observing the transmitted signal and confirming that a captured
remote command is replayed successfully.
