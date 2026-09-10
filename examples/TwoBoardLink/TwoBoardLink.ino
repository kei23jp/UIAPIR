#include <UIAPIRLink.h>

// Build both boards: endpoint 0 sends; endpoint 1 receives.
// Override these with compiler flags, or edit the defaults before uploading.
#ifndef UIAPIR_LINK_ENDPOINT
#define UIAPIR_LINK_ENDPOINT 0
#endif
#ifndef UIAPIR_LINK_PROTOCOL
#define UIAPIR_LINK_PROTOCOL UIAPIR_NEC
#endif
#ifndef UIAPIR_LINK_FORMAT
#define UIAPIR_LINK_FORMAT uiapir_link::defaultFormat(UIAPIR_LINK_PROTOCOL)
#endif
#ifndef UIAPIR_LINK_RELIABLE
#define UIAPIR_LINK_RELIABLE 1
#endif

UIAPIR ir;
UIAPIRLink link(ir, UIAPIR_LINK_FORMAT,
    UIAPIR_LINK_RELIABLE ? UIAPIRLinkMode::Reliable : UIAPIRLinkMode::Simple,
    UIAPIR_LINK_ENDPOINT, 0);

// Caller-owned buffers: no heap allocation. Keep IRCode off the small stack.
// Covers the selected data format AND the 67-duration control frame.
// Set UIAPIR_RAW_BUFFER_SIZE globally to reduce this storage if desired.
uint8_t captureStorage[UIAPIR_RAW_BUFFER_SIZE];
IRCode scratch;
UIAPIRPacket packet;
volatile uint8_t receivedValue; // Inspect here, or use packet in your application.
uint8_t nextValue;
uint8_t outgoing[UIAPIR_LINK_MAX_BYTES];
uint32_t lastSend;

void setup() {
    UIAPIRConfig config(captureStorage, sizeof(captureStorage));
    if (!link.maxPayload() || sizeof(captureStorage) < link.requiredCaptureSize() ||
        !ir.begin(3, 6, config)) while (true) {}
}

void loop() {
    // No delay(): both peers must service incoming frames and ACKs promptly.
    if (link.poll(scratch, packet)) receivedValue = packet.bytes[0];
    if (UIAPIR_LINK_ENDPOINT == 0 && !link.busy() &&
        (uint32_t)(millis() - lastSend) >= 1000) {
        // Exercise the FULL native capacity, including Sony's partial final byte.
        for (uint8_t i = 0; i < link.maxPayload(); ++i) outgoing[i] = nextValue + i;
        const uint8_t remainder = link.maxPayloadBits() & 7;
        if (remainder) outgoing[link.maxPayload() - 1] &= (1U << remainder) - 1;
        if (link.sendBits(outgoing, link.maxPayloadBits())) {
            ++nextValue;
            lastSend = millis();
        }
    }
    // link.status(): Sent / Pending / Acknowledged / Failed.
    // Failed means no ACK was obtained, even if the peer received the data.
}
