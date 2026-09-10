#include <UIAPIR.h>

// D6 is the carrier output (PC4 / TIM1_CH4) and cannot also be the receiver.
constexpr uint8_t IR_RX_PIN = 3;
constexpr uint8_t IR_TX_PIN = 6;

UIAPIR ir;
IRCode learned;
bool haveCode = false;

void setup() {
  if (!ir.begin(IR_RX_PIN, IR_TX_PIN)) {
    // Refused: the RX pin does not exist, or it is the same pad as D6.
    while (true) {}
  }
}

void loop() {
  if (!haveCode && ir.learn(learned)) {
    // A signal too long for the capture buffer, or one carrying a duration
    // beyond what a tick can hold, comes back incomplete. Replaying it would
    // transmit something other than what was learned, so wait for a clean one.
    const uint8_t lossy = UIAPIR_FLAG_RAW_OVERFLOW | UIAPIR_FLAG_TIMING_CLIPPED;
    haveCode = (learned.flags & lossy) == 0;
    if (haveCode) delay(1000);
  }

  if (haveCode) {
    ir.send(learned);
    haveCode = false;
    delay(1000);
  }
}
