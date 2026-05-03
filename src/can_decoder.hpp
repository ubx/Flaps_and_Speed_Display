#pragma once

#include <cstdint>
#include <cstring>
#include <bit>

class CANDecoder
{
public:
    static float decode_float(const uint8_t* data)
    {
        uint32_t raw;
        std::memcpy(&raw, data + 4, sizeof(raw));
        raw = __builtin_bswap32(raw);
        return std::bit_cast<float>(raw);
    }

    static double decode_double_l(const uint8_t* data)
    {
        uint32_t raw;
        std::memcpy(&raw, data + 4, sizeof(raw));
        raw = __builtin_bswap32(raw);
        return static_cast<int32_t>(raw) / 1E7;
    }

    static uint16_t decode_u16(const uint8_t* data)
    {
        uint16_t raw;
        std::memcpy(&raw, data + 4, sizeof(raw));
        return __builtin_bswap16(raw);
    }

    static int decode_char(const uint8_t* data)
    {
        return static_cast<int>(data[4]);
    }

    static int decode_flap_idx(const uint8_t* data)
    {
        // Decode 340 as 2-bytes array. Ignore Byte 0, Byte 1 is the flapIdx.
        // In this decoder, data usually points to the start of the CAN frame data (8 bytes).
        // However, looking at decode_char, it uses data[4].
        // Let's check how it is used in main.cpp.
        return static_cast<int>(data[5]);
    }
};
