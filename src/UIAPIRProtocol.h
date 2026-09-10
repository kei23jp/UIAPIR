// SPDX-License-Identifier: MIT
// Copyright (c) 2026 kei23jp

#pragma once

#include "UIAPIRTypes.h"

namespace uiapir_protocol {

bool matchTicks(uint8_t actualTicks, uint16_t targetUs);

// Everything below is per-protocol and disappears with its
// UIAPIR_ENABLE_<name> switch. See UIAPIRProtocolDefs.h.

#if UIAPIR_ENABLE_NEC
bool decodeNEC(const uint8_t *ticks, uint16_t count, IRNECData &out, bool &repeat);

// True when a 16-bit value may be sent as an extended NEC address. The 256
// values whose high byte is the complement of the low byte are reserved for
// standard NEC and would be decoded back as an 8-bit address.
bool necAddressIsExtendable(uint16_t address);
#endif

#if UIAPIR_ENABLE_AEHA
bool decodeAEHA(const uint8_t *ticks, uint16_t count, IRAEHAData &out);

// AEHA parity: the 16-bit customer code XORed in 4-bit units. Returns the
// expected low nibble of bytes[2]. `bytes` must hold at least
// UIAPIR_AEHA_MIN_BYTES entries.
uint8_t aehaParity(const uint8_t *bytes);
bool aehaParityValid(const uint8_t *bytes, uint8_t length);
#endif

#if UIAPIR_ENABLE_SONY
bool decodeSony(const uint8_t *ticks, uint16_t count, IRSonyData &out);

// Width of the SIRC address field for a 12, 15, or 20-bit frame (5, 8, or 13
// bits). Returns 0 for an unsupported frame length.
uint8_t sonyAddressBits(uint8_t bits);
#endif

// Total frames to transmit for a `repeats` count. `repeats` always means extra
// transmissions beyond the first, so the total is repeats + 1, raised to
// `minimumFrames` and saturated rather than wrapped.
uint8_t framesFromRepeats(uint8_t repeats, uint8_t minimumFrames);

// Whether a captured RAW frame still carries every timing it was sent with.
// A capture that overflowed the buffer or had a duration clipped does not, and
// replaying it would put a different signal on the air.
bool rawIsReplayable(uint8_t flags);

} // namespace uiapir_protocol
