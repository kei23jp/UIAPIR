// SPDX-License-Identifier: MIT
// Copyright (c) 2026 kei23jp

#include "../src/UIAPIR.h"

#include <type_traits>
#include <utility>

// A direction switched off must take its methods with it, so that calling one
// is a compile error rather than a runtime false. Detect the members instead of
// calling them, so this file also compiles in the builds that lack them.
template <class T, class = void>
struct HasSend : std::false_type {};
template <class T>
struct HasSend<T, decltype((void)std::declval<T &>().send(std::declval<const IRCode &>()))>
    : std::true_type {};

template <class T, class = void>
struct HasReceive : std::false_type {};
template <class T>
struct HasReceive<T, decltype((void)std::declval<T &>().receive(std::declval<IRCode &>()))>
    : std::true_type {};

static_assert(HasSend<UIAPIR>::value == (UIAPIR_ENABLE_TX != 0),
              "send() must exist exactly when UIAPIR_ENABLE_TX is set");
static_assert(HasReceive<UIAPIR>::value == (UIAPIR_ENABLE_RX != 0),
              "receive() must exist exactly when UIAPIR_ENABLE_RX is set");

void compileDirectionApi(UIAPIR &ir, IRCode &code) {
#if UIAPIR_ENABLE_RX
    (void)ir.available();
    (void)ir.receive(code);
    (void)ir.learn(code);
    ir.resume();
#endif
#if UIAPIR_ENABLE_TX
    (void)ir.send(code, 1);
    static const uint16_t raw[] = {9000, 4500, 560};
    (void)ir.sendRaw(raw, 3);
    (void)ir.sendRawTicks(code.data.raw.ticks, 3, 38);
#endif
    (void)ir;
    (void)code;
}

// The link sends and listens, so it only exists with both directions.
#if UIAPIR_ENABLE_TX && UIAPIR_ENABLE_RX
#include "../src/UIAPIRLink.h"

void compileLinkApi(UIAPIR &ir, IRCode &scratch, UIAPIRPacket &packet) {
    UIAPIRLink link(ir, UIAPIR_NEC, UIAPIRLinkMode::Reliable, 0);
    uint8_t value = 1;
    (void)link.send(&value, 1);
    (void)link.poll(scratch, packet);
    (void)link.status();
    (void)link.busy();
    UIAPIRLink sony(ir, UIAPIRLinkFormat::Sony20, UIAPIRLinkMode::Simple, 1);
    uint8_t full[3] = {0xff, 0xff, 0x0f};
    (void)sony.sendBits(full, 20);
    (void)sony.maxPayloadBits();
    (void)sony.requiredCaptureSize();
}
#endif

// Neither class may embed a UIAPIR_RAW_BUFFER_SIZE-sized array; the buffer is
// allocated by begin() instead. The bound is a fixed number rather than
// UIAPIR_RAW_BUFFER_SIZE because the point is that these sizes do not track
// that macro - and a build that switches protocols off may legitimately set it
// below the size of the objects themselves, at which case comparing against it
// fails for the wrong reason. Today IRCapture is 16 bytes and UIAPIR is 52.
static_assert(sizeof(uiapir_capture::IRCapture) <= 64,
              "IRCapture must not embed the RAW buffer");
static_assert(sizeof(UIAPIR) <= 128,
              "UIAPIR must not embed the RAW buffer");

void compileMemoryConfigurationApi(uint8_t *storage) {
    UIAPIR ir;
    UIAPIRConfig allocated(67);
    UIAPIRConfig supplied(storage, 67);

    (void)ir.begin(3, 6);
    (void)ir.begin(3, allocated);
    (void)ir.begin(3, 6, supplied);
}
