// SPDX-License-Identifier: MIT
// Copyright (c) 2026 kei23jp

#include "UIAPIRCapture.h"

namespace uiapir_capture {

// Below this many milliseconds the 16-bit counter cannot have wrapped more than
// once, so its difference is exact. 60 leaves room for the millisecond clock's
// own +/-1 ms quantisation against the counter's 65.536 ms period.
#define UIAPIR_EXACT_ELAPSED_MS 60U

uint32_t elapsedUs(uint16_t nowTicks, uint16_t lastTicks,
                   uint32_t nowMs, uint32_t lastMs) {
    if ((uint32_t)(nowMs - lastMs) > UIAPIR_EXACT_ELAPSED_MS) {
        return UIAPIR_ELAPSED_SATURATED;
    }
    return (uint16_t)(nowTicks - lastTicks);
}

void IRCapture::setBuffer(uint8_t *ticks, uint16_t capacity) {
    _ticks = ticks;
    _capacity = capacity;
    reset();
}

void IRCapture::reset() {
    _capturing = false;
    _ready = false;
    _pendingStart = false;
    _pendingSpace = false;
    _flags = 0;
    _count = 0;
}

void IRCapture::openFrame() {
    _capturing = true;
    _pendingSpace = false; // The first duration of a frame is its leading mark.
    _flags = 0;
    _count = 0;
}

void IRCapture::clearFrame() {
    const bool restart = _pendingStart;
    reset();
    if (restart) {
        openFrame();
    }
}

void IRCapture::recordDuration(uint32_t durationUs) {
    // The mark/space phase is tracked here rather than derived from _count,
    // because _count stops advancing once the buffer is full. Deriving it would
    // freeze the phase on overflow and leave the frame with no way to end.
    _pendingSpace = !_pendingSpace;

    if (_count >= _capacity) {
        _flags |= UIAPIR_FLAG_RAW_OVERFLOW;
        return;
    }

    uint32_t ticks = (durationUs + UIAPIR_RAW_TICK_US / 2U) / UIAPIR_RAW_TICK_US;
    if (ticks == 0) {
        ticks = 1;
    }
    if (ticks > 255) {
        ticks = 255;
        _flags |= UIAPIR_FLAG_TIMING_CLIPPED;
    }
    _ticks[_count++] = (uint8_t)ticks;
}

void IRCapture::onEdge(uint32_t elapsed, bool markStarting) {
    // A finished frame is waiting to be read. Edges keep arriving while the
    // sketch gets around to it; they are dropped, and the capture stays closed
    // until clearFrame() plus a fresh gap open it again.
    if (_ready) {
        // The next frame is already under way, so its opening edge is no longer
        // the most recent one and the frame can no longer be captured from its
        // start. Better to skip it than to report a fragment.
        _pendingStart = false;
        return;
    }

    if (!_capturing) {
        // Open only on a mark that follows a real gap. Requiring the gap is
        // what stops a capture from starting midway through a burst whose
        // earlier frames were dropped, which would otherwise surface as a
        // truncated RAW frame.
        if (markStarting && elapsed >= UIAPIR_FRAME_GAP_US) {
            openFrame();
        }
        return;
    }

    // Only a space may end a frame. The NEC leader mark is 9 ms, longer than
    // the 6.6 ms gap that separates two 20-bit SIRC frames, so a threshold that
    // ignored the level would have no valid value at all.
    if (_pendingSpace && elapsed >= UIAPIR_FRAME_GAP_US) {
        if (_count >= UIAPIR_MIN_FRAME_DURATIONS) {
            _ready = true;
            // This one edge both ends the gap and, when it starts a mark,
            // begins the following frame. Remembering that is what keeps a
            // burst of repeated frames from being reduced to every other frame.
            _pendingStart = markStarting;
        } else {
            // Too short to be a frame. This edge follows a gap and starts a
            // mark, so it is itself a valid frame start.
            openFrame();
        }
        return;
    }

    recordDuration(elapsed);
}

bool IRCapture::poll(uint32_t elapsed) {
    if (!_ready && _capturing && _pendingSpace &&
        _count >= UIAPIR_MIN_FRAME_DURATIONS && elapsed >= UIAPIR_FRAME_GAP_US) {
        _ready = true;
    }
    return _ready;
}

} // namespace uiapir_capture
