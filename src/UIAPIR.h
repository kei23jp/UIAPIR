// SPDX-License-Identifier: MIT
// Copyright (c) 2026 kei23jp

#pragma once

#include <Arduino.h>
#include "UIAPIRTypes.h"
#include "UIAPIRCapture.h"

// The CH32V003 backend drives TIM1_CH4 directly. Every other Arduino core
// uses the portable backend built from tone(), micros(), and attachInterrupt().
#if defined(CH32V00x) || defined(CH32V003F4) || defined(CH32V003)
#define UIAPIR_BACKEND_CH32V003 1
#else
#define UIAPIR_BACKEND_CH32V003 0
#endif

#ifndef UIAPIR_DEFAULT_TX_PIN
#define UIAPIR_DEFAULT_TX_PIN 6
#endif

#define UIAPIR_UNUSED_PIN 0xff

// Runtime memory settings for begin(). When captureBuffer is null, UIAPIR
// allocates captureBufferSize bytes and releases them in end(). Supplying a
// buffer avoids heap use; that storage must remain valid until end().
struct UIAPIRConfig {
    uint8_t *captureBuffer;
    uint16_t captureBufferSize;

    explicit UIAPIRConfig(uint16_t size = UIAPIR_RAW_BUFFER_SIZE)
        : captureBuffer(nullptr), captureBufferSize(size) {}
    UIAPIRConfig(uint8_t *buffer, uint16_t size)
        : captureBuffer(buffer), captureBufferSize(size) {}
};

class UIAPIR {
public:
    UIAPIR();

    UIAPIR(const UIAPIR &) = delete;
    UIAPIR &operator=(const UIAPIR &) = delete;

    // On UIAPduino / CH32V003, TX is fixed to PC4 (D6 / A2, TIM1_CH4).
    // Other Arduino targets can use any tone-capable output pin.
    // UIAPIR_UNUSED_PIN builds a receive-only instance.
    bool begin(uint8_t rxPin, uint8_t txPin = UIAPIR_DEFAULT_TX_PIN);
    bool begin(uint8_t rxPin, uint8_t txPin, const UIAPIRConfig &config);
    bool begin(uint8_t rxPin, const UIAPIRConfig &config) {
        return begin(rxPin, UIAPIR_DEFAULT_TX_PIN, config);
    }
    void end();

    bool available();
    bool receive(IRCode &code);
    bool learn(IRCode &code) { return receive(code); }
    void resume();

    bool send(const IRCode &code, uint8_t repeats = 0);

    // The three protocol senders go away with their UIAPIR_ENABLE_<name>
    // switch, along with the matching decoder. send(IRCode) keeps compiling
    // either way and returns false for a code whose protocol is not built.

#if UIAPIR_ENABLE_NEC
    // `address` must be 0..255 unless `extended` is set. An extended address
    // whose high byte is the complement of its low byte is rejected: those 256
    // values are reserved for standard NEC and would decode back as 8-bit.
    // Each repeat frame starts 110 ms after the previous frame started.
    bool sendNEC(uint16_t address, uint8_t command, bool extended = false, uint8_t repeats = 0);
    bool sendNECRepeat();
#endif

#if UIAPIR_ENABLE_AEHA
    // `length` is UIAPIR_AEHA_MIN_BYTES..UIAPIR_MAX_AEHA_BYTES. Frames after
    // the first start 130 ms after the previous frame started.
    bool sendAEHA(const uint8_t *data, uint8_t length, uint8_t frames = 1);
#endif

#if UIAPIR_ENABLE_SONY
    // SIRC requires a frame to be sent at least UIAPIR_SONY_MIN_FRAMES times.
    // `bits` is 12, 15, or 20, giving a 5, 8, or 13-bit address field.
    bool sendSony(uint16_t address, uint8_t command, uint8_t bits = 12,
                  uint8_t frames = UIAPIR_SONY_MIN_FRAMES);
#endif

    bool sendRaw(const uint16_t *timingsUs, uint16_t count, uint8_t carrierKHz = 38);
    bool sendRawTicks(const uint8_t *ticks, uint16_t count, uint8_t carrierKHz = 38);

private:
    static UIAPIR *_active;
    static void edgeISR();
    void onEdge();

    bool configureTimingTimer();
    bool configureCarrier(uint8_t carrierKHz);
    void carrierOn();
    void carrierOff();
#if UIAPIR_BACKEND_CH32V003
    uint16_t timerNow() const;
#endif
    void waitUs(uint32_t durationUs) const;
    void mark(uint32_t durationUs);
    void space(uint32_t durationUs);
    void resetFrameClock();
    void padToPeriod(uint32_t periodUs);
    void attachReceiver();
    void detachReceiver();
    bool rxLevelIsMark() const;
    uint32_t elapsedSinceLastEdge();
    bool beginTransmit(uint8_t carrierKHz);
    void endTransmit();
#if UIAPIR_ENABLE_NEC || UIAPIR_ENABLE_AEHA
    void sendBitSpaceEncoded(uint32_t value, uint8_t bits, uint16_t markUs,
                             uint16_t zeroSpaceUs, uint16_t oneSpaceUs);
#endif
#if UIAPIR_ENABLE_NEC
    void sendNECFrame(uint16_t address, uint8_t command, bool extended);
    void sendNECRepeatFrame();
#endif
#if UIAPIR_ENABLE_AEHA
    void sendAEHAFrame(const uint8_t *data, uint8_t length);
#endif
#if UIAPIR_ENABLE_SONY
    void sendSonyFrame(uint32_t value, uint8_t bits);
#endif

    uint8_t _rxPin;
    uint8_t _txPin;
    bool _started;
    bool _receiverAttached;
    uint16_t _carrierCompare;
    uint8_t _carrierKHz;

    // Microseconds emitted since the current frame started. Inter-frame
    // spacing is accumulated in software so it does not depend on a hardware
    // timer's counter width.
    uint32_t _txElapsedUs;

    // The CH32V003 backend resolves the receiver pin once so the ISR never
    // walks the core's pin map.
#if UIAPIR_BACKEND_CH32V003
    GPIO_TypeDef *_rxPort;
    uint32_t _rxMask;

    // TIM2 wraps every 65.536 ms, which is shorter than the idle periods
    // between bursts, so edge timestamps carry a millisecond stamp alongside
    // the counter to tell a long gap from an aliased short one.
    volatile uint16_t _lastEdgeTicks;
    volatile uint32_t _lastEdgeMs;
#else
    // micros() is a 32-bit counter on the portable backend; unsigned
    // subtraction keeps elapsed intervals correct across its wraparound.
    volatile uint32_t _lastEdgeUs;
#endif

    uint8_t *_captureBuffer;
    bool _ownsCaptureBuffer;
    uiapir_capture::IRCapture _capture;
};
