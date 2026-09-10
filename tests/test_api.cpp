// SPDX-License-Identifier: MIT
// Copyright (c) 2026 kei23jp

#include "../src/UIAPIR.h"
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
