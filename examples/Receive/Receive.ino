#include <UIAPIR.h>

// Choose an interrupt-capable input and a tone-capable output for your board.
// D3 and D6 work on Arduino Uno and UIAPduino; change them as needed.
constexpr uint8_t IR_RX_PIN = 3;
constexpr uint8_t IR_TX_PIN = 6;

UIAPIR ir;

// Not a local in loop(). IRCode is UIAPIR_RAW_BUFFER_SIZE bytes of union - 346
// with the default buffer - so keeping it out of loop() avoids a large stack
// frame on small Arduino targets. receive() overwrites it whole on every call.
IRCode code;

void setup() {
  ir.begin(IR_RX_PIN, IR_TX_PIN);
}

void loop() {
  if (!ir.receive(code)) return;

  // Inspect code.protocol and code.data here.
  // Unknown formats are returned as UIAPIR_RAW.
}
