// SPDX-License-Identifier: MIT
// Copyright (c) 2026 kei23jp

#pragma once
#include "UIAPIR.h"
#include "UIAPIRProtocol.h"

enum class UIAPIRLinkMode : uint8_t { Simple, Reliable };
enum class UIAPIRLinkStatus : uint8_t { Idle, Pending, Sent, Acknowledged, Failed };
enum class UIAPIRLinkFormat : uint8_t { NEC, NECExtended, AEHA, Sony12, Sony15, Sony20, Invalid };
#if UIAPIR_ENABLE_AEHA
#if UIAPIR_MAX_AEHA_BYTES > 255
#error "UIAPIRLink AEHA length must fit the underlying uint8_t sendAEHA API"
#endif
#define UIAPIR_LINK_MAX_BYTES UIAPIR_MAX_AEHA_BYTES
#else
#define UIAPIR_LINK_MAX_BYTES 3
#endif
#define UIAPIR_LINK_CONTROL_DURATIONS 67

struct UIAPIRPacket {
    uint16_t bitLength;
    uint8_t length; // ceil(bitLength / 8); little-endian, unused high bits are zero.
    uint8_t bytes[UIAPIR_LINK_MAX_BYTES];
};

namespace uiapir_link {
inline uint16_t crc16(const uint8_t *bytes, uint8_t length, uint16_t crc = 0xffff) {
    while (length--) {
        crc ^= (uint16_t)*bytes++ << 8;
        for (uint8_t bit = 0; bit < 8; ++bit)
            crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
    }
    return crc;
}
inline bool due(uint32_t now, uint32_t deadline) { return (int32_t)(now - deadline) >= 0; }
inline UIAPIRLinkFormat defaultFormat(IRProtocol protocol) {
    return protocol == UIAPIR_NEC ? UIAPIRLinkFormat::NECExtended :
           protocol == UIAPIR_AEHA ? UIAPIRLinkFormat::AEHA :
           protocol == UIAPIR_SONY ? UIAPIRLinkFormat::Sony20 : UIAPIRLinkFormat::Invalid;
}
} // namespace uiapir_link

// Radio is a compile-time test seam, without a vtable. No heap allocation.
// A separate control frame leaves ALL native data bits available to the caller.
template<class Radio> class UIAPIRLinkBase {
public:
    UIAPIRLinkBase(Radio &radio, UIAPIRLinkFormat format, UIAPIRLinkMode mode,
                   uint8_t endpoint, uint8_t channel = 0)
        : _radio(radio), _format(format), _mode(mode), _endpoint(endpoint), _channel(channel) {}
    // Existing constructor selects the largest native variant (NEC24 / Sony20).
    UIAPIRLinkBase(Radio &radio, IRProtocol protocol, UIAPIRLinkMode mode,
                   uint8_t endpoint, uint8_t channel = 0)
        : UIAPIRLinkBase(radio, uiapir_link::defaultFormat(protocol), mode, endpoint, channel) {}

    uint16_t maxPayloadBits() const {
        if (_endpoint > 1 || _channel > 3 || UIAPIR_RAW_BUFFER_SIZE < UIAPIR_LINK_CONTROL_DURATIONS ||
            (_mode != UIAPIRLinkMode::Simple && _mode != UIAPIRLinkMode::Reliable)) return 0;
        switch (_format) {
#if UIAPIR_ENABLE_NEC
        case UIAPIRLinkFormat::NEC: return 16;
        case UIAPIRLinkFormat::NECExtended: return 24;
#endif
#if UIAPIR_ENABLE_SONY
        case UIAPIRLinkFormat::Sony12: return 12;
        case UIAPIRLinkFormat::Sony15: return 15;
        case UIAPIRLinkFormat::Sony20: return 20;
#endif
#if UIAPIR_ENABLE_AEHA
        case UIAPIRLinkFormat::AEHA: return UIAPIR_MAX_AEHA_BYTES * 8U;
#endif
        default: return 0;
        }
    }
    uint8_t maxPayload() const { return (uint8_t)((maxPayloadBits() + 7) / 8); }
    uint16_t requiredCaptureSize() const {
        const uint16_t native = _format == UIAPIRLinkFormat::AEHA ?
            UIAPIR_AEHA_DURATIONS(UIAPIR_MAX_AEHA_BYTES) : UIAPIR_LINK_CONTROL_DURATIONS;
        return native > UIAPIR_LINK_CONTROL_DURATIONS ? native : UIAPIR_LINK_CONTROL_DURATIONS;
    }
    UIAPIRLinkStatus status() const { return _status; }
    bool busy() const {
        return _status == UIAPIRLinkStatus::Pending || _ackPending ||
               (_quiet && !uiapir_link::due(millis(), _quietDeadline));
    }
    // Sony's last byte is partial at maximum length: high unused bits MUST be zero.
    bool send(const uint8_t *bytes, uint8_t length) {
        if (!length || length > maxPayload()) return false;
        const uint16_t bits = (uint16_t)length * 8;
        return sendBits(bytes, bits > maxPayloadBits() ? maxPayloadBits() : bits);
    }
    bool sendBits(const uint8_t *bytes, uint16_t bits) {
        if (busy() || !bytes || !bits || bits > maxPayloadBits() ||
            (_format == UIAPIRLinkFormat::AEHA && (bits & 7))) return false;
        const uint8_t length = (uint8_t)((bits + 7) / 8);
        if ((bits & 7) && (bytes[length - 1] >> (bits & 7))) return false;
        _tx.bitLength = bits; _tx.length = length;
        memcpy(_tx.bytes, bytes, length);
        _txHeader = (uint8_t)((_channel << 6) | (_endpoint << 5) |
                    ((_sequence++ & 7) << 2) | (_mode == UIAPIRLinkMode::Reliable ? 2 : 0));
        _txUnits = _format == UIAPIRLinkFormat::AEHA ? length : (uint8_t)bits;
        _txCrc = digest(_txHeader, _txUnits, _tx);
        _retries = 0;
        if (!emitPacket()) { _status = UIAPIRLinkStatus::Failed; return false; }
        _status = _mode == UIAPIRLinkMode::Reliable ? UIAPIRLinkStatus::Pending : UIAPIRLinkStatus::Sent;
        quietFor(8);
        _deadline = millis() + 800;
        return true;
    }

    // Service on BOTH peers. Only true makes packet valid. Caller owns RAW scratch.
    bool poll(IRCode &scratch, UIAPIRPacket &packet) {
        bool delivered = false;
        if (_radio.receive(scratch) && !scratch.flags && maxPayloadBits()) {
            uint32_t control;
            if (decodeControl(scratch, control)) {
                const uint8_t header = (uint8_t)control;
                const uint8_t units = (uint8_t)(control >> 8);
                const uint16_t crc = (uint16_t)(control >> 16);
                if ((header >> 6) == _channel && ((header >> 5) & 1) != _endpoint &&
                    bool(header & 2) == (_mode == UIAPIRLinkMode::Reliable) && validUnits(units)) {
                    if (header & 1) {
                        if (_status == UIAPIRLinkStatus::Pending &&
                            header == (uint8_t)((_txHeader ^ 0x20) | 1) && units == _txUnits && crc == _txCrc) {
                            _status = UIAPIRLinkStatus::Acknowledged; quietFor(8);
                        }
                    } else {
                        _rxHeader = header; _rxUnits = units; _rxCrc = crc; _rxOpen = true;
                        _rxDeadline = millis() + 200 +
                            (_format == UIAPIRLinkFormat::AEHA ? (uint32_t)units * 14 : 0);
                    }
                }
            } else if (_rxOpen && !uiapir_link::due(millis(), _rxDeadline) &&
                       decodeData(scratch, packet) && digest(_rxHeader, _rxUnits, packet) == _rxCrc) {
                delivered = !_haveLast || _rxHeader != _lastHeader ||
                            _rxUnits != _lastUnits || _rxCrc != _lastCrc;
                _haveLast = true; _lastHeader = _rxHeader; _lastUnits = _rxUnits; _lastCrc = _rxCrc;
                if (_rxHeader & 2) {
                    _ackHeader = (uint8_t)((_rxHeader ^ 0x20) | 1);
                    _ackUnits = _rxUnits; _ackCrc = _rxCrc; _ackPending = true;
                    _ackDeadline = millis() + (isSony() ? 100 : 2);
                }
            }
        }
        const uint32_t now = millis();
        if (_rxOpen && uiapir_link::due(now, _rxDeadline)) _rxOpen = false;
        if (_quiet && uiapir_link::due(now, _quietDeadline)) _quiet = false;
        if (_ackPending && uiapir_link::due(now, _ackDeadline)) {
            emitControl(_ackHeader, _ackUnits, _ackCrc);
            _ackPending = false; // A data retry recovers a lost/failed ACK.
            quietFor(8);
        } else if (!_ackPending && _status == UIAPIRLinkStatus::Pending &&
                   uiapir_link::due(now, _deadline)) {
            if (_retries == 3 || !emitPacket()) _status = UIAPIRLinkStatus::Failed;
            else { ++_retries; _deadline = millis() + 800; }
        }
        return delivered;
    }

private:
    bool isSony() const {
        return _format == UIAPIRLinkFormat::Sony12 || _format == UIAPIRLinkFormat::Sony15 ||
               _format == UIAPIRLinkFormat::Sony20;
    }
    bool validUnits(uint8_t units) const {
        return units && (_format == UIAPIRLinkFormat::AEHA ? units <= maxPayload() : units <= maxPayloadBits());
    }
    void quietFor(uint16_t ms) { _quiet = true; _quietDeadline = millis() + ms; }
    uint16_t digest(uint8_t header, uint8_t units, const UIAPIRPacket &p) const {
        const uint8_t metadata[] = {0x02, (uint8_t)_format, header, units};
        return uiapir_link::crc16(p.bytes, p.length, uiapir_link::crc16(metadata, sizeof(metadata)));
    }
    bool emitControl(uint8_t header, uint8_t units, uint16_t crc) {
        uint32_t value = header | ((uint32_t)units << 8) | ((uint32_t)crc << 16);
        uint8_t ticks[UIAPIR_LINK_CONTROL_DURATIONS];
        // Private RAW envelope: 8ms mark / 1ms space is not a supported protocol leader.
        ticks[0] = UIAPIR_TICKS(8000); ticks[1] = UIAPIR_TICKS(1000);
        for (uint8_t bit = 0; bit < 32; ++bit, value >>= 1) {
            ticks[2 + bit * 2] = UIAPIR_TICKS(560);
            ticks[3 + bit * 2] = UIAPIR_TICKS((value & 1) ? 1690 : 560);
        }
        ticks[66] = UIAPIR_TICKS(560);
        return _radio.sendRawTicks(ticks, sizeof(ticks), isSony() ? 40 : 38);
    }
    static bool decodeControl(const IRCode &code, uint32_t &value) {
        using uiapir_protocol::matchTicks;
        if (code.protocol != UIAPIR_RAW || code.data.raw.count != UIAPIR_LINK_CONTROL_DURATIONS) return false;
        const uint8_t *ticks = code.data.raw.ticks;
        if (!matchTicks(ticks[0], 8000) || !matchTicks(ticks[1], 1000) || !matchTicks(ticks[66], 560)) return false;
        value = 0;
        for (uint8_t bit = 0; bit < 32; ++bit) {
            if (!matchTicks(ticks[2 + bit * 2], 560)) return false;
            if (matchTicks(ticks[3 + bit * 2], 560)) continue;
            if (!matchTicks(ticks[3 + bit * 2], 1690)) return false;
            value |= (uint32_t)1 << bit;
        }
        return true;
    }
    bool emitPacket() {
        if (!emitControl(_txHeader, _txUnits, _txCrc)) return false;
        delay(8); // Finish control capture before starting the native data frame.
        uint32_t value = 0;
        if (_format != UIAPIRLinkFormat::AEHA)
            for (uint8_t i = 0; i < _tx.length; ++i) value |= (uint32_t)_tx.bytes[i] << (8 * i);
        (void)value;
        switch (_format) {
#if UIAPIR_ENABLE_NEC
        case UIAPIRLinkFormat::NEC:
            return _radio.sendNEC(value & 0xff, (value >> 8) & 0xff);
        case UIAPIRLinkFormat::NECExtended: {
            const uint16_t address = (uint16_t)value;
            // Inverse address pairs use standard NEC; RX restores the high byte.
            // This preserves every 24-bit input, including the 256 reserved pairs.
            const bool extended = (uint8_t)(address ^ (address >> 8)) != 0xff;
            return _radio.sendNEC(extended ? address : address & 0xff, (value >> 16) & 0xff, extended);
        }
#endif
#if UIAPIR_ENABLE_SONY
        case UIAPIRLinkFormat::Sony12:
        case UIAPIRLinkFormat::Sony15:
        case UIAPIRLinkFormat::Sony20:
            return _radio.sendSony((uint16_t)(value >> 7), value & 0x7f, (uint8_t)maxPayloadBits());
#endif
#if UIAPIR_ENABLE_AEHA
        case UIAPIRLinkFormat::AEHA:
            if (_tx.length >= UIAPIR_AEHA_MIN_BYTES) return _radio.sendAEHA(_tx.bytes, _tx.length);
            else {
                uint8_t padded[UIAPIR_AEHA_MIN_BYTES] = {};
                memcpy(padded, _tx.bytes, _tx.length);
                return _radio.sendAEHA(padded, sizeof(padded));
            }
#endif
        default: return false;
        }
    }
    bool decodeData(const IRCode &code, UIAPIRPacket &p) const {
        (void)code;
        p.bitLength = _format == UIAPIRLinkFormat::AEHA ? (uint16_t)_rxUnits * 8 : _rxUnits;
        p.length = (uint8_t)((p.bitLength + 7) / 8);
        uint32_t value = 0;
        switch (_format) {
#if UIAPIR_ENABLE_NEC
        case UIAPIRLinkFormat::NEC:
            if (code.protocol != UIAPIR_NEC || code.data.nec.addressBits != 8) return false;
            value = code.data.nec.address | ((uint32_t)code.data.nec.command << 8);
            break;
        case UIAPIRLinkFormat::NECExtended:
            if (code.protocol != UIAPIR_NEC || (code.data.nec.addressBits != 8 && code.data.nec.addressBits != 16)) return false;
            value = code.data.nec.address | ((uint32_t)code.data.nec.command << 16);
            if (code.data.nec.addressBits == 8) value |= (uint32_t)(uint8_t)~code.data.nec.address << 8;
            break;
#endif
#if UIAPIR_ENABLE_SONY
        case UIAPIRLinkFormat::Sony12:
        case UIAPIRLinkFormat::Sony15:
        case UIAPIRLinkFormat::Sony20:
            if (code.protocol != UIAPIR_SONY || code.data.sony.bits != maxPayloadBits()) return false;
            value = ((uint32_t)code.data.sony.address << 7) | code.data.sony.command;
            break;
#endif
#if UIAPIR_ENABLE_AEHA
        case UIAPIRLinkFormat::AEHA: {
            const uint8_t wireLength = p.length < UIAPIR_AEHA_MIN_BYTES ? UIAPIR_AEHA_MIN_BYTES : p.length;
            if (code.protocol != UIAPIR_AEHA || code.data.aeha.length != wireLength) return false;
            for (uint8_t i = p.length; i < wireLength; ++i) if (code.data.aeha.bytes[i]) return false;
            memcpy(p.bytes, code.data.aeha.bytes, p.length);
            return true;
        }
#endif
        default: return false;
        }
        if (value >> p.bitLength) return false; // Check padding; never silently mask.
        for (uint8_t i = 0; i < p.length; ++i) { p.bytes[i] = (uint8_t)value; value >>= 8; }
        return true;
    }

    Radio &_radio;
    UIAPIRLinkFormat _format;
    UIAPIRLinkMode _mode;
    uint8_t _endpoint, _channel;
    UIAPIRLinkStatus _status = UIAPIRLinkStatus::Idle;
    UIAPIRPacket _tx = {};
    uint32_t _deadline = 0, _ackDeadline = 0, _quietDeadline = 0, _rxDeadline = 0;
    uint16_t _txCrc = 0, _rxCrc = 0, _lastCrc = 0, _ackCrc = 0;
    uint8_t _sequence = 0, _retries = 0, _txHeader = 0, _txUnits = 0;
    uint8_t _rxHeader = 0, _rxUnits = 0, _lastHeader = 0, _lastUnits = 0, _ackHeader = 0, _ackUnits = 0;
    bool _haveLast = false, _ackPending = false, _quiet = false, _rxOpen = false;
};
using UIAPIRLink = UIAPIRLinkBase<UIAPIR>;
