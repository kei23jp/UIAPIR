// HT3 - a non-owning instance must not stop the shared timers.
//
// TIM2 is the 1 MHz timebase every blocking wait in the transmitter counts on.
// end() used to disable it unconditionally, so calling end() on an instance
// whose begin() had been refused stopped the timebase underneath the instance
// that did own it. waitUs() then spins on a counter that never advances and the
// sketch never returns.
//
// A host test cannot see this: it is a hang inside a busy-wait on a hardware
// register. Here it shows up as output that simply stops.
//
// Wiring: none. Connect a USB-UART adapter to D15 (PD5) to read the results.
//
// Expected: the log reaches "HT3 PASSED". If the fix is missing, output stops
// after "sending from the owning instance..." and never resumes; that silence
// is the failure.

#include <UIAPIR.h>

static const uint8_t RX_PIN = 3;   // PC1
static const uint8_t TX_PIN = 6;   // PC4

static unsigned passed = 0;
static unsigned failed = 0;

static void check(const char *name, bool ok) {
  Serial.print(ok ? "PASS  " : "FAIL  ");
  Serial.println(name);
  if (ok) ++passed; else ++failed;
}

// sendNEC(..., repeats = 2) is two full 110 ms periods plus a trailing 11.81 ms
// repeat frame: 231.81 ms. Anything far from that means the timebase is not
// running at 1 MHz.
static const uint32_t EXPECTED_MS = 232;
static const uint32_t EXPECTED_MIN_MS = 210;
static const uint32_t EXPECTED_MAX_MS = 255;

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("HT3 shared timer ownership");

  static UIAPIR owner;
  check("owner.begin(D3, D6)", owner.begin(RX_PIN, TX_PIN));

  // Baseline: how long a transmit takes while nothing has interfered.
  uint32_t t0 = millis();
  check("baseline transmit completes", owner.sendNEC(0x12, 0x34, false, 2));
  uint32_t baseline = millis() - t0;
  Serial.print("      baseline elapsed = ");
  Serial.print(baseline);
  Serial.print(" ms (expect about ");
  Serial.print(EXPECTED_MS);
  Serial.println(")");
  check("baseline duration is plausible",
        baseline >= EXPECTED_MIN_MS && baseline <= EXPECTED_MAX_MS);

  {
    UIAPIR intruder;
    check("intruder.begin() refused", !intruder.begin(4, TX_PIN));

    Serial.println("      intruder.end() - must not touch TIM1/TIM2");
    Serial.flush();
    intruder.end();
  }

  // The dangerous moment. With the old end(), TIM2 is stopped here and the
  // call below never returns, so nothing after this line is ever printed.
  Serial.println("      sending from the owning instance...");
  Serial.flush();

  t0 = millis();
  const bool sent = owner.sendNEC(0x12, 0x34, false, 2);
  const uint32_t elapsed = millis() - t0;

  Serial.print("      returned, elapsed = ");
  Serial.print(elapsed);
  Serial.println(" ms");
  check("transmit still returns after a foreign end()", sent);
  check("transmit still takes the same time",
        elapsed >= EXPECTED_MIN_MS && elapsed <= EXPECTED_MAX_MS);

  // An instance that never called begin() at all must be equally harmless.
  {
    UIAPIR neverStarted;
    neverStarted.end();
  }
  t0 = millis();
  const bool sent2 = owner.sendNEC(0x12, 0x34, false, 2);
  const uint32_t elapsed2 = millis() - t0;
  check("transmit survives end() on a never-started instance",
        sent2 && elapsed2 >= EXPECTED_MIN_MS && elapsed2 <= EXPECTED_MAX_MS);

  // The owner's own end() does release everything, and begin() works again.
  owner.end();
  check("owner can begin() again after its own end()",
        owner.begin(RX_PIN, TX_PIN));
  check("and transmit again", owner.sendNEC(0x12, 0x34));
  owner.end();

  Serial.println();
  Serial.print(passed);
  Serial.print(" passed, ");
  Serial.print(failed);
  Serial.println(" failed");
  Serial.println(failed == 0 ? "HT3 PASSED" : "HT3 FAILED");
}

void loop() {
}
