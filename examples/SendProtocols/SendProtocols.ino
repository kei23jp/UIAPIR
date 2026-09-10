#include <UIAPIR.h>

UIAPIR ir;

void setup() {
  // No receiver in this example. Carrier output is UIAPduino D6.
  ir.begin(UIAPIR_UNUSED_PIN, 6);

  ir.sendNEC(0x12, 0x34);
  delay(500);

  const uint8_t aeha[] = {0x02, 0x20, 0x80, 0x00, 0x12, 0x34};
  ir.sendAEHA(aeha, sizeof(aeha));
  delay(500);

  // Sony SIRC is normally transmitted three times.
  ir.sendSony(1, 19, 12);
}

void loop() {
}
