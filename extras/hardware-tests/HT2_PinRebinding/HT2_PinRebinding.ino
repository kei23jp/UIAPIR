// HT2 - calling begin() again moves the receiver to the new pin.
//
// A second begin() on a started instance used to overwrite the RX pin without
// re-arming the interrupt: reception stayed on the old pin while the object
// believed it was on the new one. That is invisible to a host test because it
// lives entirely in the core's EXTI configuration.
//
// The board generates its own edges, so no IR parts are needed. Two output pins
// are wired to two candidate RX pins and drive an active-low NEC frame, the way
// a demodulating receiver output looks.
//
// Wiring:
//   D8  (PC6) ---- D3 (PC1)     generator A -> RX candidate A
//   D9  (PC7) ---- D4 (PC2)     generator B -> RX candidate B
//   USB-UART RX ---- D15 (PD5)  to read the results
//
// Expected: every line reports PASS and the summary reads "HT2 PASSED".
// Against the unfixed library, "A is ignored after rebinding to D4" fails
// (the old interrupt is still live) and "B is received after rebinding" fails
// (the new pin was never armed).

#include <UIAPIR.h>

static const uint8_t GEN_A = 8;    // PC6, wired to RX_A
static const uint8_t RX_A = 3;     // PC1
static const uint8_t GEN_B = 9;    // PC7, wired to RX_B
static const uint8_t RX_B = 4;     // PC2
static const uint8_t TX_PIN = 6;   // PC4

static UIAPIR ir;
static IRCode code;

static unsigned passed = 0;
static unsigned failed = 0;

static void check(const char *name, bool ok) {
  Serial.print(ok ? "PASS  " : "FAIL  ");
  Serial.println(name);
  if (ok) ++passed; else ++failed;
}

// A NEC frame as a demodulating receiver would present it: idle high, mark low.
static void emitNEC(uint8_t pin, uint8_t address, uint8_t command) {
  const uint32_t value = (uint32_t)address |
                         ((uint32_t)(uint8_t)~address << 8) |
                         ((uint32_t)command << 16) |
                         ((uint32_t)(uint8_t)~command << 24);

  digitalWrite(pin, LOW);
  delayMicroseconds(9000);
  digitalWrite(pin, HIGH);
  delayMicroseconds(4500);
  for (uint8_t i = 0; i < 32; ++i) {
    digitalWrite(pin, LOW);
    delayMicroseconds(560);
    digitalWrite(pin, HIGH);
    delayMicroseconds((value >> i) & 1 ? 1690 : 560);
  }
  digitalWrite(pin, LOW);
  delayMicroseconds(560);
  digitalWrite(pin, HIGH);

  delay(30); // well past the frame gap, so the frame is finalised
}

// Emit on `pin`, then report whether anything was received.
static bool emitAndReceive(uint8_t pin) {
  ir.resume();
  emitNEC(pin, 0x12, 0x34);
  const bool got = ir.receive(code);
  if (got) {
    // protocol 1 is UIAPIR_NEC, so a correctly generated frame reports 1 and
    // the address and command the generator used. Anything else still counts
    // as "received" for the purpose of this test.
    Serial.print("      received: protocol=");
    Serial.print((int)code.protocol);
    if (code.protocol == UIAPIR_NEC) {
      Serial.print(" address=");
      Serial.print(code.data.nec.address);
      Serial.print(" command=");
      Serial.print(code.data.nec.command);
    }
    Serial.println();
  }
  return got;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("HT2 receiver rebinding");

  pinMode(GEN_A, OUTPUT);
  pinMode(GEN_B, OUTPUT);
  digitalWrite(GEN_A, HIGH); // idle high, like a demodulator output
  digitalWrite(GEN_B, HIGH);
  delay(10);

  // --- bound to A ---------------------------------------------------------
  check("begin(D3, D6)", ir.begin(RX_A, TX_PIN));
  check("A is received while bound to D3", emitAndReceive(GEN_A));
  check("B is ignored while bound to D3", !emitAndReceive(GEN_B));

  // --- rebind to B without an intervening end() ---------------------------
  check("begin(D4, D6) on the already started instance", ir.begin(RX_B, TX_PIN));
  check("A is ignored after rebinding to D4", !emitAndReceive(GEN_A));
  check("B is received after rebinding to D4", emitAndReceive(GEN_B));

  // --- and back again -----------------------------------------------------
  check("begin(D3, D6) again", ir.begin(RX_A, TX_PIN));
  check("A is received again", emitAndReceive(GEN_A));
  check("B is ignored again", !emitAndReceive(GEN_B));

  // --- after end() nothing is received ------------------------------------
  ir.end();
  check("A is ignored after end()", !emitAndReceive(GEN_A));

  Serial.println();
  Serial.print(passed);
  Serial.print(" passed, ");
  Serial.print(failed);
  Serial.println(" failed");
  Serial.println(failed == 0 ? "HT2 PASSED" : "HT2 FAILED");
}

void loop() {
}
