#include "keyboard.hpp"
#include "arch.hpp"
#include "console.hpp"
namespace {
bool left_shift = false, right_shift = false, caps = false, extended = false, control = false;
constexpr char normal[] = {
    0,27,'1','2','3','4','5','6','7','8','9','0','-','=',8,'\t',
    'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
    'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\',
    'z','x','c','v','b','n','m',',','.','/',0,'*',0,' '
};
constexpr char shifted[] = {
    0,27,'!','@','#','$','%','^','&','*','(',')','_','+',8,'\t',
    'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,
    'A','S','D','F','G','H','J','K','L',':','"','~',0,'|',
    'Z','X','C','V','B','N','M','<','>','?',0,'*',0,' '
};
bool input_ready() {
    for (unsigned i = 0; i < 100000; ++i) if (!(arch::in(0x64) & 2)) return true;
    return false;
}
bool output_ready() {
    for (unsigned i = 0; i < 100000; ++i) if (arch::in(0x64) & 1) return true;
    return false;
}
bool command(unsigned char c) { if (!input_ready()) return false; arch::out(0x64, c); return true; }
}
namespace keyboard {
bool initialize() {
    if (!command(0xad) || !command(0xa7)) return false;
    for (unsigned i = 0; i < 256 && (arch::in(0x64) & 1); ++i) arch::in(0x60);
    if (!command(0x20) || !output_ready()) return false;
    unsigned char config = arch::in(0x60);
    if (!command(0x60) || !input_ready()) return false;
    arch::out(0x60, (config | 0x41) & ~0x12); // Translation, IRQ1, clock enabled; no mouse IRQ.
    if (!command(0xae) || !input_ready()) return false;
    arch::out(0x60, 0xf4);
    if (!output_ready()) return false;
    return arch::in(0x60) == 0xfa;
}
void interrupt() {
    while (arch::in(0x64) & 1) {
        unsigned char status = arch::in(0x64), code = arch::in(0x60);
        if (status & 0x20) continue;
        if (code == 0xe0 || code == 0xe1) { extended = true; continue; }
        if (extended) { extended = false; continue; }
        bool released = code & 0x80; code &= 0x7f;
        if (code == 0x2a) { left_shift = !released; continue; }
        if (code == 0x36) { right_shift = !released; continue; }
        if (code == 0x1d) { control = !released; continue; }
        if (released) continue;
        if (code == 0x3a) { caps = !caps; continue; }
        if (code >= sizeof(normal)) continue;
        bool shift = left_shift || right_shift;
        char c = shift ? shifted[code] : normal[code];
        if (caps && normal[code] >= 'a' && normal[code] <= 'z') c = shift ? normal[code] : shifted[code];
        if (control && (c == 'c' || c == 'C')) c = 3;
        if (c) console::input(c);
    }
}
}
