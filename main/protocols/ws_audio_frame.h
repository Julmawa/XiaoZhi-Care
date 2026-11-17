#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace care_protocol {
struct WsAudioFrame {
    const uint8_t* data;
    size_t size;
    uint32_t timestamp;
};

inline std::optional<WsAudioFrame> ParseWsAudioFrame(int version, const char* data, size_t len) {
    if (!data || !len) return std::nullopt;
    const auto* bytes = reinterpret_cast<const uint8_t*>(data);
    const auto read32 = [bytes](size_t off) {
        return (uint32_t(bytes[off]) << 24) | (uint32_t(bytes[off+1]) << 16) |
               (uint32_t(bytes[off+2]) << 8) | uint32_t(bytes[off+3]);
    };
    size_t header = 0, size = len;
    uint32_t timestamp = 0;
    if (version == 2) {
        header = 16; // BinaryProtocol2
        if (len < header || bytes[2] || bytes[3]) return std::nullopt;
        timestamp = read32(8);
        size = read32(12);
    } else if (version == 3) {
        header = 4; // BinaryProtocol3
        if (len < header || bytes[0]) return std::nullopt;
        size = (size_t(bytes[2]) << 8) | bytes[3];
    }
    if (size == 0 || size > 8192 || size > len - header) return std::nullopt;
    return WsAudioFrame{bytes+header,size,timestamp};
}

inline bool ValidOpusParams(int sample_rate, int duration_ms) {
    return (sample_rate == 8000 || sample_rate == 12000 || sample_rate == 16000 ||
            sample_rate == 24000 || sample_rate == 48000) &&
           (duration_ms == 5 || duration_ms == 10 || duration_ms == 20 ||
            duration_ms == 40 || duration_ms == 60 || duration_ms == 80 ||
            duration_ms == 100 || duration_ms == 120);
}
} // namespace care_protocol
