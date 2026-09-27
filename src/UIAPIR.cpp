// SPDX-License-Identifier: MIT
// Copyright (c) 2026 kei23jp

#include "UIAPIR.h"
#include "UIAPIRProtocol.h"

#include <stdlib.h>

using uiapir_capture::IRCapture;

namespace {

#if UIAPIR_BACKEND_CH32V003 && UIAPIR_ENABLE_TX
// Arduino pin numbers are not unique per pad on this board: the analog aliases
// are separate numbers for the same physical pin, so A2 (0xc2) and D6 (6) both
// resolve to PC4. Comparing the numbers would miss that.
bool samePhysicalPin(uint8_t a, uint8_t b) {
    const PinName pa = digitalPinToPinName(a);
    const PinName pb = digitalPinToPinName(b);
    if (pa == NC || pb == NC) {
        return false;
    }
    return CH_PORT(pa) == CH_PORT(pb) && CH_GPIO_PIN(pa) == CH_GPIO_PIN(pb);
}

// The TIM1 output channel behind a pad, or 0 for a pad that has none.
//
// This is the CH32V003's reset-state (non-remapped) TIM1 mapping, the one the
// core's PinMap_TIM lists. UIAPIR never touches AFIO_PCFR1, so TIM1_RM stays
// 00 and these four are the only pads a carrier can come out of. The
// complementary outputs CH1N (PD0), CH2N (PA2) and CH3N (PD1) are left out on
// purpose: PD1 is SWIO, the programming line, and the other two would need the
// dead-time and idle-state fields configured for no gain on a board that has
// four plain channels already.
//
//   channel  pad  UIAPduino silk
//   CH1      PD2  A3
//   CH2      PA1  A1
//   CH3      PC3  5
//   CH4      PC4  A2 (also D6)
uint8_t carrierChannelOf(uint8_t arduinoPin) {
    const PinName pin = digitalPinToPinName(arduinoPin);
    if (pin == NC) {
        return 0;
    }
    const uint32_t port = CH_PORT(pin);
    const uint32_t mask = CH_GPIO_PIN(pin);
    if (port == PortD && mask == GPIO_Pin_2) return 1;
    if (port == PortA && mask == GPIO_Pin_1) return 2;
    if (port == PortC && mask == GPIO_Pin_3) return 3;
    if (port == PortC && mask == GPIO_Pin_4) return 4;
    return 0;
}

volatile uint32_t *carrierCompareRegisterOf(uint8_t channel) {
    switch (channel) {
    case 1: return &TIM1->CH1CVR;
    case 2: return &TIM1->CH2CVR;
    case 3: return &TIM1->CH3CVR;
    default: return &TIM1->CH4CVR;
    }
}

// TIM_Channel_1..4 as TIM_CCxCmd() wants them: 0, 4, 8, 12.
uint16_t timChannelIdOf(uint8_t channel) {
    return (uint16_t)((channel - 1U) * 4U);
}

uint32_t gpioClockOf(const GPIO_TypeDef *port) {
    if (port == GPIOA) return RCC_APB2Periph_GPIOA;
    if (port == GPIOC) return RCC_APB2Periph_GPIOC;
    return RCC_APB2Periph_GPIOD;
}
#elif UIAPIR_ENABLE_TX && UIAPIR_ENABLE_RX
// Only begin()'s RX/TX same-pad check needs it off the CH32V003, and that
// check exists only when both directions are built.
bool samePhysicalPin(uint8_t a, uint8_t b) {
    return a == b;
}
#endif

#if !UIAPIR_BACKEND_CH32V003 && UIAPIR_ENABLE_RX
bool receiverPinIsValid(uint8_t pin) {
    const int interrupt = digitalPinToInterrupt(pin);
#ifdef NOT_AN_INTERRUPT
    return interrupt != NOT_AN_INTERRUPT;
#else
    return interrupt >= 0;
#endif
}
#endif

} // namespace

UIAPIR *UIAPIR::_active = nullptr;

// Every member has its initial value in UIAPIR.h, beside the build switch
// that decides whether it exists.
UIAPIR::UIAPIR() {
}

bool UIAPIR::begin(uint8_t rxPin, uint8_t txPin) {
    return begin(rxPin, txPin, UIAPIRConfig());
}

bool UIAPIR::begin(uint8_t rxPin, uint8_t txPin, const UIAPIRConfig &config) {
    if (_active && _active != this) {
        return false; // EXTI callback and timers are intentionally single-instance.
    }
    // A direction compiled out has no code behind it, so a pin for it is
    // refused rather than ignored: the sketch asked for something this build
    // cannot do, and a begin() that reports success would hide that.
#if !UIAPIR_ENABLE_TX
    if (txPin != UIAPIR_UNUSED_PIN) {
        return false;
    }
#endif
#if !UIAPIR_ENABLE_RX
    (void)config; // it only sizes the capture buffer
    if (rxPin != UIAPIR_UNUSED_PIN) {
        return false;
    }
#endif
    // The CH32V003 carrier is TIM1 PWM, so the transmit pin has to be a pad
    // with a TIM1 output channel behind it: PD2, PA1, PC3 or PC4 (silk A3, A1,
    // 5, A2). The check resolves the pad rather than comparing Arduino
    // numbers: A2 (0xc2) and D6 (6) are two numbers for the same PC4, and a
    // sketch written against the board silk must not be refused for it.
#if UIAPIR_BACKEND_CH32V003 && UIAPIR_ENABLE_TX
    const uint8_t carrierChannel = (txPin == UIAPIR_UNUSED_PIN) ? 0 : carrierChannelOf(txPin);
    if (txPin != UIAPIR_UNUSED_PIN && carrierChannel == 0) {
        return false;
    }
#endif
#if UIAPIR_ENABLE_RX
    if (rxPin != UIAPIR_UNUSED_PIN) {
        // An out-of-range pin makes the core's attachInterrupt() return without
        // doing anything, which would leave begin() reporting success on a
        // receiver that can never fire.
#if UIAPIR_BACKEND_CH32V003
        if (!digitalPinIsValid(rxPin)) {
#else
        if (!receiverPinIsValid(rxPin)) {
#endif
            return false;
        }
#if UIAPIR_ENABLE_TX
        // One pad cannot host both the demodulator output and a push-pull
        // carrier drive.
        if (txPin != UIAPIR_UNUSED_PIN && samePhysicalPin(rxPin, txPin)) {
            return false;
        }
#endif
    }

    uint8_t *nextCaptureBuffer = nullptr;
    bool nextOwnsCaptureBuffer = false;
    uint16_t nextCaptureBufferSize = 0;
    if (rxPin != UIAPIR_UNUSED_PIN) {
        nextCaptureBufferSize = config.captureBufferSize;
        if (nextCaptureBufferSize < UIAPIR_MIN_FRAME_DURATIONS ||
            nextCaptureBufferSize > UIAPIR_RAW_BUFFER_SIZE) {
            return false;
        }

        if (config.captureBuffer != nullptr) {
            nextCaptureBuffer = config.captureBuffer;
        } else if (_ownsCaptureBuffer && _captureBuffer != nullptr &&
                   _capture.capacity() == nextCaptureBufferSize) {
            // Reuse the current allocation when begin() only changes pins.
            nextCaptureBuffer = _captureBuffer;
            nextOwnsCaptureBuffer = true;
        } else {
            nextCaptureBuffer = (uint8_t *)malloc(nextCaptureBufferSize);
            if (nextCaptureBuffer == nullptr) {
                return false;
            }
            nextOwnsCaptureBuffer = true;
        }
    }

    // Re-configuring an already started instance: release the pin the previous
    // call armed. Overwriting _rxPin first would leave that EXTI live on a pin
    // this object no longer refers to, and end() would detach the wrong one.
    if (_started) {
        detachReceiver();
    }
#endif // UIAPIR_ENABLE_RX

    if (!configureTimingTimer()) {
#if UIAPIR_ENABLE_RX
        if (nextOwnsCaptureBuffer && nextCaptureBuffer != _captureBuffer) {
            free(nextCaptureBuffer);
        }
        if (_started && _rxPin != UIAPIR_UNUSED_PIN) {
            attachReceiver();
        }
#endif
        return false;
    }

#if UIAPIR_BACKEND_CH32V003 && UIAPIR_ENABLE_TX
    // Moving the carrier to another pad on a started instance: hand the old
    // pad back first, or TIM1 would keep it claimed and driven low.
    if (_started && _txPin != UIAPIR_UNUSED_PIN &&
        (txPin == UIAPIR_UNUSED_PIN || !samePhysicalPin(_txPin, txPin))) {
        releaseCarrierPad();
    }
    _carrierChannel = carrierChannel;
    if (carrierChannel != 0) {
        const PinName pin = digitalPinToPinName(txPin);
        _carrierPort = get_GPIO_Port(CH_PORT(pin));
        _carrierPinMask = (uint16_t)CH_GPIO_PIN(pin);
        _carrierCompareReg = carrierCompareRegisterOf(carrierChannel);
    }
#endif

    _rxPin = rxPin;
    _txPin = txPin;
#if UIAPIR_ENABLE_RX
    uint8_t *oldCaptureBuffer = _captureBuffer;
    const bool oldOwnsCaptureBuffer = _ownsCaptureBuffer;
    _captureBuffer = nextCaptureBuffer;
    _ownsCaptureBuffer = nextOwnsCaptureBuffer;
    _capture.setBuffer(nextCaptureBuffer, nextCaptureBufferSize);
    if (oldOwnsCaptureBuffer && oldCaptureBuffer != nextCaptureBuffer) {
        free(oldCaptureBuffer);
    }
#endif

    _active = this;
    _started = true;
#if UIAPIR_ENABLE_RX
    if (_rxPin != UIAPIR_UNUSED_PIN) {
        attachReceiver();
    }
#endif
    return true;
}

void UIAPIR::end() {
    if (_active != this) {
        // This instance never took ownership: begin() was refused, or end() has
        // already run. TIM1 and TIM2 are shared, so stopping them from here
        // would halt another instance mid-transmit and hang it in waitUs().
        _started = false;
#if UIAPIR_ENABLE_RX
        if (_ownsCaptureBuffer) {
            free(_captureBuffer);
        }
        _captureBuffer = nullptr;
        _ownsCaptureBuffer = false;
        _capture.setBuffer(nullptr, 0);
#endif
        return;
    }
#if UIAPIR_ENABLE_RX
    detachReceiver();
#endif
#if UIAPIR_BACKEND_CH32V003
#if UIAPIR_ENABLE_TX
    if (_carrierCompare != 0) {
        // TIM1 is only clocked once a transmit has configured the carrier.
        releaseCarrierPad();
        TIM_Cmd(TIM1, DISABLE);
    }
#endif
#if UIAPIR_ENABLE_RX
    // Only a build with the receiver ever started TIM2. A transmit-only build
    // times from SysTick, and TIM2 may be running tone() for the sketch.
    TIM_Cmd(TIM2, DISABLE);
#endif
#elif UIAPIR_ENABLE_TX
    carrierOff();
    _carrierKHz = 0;
#endif
    _started = false;
    _active = nullptr;
#if UIAPIR_ENABLE_RX
    if (_ownsCaptureBuffer) {
        free(_captureBuffer);
    }
    _captureBuffer = nullptr;
    _ownsCaptureBuffer = false;
    _capture.setBuffer(nullptr, 0);
#endif
}

bool UIAPIR::configureTimingTimer() {
#if UIAPIR_BACKEND_CH32V003 && UIAPIR_ENABLE_RX
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    TIM_TimeBaseInitTypeDef timer = {};
    uint32_t divisor = SystemCoreClock / 1000000U;
    if (divisor == 0 || divisor > 65536U) {
        return false;
    }
    timer.TIM_Period = 0xffff;
    timer.TIM_Prescaler = divisor - 1;
    timer.TIM_ClockDivision = TIM_CKD_DIV1;
    timer.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &timer);
    TIM_SetCounter(TIM2, 0);
    TIM_Cmd(TIM2, ENABLE);
    return true;
#elif UIAPIR_BACKEND_CH32V003
    // Transmit-only: waitUs() times from the core's SysTick millisecond tick
    // (see there) and TIM2 is never touched. Refuse to start if SysTick is not
    // running as an auto-reloading tick, rather than transmit with timing that
    // is wrong.
    const uint32_t enabledAutoReload = 0x9U; // CTLR.STE | CTLR.STRE
    const uint32_t reload = SysTick->CMP + 1U;
    const uint32_t ticksPerUs = reload / 1000U;
    if (ticksPerUs == 0 || (SysTick->CTLR & enabledAutoReload) != enabledAutoReload) {
        return false;
    }
    _sysTickReload = reload;
    _sysTickPerUs = ticksPerUs;
    return true;
#else
    return true;
#endif
}

#if UIAPIR_ENABLE_TX
bool UIAPIR::configureCarrier(uint8_t carrierKHz) {
    // [ChaN] the three supported formats all sit in the 33..40 kHz band.
    // begin() resolved the pad to a TIM1 channel; everything below programs
    // that channel and that pad, whichever Arduino number named it.
#if UIAPIR_BACKEND_CH32V003
    if (_carrierChannel == 0 || carrierKHz < 20 || carrierKHz > 60) {
        return false;
    }

    RCC_APB2PeriphClockCmd(gpioClockOf(_carrierPort) | RCC_APB2Periph_TIM1, ENABLE);
    GPIO_InitTypeDef gpio = {};
    gpio.GPIO_Pin = _carrierPinMask;
    gpio.GPIO_Speed = GPIO_Speed_10MHz;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(_carrierPort, &gpio);

    const uint32_t frequency = (uint32_t)carrierKHz * 1000U;
    uint32_t periodCounts = (SystemCoreClock + frequency / 2U) / frequency;
    if (periodCounts < 3 || periodCounts > 65536U) {
        return false;
    }

    TIM_Cmd(TIM1, DISABLE);
    TIM_TimeBaseInitTypeDef timer = {};
    timer.TIM_Period = periodCounts - 1U;
    timer.TIM_Prescaler = 0;
    timer.TIM_ClockDivision = TIM_CKD_DIV1;
    timer.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM1, &timer);

    TIM_OCInitTypeDef output = {};
    output.TIM_OCMode = TIM_OCMode_PWM1;
    output.TIM_OutputState = TIM_OutputState_Enable;
    output.TIM_Pulse = 0;
    output.TIM_OCPolarity = TIM_OCPolarity_High;
    // TIM_OC1..3Init() also take the complementary-output and idle-state
    // fields; zero-initialised they leave CHxN disabled and idle low.
    switch (_carrierChannel) {
    case 1:
        TIM_OC1Init(TIM1, &output);
        TIM_OC1PreloadConfig(TIM1, TIM_OCPreload_Disable);
        break;
    case 2:
        TIM_OC2Init(TIM1, &output);
        TIM_OC2PreloadConfig(TIM1, TIM_OCPreload_Disable);
        break;
    case 3:
        TIM_OC3Init(TIM1, &output);
        TIM_OC3PreloadConfig(TIM1, TIM_OCPreload_Disable);
        break;
    default:
        TIM_OC4Init(TIM1, &output);
        TIM_OC4PreloadConfig(TIM1, TIM_OCPreload_Disable);
        break;
    }
    TIM_ARRPreloadConfig(TIM1, ENABLE);
    TIM_CtrlPWMOutputs(TIM1, ENABLE);
    TIM_SetCounter(TIM1, 0);
    TIM_Cmd(TIM1, ENABLE);

    _carrierCompare = periodCounts / 3U; // [ChaN] 1/3 duty for all three formats.
    carrierOff();
    return true;
#else
    if (_txPin == UIAPIR_UNUSED_PIN || carrierKHz < 20 || carrierKHz > 60) {
        return false;
    }
    pinMode(_txPin, OUTPUT);
    _carrierKHz = carrierKHz;
    carrierOff();
    return true;
#endif
}

void UIAPIR::carrierOn() {
#if UIAPIR_BACKEND_CH32V003
    *_carrierCompareReg = _carrierCompare;
#else
    tone(_txPin, (uint32_t)_carrierKHz * 1000U);
#endif
}

void UIAPIR::carrierOff() {
#if UIAPIR_BACKEND_CH32V003
    *_carrierCompareReg = 0;
#else
    if (_txPin != UIAPIR_UNUSED_PIN) {
        noTone(_txPin);
        digitalWrite(_txPin, LOW);
    }
#endif
}

#if UIAPIR_BACKEND_CH32V003
// Undo configureCarrier() for the current pad. Used when end() releases the
// transmitter and when begin() moves it to another pad; a no-op until a
// transmit has actually programmed TIM1 for this pad.
void UIAPIR::releaseCarrierPad() {
    if (_carrierCompare == 0) {
        return;
    }
    carrierOff();
    TIM_CCxCmd(TIM1, timChannelIdOf(_carrierChannel), TIM_CCx_Disable);
    // Hand the pad back as a plain output held low. Left in alternate function
    // it would stay claimed by TIM1; left floating, an LED driver's base would
    // be undefined.
    pinMode(_txPin, OUTPUT);
    digitalWrite(_txPin, LOW);
    _carrierCompare = 0;
}
#endif

void UIAPIR::waitUs(uint32_t durationUs) const {
#if UIAPIR_BACKEND_CH32V003 && UIAPIR_ENABLE_RX
    while (durationUs) {
        const uint16_t chunk = durationUs > 60000U ? 60000U : (uint16_t)durationUs;
        const uint16_t start = timerNow();
        while ((uint16_t)(timerNow() - start) < chunk) {
        }
        durationUs -= chunk;
    }
#elif UIAPIR_BACKEND_CH32V003
    // Transmit-only build: TIM2 is left to the sketch, so time the envelope
    // from SysTick instead. The core runs it as the millisecond tick - an up
    // counter that reloads from CMP to 0 every millisecond - and never
    // reconfigures it, so reading the counter leaves millis() alone.
    //
    // Each poll adds the ticks since the previous one, allowing for a reload
    // in between. Polls are a few cycles apart, nowhere near a millisecond, so
    // no reload is missed; and summing in software avoids the window in which
    // the counter has reloaded but the millisecond count has not yet caught up.
    //
    // The core's micros() is no substitute: it computes in 64 bits on a core
    // with no multiplier or divider, and on the CH32V003 it treats the up
    // counter as a down counter, so it steps backwards inside each millisecond.
    const uint32_t reload = _sysTickReload;
    while (durationUs) {
        const uint32_t chunk = durationUs > 1000000U ? 1000000U : durationUs;
        uint32_t remaining = chunk * _sysTickPerUs;
        uint32_t previous = SysTick->CNT;
        while (remaining) {
            const uint32_t now = SysTick->CNT;
            const uint32_t step = now >= previous ? now - previous : now + reload - previous;
            previous = now;
            remaining = step >= remaining ? 0 : remaining - step;
        }
        durationUs -= chunk;
    }
#else
    const uint32_t start = micros();
    while ((uint32_t)(micros() - start) < durationUs) {
    }
#endif
}

void UIAPIR::mark(uint32_t durationUs) {
    carrierOn();
    waitUs(durationUs);
    carrierOff();
    _txElapsedUs += durationUs;
}

void UIAPIR::space(uint32_t durationUs) {
    carrierOff();
    waitUs(durationUs);
    _txElapsedUs += durationUs;
}

void UIAPIR::resetFrameClock() {
    _txElapsedUs = 0;
}

void UIAPIR::padToPeriod(uint32_t periodUs) {
    // A frame period runs from the start of one frame to the start of the
    // next, so the gap depends on how long the frame just sent actually was.
    // The timebase wraps well inside a NEC frame (TIM2 every 65.536 ms,
    // SysTick every millisecond), so the elapsed time is accumulated in
    // software rather than measured.
    const uint32_t remaining = _txElapsedUs < periodUs ? periodUs - _txElapsedUs : 0;
    space(remaining > UIAPIR_MIN_TX_GAP_US ? remaining : UIAPIR_MIN_TX_GAP_US);
    resetFrameClock();
}
#endif // UIAPIR_ENABLE_TX

#if UIAPIR_ENABLE_RX
#if UIAPIR_BACKEND_CH32V003
uint16_t UIAPIR::timerNow() const {
    return (uint16_t)TIM_GetCounter(TIM2);
}
#endif

void UIAPIR::attachReceiver() {
    if (_receiverAttached || _rxPin == UIAPIR_UNUSED_PIN) {
        return;
    }
#if UIAPIR_BACKEND_CH32V003
    attachInterrupt(_rxPin, GPIO_Mode_IPU, edgeISR, EXTI_Mode_Interrupt,
                    EXTI_Trigger_Rising_Falling);
    // Resolve the pin once. The ISR reads the level on every edge to tell a
    // mark from a space, and the core's digitalRead() walks a pin map to do it.
    const PinName pin = digitalPinToPinName(_rxPin);
    _rxPort = get_GPIO_Port(CH_PORT(pin));
    _rxMask = (uint32_t)CH_GPIO_PIN(pin);

    noInterrupts();
    _lastEdgeTicks = timerNow();
    _lastEdgeMs = millis();
    interrupts();
#else
    const int interrupt = digitalPinToInterrupt(_rxPin);
    if (!receiverPinIsValid(_rxPin)) {
        return;
    }
    pinMode(_rxPin, INPUT_PULLUP);
    noInterrupts();
    _lastEdgeUs = micros();
    interrupts();
    attachInterrupt(interrupt, edgeISR, CHANGE);
#endif
    _receiverAttached = true;
}

void UIAPIR::detachReceiver() {
    if (_receiverAttached) {
#if UIAPIR_BACKEND_CH32V003
        detachInterrupt(_rxPin);
#else
        detachInterrupt(digitalPinToInterrupt(_rxPin));
#endif
        _receiverAttached = false;
    }
}

void UIAPIR::edgeISR() {
    if (_active) {
        _active->onEdge();
    }
}

bool UIAPIR::rxLevelIsMark() const {
    // A demodulating receiver idles high and pulls low for a mark.
#if UIAPIR_BACKEND_CH32V003
    return _rxPort != nullptr && (_rxPort->INDR & _rxMask) == 0;
#else
    return digitalRead(_rxPin) == LOW;
#endif
}

uint32_t UIAPIR::elapsedSinceLastEdge() {
#if UIAPIR_BACKEND_CH32V003
    const uint16_t nowTicks = timerNow();
    const uint32_t nowMs = millis();
    const uint32_t elapsed = uiapir_capture::elapsedUs(nowTicks, _lastEdgeTicks,
                                                       nowMs, _lastEdgeMs);
    _lastEdgeTicks = nowTicks;
    _lastEdgeMs = nowMs;
    return elapsed;
#else
    const uint32_t nowUs = micros();
    const uint32_t elapsed = nowUs - _lastEdgeUs;
    _lastEdgeUs = nowUs;
    return elapsed;
#endif
}

void UIAPIR::onEdge() {
    // The timestamp advances on every edge, including those dropped while a
    // frame waits to be read, so the gap before the next frame is measured
    // from the right place.
    const uint32_t elapsed = elapsedSinceLastEdge();
    _capture.onEdge(elapsed, rxLevelIsMark());
}

bool UIAPIR::available() {
    if (!_started) return false;
    noInterrupts();
#if UIAPIR_BACKEND_CH32V003
    const uint32_t elapsed = uiapir_capture::elapsedUs(timerNow(), _lastEdgeTicks,
                                                        millis(), _lastEdgeMs);
#else
    const uint32_t elapsed = micros() - _lastEdgeUs;
#endif
    const bool ready = _capture.poll(elapsed);
    interrupts();
    return ready;
}

void UIAPIR::resume() {
    noInterrupts();
    _capture.reset();
    interrupts();
}

bool UIAPIR::receive(IRCode &code) {
    if (!available()) return false;

    code.clear();
    noInterrupts();
    const uint16_t count = _capture.count();
    code.protocol = UIAPIR_RAW;
    code.carrierKHz = UIAPIR_NEC_CARRIER_KHZ;
    code.data.raw.count = count;
    for (uint16_t i = 0; i < count; ++i) {
        code.data.raw.ticks[i] = _capture.tickAt(i);
    }
    code.flags |= _capture.flags();
    _capture.clearFrame();
    interrupts();

    if (code.flags & UIAPIR_FLAG_RAW_OVERFLOW) {
        return true;
    }

    // A decoder reads code.data.raw.ticks and writes a different member of the
    // same union, so it decodes into scratch storage that is committed only on
    // success. Each scratch lives in its own scope: an IRPayload here would put
    // the whole 342-byte RAW array on the stack of a part with 2 KiB of SRAM,
    // to hold at most the 21 bytes an AEHA payload needs.
    // A protocol switched off at build time is simply not tried, so its
    // signals arrive as UIAPIR_RAW and still replay correctly - just without
    // the address and command being read out of them.
#if UIAPIR_ENABLE_NEC
    {
        IRNECData nec;
        bool repeat = false;
        if (uiapir_protocol::decodeNEC(code.data.raw.ticks, count, nec, repeat)) {
            code.data.nec = nec;
            code.protocol = UIAPIR_NEC;
            code.carrierKHz = UIAPIR_NEC_CARRIER_KHZ;
            if (repeat) code.flags |= UIAPIR_FLAG_REPEAT;
            return true;
        }
    }
#endif
#if UIAPIR_ENABLE_AEHA
    {
        IRAEHAData aeha;
        if (uiapir_protocol::decodeAEHA(code.data.raw.ticks, count, aeha)) {
            code.data.aeha = aeha;
            code.protocol = UIAPIR_AEHA;
            code.carrierKHz = UIAPIR_AEHA_CARRIER_KHZ;
            return true;
        }
    }
#endif
#if UIAPIR_ENABLE_SONY
    {
        IRSonyData sony;
        if (uiapir_protocol::decodeSony(code.data.raw.ticks, count, sony)) {
            code.data.sony = sony;
            code.protocol = UIAPIR_SONY;
            code.carrierKHz = UIAPIR_SONY_CARRIER_KHZ;
            return true;
        }
    }
#endif
    return true;
}
#endif // UIAPIR_ENABLE_RX

#if UIAPIR_ENABLE_TX
bool UIAPIR::beginTransmit(uint8_t carrierKHz) {
    if (!_started || _txPin == UIAPIR_UNUSED_PIN) return false;
#if UIAPIR_ENABLE_RX
    // Off while transmitting, so the receiver cannot record our own carrier.
    detachReceiver();
    resume();
#endif
    if (!configureCarrier(carrierKHz)) {
#if UIAPIR_ENABLE_RX
        attachReceiver();
#endif
        return false;
    }
    resetFrameClock();
    return true;
}

void UIAPIR::endTransmit() {
    carrierOff();
#if UIAPIR_ENABLE_RX
    attachReceiver();
#endif
}

#if UIAPIR_ENABLE_NEC || UIAPIR_ENABLE_AEHA
void UIAPIR::sendBitSpaceEncoded(uint32_t value, uint8_t bits, uint16_t markUs,
                                 uint16_t zeroSpaceUs, uint16_t oneSpaceUs) {
    for (uint8_t i = 0; i < bits; ++i) {
        mark(markUs);
        space((value & 1U) ? oneSpaceUs : zeroSpaceUs);
        value >>= 1;
    }
}

#endif // UIAPIR_ENABLE_NEC || UIAPIR_ENABLE_AEHA

#if UIAPIR_ENABLE_NEC
void UIAPIR::sendNECFrame(uint16_t address, uint8_t command, bool extended) {
    uint32_t frame;
    if (extended) {
        frame = address;
    } else {
        const uint8_t shortAddress = (uint8_t)address;
        frame = shortAddress | ((uint32_t)(uint8_t)~shortAddress << 8);
    }
    frame |= (uint32_t)command << 16;
    frame |= (uint32_t)(uint8_t)~command << 24;
    mark(UIAPIR_NEC_LEADER_MARK_US);
    space(UIAPIR_NEC_LEADER_SPACE_US);
    sendBitSpaceEncoded(frame, UIAPIR_NEC_BITS, UIAPIR_NEC_BIT_MARK_US,
                        UIAPIR_NEC_ZERO_SPACE_US, UIAPIR_NEC_ONE_SPACE_US);
    mark(UIAPIR_NEC_TRAILER_MARK_US);
}

void UIAPIR::sendNECRepeatFrame() {
    mark(UIAPIR_NEC_LEADER_MARK_US);
    space(UIAPIR_NEC_REPEAT_SPACE_US);
    mark(UIAPIR_NEC_TRAILER_MARK_US);
}

bool UIAPIR::sendNEC(uint16_t address, uint8_t command, bool extended, uint8_t repeats) {
    if (extended) {
        if (!uiapir_protocol::necAddressIsExtendable(address)) {
            return false;
        }
    } else if (address > UIAPIR_NEC_MAX_STANDARD_ADDRESS) {
        return false;
    }
    if (!beginTransmit(UIAPIR_NEC_CARRIER_KHZ)) return false;

    sendNECFrame(address, command, extended);
    for (uint8_t i = 0; i < repeats; ++i) {
        padToPeriod(UIAPIR_NEC_FRAME_PERIOD_US);
        sendNECRepeatFrame();
    }
    endTransmit();
    return true;
}

bool UIAPIR::sendNECRepeat() {
    if (!beginTransmit(UIAPIR_NEC_CARRIER_KHZ)) return false;
    sendNECRepeatFrame();
    endTransmit();
    return true;
}

#endif // UIAPIR_ENABLE_NEC

#if UIAPIR_ENABLE_AEHA
void UIAPIR::sendAEHAFrame(const uint8_t *data, uint8_t length) {
    mark(UIAPIR_AEHA_LEADER_MARK_US);
    space(UIAPIR_AEHA_LEADER_SPACE_US);
    for (uint8_t i = 0; i < length; ++i) {
        sendBitSpaceEncoded(data[i], 8, UIAPIR_AEHA_BIT_MARK_US,
                            UIAPIR_AEHA_ZERO_SPACE_US, UIAPIR_AEHA_ONE_SPACE_US);
    }
    mark(UIAPIR_AEHA_TRAILER_MARK_US);
}

bool UIAPIR::sendAEHA(const uint8_t *data, uint8_t length, uint8_t frames) {
    if (!data || length < UIAPIR_AEHA_MIN_BYTES || length > UIAPIR_MAX_AEHA_BYTES ||
        frames == 0 || !beginTransmit(UIAPIR_AEHA_CARRIER_KHZ)) {
        return false;
    }
    for (uint8_t frame = 0; frame < frames; ++frame) {
        if (frame != 0) padToPeriod(UIAPIR_AEHA_FRAME_PERIOD_US);
        sendAEHAFrame(data, length);
    }
    endTransmit();
    return true;
}

#endif // UIAPIR_ENABLE_AEHA

#if UIAPIR_ENABLE_SONY
void UIAPIR::sendSonyFrame(uint32_t value, uint8_t bits) {
    mark(UIAPIR_SONY_LEADER_MARK_US);
    space(UIAPIR_SONY_LEADER_SPACE_US);
    for (uint8_t i = 0; i < bits; ++i) {
        mark((value & 1U) ? UIAPIR_SONY_ONE_MARK_US : UIAPIR_SONY_ZERO_MARK_US);
        value >>= 1;
        // No trailer: the space after the last bit merges into the frame gap.
        if (i + 1 < bits) space(UIAPIR_SONY_BIT_SPACE_US);
    }
}

bool UIAPIR::sendSony(uint16_t address, uint8_t command, uint8_t bits, uint8_t frames) {
    const uint8_t addressBits = uiapir_protocol::sonyAddressBits(bits);
    if (addressBits == 0 || frames < UIAPIR_SONY_MIN_FRAMES ||
        command >= (1U << UIAPIR_SONY_COMMAND_BITS) ||
        address >= (1U << addressBits)) {
        return false;
    }
    if (!beginTransmit(UIAPIR_SONY_CARRIER_KHZ)) return false;

    const uint32_t value = ((uint32_t)address << UIAPIR_SONY_COMMAND_BITS) | command;
    for (uint8_t frame = 0; frame < frames; ++frame) {
        if (frame != 0) padToPeriod(UIAPIR_SONY_FRAME_PERIOD_US);
        sendSonyFrame(value, bits);
    }
    endTransmit();
    return true;
}

#endif // UIAPIR_ENABLE_SONY

bool UIAPIR::sendRaw(const uint16_t *timingsUs, uint16_t count, uint8_t carrierKHz) {
    if (!timingsUs || count == 0 || !beginTransmit(carrierKHz)) return false;
    for (uint16_t i = 0; i < count; ++i) {
        if ((i & 1U) == 0) mark(timingsUs[i]);
        else space(timingsUs[i]);
    }
    endTransmit();
    return true;
}

bool UIAPIR::sendRawTicks(const uint8_t *ticks, uint16_t count, uint8_t carrierKHz) {
    if (!ticks || count == 0 || !beginTransmit(carrierKHz)) return false;
    for (uint16_t i = 0; i < count; ++i) {
        const uint16_t duration = (uint16_t)ticks[i] * UIAPIR_RAW_TICK_US;
        if ((i & 1U) == 0) mark(duration);
        else space(duration);
    }
    endTransmit();
    return true;
}

bool UIAPIR::send(const IRCode &code, uint8_t repeats) {
    // A protocol that is not built falls through to `default` and is refused.
    // That is reachable only for a hand-built IRCode: with the decoder gone,
    // receive() can never label a capture with a protocol this build lacks.
    switch (code.protocol) {
#if UIAPIR_ENABLE_NEC
    case UIAPIR_NEC:
        if (code.flags & UIAPIR_FLAG_REPEAT) return sendNECRepeat();
        return sendNEC(code.data.nec.address, code.data.nec.command,
                       code.data.nec.addressBits == 16, repeats);
#endif
#if UIAPIR_ENABLE_AEHA
    case UIAPIR_AEHA:
        return sendAEHA(code.data.aeha.bytes, code.data.aeha.length,
                        uiapir_protocol::framesFromRepeats(repeats, 1));
#endif
#if UIAPIR_ENABLE_SONY
    case UIAPIR_SONY:
        // `repeats` counts extra transmissions for every protocol, so the frame
        // count is repeats + 1, raised to the three SIRC always requires.
        return sendSony(code.data.sony.address, code.data.sony.command,
                        code.data.sony.bits,
                        uiapir_protocol::framesFromRepeats(repeats,
                                                           UIAPIR_SONY_MIN_FRAMES));
#endif
    case UIAPIR_RAW:
        // A capture that overflowed or had a duration clipped is missing
        // timing. Replaying it would emit a signal that is not the one that
        // was learned, so refuse rather than report a success.
        if (!uiapir_protocol::rawIsReplayable(code.flags)) {
            return false;
        }
        return sendRawTicks(code.data.raw.ticks, code.data.raw.count,
                            code.carrierKHz ? code.carrierKHz : UIAPIR_NEC_CARRIER_KHZ);
    default:
        return false;
    }
}
#endif // UIAPIR_ENABLE_TX
