// SPDX-License-Identifier: MIT
// Copyright (c) 2026 kei23jp

#include <stdint.h>
static uint32_t clockMs;
unsigned long millis() { return clockMs; }
void delay(unsigned long ms) { clockMs += (uint32_t)ms; }
#include "UIAPIRLink.h"
#include <assert.h>
#include <deque>
#include <stdio.h>

// Serialize actual native wire bits, then run the production decoders. This
// exercises inverse NEC addresses, full SIRC words and AEHA bytes, not just mocks
// of decoded payloads. Hardware modulation/optical timing still needs real boards.
struct Radio {
    std::deque<IRCode> inbox, sent;
    bool fail = false;
    bool receive(IRCode &c) {
        if (inbox.empty()) return false;
        c = inbox.front(); inbox.pop_front(); return true;
    }
    bool capture(const uint8_t *ticks, uint16_t count) {
        if (fail) return false;
        assert(count <= UIAPIR_RAW_BUFFER_SIZE);
        IRCode c = {}; c.protocol = UIAPIR_RAW; c.data.raw.count = count;
        memcpy(c.data.raw.ticks, ticks, count);
#if UIAPIR_ENABLE_NEC
        IRNECData nec; bool repeat;
        if (uiapir_protocol::decodeNEC(ticks, count, nec, repeat)) {
            c.protocol = UIAPIR_NEC; c.data.nec = nec;
            if (repeat) c.flags = UIAPIR_FLAG_REPEAT;
        }
#endif
#if UIAPIR_ENABLE_AEHA
        IRAEHAData aeha;
        if (c.protocol == UIAPIR_RAW && uiapir_protocol::decodeAEHA(ticks, count, aeha)) {
            c.protocol = UIAPIR_AEHA; c.data.aeha = aeha;
        }
#endif
#if UIAPIR_ENABLE_SONY
        IRSonyData sony;
        if (c.protocol == UIAPIR_RAW && uiapir_protocol::decodeSony(ticks, count, sony)) {
            c.protocol = UIAPIR_SONY; c.data.sony = sony;
        }
#endif
        sent.push_back(c); return true;
    }
    bool sendRawTicks(const uint8_t *ticks, uint16_t count, uint8_t carrier) {
        assert(carrier == 38 || carrier == 40);
        if (!capture(ticks, count)) return false;
        assert(sent.back().protocol == UIAPIR_RAW); // Control cannot alias a data protocol.
        return true;
    }
    bool sendNEC(uint16_t address, uint8_t command, bool extended = false) {
        assert(extended ? (uint8_t)(address ^ (address >> 8)) != 255 : address <= 255);
        const uint32_t word = (address & 255) |
            ((uint32_t)(extended ? address >> 8 : (uint8_t)~address) << 8) |
            ((uint32_t)command << 16) | ((uint32_t)(uint8_t)~command << 24);
        uint8_t ticks[67]; ticks[0] = UIAPIR_TICKS(9000); ticks[1] = UIAPIR_TICKS(4500);
        for (uint8_t i = 0; i < 32; ++i) {
            ticks[2 + i * 2] = UIAPIR_TICKS(560);
            ticks[3 + i * 2] = UIAPIR_TICKS((word & ((uint32_t)1 << i)) ? 1690 : 560);
        }
        ticks[66] = UIAPIR_TICKS(560); return capture(ticks, sizeof(ticks));
    }
    bool sendSony(uint16_t address, uint8_t command, uint8_t bits) {
        assert((bits == 12 || bits == 15 || bits == 20) && command < 128 && address < (1U << (bits - 7)));
        const uint32_t word = ((uint32_t)address << 7) | command;
        uint8_t ticks[41]; ticks[0] = UIAPIR_TICKS(2400); ticks[1] = UIAPIR_TICKS(600);
        for (uint8_t i = 0; i < bits; ++i) {
            ticks[2 + i * 2] = UIAPIR_TICKS((word & ((uint32_t)1 << i)) ? 1200 : 600);
            if (i + 1 < bits) ticks[3 + i * 2] = UIAPIR_TICKS(600);
        }
        for (int i = 0; i < 3; ++i) if (!capture(ticks, 2 * bits + 1)) return false;
        return true;
    }
    bool sendAEHA(const uint8_t *bytes, uint8_t length) {
        assert(length >= 3 && length <= UIAPIR_MAX_AEHA_BYTES);
        uint8_t ticks[UIAPIR_RAW_BUFFER_SIZE]; ticks[0] = UIAPIR_TICKS(3400); ticks[1] = UIAPIR_TICKS(1700);
        for (uint16_t i = 0; i < (uint16_t)length * 8; ++i) {
            ticks[2 + i * 2] = UIAPIR_TICKS(425);
            ticks[3 + i * 2] = UIAPIR_TICKS((bytes[i / 8] & (1 << (i % 8))) ? 1275 : 425);
        }
        ticks[2 + length * 16] = UIAPIR_TICKS(425);
        return capture(ticks, 3 + length * 16);
    }
};
using Link = UIAPIRLinkBase<Radio>;
using Format = UIAPIRLinkFormat;
static void transfer(Radio &from, Radio &to) {
    while (!from.sent.empty()) { to.inbox.push_back(from.sent.front()); from.sent.pop_front(); }
}
static int drain(Link &link, Radio &radio, UIAPIRPacket &packet) {
    IRCode scratch = {}; int deliveries = 0;
    while (!radio.inbox.empty()) if (link.poll(scratch, packet)) ++deliveries;
    return deliveries;
}
static void finishAck(Link &rx, Radio &b, Link &tx, Radio &a) {
    IRCode scratch = {}; UIAPIRPacket packet = {};
    clockMs += 101; assert(!rx.poll(scratch, packet));
    assert(b.sent.size() == 1); transfer(b, a);
    assert(drain(tx, a, packet) == 0);
    assert(tx.status() == UIAPIRLinkStatus::Acknowledged);
    clockMs += 8;
}

static void roundTrip(Format format, UIAPIRLinkMode mode, const uint8_t *data, uint16_t bits) {
    Radio a, b;
    Link tx(a, format, mode, 0, 2), rx(b, format, mode, 1, 2);
    UIAPIRPacket packet = {};
    assert(tx.sendBits(data, bits));
    const IRCode body = a.sent[1];
    if (format == Format::AEHA && bits >= 24) {
        assert(body.data.aeha.length == bits / 8);
        assert(!memcmp(body.data.aeha.bytes, data, bits / 8)); // NO in-band overhead.
    }
    transfer(a, b); assert(drain(rx, b, packet) == 1);
    assert(packet.bitLength == bits && packet.length == (bits + 7) / 8);
    assert(!memcmp(packet.bytes, data, packet.length));
    b.inbox.push_back(body); assert(drain(rx, b, packet) == 0);
    if (mode == UIAPIRLinkMode::Reliable) finishAck(rx, b, tx, a);
    else {
        IRCode scratch = {}; clockMs += 1000;
        assert(!rx.poll(scratch, packet) && !tx.poll(scratch, packet));
        assert(a.sent.empty() && b.sent.empty() && tx.status() == UIAPIRLinkStatus::Sent);
    }
}

static void capacity(Format format, uint16_t maxBits) {
    Radio a; Link link(a, format, UIAPIRLinkMode::Simple, 0);
    assert(link.maxPayloadBits() == maxBits && link.maxPayload() == (maxBits + 7) / 8);
    uint8_t bytes[UIAPIR_LINK_MAX_BYTES + 1]; memset(bytes, 255, sizeof(bytes));
    assert(!link.send(nullptr, 1) && !link.send(bytes, 0));
    assert(!link.sendBits(bytes, maxBits + 1) && !link.send(bytes, link.maxPayload() + 1));
    if (maxBits & 7) {
        assert(!link.send(bytes, link.maxPayload())); // Never truncate high bits.
        bytes[link.maxPayload() - 1] &= (1 << (maxBits & 7)) - 1;
    }
    assert(a.sent.empty());
    for (auto mode : {UIAPIRLinkMode::Simple, UIAPIRLinkMode::Reliable}) {
        memset(bytes, 255, sizeof(bytes));
        if (maxBits & 7) bytes[link.maxPayload() - 1] &= (1 << (maxBits & 7)) - 1;
        roundTrip(format, mode, bytes, maxBits);
        memset(bytes, 0, sizeof(bytes)); roundTrip(format, mode, bytes, maxBits);
        for (uint8_t i = 0; i < link.maxPayload(); ++i) bytes[i] = (uint8_t)(i * 17 + 123);
        if (maxBits & 7) bytes[link.maxPayload() - 1] &= (1 << (maxBits & 7)) - 1;
        roundTrip(format, mode, bytes, maxBits);
        uint8_t one = 1; roundTrip(format, mode, &one, 8);
        if (format != Format::AEHA) roundTrip(format, mode, &one, 1);
    }
    assert(link.send(bytes, link.maxPayload())); // Byte API also accepts the full native capacity.
}

static void reliability(Format format) {
    clockMs = 0xffffff00U; // Run the same loss tests across millis wrap.
    Radio a, b;
    Link tx(a, format, UIAPIRLinkMode::Reliable, 0), rx(b, format, UIAPIRLinkMode::Reliable, 1);
    uint8_t bytes[UIAPIR_LINK_MAX_BYTES] = {};
    UIAPIRPacket packet = {}; IRCode scratch = {};
    assert(tx.send(bytes, tx.maxPayload()));
    const size_t frames = a.sent.size();
    // Lose the descriptor. Orphan native data must not be delivered or ACKed.
    a.sent.pop_front(); transfer(a, b); assert(drain(rx, b, packet) == 0);
    clockMs += 799; tx.poll(scratch, packet); assert(a.sent.empty());
    ++clockMs; tx.poll(scratch, packet); assert(a.sent.size() == frames);
    transfer(a, b); assert(drain(rx, b, packet) == 1);
    clockMs += 101; rx.poll(scratch, packet); assert(b.sent.size() == 1);
    b.sent.clear(); // Lose the ACK. Repeat the WHOLE packet, deliver only once.
    clockMs += 699; tx.poll(scratch, packet); transfer(a, b);
    assert(drain(rx, b, packet) == 0);
    clockMs += 101; rx.poll(scratch, packet);
    IRCode oldAck = b.sent.front(); transfer(b, a); assert(drain(tx, a, packet) == 0);
    assert(tx.status() == UIAPIRLinkStatus::Acknowledged);
    for (uint8_t i = 0; i < 10; ++i) { // Same value, new packet, including sequence wrap.
        clockMs += 8; assert(tx.send(bytes, tx.maxPayload()));
        a.inbox.push_back(oldAck); drain(tx, a, packet);
        if (i != 7) assert(tx.status() == UIAPIRLinkStatus::Pending);
        transfer(a, b); assert(drain(rx, b, packet) == 1);
        finishAck(rx, b, tx, a);
    }
    // Local failure, bounded retries, and failure after exactly initial+3 attempts.
    a.fail = true; assert(!tx.send(bytes, 1)); assert(tx.status() == UIAPIRLinkStatus::Failed);
    a.fail = false; assert(tx.send(bytes, 1));
    for (int i = 0; i < 4; ++i) { clockMs += 800; tx.poll(scratch, packet); }
    assert(tx.status() == UIAPIRLinkStatus::Failed && a.sent.size() == frames * 4);
}

static void corruption(Format format) {
    Radio a, b; UIAPIRPacket p = {}; IRCode scratch = {};
    Link tx(a, format, UIAPIRLinkMode::Reliable, 0), rx(b, format, UIAPIRLinkMode::Reliable, 1);
    uint8_t bytes[UIAPIR_LINK_MAX_BYTES] = {};
    assert(tx.send(bytes, tx.maxPayload()));
    IRCode descriptor = a.sent[0], body = a.sent[1]; a.sent.clear();
    IRCode damaged = body;
    if (body.protocol == UIAPIR_AEHA) damaged.data.aeha.bytes[0] ^= 1;
    else if (body.protocol == UIAPIR_NEC) damaged.data.nec.command ^= 1;
    else damaged.data.sony.command ^= 1;
    b.inbox.push_back(descriptor); b.inbox.push_back(damaged);
    assert(drain(rx, b, p) == 0 && b.sent.empty()); // Native-valid data, wrong packet CRC.
    descriptor.data.raw.ticks[3 + 16 * 2] = descriptor.data.raw.ticks[3 + 16 * 2] == UIAPIR_TICKS(560) ?
        UIAPIR_TICKS(1690) : UIAPIR_TICKS(560); // Alter CRC in an otherwise valid control.
    b.inbox.push_back(descriptor); b.inbox.push_back(body);
    assert(drain(rx, b, p) == 0);
    clockMs += 800; tx.poll(scratch, p); transfer(a, b); assert(drain(rx, b, p) == 1);
    clockMs += 101; rx.poll(scratch, p);
    IRCode wrongAck = b.sent.front(); wrongAck.data.raw.ticks[35] =
        wrongAck.data.raw.ticks[35] == UIAPIR_TICKS(560) ? UIAPIR_TICKS(1690) : UIAPIR_TICKS(560);
    a.inbox.push_back(wrongAck); drain(tx, a, p); assert(tx.status() == UIAPIRLinkStatus::Pending);
    transfer(b, a); drain(tx, a, p); assert(tx.status() == UIAPIRLinkStatus::Acknowledged);
}

static void filtering(Format format) {
    Radio a; Link tx(a, format, UIAPIRLinkMode::Reliable, 0, 2);
    uint8_t value = 1; assert(tx.send(&value, 1));
    for (uint8_t test = 0; test < 5; ++test) {
        Radio b;
        Link rx(b, format, test == 0 ? UIAPIRLinkMode::Simple : UIAPIRLinkMode::Reliable,
                test == 1 ? 0 : 1, test == 2 ? 1 : 2);
        UIAPIRPacket p = {}; IRCode scratch = {};
        for (IRCode frame : a.sent) {
            if (test == 3) frame.flags = UIAPIR_FLAG_RAW_OVERFLOW;
            if (test == 4) frame.flags = UIAPIR_FLAG_TIMING_CLIPPED;
            b.inbox.push_back(frame);
        }
        assert(drain(rx, b, p) == 0);
        clockMs += 1000; assert(!rx.poll(scratch, p) && b.sent.empty());
    }
    Radio b; Link rx(b, format, UIAPIRLinkMode::Reliable, 1, 2);
    UIAPIRPacket p = {}; IRCode scratch = {};
    b.inbox.push_back(a.sent[0]); assert(drain(rx, b, p) == 0);
    clockMs += 1000; assert(!rx.poll(scratch, p));
    b.inbox.push_back(a.sent[1]); assert(drain(rx, b, p) == 0); // Expired descriptor.
}

#if UIAPIR_ENABLE_SONY
static void sonyTiming() {
    Radio a, b; Link tx(a, Format::Sony20, UIAPIRLinkMode::Reliable, 0);
    Link rx(b, Format::Sony20, UIAPIRLinkMode::Reliable, 1);
    uint8_t bytes[] = {0xff, 0xff, 15}; UIAPIRPacket p = {}; IRCode scratch = {};
    assert(tx.send(bytes, 3) && a.sent.size() == 4);
    b.inbox.push_back(a.sent[0]); b.inbox.push_back(a.sent[1]); assert(drain(rx, b, p) == 1);
    clockMs += 45; b.inbox.push_back(a.sent[2]); assert(drain(rx, b, p) == 0);
    clockMs += 45; b.inbox.push_back(a.sent[3]); assert(drain(rx, b, p) == 0);
    clockMs += 99; assert(!rx.poll(scratch, p) && b.sent.empty());
    ++clockMs; assert(!rx.poll(scratch, p) && b.sent.size() == 1);
    transfer(b, a); drain(tx, a, p); assert(tx.status() == UIAPIRLinkStatus::Acknowledged);
}
#endif

int main() {
    assert(uiapir_link::crc16((const uint8_t *)"123456789", 9) == 0x29b1);
    Radio radio; Link invalid(radio, UIAPIR_RAW, UIAPIRLinkMode::Reliable, 0);
    assert(invalid.maxPayload() == 0);
#if UIAPIR_ENABLE_NEC
    capacity(Format::NEC, 16); capacity(Format::NECExtended, 24);
    reliability(Format::NECExtended); corruption(Format::NECExtended); filtering(Format::NECExtended);
    for (uint16_t i = 0; i < 256; ++i) {
        uint8_t data[] = {(uint8_t)i, (uint8_t)~i, (uint8_t)(i * 37)};
        roundTrip(Format::NECExtended, UIAPIRLinkMode::Reliable, data, 24);
    }
#endif
#if UIAPIR_ENABLE_SONY
    capacity(Format::Sony12, 12); capacity(Format::Sony15, 15); capacity(Format::Sony20, 20);
    reliability(Format::Sony20); corruption(Format::Sony20); filtering(Format::Sony20); sonyTiming();
#endif
#if UIAPIR_ENABLE_AEHA
    capacity(Format::AEHA, UIAPIR_MAX_AEHA_BYTES * 8);
    reliability(Format::AEHA); corruption(Format::AEHA); filtering(Format::AEHA);
    uint8_t bytes[UIAPIR_MAX_AEHA_BYTES]; memset(bytes, 0xa5, sizeof(bytes));
    for (uint16_t length = 1; length <= UIAPIR_MAX_AEHA_BYTES; ++length)
        roundTrip(Format::AEHA, UIAPIRLinkMode::Reliable, bytes, length * 8);
#endif
    (void)capacity; (void)reliability; (void)corruption; (void)filtering;
    printf("UIAPIR full-capacity link tests passed (host object: %zu bytes)\n", sizeof(Link));
}
