#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
extern "C" {
#include <limine.h>
}
#include "ostreamk.h"
#include "sysdef.h"




// Set the base revision to 6, this is recommended as this is the latest
// base revision described by the Limine boot protocol specification.
// See specification for further info.

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

// The Limine requests can be placed anywhere, but it is important that
// the compiler does not optimise them away, so, usually, they should
// be made volatile or equivalent, _and_ they should be accessed at least
// once or marked as used with the "used" attribute as done here.

__attribute__((used, section(".limine_requests")))
volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

// Finally, define the start and end markers for the Limine requests.
// These can also be moved anywhere, to any .cpp file, as seen fit.

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

// Halt and catch fire function.
static void hcf(void) {
    for (;;) {
        asm ("hlt");
    }
}

// The following will be our kernel's entry point.
// If renaming kernel_main() to something else, make sure to change the
// linker script accordingly.
extern "C" void kernel_main(void) {

    // Ensure the bootloader actually understands our base revision (see spec).
    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false) {
        hcf();
    }

    // Ensure we got a framebuffer.
    if (framebuffer_request.response == NULL
     || framebuffer_request.response->framebuffer_count < 1) {
        hcf();
    }

    /*// Fetch the first framebuffer.
    struct limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[0];

    // Print a nice pattern to screen as an example.
    // Note: we assume the framebuffer model is RGB with 32-bit pixels.
    volatile uint32_t *fb_ptr = (volatile uint32_t *)framebuffer->address;
    for (size_t y = 0; y < framebuffer->height; y++) {
        for (size_t x = 0; x < framebuffer->width; x++) {
            uint32_t nX = x * 255 / framebuffer->width;
            uint32_t nY = y * 255 / framebuffer->height;
            fb_ptr[y * (framebuffer->pitch / 4) + x] = (nY << 8) | nX;
        }
    }*/
    ostreamk kout;
    kout << KERNEL_NAME << " version " << KERNEL_VERSION << " (" << COMPILER_NAME << " " << COMPILER_VERSION << ") SMP " << BUILD_DATE << " " << BUILD_TIME << endl;

    const auto fb0 = framebuffer_request.response->framebuffers[0];
    kout << "fb0: Base " << (uint64_t*)fb0->address << ", Size " << (fb0->width * fb0->height * fb0->bpp) / (uint64_t)(8 * 1024) << endl;
    kout << "fb0: Mode " << fb0->width << "x" << fb0->height << " @ " << fb0->bpp << "bpp" << endl;
    kout << "fb0: Color mode: ARGB" << endl;
    const ARGBColor_t fg_col = HexToARGB(FG_COLOR);
    const ARGBColor_t bg_col = HexToARGB(BG_COLOR);
    (kout << "fb0: Frontground color: ARGB [ A: " << fg_col.a << " R:" << fg_col.r << " G:" << fg_col.g<< " B:" << fg_col.b << " ], HEX ").writeHex_uint32(FG_COLOR);
    kout << endl;
    (kout << "fb0: Background color: ARGB [ A: " << bg_col.a << " R:" << bg_col.r << " G:" << bg_col.g<< " B:" << bg_col.b << " ], HEX ").writeHex_uint32(BG_COLOR);
    kout << endl;
    kout << "fbcon: fb0 is primary device." << endl;
    kout << "fbcon: Screen grid: " << FONT_WIDTH << "x" << FONT_HEIGHT << " characters.\n" << endl;

    kernel_panic("kernel_main: next function is not implemented yet - system halting.");
}