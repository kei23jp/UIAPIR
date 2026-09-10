// SPDX-License-Identifier: MIT
// Copyright (c) 2026 kei23jp

#pragma once

// Single source of truth for the three supported protocols.
//
// Every timing, field width and length limit below is taken from the
// references cited per section. The transmitter (UIAPIR.cpp), the decoder
// (UIAPIRProtocol.cpp) and the host tests all derive from these macros, so a
// value can never drift between the send and receive paths.
//
// References:
//   [ChaN] https://elm-chan.org/docs/ir_format.html
//   [SBP]  https://www.sbprojects.net/knowledge/ir/nec.php

// ---------------------------------------------------------------------------
// Protocol selection
// ---------------------------------------------------------------------------
// A protocol can be compiled out with -DUIAPIR_ENABLE_<name>=0. That removes
// its decoder, its send methods and its declarations, so a call to one that is
// switched off is a compile error rather than a runtime false - the point of
// the switch is to not carry the code, and a silent false would hide that the
// build cannot do what the sketch asks.
//
// RAW is not on this list. It is the fallback every capture starts as, and
// sendRawTicks() is what replays anything the decoders did not recognise, so
// there is no build without it.
//
// The timing constants below stay defined either way. They are what the
// compile-time checks in this file and UIAPIRTypes.h are written against, and
// keeping them means switching a protocol off can never quietly move a
// threshold that the receiver applies to every signal.
#ifndef UIAPIR_ENABLE_NEC
#define UIAPIR_ENABLE_NEC 1
#endif
#ifndef UIAPIR_ENABLE_AEHA
#define UIAPIR_ENABLE_AEHA 1
#endif
#ifndef UIAPIR_ENABLE_SONY
#define UIAPIR_ENABLE_SONY 1
#endif

// ---------------------------------------------------------------------------
// NEC
// ---------------------------------------------------------------------------
// [ChaN] T = 562 us, fsc = 38 kHz 1/3 duty, fixed 32-bit frame, 108 ms period.
// [SBP]  9 ms AGC burst, 4.5 ms space, 560 us bit burst, 1.69 ms "1" space,
//        repeat code every 110 ms, repeat = 9 ms + 2.25 ms + 560 us.
// UIAPIR uses the [SBP] numbers because they are what commercial remotes and
// mainstream decoders emit; they sit well inside [ChaN]'s T = 562 us.
#define UIAPIR_NEC_CARRIER_KHZ        38
#define UIAPIR_NEC_UNIT_US            560
#define UIAPIR_NEC_LEADER_MARK_US     9000
#define UIAPIR_NEC_LEADER_SPACE_US    4500
#define UIAPIR_NEC_REPEAT_SPACE_US    2250
#define UIAPIR_NEC_BIT_MARK_US        UIAPIR_NEC_UNIT_US
#define UIAPIR_NEC_ZERO_SPACE_US      UIAPIR_NEC_UNIT_US
#define UIAPIR_NEC_ONE_SPACE_US       1690
#define UIAPIR_NEC_TRAILER_MARK_US    UIAPIR_NEC_UNIT_US
#define UIAPIR_NEC_BITS               32
#define UIAPIR_NEC_FRAME_PERIOD_US    110000UL

// Durations recorded for one frame: leader mark, leader space, 2 per bit,
// trailer mark.
#define UIAPIR_NEC_FRAME_DURATIONS    (2 + 2 * UIAPIR_NEC_BITS + 1)
#define UIAPIR_NEC_REPEAT_DURATIONS   3

// [SBP] A 16-bit address whose high byte is the bitwise complement of its low
// byte is not usable as an extended address: those 256 values are what a
// standard NEC frame puts on the wire, so an extended frame carrying one is
// indistinguishable from a standard one on reception.
#define UIAPIR_NEC_MAX_STANDARD_ADDRESS 0xff

// ---------------------------------------------------------------------------
// AEHA (Association for Electric Home Appliances)
// ---------------------------------------------------------------------------
// [ChaN] T = 350..500 us (425 typ.), fsc = 33..40 kHz (38 typ.) 1/3 duty,
//        variable-length frame (48 bit typ.) terminated by a trailer,
//        16-bit customer code + 4-bit parity + data, ~130 ms period.
//        Parity is the customer code XORed in 4-bit units.
#define UIAPIR_AEHA_CARRIER_KHZ       38
#define UIAPIR_AEHA_UNIT_US           425
#define UIAPIR_AEHA_LEADER_MARK_US    (8 * UIAPIR_AEHA_UNIT_US)   // 3400
#define UIAPIR_AEHA_LEADER_SPACE_US   (4 * UIAPIR_AEHA_UNIT_US)   // 1700
#define UIAPIR_AEHA_BIT_MARK_US       UIAPIR_AEHA_UNIT_US
#define UIAPIR_AEHA_ZERO_SPACE_US     UIAPIR_AEHA_UNIT_US
#define UIAPIR_AEHA_ONE_SPACE_US      (3 * UIAPIR_AEHA_UNIT_US)   // 1275
#define UIAPIR_AEHA_TRAILER_MARK_US   UIAPIR_AEHA_UNIT_US
#define UIAPIR_AEHA_FRAME_PERIOD_US   130000UL

// Structural minimum: 16-bit customer code + 4-bit parity + 4-bit data0.
// Anything shorter cannot be a conforming AEHA frame, so the decoder rejects
// it rather than reporting a spurious 1- or 2-byte payload.
#define UIAPIR_AEHA_MIN_BYTES         3

#define UIAPIR_AEHA_DURATIONS(bytes)  (3 + 16 * (bytes))

// ---------------------------------------------------------------------------
// Sony SIRC
// ---------------------------------------------------------------------------
// [ChaN] T = 600 us, fsc = 40 kHz 1/3 duty, 7-bit data with a 5/8/13-bit
//        address (12/15/20-bit frames), 45 ms period.
// A frame carries no trailer: the space after the final bit merges into the
// inter-frame gap. SIRC requires a frame to be repeated at least three times.
#define UIAPIR_SONY_CARRIER_KHZ       40
#define UIAPIR_SONY_UNIT_US           600UL
#define UIAPIR_SONY_LEADER_MARK_US    (4 * UIAPIR_SONY_UNIT_US)   // 2400
#define UIAPIR_SONY_LEADER_SPACE_US   UIAPIR_SONY_UNIT_US
#define UIAPIR_SONY_ZERO_MARK_US      UIAPIR_SONY_UNIT_US
#define UIAPIR_SONY_ONE_MARK_US       (2 * UIAPIR_SONY_UNIT_US)   // 1200
#define UIAPIR_SONY_BIT_SPACE_US      UIAPIR_SONY_UNIT_US
#define UIAPIR_SONY_COMMAND_BITS      7
#define UIAPIR_SONY_MAX_BITS          20
#define UIAPIR_SONY_MIN_FRAMES        3
#define UIAPIR_SONY_FRAME_PERIOD_US   45000UL

// 2 leader durations + one mark per bit + one space between bits.
#define UIAPIR_SONY_DURATIONS(bits)   (2 + 2 * (bits) - 1)

// Longest possible SIRC frame: 20 bits that are all ones.
#define UIAPIR_SONY_MAX_FRAME_US                                              \
    (UIAPIR_SONY_LEADER_MARK_US + UIAPIR_SONY_LEADER_SPACE_US +               \
     UIAPIR_SONY_MAX_BITS * UIAPIR_SONY_ONE_MARK_US +                         \
     (UIAPIR_SONY_MAX_BITS - 1) * UIAPIR_SONY_BIT_SPACE_US)

// ...which leaves the shortest inter-frame gap any supported protocol produces.
#define UIAPIR_SONY_MIN_GAP_US                                                \
    (UIAPIR_SONY_FRAME_PERIOD_US - UIAPIR_SONY_MAX_FRAME_US)

// ---------------------------------------------------------------------------
// Frame boundary detection
// ---------------------------------------------------------------------------
// The receiver ends a frame on a SPACE longer than this threshold. It must be
// longer than the longest space that occurs *inside* a frame (the NEC leader
// space, plus the decoder's own tolerance) and shorter than the shortest gap
// *between* frames (a 20-bit SIRC burst).
//
// Note the threshold has to be space-only: the NEC leader MARK is 9000 us,
// which is longer than the 6600 us SIRC gap, so no level-agnostic threshold
// exists at all.
#ifndef UIAPIR_FRAME_GAP_US
#define UIAPIR_FRAME_GAP_US           6000U
#endif

// Longest intra-frame space, widened by the decoder's +25% match tolerance.
#define UIAPIR_MAX_INTRA_FRAME_SPACE_US                                       \
    (UIAPIR_NEC_LEADER_SPACE_US + UIAPIR_NEC_LEADER_SPACE_US / 4)

#if UIAPIR_FRAME_GAP_US <= UIAPIR_MAX_INTRA_FRAME_SPACE_US
#error "UIAPIR_FRAME_GAP_US would split a NEC leader space and truncate frames"
#endif
#if UIAPIR_FRAME_GAP_US >= UIAPIR_SONY_MIN_GAP_US
#error "UIAPIR_FRAME_GAP_US would merge consecutive 20-bit Sony SIRC frames"
#endif

// Longest single duration the capture buffer has to represent (NEC leader mark).
#define UIAPIR_MAX_FRAME_DURATION_US  UIAPIR_NEC_LEADER_MARK_US

// ---------------------------------------------------------------------------
// Decoder tolerance
// ---------------------------------------------------------------------------
// How far a captured duration may sit from its nominal value and still match.
// Shared with UIAPIRProtocol.cpp so the compile-time checks in UIAPIRTypes.h
// test the same bands the decoder actually applies.
#ifndef UIAPIR_TOLERANCE_PERCENT
#define UIAPIR_TOLERANCE_PERCENT 25
#endif

// A duration in UIAPIR_RAW_TICK_US ticks, and the band matchTicks() accepts for
// it. These expand against whatever UIAPIR_RAW_TICK_US is in force at the point
// of use, so they are usable from UIAPIRTypes.h once that macro is settled.
#define UIAPIR_TICKS(us) (((us) + UIAPIR_RAW_TICK_US / 2) / UIAPIR_RAW_TICK_US)
#define UIAPIR_TICK_MARGIN(us) \
    ((UIAPIR_TICKS(us) * UIAPIR_TOLERANCE_PERCENT + 99) / 100 + 1)
#define UIAPIR_BAND_LOW(us) \
    (UIAPIR_TICKS(us) > UIAPIR_TICK_MARGIN(us) \
         ? UIAPIR_TICKS(us) - UIAPIR_TICK_MARGIN(us) \
         : 1)
#define UIAPIR_BAND_HIGH(us) (UIAPIR_TICKS(us) + UIAPIR_TICK_MARGIN(us))

// True when a shorter and a longer symbol cannot both match the same tick
// count. If they can, every bit of that protocol decodes as the shorter one.
#define UIAPIR_BANDS_DISJOINT(shortUs, longUs) \
    (UIAPIR_BAND_HIGH(shortUs) < UIAPIR_BAND_LOW(longUs))

// Shortest sequence of durations that can still be a valid frame (a NEC repeat).
#define UIAPIR_MIN_FRAME_DURATIONS    UIAPIR_NEC_REPEAT_DURATIONS

// Shortest gap the transmitter will ever leave between two frames, used when a
// frame runs longer than its own nominal period. It only has to be long enough
// for a receiver to close the frame on it, and the tightest gap any supported
// format produces is the one after a 20-bit SIRC frame, so that is the floor.
// Anything larger would stretch that very frame's 45 ms period.
#define UIAPIR_MIN_TX_GAP_US          UIAPIR_SONY_MIN_GAP_US

#if UIAPIR_MIN_TX_GAP_US <= UIAPIR_FRAME_GAP_US
#error "UIAPIR_MIN_TX_GAP_US is too short for the receiver to end a frame on"
#endif
