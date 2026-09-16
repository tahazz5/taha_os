#include "keyboard.hpp"
#include "arch.hpp"
#include "console.hpp"
#include "display.hpp"
namespace {
bool left_shift = false, right_shift = false, caps = false, extended = false, control = false, alt = false;
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
bool mouse_ready = false;
bool mouse_command(unsigned char c) {
    if (!command(0xd4) || !input_ready()) return false;
    arch::out(0x60,c);
    if (!output_ready()) return false;
    return arch::in(0x60)==0xfa;
}
void mouse_byte(unsigned char code) {
    static unsigned char packet[3]; static unsigned index=0;
    if (!mouse_ready) return;
    if (!index && !(code&8)) return;
    packet[index++]=code;
    if (index!=3) return;
    index=0;
    if (packet[0]&0xc0) { display::mouse(0,0,packet[0]&7); return; }
    int dx=int(packet[1])-((packet[0]&0x10) ? 256 : 0);
    int dy=int(packet[2])-((packet[0]&0x20) ? 256 : 0);
    display::mouse(dx,-dy,packet[0]&7);
}
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
    if (arch::in(0x60)!=0xfa) return false;
    if (command(0xa8) && mouse_command(0xf6) && mouse_command(0xf4)) {
        if (command(0x60) && input_ready()) {
            arch::out(0x60,(config|0x43)&~0x30);
            mouse_ready=true;
        }
    }
    if (mouse_ready) console::write("PS/2 mouse ready\n");
    return true;
}
void interrupt() {
    while (arch::in(0x64) & 1) {
        unsigned char status = arch::in(0x64), code = arch::in(0x60);
        if (status & 0x20) { mouse_byte(code); continue; }
        if (code == 0xe0 || code == 0xe1) { extended = true; continue; }
        if (extended) {
            extended=false;
            bool released=code&0x80; code&=0x7f;
            if (code==0x1d) { control=!released; continue; }
            if (code==0x38) { alt=!released; continue; }
            if (released) continue;
            int key=0;
            switch (code) {
            case 0x4b: key=display::left_key; break;
            case 0x4d: key=display::right_key; break;
            case 0x48: key=display::up_key; break;
            case 0x50: key=display::down_key; break;
            case 0x47: key=display::home_key; break;
            case 0x4f: key=display::end_key; break;
            case 0x53: key=display::delete_key; break;
            case 0x49: key=display::page_up; break;
            case 0x51: key=display::page_down; break;
            }
            if (key) display::key(key);
            continue;
        }
        bool released = code & 0x80; code &= 0x7f;
        if (code == 0x2a) { left_shift = !released; continue; }
        if (code == 0x36) { right_shift = !released; continue; }
        if (code == 0x1d) { control = !released; continue; }
        if (code==0x38) { alt=!released; continue; }
        if (released) continue;
        if (code>=0x3b && code<=0x3d) { display::key(display::terminal_app+code-0x3b); continue; }
        if (alt && code==0x0f) { display::key(display::switch_app); continue; }
        if (code == 0x3a) { caps = !caps; continue; }
        if (code >= sizeof(normal)) continue;
        bool shift = left_shift || right_shift;
        char c = shift ? shifted[code] : normal[code];
        if (caps && normal[code] >= 'a' && normal[code] <= 'z') c = shift ? normal[code] : shifted[code];
        if (control && (c=='s' || c=='S') && shift) { display::key(display::save_as); continue; }
        if (control && normal[code]>='a' && normal[code]<='z') c=normal[code]-'a'+1;
        if (c && !display::key(c)) console::input(c);
    }
}
}
