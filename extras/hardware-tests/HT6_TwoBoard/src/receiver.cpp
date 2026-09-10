// SPDX-License-Identifier: MIT
// Copyright (c) 2026 kei23jp

// HT6 receiver - test controller, decoder, and the only board with a log.
//
// This board owns the run: it names each case on START, watches STATUS to know
// when the transmitter is working and what its send call returned, decodes
// every frame that arrives over the real optical path, and prints a verdict.
//
// Two things shape the loop.
//
// A frame has to be drained before the next one ends. The capture holds one
// finished frame at a time, and consecutive 20-bit SIRC frames are only 6.6 ms
// apart, so anything slow between receive() calls loses frames. Serial output
// is slow, so nothing is printed until the whole case is over: while a case is
// in flight the loop only decodes, compares, and counts.
//
// Nothing is stored per frame either. A single IRCode is 346 bytes on a part
// with 2048, so frames are folded into counters as they arrive rather than
// collected and checked afterwards.
//
// Build: pio run -e receiver -t upload
//        pio device monitor -e receiver

#define HT6_WITH_CASE_NAMES

#include <Arduino.h>
#include <string.h>

#include <UIAPIR.h>
#include <UIAPIRProtocol.h>

#include "HT6TestPlan.h"

// The longest frame this plan produces is the 6-byte AEHA frame at 99
// durations. Sizing the buffer to the plan instead of to
// UIAPIR_RAW_BUFFER_SIZE leaves a quarter of SRAM free, and a signal longer
// than the plan shows up as an overflow flag rather than being trimmed
// silently.
#define HT6_CAPTURE_CAPACITY 112

#if UIAPIR_AEHA_DURATIONS(HT6_AEHA_LENGTH) > HT6_CAPTURE_CAPACITY
#error "HT6_CAPTURE_CAPACITY cannot hold the AEHA frame this plan sends"
#endif
#if UIAPIR_NEC_FRAME_DURATIONS > HT6_CAPTURE_CAPACITY
#error "HT6_CAPTURE_CAPACITY cannot hold a NEC frame"
#endif
#if UIAPIR_SONY_DURATIONS(UIAPIR_SONY_MAX_BITS) > HT6_CAPTURE_CAPACITY
#error "HT6_CAPTURE_CAPACITY cannot hold the longest SIRC frame"
#endif

enum HT6Reason : uint8_t {
    HT6_OK = 0,
    HT6_BAD_PROTOCOL,
    HT6_BAD_COUNT,
    HT6_BAD_FIELDS,
    HT6_BAD_REPEAT,
    HT6_LOSSY,
    HT6_UNEXPECTED
};

struct CaseResult {
    uint8_t frames;      // frames decoded
    uint8_t bad;         // of those, how many failed a check
    uint8_t firstReason; // why the first bad frame was bad
    uint8_t firstFrame;  // and which frame that was
    // What a failing RAW frame actually contained. Printing the durations is
    // the only way to tell a distorted signal from a broken one, and a whole
    // IRCode is far too big to keep, so just the slots this plan sends.
    uint16_t rawCount;
    uint8_t rawTicks[HT6_RAW_COUNT];
    bool apiOk;          // what the transmitter's send call returned
    bool ready;          // the transmitter was idle before the case started
    bool acked;          // it answered the START train
    bool finished;       // it went idle again within the timeout
    uint32_t elapsedMs;
};

static uint8_t captureBuffer[HT6_CAPTURE_CAPACITY];
static UIAPIR ir;

// One frame at a time, reused for every case: see the note at the top.
static IRCode code;

static bool pinMapIsAsExpected() {
    return digitalPinToPinName(HT6_IR_RX_PIN) == HT6_EXPECT_IR_RX_PORT &&
           digitalPinToPinName(HT6_START_PIN) == HT6_EXPECT_START_PORT &&
           digitalPinToPinName(HT6_STATUS_PIN) == HT6_EXPECT_STATUS_PORT;
}

static bool transmitterIsIdle() {
    return digitalRead(HT6_STATUS_PIN) == HIGH;
}

// ------------------------------------------------------------- validation ---

static uint8_t checkFrame(const HT6Case &testCase, const IRCode &frame, uint8_t index) {
    if (frame.flags & (UIAPIR_FLAG_RAW_OVERFLOW | UIAPIR_FLAG_TIMING_CLIPPED)) {
        return HT6_LOSSY;
    }
    if (index >= testCase.expectFrames) {
        return HT6_UNEXPECTED;
    }

    switch (testCase.kind) {
    case HT6_KIND_NEC: {
        if (frame.protocol != UIAPIR_NEC) {
            return HT6_BAD_PROTOCOL;
        }
        // The first frame carries address and command; every repeat that
        // follows it must be a repeat code, not another full frame.
        const bool isRepeat = (frame.flags & UIAPIR_FLAG_REPEAT) != 0;
        if (isRepeat != (index != 0)) {
            return HT6_BAD_REPEAT;
        }
        if (isRepeat) {
            return HT6_OK; // a repeat code carries no payload to compare
        }
        if (frame.data.nec.address != testCase.address ||
            frame.data.nec.command != testCase.command ||
            frame.data.nec.addressBits != testCase.param) {
            return HT6_BAD_FIELDS;
        }
        return HT6_OK;
    }
    case HT6_KIND_AEHA:
        if (frame.protocol != UIAPIR_AEHA) {
            return HT6_BAD_PROTOCOL;
        }
        if (frame.data.aeha.length != testCase.param ||
            memcmp(frame.data.aeha.bytes, HT6_AEHA_PAYLOAD, testCase.param) != 0) {
            return HT6_BAD_FIELDS;
        }
        return HT6_OK;
    case HT6_KIND_SONY:
        if (frame.protocol != UIAPIR_SONY) {
            return HT6_BAD_PROTOCOL;
        }
        if (frame.data.sony.bits != testCase.param ||
            frame.data.sony.address != testCase.address ||
            frame.data.sony.command != testCase.command) {
            return HT6_BAD_FIELDS;
        }
        return HT6_OK;
    case HT6_KIND_RAW:
    case HT6_KIND_RAW_CODE: {
        if (frame.protocol != UIAPIR_RAW) {
            return HT6_BAD_PROTOCOL;
        }
        if (frame.data.raw.count != HT6_RAW_COUNT) {
            return HT6_BAD_COUNT;
        }
        for (uint8_t i = 0; i < HT6_RAW_COUNT; ++i) {
            if (!uiapir_protocol::matchTicks(frame.data.raw.ticks[i], HT6_RAW_SLOT_US)) {
                return HT6_BAD_FIELDS;
            }
        }
        return HT6_OK;
    }
    default:
        return HT6_BAD_PROTOCOL;
    }
}

// Called from every waiting loop. Must stay cheap: no serial output, no
// per-frame storage.
static void collect(const HT6Case &testCase, CaseResult &result) {
    if (!ir.receive(code)) {
        return;
    }
    // A lossy capture is caught per frame by checkFrame(), which is what
    // drives both the bad counter and the printed reason.
    const uint8_t reason = checkFrame(testCase, code, result.frames);
    if (reason != HT6_OK) {
        if (result.bad == 0) {
            result.firstReason = reason;
            result.firstFrame = result.frames;
            if (code.protocol == UIAPIR_RAW) {
                result.rawCount = code.data.raw.count;
                const uint16_t kept =
                    code.data.raw.count < HT6_RAW_COUNT ? code.data.raw.count : HT6_RAW_COUNT;
                memcpy(result.rawTicks, code.data.raw.ticks, kept);
            }
        }
        if (result.bad < 255) {
            ++result.bad;
        }
    }
    if (result.frames < 255) {
        ++result.frames;
    }
}

// ---------------------------------------------------------------- the run ---

static void discardStaleFrames() {
    if (ir.available()) {
        ir.resume();
    }
}

// Idle means HIGH and staying HIGH: the api-false blink train also spends time
// HIGH, and starting a case in the middle of one would race the next case
// against the tail of the previous answer.
static bool waitForIdle() {
    const uint32_t start = millis();
    uint32_t highSince = 0;
    bool high = false;
    while ((uint32_t)(millis() - start) < HT6_READY_TIMEOUT_MS) {
        if (transmitterIsIdle()) {
            if (!high) {
                high = true;
                highSince = millis();
            } else if ((uint32_t)(millis() - highSince) >= HT6_IDLE_STABLE_MS) {
                return true;
            }
        } else {
            high = false;
        }
        discardStaleFrames();
    }
    return false;
}

static void pulseStart(uint8_t index) {
    for (uint8_t i = 0; i <= index; ++i) {
        digitalWrite(HT6_START_PIN, HIGH);
        delayMicroseconds(HT6_PULSE_US);
        digitalWrite(HT6_START_PIN, LOW);
        delayMicroseconds(HT6_PULSE_US);
    }
}

static void runCase(uint8_t index, CaseResult &result) {
    const HT6Case &testCase = HT6_CASES[index];

    memset(&result, 0, sizeof(result));
    result.apiOk = true;

    result.ready = waitForIdle();
    if (!result.ready) {
        return;
    }
    ir.resume();

    pulseStart(index);

    uint32_t start = millis();
    while ((uint32_t)(millis() - start) < HT6_ACK_TIMEOUT_MS) {
        if (!transmitterIsIdle()) {
            result.acked = true;
            break;
        }
        collect(testCase, result);
    }
    if (!result.acked) {
        return;
    }

    start = millis();
    while ((uint32_t)(millis() - start) < HT6_CASE_TIMEOUT_MS) {
        collect(testCase, result);
        if (transmitterIsIdle()) {
            result.finished = true;
            break;
        }
    }
    result.elapsedMs = millis() - start;
    if (!result.finished) {
        return;
    }

    // STATUS has gone HIGH. A steady HIGH means the send call returned true; a
    // blink train means it returned false. Keep decoding through the window:
    // the last frame's gap has not elapsed yet when the transmitter goes idle.
    start = millis();
    while ((uint32_t)(millis() - start) < HT6_RESULT_WINDOW_MS) {
        collect(testCase, result);
        if (!transmitterIsIdle()) {
            result.apiOk = false;
        }
    }
}

static bool passed(const HT6Case &testCase, const CaseResult &result) {
    return result.ready && result.acked && result.finished &&
           result.frames == testCase.expectFrames && result.bad == 0 &&
           result.apiOk == (testCase.expectApiOk != 0);
}

// --------------------------------------------------------------- printing ---

static void printPadded(const char *text, uint8_t width) {
    uint8_t length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    Serial.print(text);
    while (length < width) {
        Serial.print(' ');
        ++length;
    }
}

// The elapsed column is the one figure worth comparing between cases, so give
// it a fixed width: unpadded it moves every time a case's frame count changes.
static void printRightAligned(uint32_t value, uint8_t width) {
    uint8_t digits = 1;
    for (uint32_t scale = 10; value >= scale && digits < 10; scale *= 10) {
        ++digits;
    }
    while (digits < width) {
        Serial.print(' ');
        ++digits;
    }
    Serial.print(value);
}

static void printReason(uint8_t reason) {
    switch (reason) {
    case HT6_BAD_PROTOCOL: Serial.print("decoded as the wrong protocol"); break;
    case HT6_BAD_COUNT:    Serial.print("wrong number of durations"); break;
    case HT6_BAD_FIELDS:   Serial.print("payload does not match what was sent"); break;
    case HT6_BAD_REPEAT:   Serial.print("repeat flag on the wrong frame"); break;
    case HT6_LOSSY:        Serial.print("capture overflowed or clipped a duration"); break;
    case HT6_UNEXPECTED:   Serial.print("more frames arrived than the case sends"); break;
    default:               Serial.print("ok"); break;
    }
}

static void printResult(uint8_t index, const CaseResult &result) {
    const HT6Case &testCase = HT6_CASES[index];
    const bool ok = passed(testCase, result);

    Serial.print('[');
    if (index < 10) {
        Serial.print('0');
    }
    Serial.print(index);
    Serial.print("] ");
    printPadded(HT6_CASE_NAMES[index], 23);

    Serial.print("frames ");
    Serial.print(result.frames);
    Serial.print('/');
    Serial.print(testCase.expectFrames);
    Serial.print("  api ");
    printPadded(result.apiOk ? "true" : "false", 6);
    printRightAligned(result.elapsedMs, 4);
    Serial.print(" ms  ");
    Serial.println(ok ? "PASS" : "FAIL");

    if (ok) {
        return;
    }
    if (!result.ready) {
        Serial.println("       -> transmitter never went idle: check STATUS, GND, and its power");
        return;
    }
    if (!result.acked) {
        Serial.println("       -> transmitter did not answer START: check the START wire");
        return;
    }
    if (!result.finished) {
        Serial.println("       -> transmitter did not go idle again: it is stuck inside send()");
        return;
    }
    if (result.apiOk != (testCase.expectApiOk != 0)) {
        Serial.print("       -> send() returned ");
        Serial.print(result.apiOk ? "true" : "false");
        Serial.print(", expected ");
        Serial.println(testCase.expectApiOk ? "true" : "false");
    }
    if (result.frames != testCase.expectFrames) {
        Serial.print("       -> ");
        Serial.print(result.frames);
        Serial.print(" frames decoded, expected ");
        Serial.println(testCase.expectFrames);
    }
    if (result.bad != 0) {
        Serial.print("       -> frame ");
        Serial.print(result.firstFrame);
        Serial.print(": ");
        printReason(result.firstReason);
        if (result.bad > 1) {
            Serial.print(" (and ");
            Serial.print(result.bad - 1);
            Serial.print(" more bad frame");
            Serial.print(result.bad > 2 ? "s)" : ")");
        }
        Serial.println();

        if (result.rawCount != 0) {
            Serial.print("       -> got ");
            Serial.print(result.rawCount);
            Serial.print(" durations, expected ");
            Serial.print(HT6_RAW_COUNT);
            Serial.print(" x ");
            Serial.print(HT6_RAW_SLOT_US);
            Serial.print(" us:");
            const uint16_t shown =
                result.rawCount < HT6_RAW_COUNT ? result.rawCount : HT6_RAW_COUNT;
            for (uint16_t i = 0; i < shown; ++i) {
                Serial.print(' ');
                Serial.print((uint16_t)result.rawTicks[i] * UIAPIR_RAW_TICK_US);
            }
            Serial.println();
        }
    }
}

void setup() {
    pinMode(HT6_START_PIN, OUTPUT);
    digitalWrite(HT6_START_PIN, LOW);
    pinMode(HT6_STATUS_PIN, INPUT_PULLDOWN);

    Serial.begin(HT6_SERIAL_BAUD);
    delay(200);
    Serial.println();
    Serial.println("UIAPIR HT6 - two-board send and receive test");
    Serial.print("cases ");
    Serial.print(HT6_CASE_COUNT);
    Serial.print(", capture ");
    Serial.print(HT6_CAPTURE_CAPACITY);
    Serial.print(" durations, frame gap ");
    Serial.print(UIAPIR_FRAME_GAP_US);
    Serial.print(" us, raw tick ");
    Serial.print(UIAPIR_RAW_TICK_US);
    Serial.println(" us");

    if (!pinMapIsAsExpected()) {
        Serial.println("pin map mismatch: this core does not put D3/D8/D9 on PC1/PC6/PC7");
        for (;;) {
        }
    }
    if (!ir.begin(HT6_IR_RX_PIN, UIAPIR_UNUSED_PIN,
                  UIAPIRConfig(captureBuffer, sizeof(captureBuffer)))) {
        Serial.println("begin() failed");
        for (;;) {
        }
    }
    Serial.println("waiting for the transmitter");
}

void loop() {
    static uint32_t run = 0;
    static CaseResult result;

    ++run;
    // The first run prints every case, so the log shows what was actually
    // exercised. After that only failures are worth the serial time, which
    // makes this usable as a soak test.
    const bool verbose = (run == 1);

    if (verbose) {
        Serial.println();
    }
    Serial.print("run ");
    Serial.println(run);

    uint8_t passes = 0;
    for (uint8_t index = 0; index < HT6_CASE_COUNT; ++index) {
        runCase(index, result);
        const bool ok = passed(HT6_CASES[index], result);
        if (ok) {
            ++passes;
        }
        if (verbose || !ok) {
            printResult(index, result);
        }
    }

    Serial.print("run ");
    Serial.print(run);
    Serial.print(": ");
    Serial.print(passes);
    Serial.print('/');
    Serial.print(HT6_CASE_COUNT);
    Serial.println(passes == HT6_CASE_COUNT ? " PASS" : " FAIL");

    delay(2000);
}
