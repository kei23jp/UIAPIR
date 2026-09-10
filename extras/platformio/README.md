# Building the sketches with PlatformIO

Twelve environments: the original five examples, two roles for TwoBoardLink,
and the five single-board hardware tests. The sketches remain ordinary Arduino sketches; Arduino
IDE and `arduino-cli` build them exactly as before, and `scripts/check.sh`
still compiles them that way.

```bash
cd extras/platformio && pio run
```

```bash
cd extras/platformio && pio run -e HT5_ReceiveDump -t upload
```

```bash
cd extras/platformio && pio device monitor -e HT5_ReceiveDump
```

In VS Code, open `UIAPIR.code-workspace` at the repository root and choose the
project and environment from the PlatformIO status bar.

| environment | sketch |
|---|---|
| `Receive` | `examples/Receive` |
| `SendProtocols` | `examples/SendProtocols` |
| `SendRaw` | `examples/SendRaw` |
| `LearnAndReplay` | `examples/LearnAndReplay` |
| `LearnRemote` | `examples/LearnRemote` |
| `TwoBoardLinkSender` / `TwoBoardLinkReceiver` | `examples/TwoBoardLink` (endpoint 0 / 1) |
| `HT1_BeginContract` .. `HT5_ReceiveDump` | `extras/hardware-tests/HTn_*` |

HT6 is not here. It is two firmwares that have to agree with each other, so it
is its own project with its own environments:
`extras/hardware-tests/HT6_TwoBoard`.

## Uploading

UIAPduino has no SWD probe on board and is not flashed through one. It has a
USB bootloader: **hold RST while plugging the board in**, then upload. The
bootloader exits as soon as the new firmware runs, so hold RST again for the
next upload.

The bootloader enumerates as a **HID device, `1209:b803`**. Being HID means
there is no libusb driver to install - and also that stock minichlink cannot
talk to it. `upload_protocol = minichlink` uses the copy PlatformIO ships,
which is upstream ch32v003fun; its programmer list is `b003boot, ardulink,
esp32s2chfun, funprog, isp`, with no `b803boot`. It probes `1209:b003`, finds
nothing, and stops:

```
VID:0x1209, PID:0xb003
Error: Could not initialize any supported programmers
```

So this project calls the minichlink inside the UIAP Arduino package instead.
That one is a fork with a `b803boot` backend, and is the binary the Arduino IDE
already uses; the command is the one from the package's own `platform.txt`:

```
minichlink -w firmware.bin flash
```

`platformio.ini` builds that path from `%LOCALAPPDATA%`, so it does not carry a
user name, but it does carry the tool's pinned version. If a UIAP core update
moves it, look under `%LOCALAPPDATA%\Arduino15\packages\UIAP\tools` and correct
the line.

The trailing `-b` matters more than it looks. Without it the board stays in the
bootloader after a successful flash and never starts the firmware, so a sketch
that prints at boot prints nothing until someone presses reset - which reads
exactly like a broken serial connection.

What gets flashed is `firmware.bin`, which this platform already emits
alongside the ELF. Handing minichlink the ELF fails on a 16 KiB part - its
debug information makes the file far larger than the flash it is checked
against - and that is a property of the file, not of the tool.

### When an upload fails

`Could not initialize any supported programmers` means no programmer was found,
and by far the most likely reason is that the board is not in bootloader mode.
Check whether Windows can see it at all:

```powershell
Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -match 'VID_1209&PID_B803' }
```

A row means the bootloader is running and the problem is elsewhere. No rows
means the board is running its firmware, or is unplugged - note that those look
identical from the host, because the CH32V003 has no USB peripheral and appears
on USB only while the bootloader is active.

### When the upload works but nothing comes out of the serial port

In order of how often it is the answer:

1. **The firmware does not print.** The HT6 transmitter has no serial port at
   all - it answers on a GPIO line, and the log lives on the receiver. Nor do
   `examples/Receive`, `SendProtocols`, `SendRaw` or `LearnAndReplay`, which
   are API examples with no output. What prints is `examples/LearnRemote`,
   HT1 to HT5, and the HT6 receiver.
2. **The board is still in the bootloader.** It only runs the new firmware
   after `-b`, a reset, or a replug without holding RST.
3. **TX is not wired to RX.** Board `15` (D15, PD5) goes to the adapter's
   **RX**, and the grounds must be tied together. The adapter's TX can stay
   unconnected; nothing here reads serial input.
4. **Wrong baud.** Every sketch here uses 115200.

`Serial` is USART1 on PD5/PD6 in this core, and PlatformIO passes the same
48 MHz clock defines the Arduino build does (`SYSCLK_FREQ_48MHz_HSI`), so the
baud divisor is not a difference between the two toolchains.

## Why src/ holds a file per sketch

An Arduino library keeps its sketches in `examples/<Name>/<Name>.ino`, one
folder each. A PlatformIO project compiles `src_dir`, and `src_dir` is set once
for the whole project, not per environment. So the natural arrangement -
`src_dir` at the repository root, one `build_src_filter` per sketch - cannot
work here, and it fails in a way that takes a while to read:

```
undefined reference to `setup'
undefined reference to `loop'
```

PlatformIO converts `.ino` to `.cpp` only for files sitting **directly** in
`src_dir`; the glob is not recursive. A sketch one folder down is never
converted, and nothing knows how to compile a `.ino`, so the environment links
an application with no `setup()` in it.

The way in is that none of these sketches actually needs the conversion. It
exists to generate the function prototypes Arduino lets a sketch omit, and
every sketch here defines its helpers before it uses them - each one is already
valid C++. So `src/<Environment>.cpp` includes the sketch, and the environment
compiles that:

```cpp
#include "../../../examples/Receive/Receive.ino"
```

One line per sketch. Passing the path in as a `-D` macro instead would collapse
these to a single shared file, and it builds - but it quietly breaks the edit
loop, which is the whole point of using the IDE: SCons finds dependencies by
reading include directives, and it does not expand macros to do it. Edit a
sketch and the build reports success without recompiling it. The literal
include is what makes the sketch a real dependency.

## What this project does not cover

`scripts/check.sh` and `scripts/check.ps1` compile these same sketches with
`arduino-cli`, against the pinned UIAP core, and that is what CI runs. This
project builds them against the openwch core the PlatformIO platform ships,
whose `variants/CH32V00x/CH32V003F4` digital pin table is byte-identical to the
UIAP one. Adding these ten environments to the check scripts would double
their running time for no coverage they do not already have, so it is left out;
HT6 is in the check scripts because nothing else builds it.
