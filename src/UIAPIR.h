// SPDX-License-Identifier: MIT
// Copyright (c) 2026 kei23jp

#pragma once

#include <Arduino.h>
#include "UIAPIRTypes.h"
#include "UIAPIRCapture.h"

// The CH32V003 backend drives a TIM1 output channel directly. Every other
// Arduino core uses the portable backend built from tone(), micros(), and
// attachInterrupt().
#if defined(CH32V00x) || defined(CH32V003F4) || defined(CH32V003)
#define UIAPIR_BACKEND_CH32V003 1
#else
#define UIAPIR_BACKEND_CH32V003 0
#endif

#ifndef UIAPIR_DEFAULT_TX_PIN
#define UIAPIR_DEFAULT_TX_PIN 6
#endif

#define UIAPIR_UNUSED_PIN 0xff

// ---------------------------------------------------------------------------
// Direction selection
// ---------------------------------------------------------------------------
// -DUIAPIR_ENABLE_TX=0 or -DUIAPIR_ENABLE_RX=0 compiles one direction out, the
// same way UIAPIR_ENABLE_<protocol> does for a protocol: its methods and their
// declarations go, so calling one is a compile error, not a runtime false.
//
// On the CH32V003 the point is as much the timers as the flash. The part has
// two, and a full build owns both:
//
//   TIM1  carrier PWM             gone with UIAPIR_ENABLE_TX=0
//   TIM2  1 MHz timebase          gone with UIAPIR_ENABLE_RX=0; a transmit-only
//                                 build times its envelope from SysTick
//
// So a receive-only build leaves TIM1 to Servo and analogWrite() on its pads,
// and a transmit-only build leaves TIM2 to tone() and analogWrite() on its.
//
// Like every UIAPIR_* option, these must be defined for the whole build (see
// README), not with a #define in the sketch.
#ifndef UIAPIR_ENABLE_TX
#define UIAPIR_ENABLE_TX 1
#endif
#ifndef UIAPIR_ENABLE_RX
#define UIAPIR_ENABLE_RX 1
#endif
#if !UIAPIR_ENABLE_TX && !UIAPIR_ENABLE_RX
#error "UIAPIR_ENABLE_TX and UIAPIR_ENABLE_RX are both 0: nothing is left to build"
#endif

// begin()'s default TX pin. Without a transmitter the only acceptable TX pin is
// UIAPIR_UNUSED_PIN, so begin(rxPin) has to default to that, not to a pad.
#if UIAPIR_ENABLE_TX
#define UIAPIR_BEGIN_DEFAULT_TX_PIN UIAPIR_DEFAULT_TX_PIN
#else
#define UIAPIR_BEGIN_DEFAULT_TX_PIN UIAPIR_UNUSED_PIN
#endif

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

    // On UIAPduino / CH32V003, TX must be a pad with a TIM1 output channel:
    // PD2 (A3, CH1), PA1 (A1, CH2), PC3 (5, CH3) or PC4 (A2 / D6, CH4).
    // Other Arduino targets can use any tone-capable output pin.
    // UIAPIR_UNUSED_PIN builds a receive-only instance.
    // A direction compiled out (UIAPIR_ENABLE_TX / _RX = 0) only accepts
    // UIAPIR_UNUSED_PIN for its pin; the config then has nothing to size.
    bool begin(uint8_t rxPin, uint8_t txPin = UIAPIR_BEGIN_DEFAULT_TX_PIN);
    bool begin(uint8_t rxPin, uint8_t txPin, const UIAPIRConfig &config);
    bool begin(uint8_t rxPin, const UIAPIRConfig &config) {
        return begin(rxPin, UIAPIR_BEGIN_DEFAULT_TX_PIN, config);
    }
    void end();

#if UIAPIR_ENABLE_RX
    bool available();
    bool receive(IRCode &code);
    bool learn(IRCode &code) { return receive(code); }
    void resume();
#endif

#if UIAPIR_ENABLE_TX
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
#endif // UIAPIR_ENABLE_TX

private:
    static UIAPIR *_active;

    bool configureTimingTimer();

#if UIAPIR_ENABLE_RX
    static void edgeISR();
    void onEdge();
    void attachReceiver();
    void detachReceiver();
    bool rxLevelIsMark() const;
    uint32_t elapsedSinceLastEdge();
#if UIAPIR_BACKEND_CH32V003
    uint16_t timerNow() const;
#endif
#endif // UIAPIR_ENABLE_RX

#if UIAPIR_ENABLE_TX
    bool configureCarrier(uint8_t carrierKHz);
    void carrierOn();
    void carrierOff();
#if UIAPIR_BACKEND_CH32V003
    void releaseCarrierPad();
#endif
    void waitUs(uint32_t durationUs) const;
    void mark(uint32_t durationUs);
    void space(uint32_t durationUs);
    void resetFrameClock();
    void padToPeriod(uint32_t periodUs);
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
#endif // UIAPIR_ENABLE_TX

    // Members carry their initial values here, next to the switch that
    // decides whether they exist, so the constructor has no list to keep in
    // step with every combination of switches.
    uint8_t _rxPin = UIAPIR_UNUSED_PIN;
    uint8_t _txPin = UIAPIR_UNUSED_PIN;
    bool _started = false;

#if UIAPIR_ENABLE_TX
    uint16_t _carrierCompare = 0;
    uint8_t _carrierKHz = 0;

    // Microseconds emitted since the current frame started. Inter-frame
    // spacing is accumulated in software so it does not depend on a hardware
    // timer's counter width.
    uint32_t _txElapsedUs = 0;

#if UIAPIR_BACKEND_CH32V003
    // The carrier pad, resolved once in begin() so that mark() and space()
    // write a single compare register and never walk the pin map.
    uint8_t _carrierChannel = 0; // TIM1 channel 1..4, or 0 with no TX pin
    GPIO_TypeDef *_carrierPort = nullptr;
    uint16_t _carrierPinMask = 0;
    volatile uint32_t *_carrierCompareReg = nullptr;

#if !UIAPIR_ENABLE_RX
    // Transmit-only builds time from SysTick instead of TIM2: its reload
    // length and ticks per microsecond, read in begin().
    uint32_t _sysTickReload = 0;
    uint32_t _sysTickPerUs = 0;
#endif
#endif
#endif // UIAPIR_ENABLE_TX

#if UIAPIR_ENABLE_RX
    bool _receiverAttached = false;

#if UIAPIR_BACKEND_CH32V003
    // The CH32V003 backend resolves the receiver pin once so the ISR never
    // walks the core's pin map.
    GPIO_TypeDef *_rxPort = nullptr;
    uint32_t _rxMask = 0;

    // TIM2 wraps every 65.536 ms, which is shorter than the idle periods
    // between bursts, so edge timestamps carry a millisecond stamp alongside
    // the counter to tell a long gap from an aliased short one.
    volatile uint16_t _lastEdgeTicks = 0;
    volatile uint32_t _lastEdgeMs = 0;
#else
    // micros() is a 32-bit counter on the portable backend; unsigned
    // subtraction keeps elapsed intervals correct across its wraparound.
    volatile uint32_t _lastEdgeUs = 0;
#endif

    uint8_t *_captureBuffer = nullptr;
    bool _ownsCaptureBuffer = false;
    uiapir_capture::IRCapture _capture;
#endif // UIAPIR_ENABLE_RX
};
