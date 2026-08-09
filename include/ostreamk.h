#pragma once

#include <stdint.h>

typedef struct argb {
    uint32_t a;
    uint32_t r;
    uint32_t g;
    uint32_t b;
} ARGBColor_t, *pARGBColor_t;

inline constexpr uint32_t ARGBToHex(const uint32_t a, const uint32_t r, const uint32_t g, const uint32_t b) {
    return (a << 24) | (r << 16) | (g << 8) | b;
}

inline constexpr uint32_t ARGBToHex(const ARGBColor_t& c) {
    return (c.a << 24) | (c.r << 16) | (c.g << 8) | c.b;
}

inline constexpr ARGBColor_t HexToARGB(uint32_t hex) {
    return ARGBColor_t {
        static_cast<uint32_t>((hex >> 24) & 0xFF),
        static_cast<uint32_t>((hex >> 16) & 0xFF),
        static_cast<uint32_t>((hex >> 8) & 0xFF),
        static_cast<uint32_t>(hex & 0xFF)
    };
}

constexpr uint32_t FG_COLOR    = ARGBToHex(  0, 255, 255, 255); // 白色前景 0x00FFFFFF
constexpr uint32_t BG_COLOR    = ARGBToHex(  0,   0,   0,   0); // 黑色背景 0x00000000
constexpr uint32_t FONT_WIDTH  = 8;
constexpr uint32_t FONT_HEIGHT = 16;

class ostreamk {
private:
    ;
public:
    ostreamk() = default;
    void write(const uint8_t c);
    void write(const uint8_t* str);
    void write(const char c);
    void write(const char* str);
    void writeHex_uint32(uint32_t val);
    friend ostreamk& operator<<(ostreamk& os, const char*     s); 
    friend ostreamk& operator<<(ostreamk& os, const uint8_t   v);  
    friend ostreamk& operator<<(ostreamk& os, const uint16_t  v);  
    friend ostreamk& operator<<(ostreamk& os, const uint32_t  v);  
    friend ostreamk& operator<<(ostreamk& os, const uint64_t  v); 
    friend ostreamk& operator<<(ostreamk& os, const uint8_t*  p);
    friend ostreamk& operator<<(ostreamk& os, const uint16_t* p);
    friend ostreamk& operator<<(ostreamk& os, const uint32_t* p);
    friend ostreamk& operator<<(ostreamk& os, const uint64_t* p);
    // friend ostreamk& operator<<(ostreamk& os, const void*     p);
    
};

extern "C" void kernel_panic(const char* message);

constexpr const char* endl = "\n";