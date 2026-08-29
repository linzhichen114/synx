#include "ps2_keyboard.h"
#include "sysdef.h"
#include "kprint.h"
#include <stddef.h>

namespace ps2 {

const uint8_t DATA_PORT            = 0x60;
const uint8_t CMD_PORT             = 0x64;
const uint8_t STATUS_PORT          = 0x64;
const uint8_t STATUS_OUTPUT_FULL   = 0x01;
const uint8_t STATUS_INPUT_FULL    = 0x02;
const uint8_t CMD_DISABLE_P2       = 0xA7;
const uint8_t CMD_ENABLE_P1        = 0xAE;
const uint8_t CMD_DISABLE_P1       = 0xAD;
const uint8_t CMD_READ_CFG         = 0x20;
const uint8_t CMD_WRITE_CFG        = 0x60;
const uint8_t KBD_CMD_SET_LED      = 0xED;
const uint8_t KBD_CMD_ACK          = 0xFA;
const uint8_t KBD_CMD_RESEND       = 0xFE;
const uint8_t KBD_CMD_SET_SCANCODE = 0xF0;

static bool s_shift     = false;
static bool s_ctrl      = false;
static bool s_alt       = false;
static bool s_caps_lock = false;
static bool s_e0_prefix = false;

static constexpr size_t KEY_BUF_SIZE = 64;
static KeyEvent s_key_buf[KEY_BUF_SIZE];
static volatile size_t s_head = 0;
static volatile size_t s_tail = 0;

static inline void wait_input_clear() {
    while (inb(STATUS_PORT) & STATUS_INPUT_FULL);
}

static inline void wait_output_full() {
    while (!(inb(STATUS_PORT) & STATUS_OUTPUT_FULL));
}

static uint8_t read_data() {
    wait_output_full();
    return inb(DATA_PORT);
}

static void write_cmd(uint8_t cmd) {
    wait_input_clear();
    outb(CMD_PORT, cmd);
}

static void write_data(uint8_t data) {
    wait_input_clear();
    outb(DATA_PORT, data);
}

static bool kbd_send_cmd(uint8_t cmd) {
    int retries = 3;
    while (retries--) {
        write_data(cmd);
        wait_output_full();
        uint8_t resp = inb(DATA_PORT);
        if (resp == KBD_CMD_ACK) return true;
        if (resp != KBD_CMD_RESEND) break;
    }
    return false;
}

static const char sc2_ascii[] = {
    /* 0x00 */ 0,   0,   0,   0,   0,   0,   0,   0,
    /* 0x08 */ 0,   0,   0,   0,   0, '\t', '`', 0,
    /* 0x10 */ 0,   0,   0,   0,   0, 'q', '1', 0,
    /* 0x18 */ 0,   0,  'z', 's', 'a', 'w', '2', 0,
    /* 0x20 */ 0,  'c', 'x', 'd', 'e', '4', '3', 0,
    /* 0x28 */ 0, ' ', 'v', 'f', 't', 'r', '5', 0,
    /* 0x30 */ 0,  'n', 'b', 'h', 'g', 'y', '6', 0,
    /* 0x38 */ 0,   0,  'm', 'j', 'u', '7', '8', 0,
    /* 0x40 */ 0,  ',', 'k', 'i', 'o', '0', '9', 0,
    /* 0x48 */ 0,  '.', '/', 'l', ';', 'p', '-', 0,
    /* 0x50 */ 0,   0, '\'', 0,  '[', '=', 0,   0,
    /* 0x58 */ 0,   0, '\n', ']', 0, '\\', 0,   0,
    /* 0x60 */ 0,   0,   0,   0,   0,   0, '\b', 0,
    /* 0x68 */ 0,   0,   0,   0,   0,   0,   0,   0,
    /* 0x70 */ '0', '.', '2', '5', '6', '8', 0,   0,
    /* 0x78 */ 0,  '+', '3', '-', '*', '9', 0,   0,
};

static char translate(uint8_t scancode, bool shift, bool caps) {
    if (scancode >= sizeof(sc2_ascii)) return 0;
    char c = sc2_ascii[scancode];
    if (c == 0) return 0;

    if (c >= 'a' && c <= 'z') {
        if (shift ^ caps) c -= 32;
        return c;
    }

    if (shift) {
        switch (c) {
            case '1': return '!'; case '2': return '@';
            case '3': return '#'; case '4': return '$';
            case '5': return '%'; case '6': return '^';
            case '7': return '&'; case '8': return '*';
            case '9': return '('; case '0': return ')';
            case '-': return '_'; case '=': return '+';
            case '[': return '{'; case ']': return '}';
            case '\\':return '|'; case ';': return ':';
            case '\'':return '"'; case ',': return '<';
            case '.': return '>'; case '/': return '?';
            case '`': return '~';
        }
    }
    return c;
}

static void push_event(const KeyEvent& ev) {
    size_t next = (s_head + 1) % KEY_BUF_SIZE;
    if (next != s_tail) { // Drop if full
        s_key_buf[s_head] = ev;
        s_head = next;
    }
}

bool poll(KeyEvent& event) {
    if (s_head == s_tail) return false;
    event = s_key_buf[s_tail];
    s_tail = (s_tail + 1) % KEY_BUF_SIZE;
    return true;
}


static void process_scancode(uint8_t code) {
    if (code == 0xE0) {
        s_e0_prefix = true;
        return;
    }

    if (code == 0xF0) {
        static bool f0_pending = false;
        f0_pending = true;
        return; // Wait for next byte
    }

    __builtin_unreachable();
}

enum class DecoderState : uint8_t {
    NORMAL,
    GOT_E0,
    GOT_F0,
    GOT_E0_F0,
};

static DecoderState s_state = DecoderState::NORMAL;

void decode_and_push(uint8_t raw) {
    bool pressed = true;
    uint16_t scancode = raw;

    switch (s_state) {
        case DecoderState::NORMAL:
            if (raw == 0xE0) { s_state = DecoderState::GOT_E0; return; }
            if (raw == 0xF0) { s_state = DecoderState::GOT_F0; return; }
            break;

        case DecoderState::GOT_E0:
            if (raw == 0xF0) { s_state = DecoderState::GOT_E0_F0; return; }
            // Extended make: E0 xx
            scancode = 0xE000 | raw;
            s_state = DecoderState::NORMAL;
            break;

        case DecoderState::GOT_F0:
            pressed = false;
            scancode = raw;
            s_state = DecoderState::NORMAL;
            break;

        case DecoderState::GOT_E0_F0:
            pressed = false;
            scancode = 0xE000 | raw;
            s_state = DecoderState::NORMAL;
            break;
    }

    uint8_t base_sc = (uint8_t)(scancode & 0xFF);
    bool is_extended = (scancode & 0xE000) != 0;

    if (!is_extended) {
        switch (base_sc) {
            case 0x12: s_shift = pressed; break;
            case 0x59: s_shift = pressed; break;
            case 0x14: s_ctrl  = pressed; break;
            case 0x11: s_alt   = pressed; break;
            case 0x58:
                if (pressed) s_caps_lock = !s_caps_lock;
                break;
        }
    } else {
        switch (base_sc) {
            case 0x11: s_alt  = pressed; break;
            case 0x14: s_ctrl = pressed; break;
        }
    }

    KeyEvent ev{};
    ev.scancode  = scancode;
    ev.pressed   = pressed;
    ev.shift     = s_shift;
    ev.ctrl      = s_ctrl;
    ev.alt       = s_alt;
    ev.caps_lock = s_caps_lock;
    ev.ascii     = pressed ? translate(base_sc, s_shift, s_caps_lock) : 0;

    push_event(ev);
}


void irq_handler() {
    uint8_t status = inb(STATUS_PORT);
    if (!(status & STATUS_OUTPUT_FULL)) return;

    uint8_t data = inb(DATA_PORT);
    decode_and_push(data);
}


bool init() {
    write_cmd(CMD_DISABLE_P1);
    write_cmd(CMD_DISABLE_P2);

    while (inb(STATUS_PORT) & STATUS_OUTPUT_FULL)
        inb(DATA_PORT);

    write_cmd(CMD_READ_CFG);
    uint8_t cfg = read_data();
    cfg |= 0x01;
    cfg &= ~0x40;
    write_cmd(CMD_WRITE_CFG);
    write_data(cfg);

    write_cmd(CMD_ENABLE_P1);

    if (!kbd_send_cmd(KBD_CMD_SET_SCANCODE)) {
        kout << "ps2: Failed to send set scancode cmd\n";
        return false;
    }
    write_data(0x02);
    wait_output_full();
    if (inb(DATA_PORT) != KBD_CMD_ACK) {
        kout << "ps2: Failed to set scan code set 2\n";
        return false;
    }

    write_data(0xFF);
    wait_output_full();
    uint8_t bat = inb(DATA_PORT);
    if (bat != 0xAA) {
        kout << "ps2: BAT failed: " << bat << "\n";
        return false;
    }

    kout << "ps2: Keyboard initialized with Scan Code Set 2.\n";
    return true;
}

} // namespace ps2