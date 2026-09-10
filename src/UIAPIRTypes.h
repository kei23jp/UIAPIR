// SPDX-License-Identifier: MIT
// Copyright (c) 2026 kei23jp

#pragma once

#include <stdint.h>
#include <string.h>

#include "UIAPIRProtocolDefs.h"

// Keep the whole SIRC frame calculation in 32-bit arithmetic. On 16-bit AVR,
// an unsuffixed unit makes the intermediate 20-bit all-ones frame overflow.
static_assert(UIAPIR_SONY_MIN_GAP_US == 6600UL,
              "Sony SIRC timing arithmetic must remain 32-bit");

#ifndef UIAPIR_RAW_BUFFER_SIZE
#define UIAPIR_RAW_BUFFER_SIZE 340
#endif

#ifndef UIAPIR_RAW_TICK_US
#define UIAPIR_RAW_TICK_US 50
#endif

#ifndef UIAPIR_MAX_AEHA_BYTES
#define UIAPIR_MAX_AEHA_BYTES 20
#endif

// A captured duration is stored as a uint8_t count of UIAPIR_RAW_TICK_US, so
// the longest duration a frame can contain has to stay under 255 ticks.
#if (UIAPIR_MAX_FRAME_DURATION_US / UIAPIR_RAW_TICK_US) > 255
#error "UIAPIR_RAW_TICK_US is too small: a NEC leader mark no longer fits in uint8_t"
#endif

// The tick also has an upper bound, for two independent reasons.
//
// First, rawMicros() and sendRawTicks() convert a tick count back to
// microseconds in a uint16_t, which a full 255 ticks must not overflow.
#if (255 * UIAPIR_RAW_TICK_US) > 65535
#error "UIAPIR_RAW_TICK_US is too large: 255 ticks overflows the uint16_t microsecond conversion"
#endif

// Second, quantising to a coarse tick widens every acceptance band until a
// short symbol and a long one both match the same count, at which point every
// bit of that protocol silently decodes as the short one.
// Only a protocol that is actually built has to stay decodable, so a build
// that switches one off is free to use a tick this coarse.
#if UIAPIR_ENABLE_NEC
#if !UIAPIR_BANDS_DISJOINT(UIAPIR_NEC_ZERO_SPACE_US, UIAPIR_NEC_ONE_SPACE_US)
#error "UIAPIR_RAW_TICK_US is too large: NEC 0 and 1 bit spaces are no longer distinguishable"
#endif
#endif
#if UIAPIR_ENABLE_AEHA
#if !UIAPIR_BANDS_DISJOINT(UIAPIR_AEHA_ZERO_SPACE_US, UIAPIR_AEHA_ONE_SPACE_US)
#error "UIAPIR_RAW_TICK_US is too large: AEHA 0 and 1 bit spaces are no longer distinguishable"
#endif
#endif
#if UIAPIR_ENABLE_SONY
#if !UIAPIR_BANDS_DISJOINT(UIAPIR_SONY_ZERO_MARK_US, UIAPIR_SONY_ONE_MARK_US)
#error "UIAPIR_RAW_TICK_US is too large: Sony SIRC 0 and 1 bit marks are no longer distinguishable"
#endif
#endif

// The RAW buffer is what every frame is decoded from, so it has to hold the
// longest frame each enabled protocol can produce. AEHA is normally the
// binding one by a wide margin - 20 bytes needs 323 durations against NEC's 67
// - which is why switching AEHA off is what makes a smaller buffer possible.
#if UIAPIR_ENABLE_AEHA
#if UIAPIR_AEHA_DURATIONS(UIAPIR_MAX_AEHA_BYTES) > UIAPIR_RAW_BUFFER_SIZE
#error "UIAPIR_RAW_BUFFER_SIZE cannot hold a UIAPIR_MAX_AEHA_BYTES AEHA frame"
#endif
#if UIAPIR_MAX_AEHA_BYTES < UIAPIR_AEHA_MIN_BYTES
#error "UIAPIR_MAX_AEHA_BYTES is below the AEHA structural minimum"
#endif
#endif

#if UIAPIR_ENABLE_NEC
#if UIAPIR_NEC_FRAME_DURATIONS > UIAPIR_RAW_BUFFER_SIZE
#error "UIAPIR_RAW_BUFFER_SIZE cannot hold a NEC frame"
#endif
#endif

#if UIAPIR_ENABLE_SONY
#if UIAPIR_SONY_DURATIONS(UIAPIR_SONY_MAX_BITS) > UIAPIR_RAW_BUFFER_SIZE
#error "UIAPIR_RAW_BUFFER_SIZE cannot hold a 20-bit Sony SIRC frame"
#endif
#endif

// A RAW capture still has to be long enough to be a frame at all, whatever is
// switched off.
#if UIAPIR_RAW_BUFFER_SIZE < UIAPIR_MIN_FRAME_DURATIONS
#error "UIAPIR_RAW_BUFFER_SIZE is below the shortest frame the capture accepts"
#endif

enum IRProtocol : uint8_t {
    UIAPIR_UNKNOWN = 0,
    UIAPIR_NEC,
    UIAPIR_AEHA,
    UIAPIR_SONY,
    UIAPIR_RAW
};

enum IRCodeFlags : uint8_t {
    UIAPIR_FLAG_NONE = 0,
    UIAPIR_FLAG_REPEAT = 1 << 0,
    UIAPIR_FLAG_RAW_OVERFLOW = 1 << 1,
    UIAPIR_FLAG_TIMING_CLIPPED = 1 << 2
};

struct IRNECData {
    uint16_t address;
    uint8_t command;
    uint8_t addressBits; // 8 for standard NEC, 16 for extended NEC.
};

struct IRAEHAData {
    uint8_t length; // UIAPIR_AEHA_MIN_BYTES .. UIAPIR_MAX_AEHA_BYTES.
    uint8_t bytes[UIAPIR_MAX_AEHA_BYTES];
};

struct IRSonyData {
    // 5-bit device for a 12-bit frame, 8-bit device for a 15-bit frame, and
    // for a 20-bit frame the 13-bit field is the 5-bit device in bits 0..4
    // plus the 8-bit extended field in bits 5..12.
    uint16_t address;
    uint8_t command; // 7 bits.
    uint8_t bits;    // 12, 15, or 20.
};

struct IRRawData {
    uint16_t count;
    uint8_t ticks[UIAPIR_RAW_BUFFER_SIZE];
};

union IRPayload {
    IRNECData nec;
    IRAEHAData aeha;
    IRSonyData sony;
    IRRawData raw;
};

struct IRCode {
    IRProtocol protocol;
    uint8_t flags;
    uint8_t carrierKHz;
    IRPayload data;

    void clear() {
        memset(this, 0, sizeof(*this));
        protocol = UIAPIR_UNKNOWN;
    }

    uint16_t rawMicros(uint16_t index) const {
        if (protocol != UIAPIR_RAW || index >= data.raw.count) {
            return 0;
        }
        return (uint16_t)data.raw.ticks[index] * UIAPIR_RAW_TICK_US;
    }
};
