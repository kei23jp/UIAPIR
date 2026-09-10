// LearnRemote - a one-button learning remote on a single board.
//
// Point any remote at the receiver module once, and the board replays that key
// for the rest of the session. Nothing here selects a protocol: receive()
// decodes NEC, AEHA and Sony SIRC on its own and falls back to RAW, and send()
// dispatches on whatever came back. A recognised remote is therefore replayed
// as a protocol frame regenerated from its address and command - correct
// carrier, correct repeat structure - and only an unrecognised one is replayed
// as the raw timings that were captured.
//
// Wiring, all on one board:
//
//   IR LED, through an NPN transistor and a series resistor:
//     drive ---------- A2   (silk A2, PC4, TIM1_CH4)
//
//   IR receiver module, 38 kHz demodulator (VS1838B, PL-IRM series):
//     OUT ------------ A3   (silk A3, PD2)
//     VCC ------------ 5V or 3V3, whichever the module wants
//     GND ------------ GND
//
//   Push switch:
//     one leg -------- D3   (silk 3, PC1)
//     other leg ------ GND
//
//   USB-UART RX ------ D15  (silk 15, PD5), optional - the log explains what
//                           the LED is saying, but the sketch works without it.
//
// Keep the emitter from shining straight into the demodulator; a few
// centimetres of separation, or a card between them, is enough. UIAPIR detaches
// its receiver while it transmits, so it cannot record its own carrier, but the
// module's AGC still recovers faster if it is not blinded.
//
// The switch is deliberately not on silk 2. That pin (PC0) drives the board's
// own LED3 through a resistor to ground, and that branch pulls the pin down
// hard enough that the internal pull-up cannot hold a reliable HIGH - a switch
// there reads as permanently pressed. Leaving silk 2 alone keeps LED3 usable as
// this sketch's indicator, which is the only status a board with no serial
// adapter attached can give.
//
// Using it:
//
//   short press   replay the stored code
//   hold 3 s      enter learn mode, and hold 3 s again to leave it
//   short press   in learn mode, leave without storing anything
//
// What the LED says:
//
//   off                  idle
//   slow blink           learn mode, waiting for a signal
//   three quick flashes  a code was stored
//   six fast flashes     something was refused; the serial log says what
//   two slow flashes     replay asked for before anything was learned
//   solid                transmitting
//
// The stored code lives in RAM only. A reset loses it; this sketch does not
// write to flash.

#include <UIAPIR.h>

// ----------------------------------------------------------------- wiring ---

// A2 and D6 are two Arduino numbers for the same pad, PC4. UIAPIR accepts
// either; A2 is what the board silk prints, so that is what this uses.
constexpr uint8_t IR_TX_PIN  = A2; // silk A2 / PC4 / TIM1_CH4
constexpr uint8_t IR_RX_PIN  = A3; // silk A3 / PD2
constexpr uint8_t BUTTON_PIN = 3;  // silk 3 / PC1, switch to GND
constexpr uint8_t LED_PIN    = 2;  // silk 2 / PC0, on-board LED3, HIGH = lit

// ----------------------------------------------------------------- tuning ---

constexpr uint32_t LONG_PRESS_MS    = 3000;
constexpr uint16_t DEBOUNCE_MS      = 25;
constexpr uint32_t LEARN_TIMEOUT_MS = 30000;

// A demodulator emits a few stray edges from sunlight and fluorescent lamps,
// and those arrive as very short RAW captures. Nothing can be replayed from
// them, so an unrecognised capture has to be at least as long as a leader plus
// eight bits before it counts as a code worth keeping.
constexpr uint16_t MIN_RAW_DURATIONS = 2 + 2 * 8 + 1;

// -------------------------------------------------------------------- LED ---

constexpr uint8_t LED_FOREVER = 0xff;

static uint16_t ledOnMs = 0; // 0 means the LED is held at ledLevel.
static uint16_t ledOffMs = 0;
static uint8_t ledFlashesLeft = 0;
static bool ledLevel = false;
static uint32_t ledNextMs = 0;

static void ledWrite(bool on) {
  ledLevel = on;
  digitalWrite(LED_PIN, on ? HIGH : LOW);
}

static void ledSteady(bool on) {
  ledOnMs = 0;
  ledWrite(on);
}

// `flashes` counts on-periods, or LED_FOREVER to keep going until something
// else asks for a different pattern.
static void ledBlink(uint16_t onMs, uint16_t offMs, uint8_t flashes) {
  ledOnMs = onMs;
  ledOffMs = offMs;
  ledFlashesLeft = (flashes == LED_FOREVER) ? LED_FOREVER : (uint8_t)(flashes - 1);
  ledWrite(true);
  ledNextMs = millis() + onMs;
}

static bool ledIsIdle() {
  return ledOnMs == 0 && !ledLevel;
}

static void ledUpdate() {
  if (ledOnMs == 0) {
    return;
  }
  const uint32_t now = millis();
  if ((int32_t)(now - ledNextMs) < 0) {
    return;
  }
  if (ledLevel) {
    ledWrite(false);
    ledNextMs = now + ledOffMs;
    return;
  }
  if (ledFlashesLeft == 0) {
    ledSteady(false);
    return;
  }
  if (ledFlashesLeft != LED_FOREVER) {
    --ledFlashesLeft;
  }
  ledWrite(true);
  // Re-based on now rather than accumulated, so a blocking send() does not
  // leave the pattern owing several periods that it then runs through at once.
  ledNextMs = now + ledOnMs;
}

static void ledLearnWaiting() { ledBlink(150, 850, LED_FOREVER); }
static void ledStored()       { ledBlink(80, 80, 3); }
static void ledRefused()      { ledBlink(40, 60, 6); }
static void ledNothingYet()   { ledBlink(250, 250, 2); }

// ----------------------------------------------------------------- button ---

enum ButtonEvent : uint8_t { BUTTON_NONE = 0, BUTTON_SHORT, BUTTON_LONG };

static bool buttonDown = false;
static uint32_t buttonChangedMs = 0;
static bool longPressFired = false;

// Returns a ButtonEvent, but says uint8_t. Arduino generates prototypes for
// every function in a sketch and puts them above the sketch body, so naming a
// type declared further down makes the generated prototype refer to something
// that does not exist yet. arduino-cli fails with "'ButtonEvent' does not name
// a type"; PlatformIO, which compiles the sketch as plain C++, does not.
static uint8_t pollButton() {
  const bool down = digitalRead(BUTTON_PIN) == LOW; // pulled up, closed = LOW
  const uint32_t now = millis();

  if (down != buttonDown) {
    // Lock-out debounce: ignore every edge for a while after an accepted one.
    if ((uint32_t)(now - buttonChangedMs) < DEBOUNCE_MS) {
      return BUTTON_NONE;
    }
    buttonDown = down;
    buttonChangedMs = now;
    if (down) {
      longPressFired = false;
      return BUTTON_NONE;
    }
    // A release only counts as a short press if the hold did not already fire.
    return longPressFired ? BUTTON_NONE : BUTTON_SHORT;
  }

  if (down && !longPressFired &&
      (uint32_t)(now - buttonChangedMs) >= LONG_PRESS_MS) {
    longPressFired = true;
    return BUTTON_LONG;
  }
  return BUTTON_NONE;
}

// ------------------------------------------------------------------ state ---

static UIAPIR ir;

// One IRCode, not two. It is 346 bytes on a part with 2 KiB of SRAM, and
// receive() writes into it in place - so a stored code cannot survive the
// arrival of the next signal anyway. Learn mode drops it up front rather than
// pretending otherwise.
static IRCode learned;
static bool haveCode = false;

static bool learning = false;
static uint32_t learnStartedMs = 0;

static void printLearned() {
  Serial.print("stored: ");
  switch (learned.protocol) {
  case UIAPIR_NEC:
    Serial.print("NEC address=0x");
    Serial.print(learned.data.nec.address, HEX);
    Serial.print(" command=0x");
    Serial.print(learned.data.nec.command, HEX);
    Serial.print(" (");
    Serial.print(learned.data.nec.addressBits);
    Serial.println("-bit address)");
    break;
  case UIAPIR_AEHA:
    Serial.print("AEHA ");
    Serial.print(learned.data.aeha.length);
    Serial.print(" bytes:");
    for (uint8_t i = 0; i < learned.data.aeha.length; ++i) {
      Serial.print(' ');
      if (learned.data.aeha.bytes[i] < 0x10) {
        Serial.print('0');
      }
      Serial.print(learned.data.aeha.bytes[i], HEX);
    }
    Serial.println();
    break;
  case UIAPIR_SONY:
    Serial.print("SIRC ");
    Serial.print(learned.data.sony.bits);
    Serial.print("-bit address=0x");
    Serial.print(learned.data.sony.address, HEX);
    Serial.print(" command=0x");
    Serial.println(learned.data.sony.command, HEX);
    break;
  default:
    Serial.print("RAW ");
    Serial.print(learned.data.raw.count);
    Serial.print(" durations, replayed at ");
    Serial.print(learned.carrierKHz);
    Serial.println(" kHz");
    break;
  }
}

static void enterLearn() {
  learning = true;
  haveCode = false;
  learnStartedMs = millis();
  ir.resume(); // discard whatever the idle receiver has been accumulating
  Serial.println();
  Serial.println("learn mode: point a remote at the module and press a key");
  ledLearnWaiting();
}

static void leaveLearn(const char *why) {
  learning = false;
  Serial.print("learn mode ended: ");
  Serial.println(why);
}

static void pollLearn() {
  if ((uint32_t)(millis() - learnStartedMs) >= LEARN_TIMEOUT_MS) {
    leaveLearn("timed out, nothing stored");
    ledRefused();
    return;
  }

  if (!ir.receive(learned)) {
    return;
  }

  // A capture that overflowed, or that carried a duration too long to store,
  // is missing timing. send() refuses to replay one, so there is no point
  // keeping it - stay in learn mode and wait for a clean press.
  if (learned.flags & (UIAPIR_FLAG_RAW_OVERFLOW | UIAPIR_FLAG_TIMING_CLIPPED)) {
    Serial.println("ignored: signal did not fit the capture buffer");
    ledRefused();
    return;
  }

  // A held key sends one frame and then bare NEC repeat codes. A repeat code
  // carries no address or command, so it is not a key on its own.
  if (learned.flags & UIAPIR_FLAG_REPEAT) {
    return;
  }

  if (learned.protocol == UIAPIR_RAW &&
      learned.data.raw.count < MIN_RAW_DURATIONS) {
    return; // too short to be anything but noise
  }

  haveCode = true;
  leaveLearn("code stored");
  printLearned();
  ledStored();
}

static void replay() {
  if (!haveCode) {
    Serial.println("nothing learned yet - hold the button for 3 s");
    ledNothingYet();
    return;
  }

  ledSteady(true);
  const bool sent = ir.send(learned);
  ledSteady(false);

  // send() re-arms the receiver as the last frame ends, so a reflection of our
  // own burst can land in the capture buffer. It is not an incoming signal.
  ir.resume();

  Serial.println(sent ? "sent" : "send refused");
  if (!sent) {
    ledRefused();
  }
}

// ------------------------------------------------------------------- main ---

void setup() {
  pinMode(LED_PIN, OUTPUT);
  ledSteady(false);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("UIAPIR LearnRemote");

  if (!ir.begin(IR_RX_PIN, IR_TX_PIN)) {
    Serial.println("begin() failed: check IR_RX_PIN");
    ledBlink(100, 100, LED_FOREVER);
    while (true) {
      ledUpdate();
    }
  }

  // The button is read against this from the first loop, so it has to start at
  // a real timestamp; leaving it at 0 would make the first edge look like it
  // arrived DEBOUNCE_MS after boot no matter when it happened.
  buttonChangedMs = millis();

  Serial.println("short press = replay, hold 3 s = learn");
}

void loop() {
  ledUpdate();
  // A finite pattern leaves the LED off when it ends. In learn mode the slow
  // blink is the resting state, so put it back once a one-shot has finished.
  if (learning && ledIsIdle()) {
    ledLearnWaiting();
  }

  switch (pollButton()) {
  case BUTTON_LONG:
    if (learning) {
      leaveLearn("cancelled");
      ledSteady(false);
    } else {
      enterLearn();
    }
    break;
  case BUTTON_SHORT:
    if (learning) {
      leaveLearn("cancelled");
      ledSteady(false);
    } else {
      replay();
    }
    break;
  default:
    break;
  }

  if (learning) {
    pollLearn();
  }
}
