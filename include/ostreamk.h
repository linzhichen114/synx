#pragma once

#include <stdint.h>

class ostreamk {
private:
    ;
public:
    ostreamk() = default;
    void write(const uint8_t c);
    void write(const uint8_t* str);
    void write(const char c);
    void write(const char* str);
    friend ostreamk& operator<<(ostreamk& os, const char*     s); 
    friend ostreamk& operator<<(ostreamk& os, const uint8_t   v);  
    friend ostreamk& operator<<(ostreamk& os, const uint16_t  v);  
    friend ostreamk& operator<<(ostreamk& os, const uint32_t  v);  
    friend ostreamk& operator<<(ostreamk& os, const uint64_t  v); 
    friend ostreamk& operator<<(ostreamk& os, const uint8_t*  p);
    friend ostreamk& operator<<(ostreamk& os, const uint16_t* p);
    friend ostreamk& operator<<(ostreamk& os, const uint32_t* p);
    friend ostreamk& operator<<(ostreamk& os, const uint64_t* p);
    friend ostreamk& operator<<(ostreamk& os, const void*     p);
    
};

//#define ostreamk_init extern ostreamk kout;
extern ostreamk kout;
constexpr const char* endl = "\n";