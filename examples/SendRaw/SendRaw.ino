#include <UIAPIR.h>

UIAPIR ir;

const uint16_t rawMicros[] = {
  9000, 4500, 560, 560, 560, 1690, 560, 560, 560
};

void setup() {
  ir.begin(UIAPIR_UNUSED_PIN, 6);
  ir.sendRaw(rawMicros, sizeof(rawMicros) / sizeof(rawMicros[0]), 38);
}

void loop() {
}
