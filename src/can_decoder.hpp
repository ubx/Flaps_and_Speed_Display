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
};
