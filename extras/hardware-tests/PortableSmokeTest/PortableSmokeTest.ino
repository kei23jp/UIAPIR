#include <UIAPIR.h>

// Override these from the build command when different pins are needed:
//   --build-property "compiler.cpp.extra_flags=-DUIAPIR_TEST_RX_PIN=... -DUIAPIR_TEST_TX_PIN=..."
#ifndef UIAPIR_TEST_RX_PIN
#if defined(ARDUINO_ARCH_ESP32)
#define UIAPIR_TEST_RX_PIN 27
#elif defined(ARDUINO_AVR_PRO)
#define UIAPIR_TEST_RX_PIN 3
#else
#define UIAPIR_TEST_RX_PIN 2
#endif
#endif

#ifndef UIAPIR_TEST_TX_PIN
#if defined(ARDUINO_ARCH_ESP32)
#define UIAPIR_TEST_TX_PIN 26
#elif defined(ARDUINO_AVR_PRO)
#define UIAPIR_TEST_TX_PIN A0
#else
#define UIAPIR_TEST_TX_PIN 6
#endif
#endif

constexpr uint8_t RX_PIN = UIAPIR_TEST_RX_PIN;
constexpr uint8_t TX_PIN = UIAPIR_TEST_TX_PIN;

UIAPIR ir;
IRCode captured;
uint8_t captureStorage[UIAPIR_RAW_BUFFER_SIZE];
uint16_t passed = 0;
uint16_t failed = 0;

void check(const __FlashStringHelper *name, bool result) {
  Serial.print(result ? F("PASS  ") : F("FAIL  "));
  Serial.println(name);
  if (result) ++passed;
  else ++failed;
}

void printSummary() {
  Serial.print(F("Summary: "));
  Serial.print(passed);
  Serial.print(F(" passed, "));
  Serial.print(failed);
  Serial.println(F(" failed"));
}

void printCaptured(const IRCode &code) {
  Serial.print(F("Received: "));
  switch (code.protocol) {
    case UIAPIR_NEC:
      Serial.print(F("NEC address=0x"));
      Serial.print(code.data.nec.address, HEX);
      Serial.print(F(" command=0x"));
      Serial.println(code.data.nec.command, HEX);
      break;
    case UIAPIR_AEHA:
      Serial.print(F("AEHA bytes="));
      Serial.println(code.data.aeha.length);
      break;
    case UIAPIR_SONY:
      Serial.print(F("Sony bits="));
      Serial.print(code.data.sony.bits);
      Serial.print(F(" address=0x"));
      Serial.print(code.data.sony.address, HEX);
      Serial.print(F(" command=0x"));
      Serial.println(code.data.sony.command, HEX);
      break;
    case UIAPIR_RAW:
      Serial.print(F("RAW durations="));
      Serial.println(code.data.raw.count);
      break;
    default:
      Serial.println(F("unknown"));
      break;
  }
}

void runTransmitChecks() {
  const uint8_t aeha[] = {0x02, 0x20, 0x80, 0x00, 0x12, 0x34};
  const uint16_t raw[] = {9000, 4500, 560, 560, 560, 1690, 560};

  check(F("send NEC"), ir.sendNEC(0x12, 0x34));
  delay(100);
  check(F("send extended NEC"), ir.sendNEC(0x1234, 0x56, true));
  delay(100);
  check(F("send NEC repeat"), ir.sendNECRepeat());
  delay(100);
  check(F("send AEHA"), ir.sendAEHA(aeha, sizeof(aeha)));
  delay(100);
  check(F("send Sony SIRC"), ir.sendSony(1, 19, 12));
  delay(100);
  check(F("send RAW"), ir.sendRaw(raw, sizeof(raw) / sizeof(raw[0]), 38));
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println(F("UIAPIR portable smoke test"));
  Serial.print(F("RX pin: "));
  Serial.println(RX_PIN);
  Serial.print(F("TX pin: "));
  Serial.println(TX_PIN);

  UIAPIR invalid;
  check(F("reject identical RX/TX pins"), !invalid.begin(RX_PIN, RX_PIN));

  UIAPIRConfig config(captureStorage, sizeof(captureStorage));
  check(F("begin with caller-provided buffer"), ir.begin(RX_PIN, TX_PIN, config));
  if (failed != 0) {
    printSummary();
    return;
  }

  runTransmitChecks();

  ir.end();
  check(F("end then begin again"), ir.begin(RX_PIN, TX_PIN, config));
  ir.resume();
  printSummary();
  Serial.println(F("Press a remote button toward the receiver."));
  Serial.println(F("Each captured frame will be printed and replayed after 250 ms."));
}

void loop() {
  if (!ir.available()) return;

  check(F("learn captured frame"), ir.learn(captured));
  if (captured.protocol == UIAPIR_UNKNOWN) return;

  printCaptured(captured);
  const uint8_t lossy = UIAPIR_FLAG_RAW_OVERFLOW | UIAPIR_FLAG_TIMING_CLIPPED;
  if ((captured.flags & lossy) != 0) {
    Serial.println(F("SKIP  captured frame is incomplete and cannot be replayed"));
    return;
  }

  delay(250);
  check(F("replay captured frame"), ir.send(captured));
}
