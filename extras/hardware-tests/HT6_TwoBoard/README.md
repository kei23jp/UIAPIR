# HT6 - two-board send and receive test

HT1 to HT5 each need someone to read a serial log or a logic analyser and
decide whether what they see is right. HT6 does not: two UIAPduino boards face
each other across a real optical path, one transmits, the other decodes and
prints a verdict. It is the only test here that exercises transmit and receive
against each other, and the only one that can be left running.

Why two boards rather than one looped back on itself: UIAPIR detaches its
receiver while it transmits, on purpose, so it never records its own emitter.
A single board therefore cannot hear itself without changing the library to
suit the test. Two boards also let a burst be captured whole - a NEC key held
down, or the three frames SIRC requires - which is where the receiver is
hardest to get right.

## What it covers that the other tests do not

| | |
|---|---|
| the optical path | carrier, LED driver, demodulator, and the delays each adds |
| transmit and receive agreeing | every accepted case is checked field by field against what was sent |
| bursts | NEC repeats, two AEHA frames, three SIRC frames, all captured without dropping one |
| frame separation | 20-bit SIRC all ones leaves a 6.60 ms gap, the tightest the 6000 us threshold ever sees |
| argument rejection | eight calls that must return false and emit nothing |
| RAW replay gating | a capture flagged as clipped must not be replayed |
| soak | the suite repeats, so an intermittent fault has a chance to show up |

It does not measure the carrier frequency or its duty cycle. That still needs
HT4 and an instrument.

## Equipment

| | |
|---|---|
| UIAPduino Pro Micro CH32V003 V1.4 | x2 |
| WCH-Link (or the programmer already in use) | x1, moved between the boards |
| USB-UART adapter, 3.3 V | x1, stays on the receiver |
| IR LED + NPN transistor + resistors | transmitter side |
| IR receiver module, 38 kHz (VS1838B, PL-IRM series) | receiver side |
| jumpers | 2 signal + 1 ground between the boards, 1 kOhm in series with each signal |

Only one USB-UART is needed. The log lives on the receiver, which is where the
decoded result is; the transmitter answers over a GPIO line instead of a serial
port. If a transmitter-side log is ever needed, move the adapter over and run
HT1 or HT3, which are single-board tests.

## Wiring

```
        transmitter                                  receiver
   +-------------------+                        +-------------------+
   |                   |     [1k] NPN           |                   |
   |  A2 (D6, PC4) o---|-----------|<  IR LED   |                   |
   |                   |                 |      |    IR module      |
   |                   |                ))) ((( |  OUT o            |
   |                   |                        |      |            |
   |                   |                        |  3 (D3, PC1) o    |
   |                   |                        |                   |
   |   8 (D8, PC6) o<--|------[1k]--------------|--o 8 (D8, PC6)    |  START
   |   9 (D9, PC7) o---|------[1k]------------->|--o 9 (D9, PC7)    |  STATUS
   |        GND    o---|------------------------|--o GND            |
   |                   |                        |                   |
   |                   |                        | 15 (D15, PD5) o---|--> USB-UART RX
   +-------------------+                        +-------------------+
```

Both boards run the same pin numbers; START and STATUS simply cross over, an
output on one board and an input on the other. Points worth getting right:

- **Tie the grounds together.** The IR path is optical and needs no common
  reference, but START and STATUS are ordinary logic signals and do.
- **Power each board from its own USB connector.** Do not bridge 5 V or 3V3.
- **Put a resistor in series with START and STATUS.** If a firmware ever
  drives the wrong direction - the wrong environment flashed to a board is the
  easy way to do that - the resistor is what keeps it from being a repair job.
- **Point the LED at the module, a few centimetres apart.** Further is fine;
  close enough to saturate the module is not, because a saturated demodulator
  stretches marks and the receiver will report timing that looks like a library
  fault.
- `A2` on the silk screen is Arduino D6, and `3` is Arduino D3. The firmware
  checks at startup that the core really maps D3, D8 and D9 to PC1, PC6 and
  PC7, and refuses to run if it does not.

## Build and run

Flash one board at a time - both present at once and the uploader has two
identical targets to choose between.

```bash
cd extras/hardware-tests/HT6_TwoBoard
pio run -e transmitter -t upload
```

```bash
pio run -e receiver -t upload
```

```bash
pio device monitor -e receiver
```

Hold RST while plugging each board in before uploading: UIAPduino is flashed
over USB by its own bootloader, not through an SWD probe, and the bootloader
exits as soon as the new firmware runs.

**The USB-UART goes on the receiver.** The transmitter has no serial port at
all - it has nothing to say that the STATUS line does not carry - so a monitor
attached to it stays empty however well the upload went.

The first `pio run` installs the CH32V platform and its toolchain, which takes
a few minutes. Build both environments without uploading with a bare `pio run`.

In VS Code, open `UIAPIR.code-workspace` at the repository root and pick
`HT6 two-board (PlatformIO)` and an environment from the PlatformIO status
bar. The other sketches build from the `sketches (PlatformIO)` project in the
same workspace; see `extras/platformio/README.md`.

Order does not matter, and neither board minds being reset while the other is
running: the receiver names each case on the START line rather than counting
along in step, so a reset costs one failed case, not a whole run of
mislabelled ones.

## Expected output

```
UIAPIR HT6 - two-board send and receive test
cases 17, capture 112 durations, frame gap 6000 us, raw tick 50 us
waiting for the transmitter

run 1
[00] NEC standard           frames 1/1  api true    98 ms  PASS
[01] NEC extended           frames 1/1  api true   106 ms  PASS
[02] NEC + 2 repeats        frames 3/3  api true   262 ms  PASS
[03] AEHA 6 byte x2         frames 2/2  api true   226 ms  PASS
[04] SIRC 12 bit x3         frames 3/3  api true   139 ms  PASS
[05] SIRC 15 bit x3         frames 3/3  api true   143 ms  PASS
[06] SIRC 20 bit x3         frames 3/3  api true   158 ms  PASS
[07] RAW ticks              frames 1/1  api true    39 ms  PASS
[08] RAW via send()         frames 1/1  api true    39 ms  PASS
[09] reject NEC addr > 255  frames 0/0  api false   30 ms  PASS
[10] reject NEC complement  frames 0/0  api false   30 ms  PASS
[11] reject AEHA too short  frames 0/0  api false   30 ms  PASS
[12] reject AEHA too long   frames 0/0  api false   30 ms  PASS
[13] reject SIRC 13 bit     frames 0/0  api false   30 ms  PASS
[14] reject SIRC cmd 0x80   frames 0/0  api false   30 ms  PASS
[15] reject SIRC 2 frames   frames 0/0  api false   30 ms  PASS
[16] reject RAW clipped     frames 0/0  api false   30 ms  PASS
run 1: 17/17 PASS
```

After the first run only failures are printed, so a soak run is one summary
line every few seconds. A run takes roughly ten seconds including the 2 s pause
between runs.

The suite has been run on hardware: 17/17, with the elapsed column agreeing
with the figures below to within 1 ms. Those figures are computed from the
protocol constants rather than measured, so a case that drifts further than
that is worth looking at:

| | frames | emitted | elapsed |
|---|---|---|---|
| NEC 0x12 / 0x34 | 1 | 67.98 ms | 98 ms |
| NEC extended 0xfffe / 0xff | 1 | 75.89 ms | 106 ms |
| NEC + 2 repeats | 3 | 110 + 110 + 11.81 ms | 262 ms |
| AEHA 6 byte x2 | 2 | 130 + 65.88 ms | 226 ms |
| SIRC 12 bit x3 | 3 | 45 + 45 + 19.20 ms | 139 ms |
| SIRC 15 bit x3 | 3 | 45 + 45 + 23.40 ms | 143 ms |
| SIRC 20 bit x3 | 3 | 45 + 45 + 38.40 ms | 158 ms |
| RAW, 9 x 1 ms | 1 | 9.00 ms | 39 ms |
| any rejection | 0 | nothing | 30 ms |

The 30 ms every case carries is the handshake: 12 ms for the START train to
end plus 20 ms of settling before the carrier starts. The repeat and
multi-frame cases are measured start-to-start, which is why case 02 comes to
262 ms rather than three frame lengths added together - if a period ever
regressed to "frame plus a fixed gap", these are the numbers that would move.

## Reading a failure

Each failing case prints why. The distinctions the STATUS line buys:

| line | meaning |
|---|---|
| `transmitter never went idle` | STATUS is never HIGH. Wrong firmware on that board, no ground between the boards, a broken STATUS wire, or the transmitter refused `begin()` / failed its pin-map check and is holding the line low deliberately. |
| `transmitter did not answer START` | STATUS never went LOW after the pulse train. The START wire is broken, or it is wired to the wrong pin. |
| `transmitter did not go idle again` | STATUS went LOW and stayed there. The transmitter is inside `send()` and not coming out, which is what a stopped TIM2 looks like - the same fault HT3 isolates. |
| `send() returned false, expected true` | The call was rejected. Nothing to do with the optical path; compare the case's arguments against the library's validation. |
| `send() returned true, expected false` | Worse: an argument that must be rejected was accepted. Check whether the case still matches the limits in `UIAPIRProtocolDefs.h`. |
| `N frames decoded, expected M` | Fewer means frames were lost or never emitted; more means one frame was split, or the case before it is bleeding into this one. |
| `payload does not match what was sent` | The frame decoded but carries different data. For a RAW case the durations it actually received are printed on the next line, in microseconds. |
| `decoded as the wrong protocol` | Usually RAW where a protocol was expected: the frame arrived, but at least one duration missed its acceptance band. |
| `wrong number of durations` | A RAW frame arrived with more or fewer edges than were sent. The durations are printed underneath. |
| `repeat flag on the wrong frame` | The first frame was a repeat code, or a following one was a full frame. |
| `capture overflowed or clipped a duration` | A duration exceeded 12.75 ms or the frame outgrew the buffer. On this plan that means something outside the test is emitting - sunlight, a fluorescent lamp, a plasma display, someone else's remote. |

Two failures that look alike and are not:

- **`frames 0/1` with `api true`** - the transmitter believes it emitted and
  the receiver heard nothing. That is the optical path: LED polarity, the
  transistor, the module's supply voltage, or line of sight.
- **`frames 0/1` with `api false`** - nothing was emitted, because the call was
  rejected. The optical path is not involved.

If every case fails at once, suspect the wiring or the pairing, not the
library. If one protocol fails and the rest pass, the timing constants for that
protocol are the place to look.

## Changing the plan

`include/HT6TestPlan.h` is the whole plan: pins, handshake timing, and the case
table. Both firmwares include it, and nothing about a case lives anywhere else.
Adding a case means one row in `HT6_CASES`, one name in `HT6_CASE_NAMES`, and
sometimes one branch in the transmitter's `runCase()`. A `static_assert` fails
the build if the row and the name get out of step, so only the `runCase()`
branch is left to remember - and a case whose kind has no branch reports
`send() returned false`.

The case index travels over START as a pulse count, so **both boards must be
flashed from the same revision of the header**. Two boards built from different
revisions will run different tests under the same names, and an index past the
end of the table shows up as `send() returned false` rather than as the
mismatch it is.

Frames are folded into counters as they arrive rather than stored, because one
`IRCode` is 346 bytes of 2048. Nothing is printed while a case is in flight
either: consecutive 20-bit SIRC frames are 6.6 ms apart and the capture holds
one finished frame at a time, so a `Serial.print()` in that loop drops frames -
which the test would then report as a receiver fault.

## Notes on the build

The board definition and toolchain come from
[Community-PIO-CH32V/platform-ch32v](https://github.com/Community-PIO-CH32V/platform-ch32v),
which already ships a `UIAPduino_Pro_Micro_CH32V003_v1dot4` board. That
platform builds against the openwch Arduino core rather than the UIAP package
the Arduino IDE uses. For this test the two are equivalent: their
`variants/CH32V00x/CH32V003F4` digital pin tables are byte-identical, so D3,
D6, D8, D9 and D15 land on PC1, PC4, PC6, PC7 and PD5 either way. The only
difference is two ADC channel entries in `PeripheralPins.c`, and neither
firmware calls `analogRead()`. `platformio.ini` carries a commented-out
`platform_packages` line for building against the installed UIAP package
instead.

Room left on a 16 KiB part, `-Os`:

| | flash | RAM |
|---|---|---|
| transmitter | 9720 B, 59% | 944 B, 46% |
| receiver | 14472 B, 88% | 1068 B, 52% |

The receiver is the tight one, and nearly all of it is text. If a core update
pushes it over, cut the per-failure explanations in `printResult()` before
cutting cases - a failing case still names itself, and this file says what each
message means.
