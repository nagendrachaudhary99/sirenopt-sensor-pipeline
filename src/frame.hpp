#pragma once
#include <cstdint>
#include <cstring>
#include <array>
#include "crc.hpp"

// Fixed-size wire frame emitted by every sensor, little-endian fields:
//   magic(2) | sensor_id(2) | seq(4) | timestamp_us(8) | payload(32) | crc16(2)
// CRC covers every byte before the crc field.
struct Frame {
    static constexpr uint16_t kMagic = 0x51E0;      // "SE" - sensing edge
    static constexpr size_t kPayloadBytes = 32;
    static constexpr size_t kHeaderBytes = 2 + 2 + 4 + 8;
    static constexpr size_t kWireBytes = kHeaderBytes + kPayloadBytes + 2;

    uint16_t magic = kMagic;
    uint16_t sensor_id = 0;
    uint32_t seq = 0;
    uint64_t timestamp_us = 0;
    std::array<uint8_t, kPayloadBytes> payload{};
    uint16_t crc = 0;

    void seal() {
        std::array<uint8_t, kWireBytes> buf{};
        size_t n = serialize_prefix(buf.data());
        crc = crc16_ccitt(buf.data(), n);
    }

    bool valid() const {
        if (magic != kMagic) return false;
        std::array<uint8_t, kWireBytes> buf{};
        size_t n = serialize_prefix(buf.data());
        return crc16_ccitt(buf.data(), n) == crc;
    }

    size_t serialize(uint8_t* out) const {
        size_t n = serialize_prefix(out);
        out[n++] = static_cast<uint8_t>(crc & 0xFF);
        out[n++] = static_cast<uint8_t>(crc >> 8);
        return n;
    }

    static bool deserialize(const uint8_t* in, size_t len, Frame& f) {
        if (len != kWireBytes) return false;
        f.magic = static_cast<uint16_t>(in[0] | (in[1] << 8));
        f.sensor_id = static_cast<uint16_t>(in[2] | (in[3] << 8));
        std::memcpy(&f.seq, in + 4, 4);
        std::memcpy(&f.timestamp_us, in + 8, 8);
        std::memcpy(f.payload.data(), in + 16, kPayloadBytes);
        f.crc = static_cast<uint16_t>(in[16 + kPayloadBytes] | (in[16 + kPayloadBytes + 1] << 8));
        return true;
    }

private:
    size_t serialize_prefix(uint8_t* out) const {
        size_t n = 0;
        out[n++] = static_cast<uint8_t>(magic & 0xFF);
        out[n++] = static_cast<uint8_t>(magic >> 8);
        out[n++] = static_cast<uint8_t>(sensor_id & 0xFF);
        out[n++] = static_cast<uint8_t>(sensor_id >> 8);
        std::memcpy(out + n, &seq, 4); n += 4;
        std::memcpy(out + n, &timestamp_us, 8); n += 8;
        std::memcpy(out + n, payload.data(), kPayloadBytes); n += kPayloadBytes;
        return n;
    }
};
