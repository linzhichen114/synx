#include "kprint.h"
#include "font.h"
#include <limine.h>
#include <stddef.h>
#include <string.h>


extern volatile struct limine_framebuffer_request framebuffer_request;

namespace kprint {

ostreamk __kout(0x00FFFFFF, 0x00000000); 
lock::SpinLock __kprint_lock;
__kprint_locked __locked_kout;

namespace {
    size_t cursor_x = 0;
    size_t cursor_y = 0;
    size_t max_cols = 0;
    size_t max_rows = 0;

    inline volatile uint32_t* getframebuffer() {
        if (!framebuffer_request.response || 
            framebuffer_request.response->framebuffer_count < 1) {
            return nullptr;
        }
        auto* fb = framebuffer_request.response->framebuffers[0];
        // 延迟初始化最大行列数
        if (max_cols == 0) {
            max_cols = fb->width / FONT_WIDTH;
            max_rows = fb->height / FONT_HEIGHT;
        }
        return reinterpret_cast<volatile uint32_t*>(fb->address);
    }

} // anonymous namespace


void ostreamk::newline() {
    cursor_x = 0;
    cursor_y++;
    if (cursor_y >= max_rows) {
        this->scroll();
        cursor_y = max_rows - 1;
    }
}

void ostreamk::scroll() {
    volatile uint32_t* fb = getframebuffer();
    if (!fb) return;

    auto* fb_info = framebuffer_request.response->framebuffers[0];
    size_t row_stride = fb_info->pitch / sizeof(uint32_t);
    size_t line_height = FONT_HEIGHT;
    size_t pixels_to_move = (max_rows - 1) * line_height * row_stride;
    size_t pixels_in_one_line = line_height * row_stride;
    const volatile uint32_t* src = fb + pixels_in_one_line;
    volatile uint32_t* dst = fb;
    
    memcpy((void*)dst, (const void*)src, pixels_to_move);

    volatile uint32_t* last_line = fb + (max_rows - 1) * line_height * row_stride;
    for (size_t i = 0; i < pixels_in_one_line; ++i) {
        last_line[i] = this->__get_bg();
    }
}

void ostreamk::drawChar(char c, size_t x, size_t y) {
    volatile uint32_t* fb = getframebuffer();
    if (!fb) return;

    auto* fb_info = framebuffer_request.response->framebuffers[0];
    size_t row_stride = fb_info->pitch / sizeof(uint32_t);

    uint8_t glyph_index = (uint8_t)c;

    if (glyph_index >= (3904 / FONT_HEIGHT)) 
            glyph_index = '?'; // 越界字符显示为问号

    const uint8_t* glyph = &ascii_font[glyph_index * FONT_HEIGHT];

    size_t base_y = y * FONT_HEIGHT;
    size_t base_x = x * FONT_WIDTH;

    if (base_y + FONT_HEIGHT > fb_info->height || 
        base_x + FONT_WIDTH > fb_info->width) {
        return; 
    }

    for (size_t row = 0; row < FONT_HEIGHT; row++) {
        uint8_t bits = glyph[row];
        for (size_t col = 0; col < FONT_WIDTH; col++) {
            bool pixel_set = (bits >> (7 - col)) & 1;
            
            fb[(base_y + row) * row_stride + (base_x + col)] = 
                pixel_set ? this->__get_fg() : this->__get_bg();
        }
    }
}

void ostreamk::write(const uint8_t c) {
    if (c == '\n') {
        newline();
        return;
    }
    if (c == '\r') {
        cursor_x = 0;
        return;
    }
    if (c == '\t') {
        cursor_x = (cursor_x + 4) & ~(size_t)3;
        if (cursor_x >= max_cols) newline();
        return;
    }

    drawChar(c, cursor_x, cursor_y);
    cursor_x++;
    if (cursor_x >= max_cols) {
        newline();
    }
}

void ostreamk::write(const uint8_t* str) {
    if (!str) return;
    while (*str) {
        write(*str++);
    }
}

void ostreamk::write(const char c) {
    write(static_cast<const uint8_t>(c));
}

void ostreamk::write(const char* str) {
    write((const uint8_t*)(str));
}

void ostreamk::writeHex_uint32(uint32_t val) {
    const char hex_chars[] = "0123456789abcdef";
    *this << "0x";
    for (int i = 60; i >= 0; i -= 4) {
        this->write(hex_chars[(val >> i) & 0xF]);
    }
}

void ostreamk::writeHex_uint16(uint16_t val) {
    const char hex_chars[] = "0123456789abcdef";
    *this << "0x";
    for (int i = 30; i >= 0; i -= 4) {
        this->write(hex_chars[(val >> i) & 0xF]);
    }
}


namespace {
    // 通用无符号整数转字符串 (支持任意进制)
    template<typename T>
    void uint_to_str(T value, char* buf, int& len, int base = 10) {
        if (value == 0) {
            buf[0] = '0';
            len = 1;
            return;
        }
        char tmp[sizeof(T) * 8 + 1];
        int i = 0;
        while (value > 0) {
            uint8_t digit = value % base;
            tmp[i++] = digit < 10 ? ('0' + digit) : ('A' + digit - 10);
            value /= base;
        }
        // 反转
        len = i;
        for (int j = 0; j < i; j++) {
            buf[j] = tmp[i - 1 - j];
        }
    }

    void ptr_to_str(const void* p, char* buf, int& len) {
        buf[0] = '0'; buf[1] = 'x';
        uint64_t val = reinterpret_cast<uint64_t>(p);
        int num_len;
        uint_to_str(val, buf + 2, num_len, 16);
        // 补齐前导零至 16 位
        while (num_len < 16) {
            buf[2 + num_len] = '0';
            num_len++;
        }
        for (int i = 0; i < 16; i++) {
            uint8_t nibble = (val >> (60 - i * 4)) & 0xF;
            buf[2 + i] = nibble < 10 ? ('0' + nibble) : ('a' + nibble - 10);
        }
        len = 18;
    }
}


ostreamk::ostreamk() {
    fg    = HexToARGB(0x00FFFFFF);
    bg    = HexToARGB(0x00000000);
}

ostreamk::ostreamk(const ARGBColor_t frontground, const ARGBColor_t background)
    : fg(frontground), bg(background) {}

ostreamk::ostreamk(const uint32_t frontground, const uint32_t background)
    : fg(HexToARGB(frontground)), bg(HexToARGB(background)) {}

ostreamk& operator<<(ostreamk& os, const uint8_t v) {
    char buf[4]; int len;
    uint_to_str(v, buf, len);
    for (int i = 0; i < len; i++) os.write(buf[i]);
    return os;
}

ostreamk& operator<<(ostreamk& os, const uint16_t v) {
    char buf[6]; int len;
    uint_to_str(v, buf, len);
    for (int i = 0; i < len; i++) os.write(buf[i]);
    return os;
}

ostreamk& operator<<(ostreamk& os, const uint32_t v) {
    char buf[11]; int len;
    uint_to_str(v, buf, len);
    for (int i = 0; i < len; i++) os.write(buf[i]);
    return os;
}

ostreamk& operator<<(ostreamk& os, const uint64_t v) {
    char buf[21]; int len;
    uint_to_str(v, buf, len);
    for (int i = 0; i < len; i++) os.write(buf[i]);
    return os;
}

ostreamk& operator<<(ostreamk& os, const uint8_t* p) {
    char buf[20]; int len;
    ptr_to_str(p, buf, len);
    for (int i = 0; i < len; i++) os.write(buf[i]);
    return os;
}

ostreamk& operator<<(ostreamk& os, const uint16_t* p) {
    char buf[20]; int len;
    ptr_to_str(p, buf, len);
    for (int i = 0; i < len; i++) os.write(buf[i]);
    return os;
}

ostreamk& operator<<(ostreamk& os, const uint32_t* p) {
    char buf[20]; int len;
    ptr_to_str(p, buf, len);
    for (int i = 0; i < len; i++) os.write(buf[i]);
    return os;
}

ostreamk& operator<<(ostreamk& os, const uint64_t* p) {
    char buf[20]; int len;
    ptr_to_str(p, buf, len);
    for (int i = 0; i < len; i++) os.write(buf[i]);
    return os;
}

ostreamk& operator<<(ostreamk& os, const char c) {
    os.write(c);
    return os;
}

ostreamk& operator<<(ostreamk& os, const char* s) {
    os.write(s);
    return os;
}


ostreamk& ostreamk::__log_prefix() {
    // TODO: Print timestamp
    return *this;
}
}