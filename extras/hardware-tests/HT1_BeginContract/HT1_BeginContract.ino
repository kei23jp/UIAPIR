// HT1 - begin() / end() contract.
//
// Covers the argument validation that cannot be exercised on a host, because it
// resolves Arduino pin numbers through the CH32 core's pin map.
//
// Wiring: none. Connect a USB-UART adapter to D15 (PD5) to read the results;
// the CH32V003 has no USB peripheral, so Serial is USART1 on PD5/PD6.
//
// Expected: every line reports PASS and the summary reads "HT1 PASSED".

#include <UIAPIR.h>

static const uint8_t RX_PIN = 3;   // PC1
static const uint8_t TX_PIN = 6;   // PC4 / TIM1_CH4, the only carrier pad

static unsigned passed = 0;
static unsigned failed = 0;

static void check(const char *name, bool ok) {
  Serial.print(ok ? "PASS  " : "FAIL  ");
  Serial.println(name);
  if (ok) ++passed; else ++failed;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("HT1 begin()/end() contract");

  UIAPIR ir;

  // --- accepted configurations -------------------------------------------
  check("begin(D3, D6) accepted", ir.begin(RX_PIN, TX_PIN));
  ir.end();
  check("begin(UNUSED, D6) accepted (transmit only)",
        ir.begin(UIAPIR_UNUSED_PIN, TX_PIN));
  ir.end();

  // --- rejected: RX pin does not exist on this part (#9) -------------------
  // The part has 18 digital pins. The core's attachInterrupt() returns without
  // doing anything for an unknown pin, so accepting this would report success
  // on a receiver that can never fire.
  check("begin(100, D6) rejected: no such pin", !ir.begin(100, TX_PIN));
  check("begin(200, D6) rejected: no such pin", !ir.begin(200, TX_PIN));
  check("begin(D17, D6) accepted: D17 exists", ir.begin(17, TX_PIN));
  ir.end();

  // --- rejected: RX and TX on the same pad (#5) ---------------------------
  // A2 and D6 are different Arduino pin numbers for the same PC4, so comparing
  // the numbers would let these through.
  check("begin(D6, D6) rejected: same pad", !ir.begin(TX_PIN, TX_PIN));
  check("begin(A2, D6) rejected: A2 is the same PC4 as D6",
        !ir.begin(A2, TX_PIN));
  check("begin(D6, A2) rejected: same pad, named the other way round",
        !ir.begin(TX_PIN, A2));

  // --- the TX pad may be named either way ---------------------------------
  // The carrier is TIM1_CH4, which is PC4 and nowhere else, but the board silk
  // prints A2 on that pad. Both spellings have to be accepted, or a sketch
  // written against the silk is refused for no reason a user can see.
  check("begin(D3, A2) accepted: A2 is the same PC4 as D6",
        ir.begin(RX_PIN, A2));
  ir.end();
  check("begin(UNUSED, A2) accepted (transmit only)",
        ir.begin(UIAPIR_UNUSED_PIN, A2));
  check("sendNEC() works with TX given as A2", ir.sendNEC(0x12, 0x34));
  ir.end();

  // --- rejected: TX pin other than PC4 ------------------------------------
  check("begin(D3, D5) rejected: TIM1_CH4 is only on PC4", !ir.begin(RX_PIN, 5));
  check("begin(D3, 100) rejected: no such pin", !ir.begin(RX_PIN, 100));

  // --- a second instance may not take the shared timers (#1) --------------
  check("first instance begins", ir.begin(RX_PIN, TX_PIN));
  {
    UIAPIR second;
    check("second instance refused while the first owns the timers",
          !second.begin(4, TX_PIN));

    // end() on the instance that was refused must be a no-op. If it stopped
    // TIM2 the transmit below would never return. See HT3 for the timing.
    second.end();
  }
  check("first instance still transmits after the second called end()",
        ir.sendNEC(0x12, 0x34));
  ir.end();

  // A second instance may take over once the first has released.
  {
    UIAPIR second;
    check("second instance begins after the first released", second.begin(4, TX_PIN));
    second.end();
  }

  Serial.println();
  Serial.print(passed);
  Serial.print(" passed, ");
  Serial.print(failed);
  Serial.println(" failed");
  Serial.println(failed == 0 ? "HT1 PASSED" : "HT1 FAILED");
}

void loop() {
}
