// HT5 - dump what a real remote actually looks like.
//
// The host tests feed the capture state machine timings the library itself
// believes in. Only a real remote and a real demodulator can show what arrives:
// how far the durations sit from nominal once the receiver module and the edge
// interrupt have added their own delays, and whether the 6 ms space threshold
// that separates frames still holds.
//
// Wiring:
//   IR receiver module (38 kHz, e.g. a VS1838B or PL-IRM series):
//     OUT ---- D3 (PC1)
//     VCC ---- 5V or 3V3, matching the module
//     GND ---- GND
//   USB-UART RX ---- D15 (PD5)
//
// Nothing transmits here, so D6 is left unused.
//
// What to look at:
//   - "max space" must stay clear of UIAPIR_FRAME_GAP_US. That threshold is
//     what ends a frame, so a real intra-frame space close to it means frames
//     will start being cut in half.
//   - the leader mark tells you the edge interrupt's bias: it should read close
//     to 9000 us for NEC, and a consistent offset across every duration is
//     interrupt latency rather than a remote that is out of spec.
//   - press and hold a key. NEC should give one frame then repeat codes, and a
//     Sony remote should give three frames per press.

#include <UIAPIR.h>

static const uint8_t RX_PIN = 3; // PC1

static UIAPIR ir;
static IRCode code;
static uint32_t frameNumber = 0;

static void printProtocol(IRProtocol protocol) {
  switch (protocol) {
  case UIAPIR_NEC:  Serial.print("NEC");     break;
  case UIAPIR_AEHA: Serial.print("AEHA");    break;
  case UIAPIR_SONY: Serial.print("SIRC");    break;
  case UIAPIR_RAW:  Serial.print("RAW");     break;
  default:          Serial.print("UNKNOWN"); break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("HT5 receive dump - point a remote at the receiver module");
  Serial.print("frame gap threshold = ");
  Serial.print(UIAPIR_FRAME_GAP_US);
  Serial.println(" us");

  if (!ir.begin(RX_PIN, UIAPIR_UNUSED_PIN)) {
    Serial.println("begin() failed");
    while (true) {}
  }
}

void loop() {
  if (!ir.receive(code)) {
    return;
  }

  ++frameNumber;
  Serial.println();
  Serial.print("#");
  Serial.print(frameNumber);
  Serial.print("  ");
  printProtocol(code.protocol);

  if (code.flags & UIAPIR_FLAG_REPEAT) Serial.print(" REPEAT");
  if (code.flags & UIAPIR_FLAG_RAW_OVERFLOW) Serial.print(" OVERFLOW");
  if (code.flags & UIAPIR_FLAG_TIMING_CLIPPED) Serial.print(" CLIPPED");

  switch (code.protocol) {
  case UIAPIR_NEC:
    Serial.print("  address=");
    Serial.print(code.data.nec.address);
    Serial.print(" (");
    Serial.print(code.data.nec.addressBits);
    Serial.print(" bit) command=");
    Serial.print(code.data.nec.command);
    break;
  case UIAPIR_AEHA:
    Serial.print("  bytes:");
    for (uint8_t i = 0; i < code.data.aeha.length; ++i) {
      Serial.print(' ');
      Serial.print(code.data.aeha.bytes[i]);
    }
    break;
  case UIAPIR_SONY:
    Serial.print("  address=");
    Serial.print(code.data.sony.address);
    Serial.print(" command=");
    Serial.print(code.data.sony.command);
    Serial.print(" bits=");
    Serial.print(code.data.sony.bits);
    break;
  default:
    break;
  }
  Serial.println();

  // A decoded frame no longer carries its raw timings: the decoders write into
  // the same union. Only an undecoded RAW frame can be dumped, which is also
  // the case where the timings are what you need to look at.
  if (code.protocol != UIAPIR_RAW) {
    return;
  }

  Serial.print("  ");
  Serial.print(code.data.raw.count);
  Serial.println(" durations, us (even index = mark, odd = space):");

  uint16_t maxSpace = 0;
  uint16_t maxMark = 0;
  for (uint16_t i = 0; i < code.data.raw.count; ++i) {
    const uint16_t us = code.rawMicros(i);
    if ((i & 1) == 0) {
      if (us > maxMark) maxMark = us;
    } else {
      if (us > maxSpace) maxSpace = us;
    }
    Serial.print(us);
    Serial.print((i + 1) % 10 == 0 ? '\n' : ' ');
  }
  Serial.println();

  Serial.print("  max mark = ");
  Serial.print(maxMark);
  Serial.print(" us, max space = ");
  Serial.print(maxSpace);
  Serial.print(" us, threshold = ");
  Serial.print(UIAPIR_FRAME_GAP_US);
  Serial.println(" us");
  // The longest space any supported format puts inside a frame is the NEC
  // leader at 4500 us. Anything longer is heading for the 6000 us threshold
  // that ends a frame, and that remote will start getting cut in half.
  if (maxSpace > UIAPIR_NEC_LEADER_SPACE_US) {
    Serial.print("  WARNING: intra-frame space exceeds the NEC leader space (");
    Serial.print(UIAPIR_NEC_LEADER_SPACE_US);
    Serial.println(" us)");
  }
}
