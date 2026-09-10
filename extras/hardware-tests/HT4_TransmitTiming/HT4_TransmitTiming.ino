// HT4 - transmit timing, for a logic analyser or oscilloscope on D6.
//
// The host tests prove the transmitter and the decoder agree on what the
// timings should be. They cannot prove the hardware produces them: the carrier
// comes from TIM1 and every envelope duration from a busy-wait on TIM2, and
// neither exists off the target.
//
// Probe D6 (PC4). Sampling at 4 MHz or more resolves the 38 kHz carrier; 1 MHz
// is enough if you only need the envelope.
//
// Each case is announced on Serial (D15 / PD5) and separated by 500 ms of
// silence, so the capture segments line up with the log. The whole sequence
// repeats, so a single long capture can be split anywhere.

#include <UIAPIR.h>

static const uint8_t TX_PIN = 6; // PC4 / TIM1_CH4

static UIAPIR ir;

static void announce(const char *what) {
  Serial.println(what);
  Serial.flush();
  delay(500); // silence between cases
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("HT4 transmit timing - probe D6 (PC4)");
  if (!ir.begin(UIAPIR_UNUSED_PIN, TX_PIN)) {
    Serial.println("begin() failed");
    while (true) {}
  }
}

void loop() {
  // 1. A 20 ms unmodulated burst: measure carrier frequency and duty cycle.
  //    Expect 38.0 kHz +/- 1% and a 33% duty (1/3, per ChaN).
  {
    announce("1. 20 ms burst at 38 kHz - measure frequency and duty");
    const uint16_t burst[] = {20000};
    ir.sendRaw(burst, 1, 38);
  }

  // 2. The same at the Sony carrier. Expect 40.0 kHz +/- 1%, 33% duty.
  {
    announce("2. 20 ms burst at 40 kHz");
    const uint16_t burst[] = {20000};
    ir.sendRaw(burst, 1, 40);
  }

  // 3. NEC with two repeat codes. Expect, on the envelope:
  //      leader     9000 us mark, 4500 us space
  //      bit 0       560 us mark,  560 us space
  //      bit 1       560 us mark, 1690 us space
  //      trailer     560 us mark
  //      frame start to repeat start, and repeat to repeat: 110 ms
  //    Address 0x12 / command 0x34 makes the frame 67.98 ms long, so the gap
  //    before the first repeat is about 42 ms.
  announce("3. NEC 0x12/0x34 plus two repeats - expect 110 ms periods");
  ir.sendNEC(0x12, 0x34, false, 2);

  // 4. Extended NEC, all ones. The frame is at its longest here (77.0 ms), so
  //    the gap shrinks to about 33 ms while the period stays 110 ms. A fixed
  //    gap instead of a period would show up as a different spacing between
  //    cases 3 and 4.
  announce("4. extended NEC 0xfffe/0xff plus two repeats - period still 110 ms");
  ir.sendNEC(0xfffe, 0xff, true, 2);

  // 5. AEHA, two frames. Expect 3400/1700 leader, 425 us marks, 425/1275 us
  //    spaces, a 425 us trailer, and 130 ms from frame start to frame start.
  {
    announce("5. AEHA 6 bytes, two frames - expect a 130 ms period");
    const uint8_t data[] = {0x02, 0x20, 0x80, 0x00, 0x12, 0x34};
    ir.sendAEHA(data, sizeof(data), 2);
  }

  // 6. Sony SIRC 20 bit, all ones, three frames. This is the tightest timing
  //    the library produces: the frame is 38.4 ms and the period 45 ms, so the
  //    gap between frames is only 6.6 ms. The receiver's frame threshold is
  //    6 ms, so this gap is what sets its upper bound - confirm it really is
  //    6.6 ms and not shorter.
  announce("6. SIRC 20 bit all ones, three frames - expect 45 ms period, 6.6 ms gap");
  ir.sendSony(0x1fff, 0x7f, 20, 3);

  // 7. Sony SIRC 12 bit. Expect 2400/600 leader, 600 us and 1200 us marks with
  //    600 us spaces, no trailer, and a 45 ms period.
  announce("7. SIRC 12 bit 1/19, three frames - expect a 45 ms period");
  ir.sendSony(1, 19, 12, 3);

  announce("--- sequence complete, repeating ---");
}
