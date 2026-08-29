#pragma once
#include <stdint.h>

namespace ps2 {

extern const uint8_t DATA_PORT;
extern const uint8_t CMD_PORT;
extern const uint8_t STATUS_PORT;
extern const uint8_t STATUS_OUTPUT_FULL;
extern const uint8_t STATUS_INPUT_FULL;
extern const uint8_t CMD_DISABLE_P2;
extern const uint8_t CMD_ENABLE_P1;
extern const uint8_t CMD_DISABLE_P1;
extern const uint8_t CMD_READ_CFG;
extern const uint8_t CMD_WRITE_CFG;
extern const uint8_t KBD_CMD_SET_LED;
extern const uint8_t KBD_CMD_ACK;
extern const uint8_t KBD_CMD_RESEND;
extern const uint8_t KBD_CMD_SET_SCANCODE;

constexpr uint8_t PS2_KEYBOARD_VECTOR = 33;

struct KeyEvent {
    uint16_t scancode;
    char     ascii;
    bool     pressed;
    bool     shift;
    bool     ctrl;
    bool     alt;
    bool     caps_lock;
};

bool init();
bool poll(KeyEvent& event);
void decode_and_push(uint8_t raw);

} // namespace ps2