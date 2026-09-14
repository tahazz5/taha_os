#include "display.hpp"
#include "font.hpp"
#include <limine.h>
namespace {
volatile uint32_t* pixels = nullptr;
unsigned stride, width, height, red, green, blue;
unsigned origin_x, origin_y, columns, rows, column = 0, row = 0;
char cells[64][160];
unsigned escape = 0;
constexpr uint32_t background = 0x101923, text = 0xdce6f2, muted = 0x94a6ba, accent = 0x67dec8;
uint32_t color(uint32_t rgb) { return ((rgb >> 16) & 255) << red | ((rgb >> 8) & 255) << green | (rgb & 255) << blue; }
void rectangle(unsigned x, unsigned y, unsigned w, unsigned h, uint32_t rgb) {
    uint32_t value = color(rgb);
    for (unsigned j = y; j < y + h && j < height; ++j)
        for (unsigned i = x; i < x + w && i < width; ++i) pixels[j * stride + i] = value;
}
void glyph(unsigned x, unsigned y, unsigned char c, uint32_t foreground, uint32_t bg, unsigned scale = 1) {
    if (c >= 128) c = '?';
    for (unsigned j = 0; j < 16; ++j)
        for (unsigned i = 0; i < 8; ++i)
            rectangle(x + i * scale, y + j * scale, scale, scale, font[c][j] & (0x80 >> i) ? foreground : bg);
}
void label(unsigned x, unsigned y, const char* s, uint32_t foreground, uint32_t bg, unsigned scale = 1) {
    while (*s) { glyph(x, y, *s++, foreground, bg, scale); x += 8 * scale; }
}
void cell(unsigned x, unsigned y) { glyph(origin_x + x * 8, origin_y + y * 16, cells[y][x], text, background); }
void clear() {
    for (unsigned y = 0; y < rows; ++y) for (unsigned x = 0; x < columns; ++x) cells[y][x] = ' ';
    rectangle(origin_x, origin_y, columns * 8, rows * 16, background); column = row = 0;
}
void newline() {
    column = 0;
    if (++row < rows) return;
    row = rows - 1;
    for (unsigned y = 0; y < rows; ++y) for (unsigned x = 0; x < columns; ++x) {
        cells[y][x] = y + 1 < rows ? cells[y + 1][x] : ' '; cell(x, y);
    }
}
}
namespace display {
bool initialize(limine_framebuffer* fb) {
    if (!fb || !fb->address || fb->bpp != 32 || fb->memory_model != LIMINE_FRAMEBUFFER_RGB ||
        fb->width < 640 || fb->height < 480 || fb->width > 4096 || fb->height > 2160 ||
        fb->pitch < fb->width * 4 || fb->pitch % 4 || fb->pitch > 65536 ||
        fb->red_mask_size != 8 || fb->green_mask_size != 8 || fb->blue_mask_size != 8 ||
        fb->red_mask_shift > 24 || fb->green_mask_shift > 24 || fb->blue_mask_shift > 24) return false;
    pixels = static_cast<volatile uint32_t*>(fb->address);
    width = fb->width; height = fb->height; stride = fb->pitch / 4;
    red = fb->red_mask_shift; green = fb->green_mask_shift; blue = fb->blue_mask_shift;
    rectangle(0, 0, width, height, 0x0b111a);
    rectangle(0, 0, width, 60, 0x182431);
    rectangle(24, 19, 5, 25, accent);
    label(42, 16, "TahaOS", text, 0x182431, 2);
    label(160, 27, "0.4  /  PERSONAL SYSTEM", muted, 0x182431);
    unsigned panel = width >= 900 ? 200 : 0;
    if (panel) {
        label(24, 91, "VOTRE ESPACE", accent, 0x0b111a);
        label(24, 124, "/home  Fichiers", text, 0x0b111a);
        label(24, 148, "/bin   Programmes", text, 0x0b111a);
        label(24, 172, "/tmp   Temporaire", text, 0x0b111a);
        label(24, 238, "COMMANDES", accent, 0x0b111a);
        label(24, 271, "help   Aide", muted, 0x0b111a);
        label(24, 295, "apps   Applications", muted, 0x0b111a);
        label(24, 319, "edit   Ecrire", muted, 0x0b111a);
        label(24, 343, "ps     Processus", muted, 0x0b111a);
        label(24, 367, "sync   Sauvegarder", muted, 0x0b111a);
    }
    unsigned x = panel + 20;
    rectangle(x, 80, width - x - 20, height - 128, background);
    rectangle(x, 80, width - x - 20, 32, 0x213041);
    rectangle(x + 12, 92, 8, 8, accent);
    label(x + 32, 88, "Terminal  /  user@taha", text, 0x213041);
    origin_x = x + 12; origin_y = 124;
    columns = (width - origin_x - 32) / 8; if (columns > 160) columns = 160;
    rows = (height - origin_y - 60) / 16; if (rows > 64) rows = 64;
    label(24, height - 28, "Clavier US  |  help pour commencer  |  /home pour vos fichiers", muted, 0x0b111a);
    clear(); return true;
}
void putc(char c) {
    if (!pixels) return;
    if (escape) {
        if (escape == 1 && c == '[') { escape = 2; return; }
        if (escape == 2 && c >= '0' && c <= '9') return;
        if (escape == 2 && (c == 'J' || c == 'H')) clear();
        escape = 0; return;
    }
    if (c == 27) { escape = 1; return; }
    cell(column, row); // Remove the old cursor.
    if (c == '\r') column = 0;
    else if (c == '\n') newline();
    else if (c == '\b') { if (column) --column; }
    else if (c >= 32 && c <= 126) {
        cells[row][column] = c; cell(column, row);
        if (++column == columns) newline();
    }
    rectangle(origin_x + column * 8, origin_y + row * 16 + 14, 8, 2, accent);
}
}
