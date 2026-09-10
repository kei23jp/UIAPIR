// SPDX-License-Identifier: MIT
// Copyright (c) 2026 kei23jp

// HT6 - the one description of the two-board test that both firmwares share.
//
// The transmitter and the receiver have to agree on the wiring, on the
// handshake, and on every test case, and neither board can ask the other what
// it thinks. Anything both sides need therefore lives here and nowhere else,
// so a case cannot be changed on one board only.
//
// Flash both boards from the same source tree. The case index travels over the
// START line as a pulse count, but the meaning of that index comes from this
// table: two boards built from different revisions of it will run different
// tests under the same name.

#pragma once

#include <stdint.h>

#include "UIAPIRTypes.h"

// ----------------------------------------------------------------- wiring ---
//
// Arduino pin numbers. The V1.4 silk screen prints an analog name for some
// pads, so both names are given; the firmware always uses the number.
//
//   role                        Arduino   silk   port
//   IR carrier out (transmitter)      6     A2    PC4 / TIM1_CH4
//   demodulator in (receiver)         3      3    PC1 / EXTI1
//   START,  receiver -> transmitter   8      8    PC6
//   STATUS, transmitter -> receiver   9      9    PC7
//   serial out (receiver)            15     15    PD5 / USART1_TX
//
// START and STATUS cross over: the same pin number is an output on one board
// and an input on the other. Put 1 kOhm in series with each line so a firmware
// that drives the wrong direction cannot damage either part, and tie the two
// grounds together - these are electrical signals, unlike the IR path.
#define HT6_IR_TX_PIN 6
#define HT6_IR_RX_PIN 3
#define HT6_START_PIN 8
#define HT6_STATUS_PIN 9
#define HT6_SERIAL_BAUD 115200

// The pins above are Arduino numbers, which only mean anything through the
// core's variant table. Both firmwares check these at startup, because a core
// whose CH32V003F4 variant orders its pins differently would silently move
// every signal in this test to a different pad.
#define HT6_EXPECT_IR_TX_PORT PC_4
#define HT6_EXPECT_IR_RX_PORT PC_1
#define HT6_EXPECT_START_PORT PC_6
#define HT6_EXPECT_STATUS_PORT PC_7

// -------------------------------------------------------------- handshake ---
//
// STATUS is the transmitter's line: HIGH means idle, LOW means a case is in
// flight. START is the receiver's line, and carries the case index as
// index + 1 pulses. Addressing each case rather than relying on a shared
// counter means either board can be reset mid-run without the two drifting
// into running different cases under the same number.
//
// A case runs like this:
//
//   receiver waits for STATUS to be HIGH and stay HIGH        (idle_stable)
//   receiver pulses START index + 1 times                     (pulse)
//   transmitter pulls STATUS LOW on the first pulse           (ack)
//   transmitter counts the rest, then waits for the train to end (train_end)
//   transmitter waits, then transmits                         (pre_send)
//   receiver decodes and validates every frame as it arrives
//   transmitter releases STATUS HIGH, or blinks it first      (api_false)
//   receiver reads the blink as "the send API returned false" (result_window)
//
// The blink is how the API's return value crosses the gap: nothing about a
// rejected argument is visible in the IR path, because a rejected call emits
// nothing at all - which looks exactly like a transmitter that never woke up.
#define HT6_PULSE_US 2000UL
#define HT6_TRAIN_END_MS 12UL
#define HT6_PRE_SEND_MS 20UL
#define HT6_IDLE_STABLE_MS 100UL
#define HT6_READY_TIMEOUT_MS 3000UL
#define HT6_ACK_TIMEOUT_MS 250UL

// Longest case is NEC with two repeats, 232 ms. The timeout only has to catch
// a transmitter that has stopped making progress at all.
#define HT6_CASE_TIMEOUT_MS 1500UL

// Four blinks span 320 ms, so a 200 ms window always contains at least one of
// them, and a transmitter that simply went idle never shows a LOW in it.
#define HT6_API_FALSE_BLINKS 4
#define HT6_API_FALSE_BLINK_MS 40UL
#define HT6_RESULT_WINDOW_MS 200UL

// ------------------------------------------------------------- test cases ---

#define HT6_AEHA_LENGTH 6

enum HT6Kind : uint8_t {
    HT6_KIND_NEC = 0,
    HT6_KIND_AEHA,
    HT6_KIND_SONY,
    HT6_KIND_RAW,      // sendRawTicks() directly
    HT6_KIND_RAW_CODE  // send(IRCode) down the RAW path
};

struct HT6Case {
    uint8_t kind;
    uint16_t address; // NEC address, SIRC address, otherwise unused
    uint8_t command;  // NEC command, SIRC command, otherwise unused
    // NEC: address bits, 8 or 16, which also selects sendNEC()'s `extended`
    // AEHA: byte count. SIRC: frame bits. RAW_CODE: flags to stamp on the code
    uint8_t param;
    // NEC: sendNEC()'s `repeats`. AEHA and SIRC: the `frames` argument.
    uint8_t count;
    uint8_t expectFrames; // frames the receiver must decode, 0 for a rejection
    uint8_t expectApiOk;  // what the send call must return
};

static const HT6Case HT6_CASES[] = {
    // Accepted transmissions.
    {HT6_KIND_NEC, 0x12, 0x34, 8, 0, 1, 1},
    {HT6_KIND_NEC, 0xfffe, 0xff, 16, 0, 1, 1},
    {HT6_KIND_NEC, 0x12, 0x34, 8, 2, 3, 1},
    {HT6_KIND_AEHA, 0, 0, HT6_AEHA_LENGTH, 2, 2, 1},
    {HT6_KIND_SONY, 1, 19, 12, 3, 3, 1},
    {HT6_KIND_SONY, 0x5a, 0x40, 15, 3, 3, 1},
    // Every bit set makes the longest SIRC frame there is, 38.4 ms, which
    // leaves the 6.6 ms gap the receive threshold was chosen against.
    {HT6_KIND_SONY, 0x1fff, 0x7f, 20, 3, 3, 1},
    {HT6_KIND_RAW, 0, 0, 0, 0, 1, 1},
    {HT6_KIND_RAW_CODE, 0, 0, UIAPIR_FLAG_NONE, 0, 1, 1},

    // Rejections. Each has to emit nothing and return false; a transmitter
    // that emits nothing because it crashed is told apart by the STATUS line.
    {HT6_KIND_NEC, 0x100, 0x34, 8, 0, 0, 0},
    // Extended address whose bytes are complements is reserved for standard
    // NEC: sent as extended it would decode back as 8-bit address 0x12.
    {HT6_KIND_NEC, 0xed12, 0x34, 16, 0, 0, 0},
    {HT6_KIND_AEHA, 0, 0, UIAPIR_AEHA_MIN_BYTES - 1, 1, 0, 0},
    {HT6_KIND_AEHA, 0, 0, UIAPIR_MAX_AEHA_BYTES + 1, 1, 0, 0},
    {HT6_KIND_SONY, 1, 19, 13, 3, 0, 0},
    {HT6_KIND_SONY, 1, 0x80, 12, 3, 0, 0},
    {HT6_KIND_SONY, 1, 19, 12, UIAPIR_SONY_MIN_FRAMES - 1, 0, 0},
    // A capture that lost timing must not be replayed as if it were intact.
    {HT6_KIND_RAW_CODE, 0, 0, UIAPIR_FLAG_TIMING_CLIPPED, 0, 0, 0}
};

#define HT6_CASE_COUNT ((uint8_t)(sizeof(HT6_CASES) / sizeof(HT6_CASES[0])))

// Only the receiver has a serial port, so only the receiver pays for the
// names. It defines HT6_WITH_CASE_NAMES before including this header.
#if defined(HT6_WITH_CASE_NAMES)
static const char *const HT6_CASE_NAMES[] = {
    "NEC standard",
    "NEC extended",
    "NEC + 2 repeats",
    "AEHA 6 byte x2",
    "SIRC 12 bit x3",
    "SIRC 15 bit x3",
    "SIRC 20 bit x3",
    "RAW ticks",
    "RAW via send()",
    "reject NEC addr > 255",
    "reject NEC complement",
    "reject AEHA too short",
    "reject AEHA too long",
    "reject SIRC 13 bit",
    "reject SIRC cmd 0x80",
    "reject SIRC 2 frames",
    "reject RAW clipped"};

// The bound is deliberately left off above. Writing [HT6_CASE_COUNT] would
// make this check a tautology, and worse: a missing name would then be a null
// element rather than a short array, because C++ zero-initialises the elements
// with no initialiser. The case would print through that null pointer instead
// of failing to build. Adding a case is a two-step edit, so make the second
// step compulsory.
static_assert(sizeof(HT6_CASE_NAMES) / sizeof(HT6_CASE_NAMES[0]) == HT6_CASE_COUNT,
              "HT6_CASE_NAMES and HT6_CASES are different lengths");
#endif

// Customer code 0x1234 with the matching 4-bit parity in bytes[2], so this is
// a legal AEHA frame rather than an arbitrary bit pattern. The array is as
// long as the longest length any case passes to sendAEHA(), so the
// over-length rejection cannot read past the end regardless of the order
// sendAEHA() happens to run its argument checks in.
static const uint8_t HT6_AEHA_PAYLOAD[UIAPIR_MAX_AEHA_BYTES + 1] = {0x34, 0x12, 0x54,
                                                                    0x9a, 0xbc, 0xde};

// Nine equal 1 ms slots: mark, space, ..., mark. No decoder can claim it. The
// first mark matches no leader, and nine durations match no SIRC frame length,
// so it comes back as RAW - which is the point, since RAW is the only path
// that carries a signal the library does not understand.
#define HT6_RAW_SLOT_TICKS ((uint8_t)(1000U / UIAPIR_RAW_TICK_US))
#define HT6_RAW_COUNT 9

static const uint8_t HT6_RAW_TICKS[HT6_RAW_COUNT] = {
    HT6_RAW_SLOT_TICKS, HT6_RAW_SLOT_TICKS, HT6_RAW_SLOT_TICKS,
    HT6_RAW_SLOT_TICKS, HT6_RAW_SLOT_TICKS, HT6_RAW_SLOT_TICKS,
    HT6_RAW_SLOT_TICKS, HT6_RAW_SLOT_TICKS, HT6_RAW_SLOT_TICKS};

#define HT6_RAW_SLOT_US 1000U

// The receiver judges each slot with matchTicks(), the same acceptance band
// the protocol decoders use.
//
// An earlier revision demanded the slot come back within one tick of nominal,
// on the reasoning that the receiver quantises with the same 50 us tick and
// only the edge interrupt's latency is left over. That reasoning left out the
// demodulator, which is the larger error source and the whole reason HT5
// exists: an IR receiver module's AGC stretches marks and eats into spaces by
// far more than 50 us. On hardware the two RAW cases failed every run against
// a +/-1 tick band while every protocol case passed, because the decoders were
// already being judged by a band six times wider.
//
// Deferring to matchTicks() also states what the test is actually for: a RAW
// capture is good if replaying it would land inside the tolerance the library
// itself accepts.
