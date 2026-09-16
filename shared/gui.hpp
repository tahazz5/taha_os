#pragma once
#include <stdint.h>
#include <stddef.h>
namespace gui {
constexpr size_t command_limit = 512;
enum Key { left_key=256, right_key, up_key, down_key, home_key, end_key,
    delete_key, page_up, page_down, switch_app, save_as, terminal_app, files_app, notes_app };
enum class Draw : uint32_t { rectangle, text };
struct Command {
    Draw kind;
    int32_t x, y, width, height;
    uint32_t color, background;
    char text[64];
};
static_assert(sizeof(Command)==92);
// Slots 1 and 2 are the desktop's two application windows. Slot 0 is the terminal.
struct Config { uint32_t slot; char title[32]; };
enum class Type : uint32_t { resize, key, click, close, open_document, input_lost };
struct Event {
    Type type;
    int32_t key, x, y, width, height;
    char path[96];
};
static_assert(sizeof(Event)==120);
}
