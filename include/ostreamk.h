#pragma once

#include <stdint.h>

namespace kprint {

typedef struct {
    uint8_t a;
    uint8_t r;
    uint8_t g;
    uint8_t b;
} ARGBColor_t;

inline constexpr uint32_t ARGBToHex(const uint8_t a, const uint8_t r, const uint8_t g, const uint8_t b) {
    return (a << 24) | (r << 16) | (g << 8) | b;
}

inline constexpr uint32_t ARGBToHex(const ARGBColor_t& c) {
    return (c.a << 24) | (c.r << 16) | (c.g << 8) | c.b;
}

inline constexpr ARGBColor_t HexToARGB(uint32_t hex) {
    return ARGBColor_t {
        static_cast<uint8_t>((hex >> 24) & 0xFF),
        static_cast<uint8_t>((hex >> 16) & 0xFF),
        static_cast<uint8_t>((hex >> 8) & 0xFF),
        static_cast<uint8_t>(hex & 0xFF)
    };
}

class ostreamk {
private:
    ARGBColor_t fg;
    ARGBColor_t bg;
public:
    ostreamk();
    ostreamk(const ARGBColor_t fg, const ARGBColor_t bg);
    ostreamk(const uint32_t    fg, const uint32_t    bg);
    ostreamk(ostreamk&) = delete;
    ostreamk(ostreamk&&) = delete;

    void drawChar(char c, uint64_t x, uint64_t y);
    void newline();
    void scroll();

    void write(const uint8_t c);
    void write(const uint8_t* str);
    void write(const char c);
    void write(const char* str);
    void writeHex_uint32(uint32_t val);
    void writeHex_uint16(uint16_t val);
    void writeHex_uint8(uint8_t val);
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
    
    uint32_t __get_fg() {
        return ARGBToHex(fg);
    }
    uint32_t __get_bg() {
        return ARGBToHex(bg);
    }
    ostreamk& __log_prefix();
};

extern ostreamk __kout;
}

extern "C" void kernel_panic(const char* message);


#define FONT_WIDTH ((uint32_t)8)
#define FONT_HEIGHT ((uint32_t)16)
#define endl "\n";
#define kout (kprint::__kout)
