// SPDX-License-Identifier: MIT
// Copyright (c) 2026 kei23jp

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <vector>
#include "../src/UIAPIRCapture.h"
#include "../src/UIAPIRProtocol.h"

// Drives the capture state machine with synthetic edge streams and checks what
// comes out. Everything here is timing the receiver has to survive on real
// hardware: bursts of repeated frames, idle periods longer than the hardware
// counter can represent, and frames that arrive while an earlier one is still
// waiting to be read.

using uiapir_capture::IRCapture;
using uiapir_capture::elapsedUs;

typedef std::vector<uint32_t> Durations;

// A captured frame, as receive() would hand it to the decoders.
typedef std::vector<uint8_t> Frame;

// ---------------------------------------------------------------------------
// Frame builders, mirroring the transmitter in UIAPIR.cpp
// ---------------------------------------------------------------------------

static Durations necFrame(uint16_t address, uint8_t command, bool extended) {
    uint32_t value = extended ? address
                              : ((uint8_t)address | ((uint32_t)(uint8_t)~address << 8));
    value |= (uint32_t)command << 16;
    value |= (uint32_t)(uint8_t)~command << 24;
    Durations d = {UIAPIR_NEC_LEADER_MARK_US, UIAPIR_NEC_LEADER_SPACE_US};
    for (uint8_t i = 0; i < UIAPIR_NEC_BITS; ++i) {
        d.push_back(UIAPIR_NEC_BIT_MARK_US);
        d.push_back((value >> i) & 1 ? UIAPIR_NEC_ONE_SPACE_US : UIAPIR_NEC_ZERO_SPACE_US);
    }
    d.push_back(UIAPIR_NEC_TRAILER_MARK_US);
    return d;
}

static Durations necRepeatFrame() {
    return {UIAPIR_NEC_LEADER_MARK_US, UIAPIR_NEC_REPEAT_SPACE_US,
            UIAPIR_NEC_TRAILER_MARK_US};
}

static Durations aehaFrame(const std::vector<uint8_t> &bytes) {
    Durations d = {UIAPIR_AEHA_LEADER_MARK_US, UIAPIR_AEHA_LEADER_SPACE_US};
    for (size_t n = 0; n < bytes.size(); ++n) {
        for (uint8_t i = 0; i < 8; ++i) {
            d.push_back(UIAPIR_AEHA_BIT_MARK_US);
            d.push_back((bytes[n] >> i) & 1 ? UIAPIR_AEHA_ONE_SPACE_US
                                            : UIAPIR_AEHA_ZERO_SPACE_US);
        }
    }
    d.push_back(UIAPIR_AEHA_TRAILER_MARK_US);
    return d;
}

static Durations sonyFrame(uint16_t address, uint8_t command, uint8_t bits) {
    uint32_t value = ((uint32_t)address << UIAPIR_SONY_COMMAND_BITS) | command;
    Durations d = {UIAPIR_SONY_LEADER_MARK_US, UIAPIR_SONY_LEADER_SPACE_US};
    for (uint8_t i = 0; i < bits; ++i) {
        d.push_back((value >> i) & 1 ? UIAPIR_SONY_ONE_MARK_US : UIAPIR_SONY_ZERO_MARK_US);
        if (i + 1 < bits) d.push_back(UIAPIR_SONY_BIT_SPACE_US);
    }
    return d;
}

static uint32_t total(const Durations &d) {
    uint32_t sum = 0;
    for (size_t i = 0; i < d.size(); ++i) sum += d[i];
    return sum;
}

// ---------------------------------------------------------------------------
// Line model
// ---------------------------------------------------------------------------

// A timeline of alternating levels. Index 0 is a space (the line idles high),
// so even indices are spaces and odd indices are marks. Every frame builder
// above produces an odd number of durations starting and ending with a mark, so
// appending [frame, gap, frame, gap, ...] after an initial idle keeps that
// alternation intact.
struct Line {
    Durations timeline;

    Line &idle(uint32_t us) {
        assert(timeline.size() % 2 == 0 && "an idle space must land on an even index");
        timeline.push_back(us);
        return *this;
    }
    Line &frame(const Durations &d) {
        assert(timeline.size() % 2 == 1 && "a frame must start on an odd index");
        assert(d.size() % 2 == 1 && "a frame starts and ends with a mark");
        timeline.insert(timeline.end(), d.begin(), d.end());
        return *this;
    }
    // Lengthen the gap burst() already appended, to let the line go quiet.
    Line &extendTrailingGap(uint32_t us) {
        // The last entry must already be a space, i.e. sit on an even index.
        assert(!timeline.empty() && (timeline.size() - 1) % 2 == 0);
        timeline.back() += us;
        return *this;
    }
    // A frame followed by the gap that its protocol's period implies.
    Line &burst(const Durations &d, uint32_t periodUs) {
        const uint32_t used = total(d);
        frame(d);
        return idle(periodUs > used ? periodUs - used : UIAPIR_MIN_TX_GAP_US);
    }
};

// How the sketch behaves: `consumeAfter` is the number of further edges that
// arrive before a finished frame is read. 0 models a tight polling loop, a
// large value models a sketch busy doing something else.
struct Capture {
    std::vector<Frame> frames;
    std::vector<uint8_t> frameFlags;
};

static Capture run(const Line &line, size_t consumeAfter = 0,
                   uint16_t capacity = UIAPIR_RAW_BUFFER_SIZE) {
    std::vector<uint8_t> storage(capacity);
    IRCapture cap(storage.data(), capacity);
    Capture out;
    size_t readyForEdges = 0;

    for (size_t i = 0; i < line.timeline.size(); ++i) {
        // The edge at the end of duration i starts the opposite level; even
        // indices are spaces, so those edges start a mark.
        cap.onEdge(line.timeline[i], (i % 2) == 0);

        if (cap.ready()) {
            if (readyForEdges >= consumeAfter) {
                Frame f;
                for (uint16_t t = 0; t < cap.count(); ++t) f.push_back(cap.tickAt(t));
                out.frames.push_back(f);
                out.frameFlags.push_back(cap.flags());
                cap.clearFrame();
                readyForEdges = 0;
            } else {
                ++readyForEdges;
            }
        }
    }

    // The sketch polls once more after the last edge, as loop() would.
    if (cap.poll(UIAPIR_ELAPSED_SATURATED)) {
        Frame f;
        for (uint16_t t = 0; t < cap.count(); ++t) f.push_back(cap.tickAt(t));
        out.frames.push_back(f);
        out.frameFlags.push_back(cap.flags());
    }
    return out;
}

// The state machine must honor the per-instance capacity rather than the
// compile-time maximum used by IRCode.
static void testRuntimeCapacityIsHonored() {
    const uint16_t capacity = UIAPIR_NEC_FRAME_DURATIONS - 1;
    Line line;
    line.idle(200000).frame(necFrame(0x12, 0x34, false)).idle(200000);
    Capture got = run(line, 0, capacity);
    assert(got.frames.size() == 1);
    assert(got.frames[0].size() == capacity);
    assert((got.frameFlags[0] & UIAPIR_FLAG_RAW_OVERFLOW) != 0);
}

// ---------------------------------------------------------------------------
// elapsedUs: a 16-bit 1 MHz counter disambiguated by a millisecond clock
// ---------------------------------------------------------------------------

static void testElapsedUs() {
    // Short intervals are exact.
    assert(elapsedUs(1000, 0, 1, 0) == 1000);
    assert(elapsedUs(9000, 0, 9, 0) == 9000);
    assert(elapsedUs(0, 60000, 6, 0) == 5536); // counter wrapped once, still exact

    // The counter alone reads a 70 ms gap as 4464 us, which is below the frame
    // gap threshold and would be appended as an in-frame duration. The
    // millisecond clock has to catch that.
    const uint16_t lastTicks = 0;
    for (uint32_t gapMs : {66U, 70U, 80U, 132U, 200U, 1000U, 60000U}) {
        const uint16_t nowTicks = (uint16_t)((uint64_t)gapMs * 1000U);
        const uint32_t aliased = (uint16_t)(nowTicks - lastTicks);
        const uint32_t measured = elapsedUs(nowTicks, lastTicks, gapMs, 0);
        assert(measured >= UIAPIR_FRAME_GAP_US);
        if (aliased < UIAPIR_FRAME_GAP_US) {
            printf("  %6u ms gap: raw counter says %5u us, disambiguated -> long\n",
                   (unsigned)gapMs, (unsigned)aliased);
        }
    }

    // Both clocks wrapping at once is still handled: they are compared with
    // unsigned arithmetic, not ordering.
    assert(elapsedUs(10, 0xfff0, 0, 0xffffffffU) == 26);
}

// ---------------------------------------------------------------------------
// Capture behaviour
// ---------------------------------------------------------------------------

static void testSingleFrames() {
    struct Case {
        const char *name;
        Durations frame;
    } cases[] = {
        {"NEC", necFrame(0x12, 0x34, false)},
        {"NEC repeat", necRepeatFrame()},
        {"AEHA 6 byte", aehaFrame({0x02, 0x20, 0x80, 0x00, 0x12, 0x34})},
        {"SIRC 12 bit", sonyFrame(1, 19, 12)},
        {"SIRC 20 bit", sonyFrame(0x1fff, 0x7f, 20)},
    };
    for (const Case &c : cases) {
        Line line;
        line.idle(200000).frame(c.frame).idle(200000);
        Capture got = run(line);
        assert(got.frames.size() == 1);
        assert(got.frames[0].size() == c.frame.size());
        assert(got.frameFlags[0] == 0);
    }

    // 20 bytes is the longest AEHA payload the library supports and has to fit
    // the buffer without setting the overflow flag.
    std::vector<uint8_t> longPayload;
    for (uint8_t i = 0; i < UIAPIR_MAX_AEHA_BYTES; ++i) longPayload.push_back((uint8_t)(i * 37 + 5));
    Line line;
    line.idle(200000).frame(aehaFrame(longPayload)).idle(200000);
    Capture got = run(line);
    assert(got.frames.size() == 1);
    assert(got.frames[0].size() == (size_t)UIAPIR_AEHA_DURATIONS(UIAPIR_MAX_AEHA_BYTES));
    assert(got.frameFlags[0] == 0);
}

// A 20-bit SIRC burst leaves only 6.6 ms between frames while the NEC leader
// mark is 9 ms long. A frame gap threshold that ignored the level would have to
// exceed 9 ms and would merge every frame of this burst into one.
static void testSonyBurstIsNotMerged() {
    for (uint8_t bits : {(uint8_t)12, (uint8_t)15, (uint8_t)20}) {
        const uint16_t address = (uint16_t)((1U << uiapir_protocol::sonyAddressBits(bits)) - 1U);
        const Durations frame = sonyFrame(address, 0x7f, bits);
        Line line;
        line.idle(200000);
        for (int i = 0; i < UIAPIR_SONY_MIN_FRAMES; ++i) {
            line.burst(frame, UIAPIR_SONY_FRAME_PERIOD_US);
        }
        line.extendTrailingGap(200000);
        Capture got = run(line);
        assert(got.frames.size() == (size_t)UIAPIR_SONY_MIN_FRAMES);
        for (const Frame &f : got.frames) {
            assert(f.size() == (size_t)UIAPIR_SONY_DURATIONS(bits));
            IRSonyData sony = {};
            assert(uiapir_protocol::decodeSony(f.data(), (uint16_t)f.size(), sony));
            assert(sony.bits == bits && sony.address == address && sony.command == 0x7f);
        }
        printf("  SIRC %2d bit burst: %u frames, gap %u us\n", bits,
               (unsigned)got.frames.size(),
               (unsigned)(UIAPIR_SONY_FRAME_PERIOD_US - total(frame)));
    }
}

// The 9 ms NEC leader is the longest duration inside any supported frame. It is
// a mark, and marks must never end a frame.
static void testLeaderMarkDoesNotEndFrame() {
    Line line;
    line.idle(200000).frame(necFrame(0x12, 0x34, false)).idle(200000);
    Capture got = run(line);
    assert(got.frames.size() == 1);
    assert(got.frames[0].size() == (size_t)UIAPIR_NEC_FRAME_DURATIONS);
    assert(UIAPIR_NEC_LEADER_MARK_US > UIAPIR_FRAME_GAP_US &&
           "the test is pointless unless the leader really exceeds the threshold");

    IRNECData nec = {};
    bool repeat = false;
    assert(uiapir_protocol::decodeNEC(got.frames[0].data(),
                                      (uint16_t)got.frames[0].size(), nec, repeat));
    assert(!repeat && nec.address == 0x12 && nec.command == 0x34);
}

// A key held down: one frame then repeat codes on the protocol's period.
static void testNecKeyHeld() {
    const Durations data = necFrame(0x12, 0x34, false);
    const Durations rep = necRepeatFrame();
    Line line;
    line.idle(200000).burst(data, UIAPIR_NEC_FRAME_PERIOD_US);
    for (int i = 0; i < 3; ++i) line.burst(rep, UIAPIR_NEC_FRAME_PERIOD_US);
    line.extendTrailingGap(200000);

    Capture got = run(line);
    assert(got.frames.size() == 4);
    assert(got.frames[0].size() == (size_t)UIAPIR_NEC_FRAME_DURATIONS);

    IRNECData nec = {};
    bool repeat = false;
    for (size_t i = 1; i < got.frames.size(); ++i) {
        assert(got.frames[i].size() == (size_t)UIAPIR_NEC_REPEAT_DURATIONS);
        assert(uiapir_protocol::decodeNEC(got.frames[i].data(),
                                          (uint16_t)got.frames[i].size(), nec, repeat));
        assert(repeat);
    }
}

// Idle periods longer than the hardware counter's 65.536 ms period must not be
// mistaken for an in-frame duration, whatever their length.
static void testLongIdleBetweenFrames() {
    const Durations frame = necFrame(0x12, 0x34, false);
    for (uint32_t idleUs : {70000U, 132000U, 200000U, 1000000U, 5000000U}) {
        Line line;
        line.idle(idleUs).frame(frame).idle(idleUs).frame(frame).idle(idleUs);
        Capture got = run(line);
        assert(got.frames.size() == 2);
        for (const Frame &f : got.frames) {
            assert(f.size() == (size_t)UIAPIR_NEC_FRAME_DURATIONS);
        }
    }
}

// If the sketch is slow to read a frame, the frames that arrive meanwhile are
// lost. What must not happen is a capture opening midway through one of them
// and surfacing as a truncated frame.
static void testSlowReaderNeverYieldsPartialFrames() {
    const Durations frame = sonyFrame(1, 19, 12);
    for (size_t lateBy : {0U, 5U, 20U, 40U}) {
        Line line;
        line.idle(200000);
        for (int i = 0; i < 6; ++i) line.burst(frame, UIAPIR_SONY_FRAME_PERIOD_US);
        line.extendTrailingGap(200000);

        Capture got = run(line, lateBy);
        assert(!got.frames.empty());
        for (const Frame &f : got.frames) {
            // Every frame handed out is a whole one, never a fragment.
            assert(f.size() == (size_t)UIAPIR_SONY_DURATIONS(12));
            IRSonyData sony = {};
            assert(uiapir_protocol::decodeSony(f.data(), (uint16_t)f.size(), sony));
            assert(sony.address == 1 && sony.command == 19);
        }
        printf("  reader %2u edges late: %u of 6 frames captured, 0 fragments\n",
               (unsigned)lateBy, (unsigned)got.frames.size());
    }
}

// A burst too short to be any frame is dropped rather than reported.
static void testRuntFramesAreDropped() {
    // A burst always runs mark..mark, so the only shape shorter than a frame is
    // a single blip. Noise on the receiver output looks like this.
    for (uint32_t blipUs : {200U, 600U, 3000U, 9000U}) {
        Line line;
        line.idle(200000).frame({blipUs}).idle(200000);
        Capture got = run(line);
        assert(got.frames.empty());
    }

    // A blip immediately followed by a real frame must not swallow it.
    Line line;
    line.idle(200000)
        .frame({UIAPIR_NEC_BIT_MARK_US})
        .idle(UIAPIR_FRAME_GAP_US * 2)
        .frame(necFrame(0x12, 0x34, false))
        .idle(200000);
    Capture got = run(line);
    assert(got.frames.size() == 1);
    assert(got.frames[0].size() == (size_t)UIAPIR_NEC_FRAME_DURATIONS);
}

// An unknown signal longer than the buffer is reported with the overflow flag
// rather than as a silently truncated frame.
static void testOverflowIsFlagged() {
    Durations huge = {UIAPIR_NEC_LEADER_MARK_US, UIAPIR_NEC_LEADER_SPACE_US};
    while (huge.size() < (size_t)UIAPIR_RAW_BUFFER_SIZE + 40) {
        huge.push_back(UIAPIR_NEC_BIT_MARK_US);
        huge.push_back(UIAPIR_NEC_ZERO_SPACE_US);
    }
    huge.push_back(UIAPIR_NEC_TRAILER_MARK_US);

    Line line;
    line.idle(200000).frame(huge).idle(200000);
    Capture got = run(line);
    assert(got.frames.size() == 1);
    assert(got.frames[0].size() == (size_t)UIAPIR_RAW_BUFFER_SIZE);
    assert((got.frameFlags[0] & UIAPIR_FLAG_RAW_OVERFLOW) != 0);
    // ...and a frame flagged that way must never be replayed.
    assert(!uiapir_protocol::rawIsReplayable(got.frameFlags[0]));
}

// A mark longer than a uint8_t of ticks can hold is clipped, and says so.
static void testClippingIsFlagged() {
    const uint32_t clipAt = 255U * UIAPIR_RAW_TICK_US;
    Line line;
    line.idle(200000)
        .frame({clipAt + 5000U, UIAPIR_NEC_LEADER_SPACE_US, UIAPIR_NEC_TRAILER_MARK_US})
        .idle(200000);
    Capture got = run(line);
    assert(got.frames.size() == 1);
    assert(got.frames[0][0] == 255);
    assert((got.frameFlags[0] & UIAPIR_FLAG_TIMING_CLIPPED) != 0);
    assert(!uiapir_protocol::rawIsReplayable(got.frameFlags[0]));

    // Nothing inside a supported frame ever reaches that limit.
    assert(UIAPIR_MAX_FRAME_DURATION_US / UIAPIR_RAW_TICK_US < 255);
}

// Two protocols arriving back to back stay separate frames.
static void testMixedProtocolStream() {
    Line line;
    line.idle(200000)
        .burst(necFrame(0x12, 0x34, false), UIAPIR_NEC_FRAME_PERIOD_US)
        .burst(aehaFrame({0x02, 0x20, 0x80, 0x00, 0x12, 0x34}), UIAPIR_AEHA_FRAME_PERIOD_US)
        .burst(sonyFrame(1, 19, 12), UIAPIR_SONY_FRAME_PERIOD_US)
        .extendTrailingGap(200000);

    Capture got = run(line);
    assert(got.frames.size() == 3);
    assert(got.frames[0].size() == (size_t)UIAPIR_NEC_FRAME_DURATIONS);
    assert(got.frames[1].size() == (size_t)UIAPIR_AEHA_DURATIONS(6));
    assert(got.frames[2].size() == (size_t)UIAPIR_SONY_DURATIONS(12));

    IRNECData nec = {};
    IRAEHAData aeha = {};
    IRSonyData sony = {};
    bool repeat = false;
    assert(uiapir_protocol::decodeNEC(got.frames[0].data(), (uint16_t)got.frames[0].size(), nec, repeat));
    assert(uiapir_protocol::decodeAEHA(got.frames[1].data(), (uint16_t)got.frames[1].size(), aeha));
    assert(uiapir_protocol::decodeSony(got.frames[2].data(), (uint16_t)got.frames[2].size(), sony));
    assert(nec.address == 0x12 && aeha.length == 6 && sony.command == 19);
}

int main() {
    printf("elapsedUs\n");
    testElapsedUs();
    printf("capture\n");
    testSingleFrames();
    testSonyBurstIsNotMerged();
    testLeaderMarkDoesNotEndFrame();
    testNecKeyHeld();
    testLongIdleBetweenFrames();
    testSlowReaderNeverYieldsPartialFrames();
    testRuntFramesAreDropped();
    testOverflowIsFlagged();
    testRuntimeCapacityIsHonored();
    testClippingIsFlagged();
    testMixedProtocolStream();
    printf("UIAPIR capture tests passed\n");
    return 0;
}
