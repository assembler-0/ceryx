#include <lib/linearfb.hpp>
#include <lib/embedded_font.hpp>
#include <stdint.h>
#include <FoundationKitMemory/Core/MemoryOperations.hpp>
#include <arch/x86_64/boot/limine.hpp>
#include <arch/x86_64/boot/requests.hpp>

static int fb_initialized = 0;
static struct limine_framebuffer *fb = nullptr;
static linearfb_mode_t fb_mode = FB_MODE_CONSOLE;
static linearfb_font_t fb_font = {0};
static uint32_t font_glyph_count = 0;
static uint32_t font_glyph_w = 0, font_glyph_h = 0;

// --- Console state ---
static uint32_t console_col = 0, console_row = 0;
static uint32_t console_cols = 0, console_rows = 0;

static uint32_t console_bg = 0x00000000;

int linearfb_probe(void) {
    return fb_initialized;
}

int linearfb_console_init() {
    linearfb_init(get_framebuffer_request());

    linearfb_font_t font = {
        .width = 8, .height = 16, .data = reinterpret_cast<uint8_t *>(console_font)};
    linearfb_load_font(&font, 256);
    linearfb_set_mode(FB_MODE_CONSOLE);
    linearfb_console_clear(0x00000000);
    linearfb_console_set_cursor(0, 0);

    return fb ? 0 : -1;
}

void linearfb_console_set_cursor(uint32_t col, uint32_t row) {
    if (col < console_cols) __atomic_store_n(&console_col, col, __ATOMIC_SEQ_CST);
    if (row < console_rows) __atomic_store_n(&console_row, row, __ATOMIC_SEQ_CST);
}

void linearfb_console_get_cursor(uint32_t *col, uint32_t *row) {
    if (col) *col = console_col;
    if (row) *row = console_row;
}

static void putpixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!fb || x >= fb->width || y >= fb->height) return;
    uint8_t *p = static_cast<uint8_t *>(fb->address) + y * fb->pitch + x * (fb->bpp / 8);
    FoundationKitMemory::MemoryCopy(p, &color, fb->bpp / 8);
}

void linearfb_console_clear(uint32_t color) {
    if (!fb) return;
    for (uint32_t y = 0; y < fb->height; ++y) {
        for (uint32_t x = 0; x < fb->width; ++x) {
            putpixel(x, y, color);
        }
    }
    console_col = 0;
    console_row = 0;
    console_bg = color;
}

static void console_scroll(void) {
    if (!fb || !fb_font.data) return;
    uint32_t row_bytes = fb->pitch * font_glyph_h;
    uint8_t *dst = (uint8_t*)fb->address;
    uint8_t *src = dst + row_bytes;
    FoundationKitMemory::MemoryMove(dst, src, row_bytes * (console_rows - 1));
    // Clear last row
    for (uint32_t y = (console_rows - 1) * font_glyph_h; y < fb->height; ++y) {
        for (uint32_t x = 0; x < fb->width; ++x) {
            putpixel(x, y, console_bg);
        }
    }
    console_row = console_rows - 1;
}

void linearfb_console_putc(char c) {
    if (fb_mode != FB_MODE_CONSOLE || !fb_font.data) return;
    if (c == '\n') {
        console_col = 0;
        if (++console_row >= console_rows) console_scroll();
        return;
    } if (c == '\r') {
        console_col = 0;
        return;
    } if (c == '\b') {
        if (console_col > 0) --console_col;
        return;
    }
    // Draw glyph
    uint8_t ch = (uint8_t)c;
    if (ch >= font_glyph_count) ch = '?';
    const uint8_t *glyph = fb_font.data + ch * font_glyph_h;
    uint32_t px = console_col * font_glyph_w;
    uint32_t py = console_row * font_glyph_h;
    // Draw background
    for (uint32_t row = 0; row < font_glyph_h; ++row) {
        for (uint32_t col = 0; col < font_glyph_w; ++col) {
            putpixel(px + col, py + row, console_bg);
        }
    }
    // Draw glyph
    for (uint32_t row = 0; row < font_glyph_h; ++row) {
        uint8_t bits = glyph[row];
        for (uint32_t col = 0; col < font_glyph_w; ++col) {
            if (bits & (1 << (7 - col)))
                putpixel(px + col, py + row, 0xFFFFFFFF);
        }
    }
    if (++console_col >= console_cols) {
        console_col = 0;
        if (++console_row >= console_rows) console_scroll();
    }
}

void linearfb_console_puts(const char *s) {
    while (*s) linearfb_console_putc(*s++);
}

#define abs(x) ((x) >= 0 ? (x) : -(x))

int linearfb_init(volatile limine_framebuffer_request *fb_req) {
    if (!fb_req || !fb_req->response || fb_req->response->framebuffer_count == 0)
        return -1;
    fb = fb_req->response->framebuffers[0];
    // Update console cols/rows if font is ready
    if (fb && font_glyph_w && font_glyph_h) {
        console_cols = fb->width / font_glyph_w;
        console_rows = fb->height / font_glyph_h;
    }
    fb_initialized = 1;
    return 0;
}

void linearfb_set_mode(const linearfb_mode_t mode) {
    __atomic_store_n(&fb_mode, mode, __ATOMIC_SEQ_CST);
}

int linearfb_load_font(const linearfb_font_t* font, const uint32_t count) {
    if (!font)
        return -1;
    fb_font = *font;
    font_glyph_w = font->width;
    font_glyph_h = font->height;
    font_glyph_count = count;
    // Update console cols/rows if fb is ready
    if (fb && font_glyph_w && font_glyph_h) {
        console_cols = fb->width / font_glyph_w;
        console_rows = fb->height / font_glyph_h;
    }
    return 0;
}