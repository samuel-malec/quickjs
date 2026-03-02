#pragma once

#include <cstdint>
#include <vector>
#include <string>

using bytes = std::vector<uint8_t>;

// Basic byte encoding
inline void encode_u8(bytes& buf, uint8_t val) {
    buf.push_back(val);
}

inline void encode_i8(bytes& buf, int8_t val) {
    buf.push_back(static_cast<uint8_t>(val));
}

// Little-endian integer encoding
template<typename T>
inline void encode_int_le(bytes& buf, T value, int width) {
    auto u = static_cast<std::make_unsigned_t<T>>(value);
    for (int i = 0; i < width; i++) {
        buf.push_back(static_cast<uint8_t>((u >> (i * 8)) & 0xFF));
    }
}

inline void encode_u16(bytes& buf, uint16_t val) {
    encode_int_le(buf, val, 2);
}

inline void encode_i16(bytes& buf, int16_t val) {
    encode_int_le(buf, val, 2);
}

inline void encode_u32(bytes& buf, uint32_t val) {
    encode_int_le(buf, val, 4);
}

inline void encode_i32(bytes& buf, int32_t val) {
    encode_int_le(buf, val, 4);
}

// LEB128 encoding (variable-length integers)
inline void encode_leb128_u(bytes& buf, uint32_t val) {
    do {
        uint8_t byte = val & 0x7f;
        val >>= 7;
        if (val != 0) {
            byte |= 0x80;
        }
        buf.push_back(byte);
    } while (val != 0);
}

inline void encode_leb128_i(bytes& buf, int32_t val) {
    bool more = true;
    while (more) {
        uint8_t byte = val & 0x7f;
        val >>= 7;
        if ((val == 0 && (byte & 0x40) == 0) || (val == -1 && (byte & 0x40) != 0)) {
            more = false;
        } else {
            byte |= 0x80;
        }
        buf.push_back(byte);
    }
}

// Encode atom as LEB128 index (shifted left by 1)
inline void encode_atom(bytes& buf, uint32_t atom_index) {
    encode_leb128_u(buf, atom_index << 1);
}

// Encode string with LEB128 length prefix
inline void encode_string(bytes& buf, const std::string& str, bool is_wide_char = false) {
    uint32_t len = str.size();
    encode_leb128_u(buf, (len << 1) | (is_wide_char ? 1 : 0));
    for (char c : str) {
        buf.push_back(static_cast<uint8_t>(c));
    }
}
