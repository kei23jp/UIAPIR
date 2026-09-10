// SPDX-License-Identifier: MIT
// Copyright (c) 2026 kei23jp

#include "UIAPIRProtocol.h"

namespace uiapir_protocol {

bool matchTicks(uint8_t actualTicks, uint16_t targetUs) {
    // Kept in step with the UIAPIR_BAND_* macros the build-time checks use.
    const uint16_t targetTicks = UIAPIR_TICKS(targetUs);
    const uint16_t margin = UIAPIR_TICK_MARGIN(targetUs);
    const uint16_t low = targetTicks > margin ? targetTicks - margin : 1;
    const uint16_t high = targetTicks + margin;
    return actualTicks >= low && actualTicks <= high;
}

#if UIAPIR_ENABLE_NEC || UIAPIR_ENABLE_AEHA
static bool readSpaceBit(uint8_t space, uint16_t zeroUs, uint16_t oneUs, bool &bit) {
    if (matchTicks(space, zeroUs)) {
        bit = false;
        return true;
    }
    if (matchTicks(space, oneUs)) {
        bit = true;
        return true;
    }
    return false;
}

#endif // UIAPIR_ENABLE_NEC || UIAPIR_ENABLE_AEHA

#if UIAPIR_ENABLE_NEC
bool necAddressIsExtendable(uint16_t address) {
    const uint8_t low = (uint8_t)address;
    const uint8_t high = (uint8_t)(address >> 8);
    return (uint8_t)(low ^ high) != 0xff;
}

#endif // UIAPIR_ENABLE_NEC

#if UIAPIR_ENABLE_SONY
uint8_t sonyAddressBits(uint8_t bits) {
    if (bits != 12 && bits != 15 && bits != 20) {
        return 0;
    }
    return (uint8_t)(bits - UIAPIR_SONY_COMMAND_BITS);
}

#endif // UIAPIR_ENABLE_SONY

#if UIAPIR_ENABLE_AEHA
uint8_t aehaParity(const uint8_t *bytes) {
    const uint8_t folded = (uint8_t)(bytes[0] ^ bytes[1]);
    return (uint8_t)((folded ^ (folded >> 4)) & 0x0f);
}

bool aehaParityValid(const uint8_t *bytes, uint8_t length) {
    if (!bytes || length < UIAPIR_AEHA_MIN_BYTES) {
        return false;
    }
    return (uint8_t)(bytes[2] & 0x0f) == aehaParity(bytes);
}

#endif // UIAPIR_ENABLE_AEHA

uint8_t framesFromRepeats(uint8_t repeats, uint8_t minimumFrames) {
    const uint16_t frames = (uint16_t)repeats + 1U;
    if (frames < minimumFrames) {
        return minimumFrames;
    }
    return frames > 255U ? (uint8_t)255U : (uint8_t)frames;
}

bool rawIsReplayable(uint8_t flags) {
    return (flags & (UIAPIR_FLAG_RAW_OVERFLOW | UIAPIR_FLAG_TIMING_CLIPPED)) == 0;
}

#if UIAPIR_ENABLE_NEC
bool decodeNEC(const uint8_t *ticks, uint16_t count, IRNECData &out, bool &repeat) {
    repeat = false;
    if (!ticks || count < UIAPIR_NEC_REPEAT_DURATIONS ||
        !matchTicks(ticks[0], UIAPIR_NEC_LEADER_MARK_US)) {
        return false;
    }

    if (count == UIAPIR_NEC_REPEAT_DURATIONS &&
        matchTicks(ticks[1], UIAPIR_NEC_REPEAT_SPACE_US) &&
        matchTicks(ticks[2], UIAPIR_NEC_TRAILER_MARK_US)) {
        out.address = 0;
        out.command = 0;
        out.addressBits = 0;
        repeat = true;
        return true;
    }

    if (count != UIAPIR_NEC_FRAME_DURATIONS ||
        !matchTicks(ticks[1], UIAPIR_NEC_LEADER_SPACE_US) ||
        !matchTicks(ticks[UIAPIR_NEC_FRAME_DURATIONS - 1], UIAPIR_NEC_TRAILER_MARK_US)) {
        return false;
    }

    uint32_t value = 0;
    uint16_t index = 2;
    for (uint8_t bitIndex = 0; bitIndex < UIAPIR_NEC_BITS; ++bitIndex, index += 2) {
        if (!matchTicks(ticks[index], UIAPIR_NEC_BIT_MARK_US)) {
            return false;
        }
        bool bit = false;
        if (!readSpaceBit(ticks[index + 1], UIAPIR_NEC_ZERO_SPACE_US,
                          UIAPIR_NEC_ONE_SPACE_US, bit)) {
            return false;
        }
        if (bit) {
            value |= (uint32_t)1 << bitIndex;
        }
    }

    const uint8_t addressLow = value & 0xff;
    const uint8_t addressHigh = (value >> 8) & 0xff;
    const uint8_t command = (value >> 16) & 0xff;
    const uint8_t invertedCommand = (value >> 24) & 0xff;
    if ((uint8_t)(command ^ invertedCommand) != 0xff) {
        return false;
    }

    const uint16_t address = (uint16_t)addressLow | ((uint16_t)addressHigh << 8);
    if (necAddressIsExtendable(address)) {
        out.address = address;
        out.addressBits = 16;
    } else {
        out.address = addressLow;
        out.addressBits = 8;
    }
    out.command = command;
    return true;
}

#endif // UIAPIR_ENABLE_NEC

#if UIAPIR_ENABLE_AEHA
bool decodeAEHA(const uint8_t *ticks, uint16_t count, IRAEHAData &out) {
    if (!ticks || count < UIAPIR_AEHA_DURATIONS(UIAPIR_AEHA_MIN_BYTES) ||
        !matchTicks(ticks[0], UIAPIR_AEHA_LEADER_MARK_US) ||
        !matchTicks(ticks[1], UIAPIR_AEHA_LEADER_SPACE_US) ||
        !matchTicks(ticks[count - 1], UIAPIR_AEHA_TRAILER_MARK_US)) {
        return false;
    }

    const uint16_t encodedDurations = count - 3;
    if ((encodedDurations % 16) != 0) {
        return false;
    }
    const uint16_t byteCount = encodedDurations / 16;
    if (byteCount < UIAPIR_AEHA_MIN_BYTES || byteCount > UIAPIR_MAX_AEHA_BYTES) {
        return false;
    }

    uint8_t decoded[UIAPIR_MAX_AEHA_BYTES] = {0};
    uint16_t index = 2;
    for (uint16_t byteIndex = 0; byteIndex < byteCount; ++byteIndex) {
        for (uint8_t bitIndex = 0; bitIndex < 8; ++bitIndex, index += 2) {
            if (!matchTicks(ticks[index], UIAPIR_AEHA_BIT_MARK_US)) {
                return false;
            }
            bool bit = false;
            if (!readSpaceBit(ticks[index + 1], UIAPIR_AEHA_ZERO_SPACE_US,
                              UIAPIR_AEHA_ONE_SPACE_US, bit)) {
                return false;
            }
            if (bit) {
                decoded[byteIndex] |= (uint8_t)(1U << bitIndex);
            }
        }
    }

    out.length = (uint8_t)byteCount;
    memcpy(out.bytes, decoded, byteCount);
    return true;
}

#endif // UIAPIR_ENABLE_AEHA

#if UIAPIR_ENABLE_SONY
bool decodeSony(const uint8_t *ticks, uint16_t count, IRSonyData &out) {
    uint8_t bits = 0;
    if (count == UIAPIR_SONY_DURATIONS(12)) bits = 12;
    else if (count == UIAPIR_SONY_DURATIONS(15)) bits = 15;
    else if (count == UIAPIR_SONY_DURATIONS(20)) bits = 20;
    else return false;

    if (!ticks || !matchTicks(ticks[0], UIAPIR_SONY_LEADER_MARK_US) ||
        !matchTicks(ticks[1], UIAPIR_SONY_LEADER_SPACE_US)) {
        return false;
    }

    uint32_t value = 0;
    uint16_t index = 2;
    for (uint8_t bitIndex = 0; bitIndex < bits; ++bitIndex) {
        bool bit;
        if (matchTicks(ticks[index], UIAPIR_SONY_ZERO_MARK_US)) bit = false;
        else if (matchTicks(ticks[index], UIAPIR_SONY_ONE_MARK_US)) bit = true;
        else return false;

        if (bit) {
            value |= (uint32_t)1 << bitIndex;
        }
        ++index;
        if (bitIndex + 1 < bits) {
            if (!matchTicks(ticks[index], UIAPIR_SONY_BIT_SPACE_US)) {
                return false;
            }
            ++index;
        }
    }

    const uint8_t addressBits = sonyAddressBits(bits);
    out.command = value & ((1U << UIAPIR_SONY_COMMAND_BITS) - 1U);
    out.address = (value >> UIAPIR_SONY_COMMAND_BITS) & ((1U << addressBits) - 1U);
    out.bits = bits;
    return true;
}

#endif // UIAPIR_ENABLE_SONY

} // namespace uiapir_protocol
