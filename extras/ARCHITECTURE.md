# UIAPIR architecture

## Hardware backends

UIAPIR has two backends. Both keep the protocol encoder, capture state machine,
and public API identical.

### UIAPduino V1.4 / CH32V003

- TIM1 CH4 / D6 / PC4: 33% duty carrier PWM (38 or 40 kHz)
- TIM2: free-running 1 MHz timebase for capture and blocking envelope timing
  (16-bit, so it wraps every 65.536 ms; inter-frame spacing on the send side is
  accumulated in software instead, because a NEC frame is longer than that)
- EXTI: both-edge interrupt from a demodulating IR receiver

### Portable Arduino backend

All other Arduino cores use `tone()` for the carrier, `micros()` for elapsed
time, and `attachInterrupt()` for both-edge reception. TX can be any
tone-capable output and RX must be interrupt-capable. This path is compiled for
Arduino Uno and ESP32 Dev Module in CI. Its signal timing still needs hardware
validation on each target family.

The receive path records alternating MARK/SPACE durations. Decoding is performed
after the frame gap, outside the ISR. NEC, AEHA, and Sony SIRC become compact
structured data; other signals remain RAW.

TX and RX are intentionally not simultaneous. Receiver EXTI is detached while
transmitting to avoid recording the local emitter.

## Compact RAW representation

CH32V003 has 2 KiB SRAM. A conventional `uint16_t raw[340]` consumes 680 bytes
per buffer, and both the ISR buffer and returned `IRCode` may coexist. UIAPIR
therefore records 50 us ticks in `uint8_t`:

```
duration_us = ticks[index] * UIAPIR_RAW_TICK_US
```

The default 340 entries consume 340 bytes and hold the 323 durations needed by
a 20-byte AEHA frame. The buffer is selected at `begin()` time: UIAPIR can
allocate it, or the caller can provide storage and its capacity. Keeping it out
of the `UIAPIR` object means a smaller runtime capacity really saves SRAM, and
a transmit-only instance needs no capture buffer. A single duration is limited
to 12.75 ms. The inter-frame gap is detected separately and is not stored.

## State flow

1. A falling edge starts a capture. A rising edge cannot, because every
   supported frame opens with a mark and the gap test below depends on knowing
   whether the duration being timed is a mark or a space.
2. Each following edge quantizes and appends the preceding level duration.
3. A quiet SPACE longer than `UIAPIR_FRAME_GAP_US` finalizes the frame.
4. `receive()` tries NEC, AEHA, then Sony decoders.
5. If all decoders reject it, the compact RAW timings are returned unchanged.

## Why the frame gap is space-only

The threshold has to be longer than the longest duration inside a frame and
shorter than the shortest gap between frames. Those two bounds cross:

```
NEC leader mark                       9000 us   longest intra-frame duration
20-bit SIRC frame of all ones        38400 us
SIRC period                          45000 us
  -> gap after that frame             6600 us   shortest inter-frame gap
```

No level-agnostic threshold fits between 9000 and 6600, which is why a
threshold picked without checking SIRC's limits merges consecutive 20-bit
frames. Restricting the test to spaces drops the lower bound to the NEC leader
space (4500 us, 5700 us once the decoder's tolerance is included) and opens a
usable window. Durations alternate mark, space, mark, ..., so a single tracked
phase bit says which one is being timed; no extra pin read is needed.

`UIAPIRProtocolDefs.h` states both bounds as `#error` guards, so changing a
protocol constant or the threshold breaks the build instead of the receiver.

## Measuring intervals longer than the timebase

On CH32V003, TIM2 is 16 bit, so its 1 MHz count wraps every 65.536 ms and cannot tell a 4 ms
in-frame space from a 69.5 ms idle. Reading the second as the first appends the
idle period to whatever frame follows it.

CH32V003 edge timestamps therefore carry `millis()` alongside the counter. Below 60 ms of
millisecond difference the counter can have wrapped at most once, so its own
difference is exact and is used as is; above that the exact figure no longer
matters, because every interval the capture resolves precisely is far shorter,
and `elapsedUs()` saturates. `millis()` is a plain variable read on this core,
unlike `micros()`, which does 64-bit division in software on a core with no
hardware divider and has no business in an edge ISR.

The CH32V003 transmitter has the same problem in reverse: a NEC frame runs up to 76 ms,
longer than the counter's period, so `padToPeriod()` accumulates what it emitted
rather than measuring it. The portable backend uses unsigned subtraction of
the 32-bit `micros()` counter, which remains correct across its wraparound for
the short durations UIAPIR measures.

## Capture state machine

`UIAPIRCapture.h/.cpp` holds the frame boundary logic with no Arduino or SPL
dependency, so the host tests can drive it with synthetic edge streams.
`UIAPIR.cpp` supplies the timestamps and the pin level; everything about what
constitutes a frame lives in the capture module.

Three rules keep a burst of repeated frames intact:

- a capture opens only on a mark preceded by a real gap, so it never joins a
  burst partway through and reports a fragment
- the edge that closes a frame is also the edge that opens the next one, so it
  is remembered and reused if the sketch reads the frame before the next edge
- the mark/space phase is tracked explicitly rather than derived from the
  duration count, which stops advancing when the buffer overflows and would
  otherwise leave an over-long signal with no way to terminate

## Shared resource ownership

TIM1, TIM2 and the EXTI callback are process-wide, so exactly one `UIAPIR` may
own them. `_active` records the owner: a second instance's `begin()` is refused,
and `end()` returns without touching a register unless the caller is the owner.

That asymmetry matters. `end()` is the natural thing to call after a failed
`begin()`, and an unconditional `TIM_Cmd(TIM2, DISABLE)` there would stop the
timebase underneath the instance that does own it - leaving that instance spinning
forever in `waitUs()`, which waits on a counter that no longer advances.

## Resource conflicts

The initial implementation owns TIM1 and TIM2. Do not use Servo (TIM1), tone
(TIM2), or another PWM output while UIAPIR is active. The EXTI input must not be
shared with another interrupt owner.
