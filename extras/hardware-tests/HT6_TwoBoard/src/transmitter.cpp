// SPDX-License-Identifier: MIT
// Copyright (c) 2026 kei23jp

// HT6 transmitter - a signal generator with no opinions.
//
// This board has no serial port. Everything it knows travels over two wires:
// the receiver names a case on START, and this board answers on STATUS. That
// keeps the single USB-UART on the receiver, where the decoded result is.
//
// STATUS carries three things:
//   HIGH, steady            idle, ready for the next case
//   LOW                     a case is in flight
//   blink train before HIGH the send call returned false
//
// The last one is the only reason this board needs an output at all. A
// rejected argument emits no infrared, and so does a board that never woke up;
// without the blink the receiver could not tell a working rejection from a
// dead transmitter.
//
// Build: pio run -e transmitter -t upload

#include <Arduino.h>
#include <string.h>

#include <UIAPIR.h>

#include "HT6TestPlan.h"

static UIAPIR ir;

// send(IRCode) needs somewhere to build the code. It is the only large object
// here: this instance passes UIAPIR_UNUSED_PIN as its receive pin, so UIAPIR
// allocates no capture buffer.
static IRCode code;

static void setIdle(bool idle) {
    digitalWrite(HT6_STATUS_PIN, idle ? HIGH : LOW);
}

// An Arduino pin number is only a promise the variant table makes. If a
// different core numbers its pins differently, every signal in this test moves
// to a different pad, and the failure would look like bad wiring.
static bool pinMapIsAsExpected() {
    return digitalPinToPinName(HT6_IR_TX_PIN) == HT6_EXPECT_IR_TX_PORT &&
           digitalPinToPinName(HT6_START_PIN) == HT6_EXPECT_START_PORT &&
           digitalPinToPinName(HT6_STATUS_PIN) == HT6_EXPECT_STATUS_PORT;
}

// Reads one START pulse train and returns the case index it names, or -1 when
// nothing arrived. STATUS drops on the first pulse rather than at the end of
// the train, so the receiver sees an acknowledgement immediately and can tell
// "did not answer" from "answered and then hung".
static int16_t readStartTrain() {
    if (digitalRead(HT6_START_PIN) == LOW) {
        return -1;
    }
    setIdle(false);

    uint8_t pulses = 0;
    bool high = true;
    uint32_t lastEdgeMs = millis();
    while ((uint32_t)(millis() - lastEdgeMs) < HT6_TRAIN_END_MS) {
        const bool level = digitalRead(HT6_START_PIN) == HIGH;
        if (level == high) {
            continue;
        }
        high = level;
        lastEdgeMs = millis();
        // Count on the falling edge, so a pulse is only counted once it has
        // completed and a train that is still arriving is never read short.
        if (!level && pulses < 255) {
            ++pulses;
        }
    }
    return pulses == 0 ? (int16_t)-1 : (int16_t)(pulses - 1);
}

static bool runCase(const HT6Case &testCase) {
    switch (testCase.kind) {
    case HT6_KIND_NEC:
        return ir.sendNEC(testCase.address, testCase.command, testCase.param == 16,
                          testCase.count);
    case HT6_KIND_AEHA:
        return ir.sendAEHA(HT6_AEHA_PAYLOAD, testCase.param, testCase.count);
    case HT6_KIND_SONY:
        return ir.sendSony(testCase.address, testCase.command, testCase.param,
                           testCase.count);
    case HT6_KIND_RAW:
        return ir.sendRawTicks(HT6_RAW_TICKS, HT6_RAW_COUNT, UIAPIR_NEC_CARRIER_KHZ);
    case HT6_KIND_RAW_CODE:
        // The replay path a learn-and-replay sketch actually takes, including
        // the check that refuses a capture whose timing was lost. `param`
        // carries the flags to stamp on the code.
        code.clear();
        code.protocol = UIAPIR_RAW;
        code.flags = testCase.param;
        code.carrierKHz = UIAPIR_NEC_CARRIER_KHZ;
        code.data.raw.count = HT6_RAW_COUNT;
        memcpy(code.data.raw.ticks, HT6_RAW_TICKS, HT6_RAW_COUNT);
        return ir.send(code);
    default:
        return false;
    }
}

static void signalApiFalse() {
    for (uint8_t i = 0; i < HT6_API_FALSE_BLINKS; ++i) {
        setIdle(true);
        delay(HT6_API_FALSE_BLINK_MS);
        setIdle(false);
        delay(HT6_API_FALSE_BLINK_MS);
    }
}

void setup() {
    pinMode(HT6_STATUS_PIN, OUTPUT);
    setIdle(false); // busy until this board is actually able to transmit
    pinMode(HT6_START_PIN, INPUT_PULLDOWN);

    // Holding STATUS low forever is the only way this board can report a
    // problem. The receiver prints it as a transmitter that never went idle.
    if (!pinMapIsAsExpected()) {
        for (;;) {
        }
    }
    if (!ir.begin(UIAPIR_UNUSED_PIN, HT6_IR_TX_PIN)) {
        for (;;) {
        }
    }

    setIdle(true);
}

void loop() {
    const int16_t index = readStartTrain();
    if (index < 0) {
        setIdle(true);
        return;
    }

    // Let the START line settle before the carrier starts, so an edge of the
    // handshake cannot land inside a measured frame.
    delay(HT6_PRE_SEND_MS);

    // An index past the end of the table means the two boards were built from
    // different revisions of the test plan. Reporting it as a rejected call is
    // not ideal, but it is a failure either way and the receiver names the
    // case, so the mismatch is visible in the log.
    const bool apiOk =
        (uint8_t)index < HT6_CASE_COUNT ? runCase(HT6_CASES[index]) : false;

    if (!apiOk) {
        signalApiFalse();
    }
    setIdle(true);
}
