// SPDX-License-Identifier: MIT
// Copyright (c) 2026 kei23jp

#pragma once

#include "UIAPIRTypes.h"

// Edge capture state machine, kept free of any hardware dependency so the host
// tests can drive it with synthetic edge streams. UIAPIR.cpp supplies the
// timing; everything about what makes a frame lives here.

namespace uiapir_capture {

// Microseconds between two edges, taken from a 16-bit 1 MHz counter that wraps
// every 65.536 ms and disambiguated by a millisecond clock.
//
// The counter alone cannot tell 4 ms from 69.5 ms, and reading the second as
// the first is what lets a long idle period masquerade as an in-frame duration.
// Any interval the capture cares about resolving exactly is well under 61 ms,
// so once the millisecond clock says more than that has passed, the exact value
// no longer matters and a saturated result is returned.
#define UIAPIR_ELAPSED_SATURATED 0xffffffffUL

uint32_t elapsedUs(uint16_t nowTicks, uint16_t lastTicks,
                   uint32_t nowMs, uint32_t lastMs);

class IRCapture {
public:
    IRCapture() : _ticks(nullptr), _capacity(0) { reset(); }
    IRCapture(uint8_t *ticks, uint16_t capacity)
        : _ticks(ticks), _capacity(capacity) { reset(); }

    // The buffer is owned by the caller and must remain valid while capture is
    // active. Replacing it also discards any frame currently in progress.
    void setBuffer(uint8_t *ticks, uint16_t capacity);

    void reset();

    // One edge of the receiver output. `elapsedUs` is the time since the
    // previous edge, whether or not that edge was part of a capture.
    // `markStarting` is true when this edge begins a MARK, which for an
    // active-low demodulator means the line just went low.
    void onEdge(uint32_t elapsedUs, bool markStarting);

    // Finish a frame whose trailing gap has run long enough, for callers that
    // poll rather than wait for the next edge. `elapsedUs` is measured from the
    // last edge. Returns whether a frame is now waiting.
    bool poll(uint32_t elapsedUs);

    bool ready() const { return _ready; }
    uint16_t count() const { return _count; }
    uint8_t flags() const { return _flags; }
    uint8_t tickAt(uint16_t index) const { return _ticks[index]; }
    uint16_t capacity() const { return _capacity; }

    // Discard the finished frame and arm for the next one. If the edge that
    // closed the frame also opened the next one and no edge has arrived since,
    // the new frame is picked up from its true start; otherwise the capture
    // stays closed until a fresh gap, so a burst already in flight cannot be
    // joined from the middle.
    void clearFrame();

private:
    void openFrame();
    void recordDuration(uint32_t durationUs);

    volatile bool _capturing;
    volatile bool _ready;
    // The edge that finished a frame was also the start of the next one, and
    // that start is still the most recent edge.
    volatile bool _pendingStart;
    // Whether the duration being timed right now is a space. Tracked rather
    // than derived from the count, which stops advancing once the buffer fills.
    volatile bool _pendingSpace;
    volatile uint8_t _flags;
    volatile uint16_t _count;
    volatile uint8_t *_ticks;
    uint16_t _capacity;
};

} // namespace uiapir_capture
