// SPDX-License-Identifier: MIT
// Copyright (c) 2026 kei23jp

#include <assert.h>
#include <stdint.h>
#include <vector>
#include "../src/UIAPIRProtocol.h"

// The frame builders below mirror UIAPIR.cpp's transmitter. Both sides read the
// same macros from UIAPIRProtocolDefs.h, so a timing can no longer drift
// between the send path, the decode path and these tests.

static uint8_t tick(uint16_t us) {
    return (uint8_t)((us + UIAPIR_RAW_TICK_US / 2) / UIAPIR_RAW_TICK_US);
}

static std::vector<uint8_t> makeNEC(uint16_t address, uint8_t command, bool extended) {
    uint32_t value;
    if (extended) value = address;
    else value = (uint8_t)address | ((uint32_t)(uint8_t)~address << 8);
    value |= (uint32_t)command << 16;
    value |= (uint32_t)(uint8_t)~command << 24;
    std::vector<uint8_t> raw = {tick(UIAPIR_NEC_LEADER_MARK_US),
                                tick(UIAPIR_NEC_LEADER_SPACE_US)};
    for (uint8_t i = 0; i < UIAPIR_NEC_BITS; ++i) {
        raw.push_back(tick(UIAPIR_NEC_BIT_MARK_US));
        raw.push_back(tick((value >> i) & 1 ? UIAPIR_NEC_ONE_SPACE_US
                                            : UIAPIR_NEC_ZERO_SPACE_US));
    }
    raw.push_back(tick(UIAPIR_NEC_TRAILER_MARK_US));
    return raw;
}

static std::vector<uint8_t> makeAEHA(const uint8_t *data, uint8_t length) {
    std::vector<uint8_t> raw = {tick(UIAPIR_AEHA_LEADER_MARK_US),
                                tick(UIAPIR_AEHA_LEADER_SPACE_US)};
    for (uint8_t n = 0; n < length; ++n) {
        for (uint8_t i = 0; i < 8; ++i) {
            raw.push_back(tick(UIAPIR_AEHA_BIT_MARK_US));
            raw.push_back(tick((data[n] >> i) & 1 ? UIAPIR_AEHA_ONE_SPACE_US
                                                  : UIAPIR_AEHA_ZERO_SPACE_US));
        }
    }
    raw.push_back(tick(UIAPIR_AEHA_TRAILER_MARK_US));
    return raw;
}

static std::vector<uint8_t> makeSony(uint16_t address, uint8_t command, uint8_t bits) {
    uint32_t value = ((uint32_t)address << UIAPIR_SONY_COMMAND_BITS) | command;
    std::vector<uint8_t> raw = {tick(UIAPIR_SONY_LEADER_MARK_US),
                                tick(UIAPIR_SONY_LEADER_SPACE_US)};
    for (uint8_t i = 0; i < bits; ++i) {
        raw.push_back(tick((value >> i) & 1 ? UIAPIR_SONY_ONE_MARK_US
                                            : UIAPIR_SONY_ZERO_MARK_US));
        if (i + 1 < bits) raw.push_back(tick(UIAPIR_SONY_BIT_SPACE_US));
    }
    return raw;
}

// Longest and shortest a frame can be, in microseconds, as the transmitter
// emits it. Used to prove the frame gap threshold sits in a workable window.
static uint32_t sonyFrameUs(uint8_t bits, bool allOnes) {
    return UIAPIR_SONY_LEADER_MARK_US + UIAPIR_SONY_LEADER_SPACE_US +
           (uint32_t)bits * (allOnes ? UIAPIR_SONY_ONE_MARK_US : UIAPIR_SONY_ZERO_MARK_US) +
           (uint32_t)(bits - 1) * UIAPIR_SONY_BIT_SPACE_US;
}

// The compile-time guards in UIAPIRTypes.h reason about acceptance bands with
// macros. Those have to describe exactly what matchTicks() does at runtime, or
// a build that passes the guards can still mis-decode.
static uint32_t bandLow(uint32_t us) { return UIAPIR_BAND_LOW(us); }
static uint32_t bandHigh(uint32_t us) { return UIAPIR_BAND_HIGH(us); }

static void testBandMacrosMatchTheDecoder() {
    const uint16_t targets[] = {
        UIAPIR_NEC_ZERO_SPACE_US,  UIAPIR_NEC_ONE_SPACE_US,
        UIAPIR_NEC_LEADER_MARK_US, UIAPIR_NEC_LEADER_SPACE_US,
        UIAPIR_AEHA_ZERO_SPACE_US, UIAPIR_AEHA_ONE_SPACE_US,
        UIAPIR_AEHA_LEADER_MARK_US, UIAPIR_AEHA_LEADER_SPACE_US,
        UIAPIR_SONY_ZERO_MARK_US,  UIAPIR_SONY_ONE_MARK_US,
        UIAPIR_SONY_LEADER_MARK_US,
    };
    for (uint16_t target : targets) {
        for (unsigned t = 1; t <= 255; ++t) {
            const bool byMacro = t >= bandLow(target) && t <= bandHigh(target);
            assert(byMacro == uiapir_protocol::matchTicks((uint8_t)t, target));
        }
    }

    // ...and the disjointness the guards require actually holds here.
    assert(UIAPIR_BANDS_DISJOINT(UIAPIR_NEC_ZERO_SPACE_US, UIAPIR_NEC_ONE_SPACE_US));
    assert(UIAPIR_BANDS_DISJOINT(UIAPIR_AEHA_ZERO_SPACE_US, UIAPIR_AEHA_ONE_SPACE_US));
    assert(UIAPIR_BANDS_DISJOINT(UIAPIR_SONY_ZERO_MARK_US, UIAPIR_SONY_ONE_MARK_US));

    // A full tick count must survive the conversion back to microseconds.
    assert(255U * UIAPIR_RAW_TICK_US <= 65535U);
}

// `repeats` counts extra transmissions, so every protocol sends repeats + 1
// frames. SIRC additionally never sends fewer than three.
static void testFramesFromRepeats() {
    using uiapir_protocol::framesFromRepeats;
    for (unsigned r = 0; r <= 255; ++r) {
        const uint8_t repeats = (uint8_t)r;
        const uint8_t plain = framesFromRepeats(repeats, 1);
        assert(plain == (r == 255 ? 255 : (uint8_t)(r + 1)));

        const uint8_t sirc = framesFromRepeats(repeats, UIAPIR_SONY_MIN_FRAMES);
        assert(sirc >= UIAPIR_SONY_MIN_FRAMES);
        // Above the SIRC floor the two must agree: the same `repeats` has to
        // mean the same number of transmissions whatever the protocol.
        assert(sirc == (plain >= UIAPIR_SONY_MIN_FRAMES ? plain
                                                       : UIAPIR_SONY_MIN_FRAMES));
    }
    assert(framesFromRepeats(0, UIAPIR_SONY_MIN_FRAMES) == UIAPIR_SONY_MIN_FRAMES);
    assert(framesFromRepeats(3, UIAPIR_SONY_MIN_FRAMES) == 4); // was 3: one short
    assert(framesFromRepeats(255, 1) == 255);                  // saturates
}

// A capture missing timings must not be replayed as if it were intact.
static void testRawReplayGating() {
    using uiapir_protocol::rawIsReplayable;
    assert(rawIsReplayable(UIAPIR_FLAG_NONE));
    assert(rawIsReplayable(UIAPIR_FLAG_REPEAT));
    assert(!rawIsReplayable(UIAPIR_FLAG_RAW_OVERFLOW));
    assert(!rawIsReplayable(UIAPIR_FLAG_TIMING_CLIPPED));
    assert(!rawIsReplayable(UIAPIR_FLAG_RAW_OVERFLOW | UIAPIR_FLAG_TIMING_CLIPPED));
}

static void testFrameLengths() {
    assert(makeNEC(0x12, 0x34, false).size() == UIAPIR_NEC_FRAME_DURATIONS);
    assert(makeAEHA((const uint8_t *)"\x01\x02\x03", 3).size() ==
           (size_t)UIAPIR_AEHA_DURATIONS(3));
    assert(makeSony(1, 19, 12).size() == (size_t)UIAPIR_SONY_DURATIONS(12));
    assert(makeSony(1, 19, 15).size() == (size_t)UIAPIR_SONY_DURATIONS(15));
    assert(makeSony(1, 19, 20).size() == (size_t)UIAPIR_SONY_DURATIONS(20));

    // The 20-byte AEHA frame the library advertises has to fit the RAW buffer.
    assert(UIAPIR_AEHA_DURATIONS(UIAPIR_MAX_AEHA_BYTES) <= UIAPIR_RAW_BUFFER_SIZE);
}

static void testFrameGapWindow() {
    // A space longer than the threshold ends a frame, so the threshold has to
    // clear the longest space inside a frame...
    assert(UIAPIR_FRAME_GAP_US > UIAPIR_NEC_LEADER_SPACE_US);
    assert(UIAPIR_FRAME_GAP_US > UIAPIR_AEHA_LEADER_SPACE_US);
    assert(UIAPIR_FRAME_GAP_US > UIAPIR_AEHA_ONE_SPACE_US);
    assert(UIAPIR_FRAME_GAP_US > UIAPIR_NEC_ONE_SPACE_US);

    // ...and stay under the shortest gap between two frames, which a 20-bit
    // SIRC burst of all ones produces.
    const uint32_t worstSonyFrame = sonyFrameUs(UIAPIR_SONY_MAX_BITS, true);
    assert(worstSonyFrame == UIAPIR_SONY_MAX_FRAME_US);
    assert(UIAPIR_SONY_FRAME_PERIOD_US > worstSonyFrame);
    assert(UIAPIR_FRAME_GAP_US < UIAPIR_SONY_FRAME_PERIOD_US - worstSonyFrame);

    // The NEC leader mark is longer than that gap, which is why the receiver
    // may only end a frame on a space and never on a mark.
    assert(UIAPIR_NEC_LEADER_MARK_US >
           UIAPIR_SONY_FRAME_PERIOD_US - worstSonyFrame);
}

static void testNEC() {
    IRNECData nec = {};
    bool repeat = false;

    auto nec8 = makeNEC(0x12, 0x34, false);
    assert(uiapir_protocol::decodeNEC(nec8.data(), (uint16_t)nec8.size(), nec, repeat));
    assert(!repeat && nec.address == 0x12 && nec.command == 0x34 && nec.addressBits == 8);

    auto nec16 = makeNEC(0x1234, 0x56, true);
    assert(uiapir_protocol::decodeNEC(nec16.data(), (uint16_t)nec16.size(), nec, repeat));
    assert(nec.address == 0x1234 && nec.command == 0x56 && nec.addressBits == 16);

    const uint8_t necRepeat[] = {tick(UIAPIR_NEC_LEADER_MARK_US),
                                 tick(UIAPIR_NEC_REPEAT_SPACE_US),
                                 tick(UIAPIR_NEC_TRAILER_MARK_US)};
    assert(uiapir_protocol::decodeNEC(necRepeat, 3, nec, repeat) && repeat);

    // Extended addresses whose high byte is the complement of the low byte are
    // reserved for standard NEC: they must be refused on send and reported as
    // 8-bit on receive, never round-tripped as 16-bit.
    assert(!uiapir_protocol::necAddressIsExtendable(0x00ff));
    assert(!uiapir_protocol::necAddressIsExtendable(0xff00));
    assert(!uiapir_protocol::necAddressIsExtendable(0x12ed));
    assert(uiapir_protocol::necAddressIsExtendable(0x0000));
    assert(uiapir_protocol::necAddressIsExtendable(0x1234));

    auto necAmbiguous = makeNEC(0x00ff, 0x56, true);
    assert(uiapir_protocol::decodeNEC(necAmbiguous.data(), (uint16_t)necAmbiguous.size(), nec, repeat));
    assert(nec.addressBits == 8 && nec.address == 0xff);

    nec8[65] = tick(UIAPIR_NEC_ZERO_SPACE_US); // Corrupt bit 7 of the inverted command.
    assert(!uiapir_protocol::decodeNEC(nec8.data(), (uint16_t)nec8.size(), nec, repeat));
}

static void testAEHA() {
    const uint8_t aehaBytes[] = {0x02, 0x20, 0x80, 0x00, 0x12, 0x34};
    auto aehaRaw = makeAEHA(aehaBytes, sizeof(aehaBytes));
    IRAEHAData aeha = {};
    assert(uiapir_protocol::decodeAEHA(aehaRaw.data(), (uint16_t)aehaRaw.size(), aeha));
    assert(aeha.length == sizeof(aehaBytes));
    for (uint8_t i = 0; i < aeha.length; ++i) assert(aeha.bytes[i] == aehaBytes[i]);

    // Shortest and longest conforming payloads.
    uint8_t shortest[UIAPIR_AEHA_MIN_BYTES] = {0x02, 0x20, 0x80};
    auto shortestRaw = makeAEHA(shortest, UIAPIR_AEHA_MIN_BYTES);
    assert(uiapir_protocol::decodeAEHA(shortestRaw.data(), (uint16_t)shortestRaw.size(), aeha));
    assert(aeha.length == UIAPIR_AEHA_MIN_BYTES);

    uint8_t longest[UIAPIR_MAX_AEHA_BYTES];
    for (uint8_t i = 0; i < UIAPIR_MAX_AEHA_BYTES; ++i) longest[i] = (uint8_t)(i * 37 + 5);
    auto longestRaw = makeAEHA(longest, UIAPIR_MAX_AEHA_BYTES);
    assert(uiapir_protocol::decodeAEHA(longestRaw.data(), (uint16_t)longestRaw.size(), aeha));
    assert(aeha.length == UIAPIR_MAX_AEHA_BYTES);
    for (uint8_t i = 0; i < aeha.length; ++i) assert(aeha.bytes[i] == longest[i]);

    // A frame carrying less than a customer code plus parity plus data0 is not
    // a conforming AEHA frame and must be rejected, not reported as a payload.
    for (uint8_t length = 1; length < UIAPIR_AEHA_MIN_BYTES; ++length) {
        auto tooShort = makeAEHA(longest, length);
        assert(!uiapir_protocol::decodeAEHA(tooShort.data(), (uint16_t)tooShort.size(), aeha));
    }
    // ...and so is one longer than the library's buffer can hold.
    auto tooLong = makeAEHA(longest, UIAPIR_MAX_AEHA_BYTES);
    tooLong.insert(tooLong.end() - 1, 16, tick(UIAPIR_AEHA_BIT_MARK_US));
    assert(!uiapir_protocol::decodeAEHA(tooLong.data(), (uint16_t)tooLong.size(), aeha));

    // Parity is the 16-bit customer code XORed in 4-bit units [ChaN].
    const uint8_t parityOk[] = {0x34, 0x12, 0x54, 0x00}; // data0 = 5, parity = 4
    assert(uiapir_protocol::aehaParity(parityOk) == (0x3 ^ 0x4 ^ 0x1 ^ 0x2));
    assert(uiapir_protocol::aehaParityValid(parityOk, sizeof(parityOk)));
    const uint8_t parityBad[] = {0x34, 0x12, 0x55, 0x00};
    assert(!uiapir_protocol::aehaParityValid(parityBad, sizeof(parityBad)));
}

static void testSony() {
    assert(uiapir_protocol::sonyAddressBits(12) == 5);
    assert(uiapir_protocol::sonyAddressBits(15) == 8);
    assert(uiapir_protocol::sonyAddressBits(20) == 13);
    assert(uiapir_protocol::sonyAddressBits(13) == 0);
    assert(uiapir_protocol::sonyAddressBits(0) == 0);

    for (uint8_t bits : {(uint8_t)12, (uint8_t)15, (uint8_t)20}) {
        const uint8_t addressBits = uiapir_protocol::sonyAddressBits(bits);
        const uint16_t widest = (uint16_t)((1U << addressBits) - 1U);
        for (uint16_t address : {(uint16_t)0, (uint16_t)1, widest}) {
            for (uint8_t command : {(uint8_t)0, (uint8_t)19, (uint8_t)0x7f}) {
                auto sonyRaw = makeSony(address, command, bits);
                IRSonyData sony = {};
                assert(uiapir_protocol::decodeSony(sonyRaw.data(), (uint16_t)sonyRaw.size(), sony));
                assert(sony.address == address && sony.command == command &&
                       sony.bits == bits);
            }
        }
    }

    // Frame lengths other than 12, 15, and 20 bits are not SIRC.
    IRSonyData sony = {};
    auto sonyRaw = makeSony(1, 19, 12);
    sonyRaw.pop_back();
    assert(!uiapir_protocol::decodeSony(sonyRaw.data(), (uint16_t)sonyRaw.size(), sony));
    // A too-short buffer must be rejected on count before any tick is read.
    assert(!uiapir_protocol::decodeSony(nullptr, 0, sony));
}

static void testCrossProtocolRejection() {
    IRNECData nec = {};
    IRAEHAData aeha = {};
    IRSonyData sony = {};
    bool repeat = false;

    const uint8_t aehaBytes[] = {0x02, 0x20, 0x80, 0x00, 0x12, 0x34};
    auto aehaRaw = makeAEHA(aehaBytes, sizeof(aehaBytes));
    auto necRaw = makeNEC(0x12, 0x34, false);
    auto sonyRaw = makeSony(1, 19, 12);

    assert(!uiapir_protocol::decodeNEC(aehaRaw.data(), (uint16_t)aehaRaw.size(), nec, repeat));
    assert(!uiapir_protocol::decodeNEC(sonyRaw.data(), (uint16_t)sonyRaw.size(), nec, repeat));
    assert(!uiapir_protocol::decodeAEHA(necRaw.data(), (uint16_t)necRaw.size(), aeha));
    assert(!uiapir_protocol::decodeAEHA(sonyRaw.data(), (uint16_t)sonyRaw.size(), aeha));
    assert(!uiapir_protocol::decodeSony(necRaw.data(), (uint16_t)necRaw.size(), sony));
    assert(!uiapir_protocol::decodeSony(aehaRaw.data(), (uint16_t)aehaRaw.size(), sony));
}

static void testToleranceBandsDoNotOverlap() {
    // A "0" space and a "1" space must never both match the same tick count,
    // or a bit would decode ambiguously.
    for (unsigned t = 1; t <= 255; ++t) {
        const uint8_t ticks = (uint8_t)t;
        assert(!(uiapir_protocol::matchTicks(ticks, UIAPIR_NEC_ZERO_SPACE_US) &&
                 uiapir_protocol::matchTicks(ticks, UIAPIR_NEC_ONE_SPACE_US)));
        assert(!(uiapir_protocol::matchTicks(ticks, UIAPIR_AEHA_ZERO_SPACE_US) &&
                 uiapir_protocol::matchTicks(ticks, UIAPIR_AEHA_ONE_SPACE_US)));
        assert(!(uiapir_protocol::matchTicks(ticks, UIAPIR_SONY_ZERO_MARK_US) &&
                 uiapir_protocol::matchTicks(ticks, UIAPIR_SONY_ONE_MARK_US)));
    }
}

int main() {
    testBandMacrosMatchTheDecoder();
    testFramesFromRepeats();
    testRawReplayGating();
    testFrameLengths();
    testFrameGapWindow();
    testNEC();
    testAEHA();
    testSony();
    testCrossProtocolRejection();
    testToleranceBandsDoNotOverlap();
    return 0;
}
