#include "display.hpp"
#include "font.hpp"
#include "process.hpp"
#include "strings.hpp"
#include "console.hpp"
#include "arch.hpp"
#include "../shared/abi.hpp"
#include <limine.h>
namespace {
volatile uint32_t* pixels=nullptr;
unsigned stride,width,height,red,green,blue;
uint32_t canvas[1920*1080],previous[1920*1080];
bool presented=false,dirty=true;
uint64_t last_render=0;
constexpr uint32_t background=0x101923,text=0xdce6f2,muted=0x94a6ba,accent=0x67dec8;
struct Window { int x,y,w,h; bool visible; };
Window windows[3],restored[3];
bool maximized[3]{};
struct Application {
    uint64_t owner=0;
    bool initialized=false,resize=false,closing=false,overflow=false;
    char title[32]{};
    gui::Command commands[gui::command_limit]; size_t count=0;
    gui::Event events[64]; unsigned head=0,tail=0;
};
Application apps[3];
int order[3]={0,1,2},focus=0,mx=40,my=70,dragging=-1,grab_x,grab_y;
bool left=false;
unsigned columns,rows,column,row,escape;
char cells[64][160];
struct Input { int dx,dy; unsigned buttons; int key; };
Input inputs[256]; unsigned input_head=0,input_tail=0;
bool input_overflow=false;
const char* names[3]={"Terminal","Files","Notes"};
const char* launch_error[3]{};
int clamp(int n,int lo,int hi) { return n<lo ? lo : n>hi ? hi : n; }
uint32_t color(uint32_t rgb) { return ((rgb>>16)&255)<<red | ((rgb>>8)&255)<<green | (rgb&255)<<blue; }
void rectangle(int x,int y,int w,int h,uint32_t rgb) {
    uint32_t value=color(rgb);
    int x0=clamp(x,0,width),x1=clamp(x+w,0,width),y0=clamp(y,0,height),y1=clamp(y+h,0,height);
    for (int j=y0;j<y1;++j) for (int i=x0;i<x1;++i) canvas[j*width+i]=value;
}
void glyph(int x,int y,unsigned char c,uint32_t fg,uint32_t bg) {
    if (c>=128) c='?';
    if (x<0 || y<0 || x+8>int(width) || y+16>int(height)) return;
    fg=color(fg); bg=color(bg);
    for (unsigned j=0;j<16;++j) for (unsigned i=0;i<8;++i)
        canvas[(y+j)*width+x+i]=(font[c][j]&(0x80>>i)) ? fg : bg;
}
void label(int x,int y,const char* s,uint32_t fg,uint32_t bg,unsigned max=160) {
    while (*s && max--) { glyph(x,y,*s++,fg,bg); x+=8; }
}
int owned(uint64_t pid) { for (int i=1;i<3;++i) if (apps[i].owner==pid) return i; return -1; }
void focus_window(int id) {
    for (int i=0;i<3;++i) if (order[i]==id) {
        for (int j=i;j<2;++j) order[j]=order[j+1];
        break;
    }
    order[2]=id; focus=id; windows[id].visible=true; dirty=true;
}
void hide(int id) {
    windows[id].visible=false; focus=-1;
    for (int j=2;j>=0;--j) if (windows[order[j]].visible) { focus=order[j]; break; }
    dirty=true;
}
void queue(int id,const gui::Event& event) {
    if (id<1 || id>2 || !apps[id].owner) return;
    auto& app=apps[id]; unsigned next=(app.head+1)%64;
    if (next==app.tail) { app.overflow=true; return; }
    app.events[app.head]=event; app.head=next;
}
void draw_application(int id) {
    const auto& w=windows[id]; const auto& app=apps[id];
    if (!app.initialized) {
        label(w.x+12,w.y+48,launch_error[id] ? launch_error[id] : "Starting application...",muted,background);
        return;
    }
    for (size_t n=0;n<app.count;++n) {
        const auto& cmd=app.commands[n];
        if (cmd.kind==gui::Draw::rectangle) {
            int x0=clamp(cmd.x,0,w.w),x1=clamp(cmd.x+cmd.width,0,w.w);
            int y0=clamp(cmd.y,0,w.h-30),y1=clamp(cmd.y+cmd.height,0,w.h-30);
            rectangle(w.x+x0,w.y+30+y0,x1-x0,y1-y0,cmd.color);
        } else {
            // Clip each glyph to the client area, including partial glyphs.
            uint32_t fg=color(cmd.color),bg=color(cmd.background);
            for (unsigned k=0;k<64 && cmd.text[k];++k) {
                unsigned char c=cmd.text[k]; int gx=cmd.x+k*8;
                for (int j=0;j<16;++j) for (int i=0;i<8;++i) {
                    int x=gx+i,y=cmd.y+j;
                    if (x>=0 && x<w.w && y>=0 && y<w.h-30)
                        canvas[(w.y+30+y)*width+w.x+x]=(font[c][j]&(0x80>>i)) ? fg : bg;
                }
            }
        }
    }
}
void render_window(int id) {
    const auto& w=windows[id]; if (!w.visible) return;
    rectangle(w.x+5,w.y+5,w.w,w.h,0x080e16); rectangle(w.x,w.y,w.w,w.h,background);
    uint32_t bar=focus==id ? 0x285260 : 0x213041;
    rectangle(w.x,w.y,w.w,30,bar);
    label(w.x+12,w.y+7,id && apps[id].initialized ? apps[id].title : names[id],text,bar,32);
    rectangle(w.x+w.w-94,w.y+3,28,24,0x213746);
    rectangle(w.x+w.w-62,w.y+3,28,24,0x213746);
    rectangle(w.x+w.w-30,w.y+3,28,24,0x513c48);
    label(w.x+w.w-88,w.y+7,"_",text,0x213746);
    label(w.x+w.w-56,w.y+7,maximized[id] ? "o" : "+",text,0x213746);
    label(w.x+w.w-24,w.y+7,"x",text,0x513c48);
    if (!id) {
        for (unsigned y=0;y<rows;++y) for (unsigned x=0;x<columns;++x)
            glyph(w.x+12+x*8,w.y+40+y*16,cells[y][x],text,background);
        if (focus==0) rectangle(w.x+12+column*8,w.y+40+row*16+14,8,2,accent);
    } else draw_application(id);
}
void render() {
    if (!pixels || !dirty) return;
    for (unsigned y=44;y<height;++y) rectangle(0,y,width,1,0x0b1825+(y*14/height)*0x010101);
    label(28,60,"YOUR WORKSPACE",muted,0x0d1a27);
    label(28,84,"F1 Terminal   F2 Files   F3 Notes",muted,0x0d1a27);
    rectangle(0,0,width,44,0x182431);
    label(20,14,"TahaOS / Desktop",text,0x182431);
    label(width-208,14,"Alt+Tab: switch apps",muted,0x182431);
    for (int id:order) render_window(id);
    rectangle(0,height-40,width,40,0x182431);
    for (int i=0;i<3;++i) {
        uint32_t bg=focus==i && windows[i].visible ? 0x285260 : 0x213041;
        rectangle(12+i*140,height-34,132,28,bg); label(24+i*140,height-28,names[i],text,bg);
        if (i && apps[i].owner) rectangle(24+i*140,height-6,24,2,accent);
    }
    for (int y=0;y<16;++y) for (int x=0;x<=y/2;++x)
        rectangle(mx+x,my+y,1,1,(x==0 || x==y/2 || y==15) ? 0x071019 : 0xffffff);
    for (unsigned y=0;y<height;++y) for (unsigned x=0;x<width;++x) {
        unsigned i=y*width+x;
        if (!presented || canvas[i]!=previous[i]) { pixels[y*stride+x]=canvas[i]; previous[i]=canvas[i]; }
    }
    presented=true; dirty=false; last_render=arch::ticks();
}
void clear() { for (auto& line:cells) for (char& c:line) c=' '; column=row=0; dirty=true; }
void newline() {
    column=0; if (++row<rows) return; row=rows-1;
    for (unsigned y=0;y<rows;++y) for (unsigned x=0;x<columns;++x) cells[y][x]=y+1<rows ? cells[y+1][x] : ' ';
}
void toggle_maximize(int id) {
    auto& w=windows[id];
    if (maximized[id]) w=restored[id];
    else { restored[id]=w; w.x=8; w.y=52; w.w=width-16; w.h=height-100; }
    maximized[id]=!maximized[id]; if (id) apps[id].resize=true; dirty=true;
}
void click() {
    if (my>=int(height)-40) {
        for (int i=0;i<3;++i) if (mx>=12+i*140 && mx<144+i*140) {
            if (i) display::launch(i); else focus_window(0); return;
        }
        return;
    }
    for (int z=2;z>=0;--z) {
        int id=order[z]; auto& w=windows[id];
        if (!w.visible || mx<w.x || mx>=w.x+w.w || my<w.y || my>=w.y+w.h) continue;
        focus_window(id);
        if (my<w.y+30) {
            if (mx>=w.x+w.w-32) { if (id && apps[id].owner) apps[id].closing=true; else hide(id); }
            else if (mx>=w.x+w.w-64) toggle_maximize(id);
            else if (mx>=w.x+w.w-96) hide(id);
            else if (!maximized[id]) { dragging=id; grab_x=mx-w.x; grab_y=my-w.y; }
        } else if (id) { gui::Event event{}; event.type=gui::Type::click; event.x=mx-w.x; event.y=my-w.y-30; queue(id,event); }
        return;
    }
}
bool ascii(const char* s,size_t size) {
    for (size_t i=0;i<size;++i) { if (!s[i]) return true; if (s[i]<32 || s[i]>126) return false; }
    return false;
}
}
namespace display {
bool initialize(limine_framebuffer* fb) {
    if (!fb || !fb->address || fb->bpp!=32 || fb->memory_model!=LIMINE_FRAMEBUFFER_RGB ||
        fb->width<640 || fb->height<480 || fb->width>1920 || fb->height>1080 ||
        fb->pitch<fb->width*4 || fb->pitch%4 || fb->pitch>65536 ||
        fb->red_mask_size!=8 || fb->green_mask_size!=8 || fb->blue_mask_size!=8 ||
        fb->red_mask_shift>24 || fb->green_mask_shift>24 || fb->blue_mask_shift>24) return false;
    pixels=static_cast<volatile uint32_t*>(fb->address); width=fb->width; height=fb->height; stride=fb->pitch/4;
    red=fb->red_mask_shift; green=fb->green_mask_shift; blue=fb->blue_mask_shift;
    int tw=width-64; if (tw>1000) tw=1000; int th=height-120; if (th>720) th=720;
    windows[0]={24,60,tw,th,true}; windows[1]={64,84,480,int(height)-160,false}; windows[2]={96,108,480,int(height)-180,false};
    columns=(tw-24)/8; rows=(th-52)/16; clear(); render(); return true;
}
void putc(char c) {
    if (!pixels) return; dirty=true;
    if (escape) {
        if (escape==1 && c=='[') { escape=2; return; }
        if (escape==2 && c>='0' && c<='9') return;
        if (escape==2 && (c=='J' || c=='H')) clear(); escape=0; return;
    }
    if (c==27) { escape=1; return; }
    if (c=='\r') column=0;
    else if (c=='\n') newline();
    else if (c=='\b') { if (column) --column; }
    else if (c>=32 && c<=126) { cells[row][column]=c; if (++column==columns) newline(); }
}
void ready() {
    if (!pixels) return; clear();
    const char* s="Welcome to TahaOS\n\nF1 Terminal | F2 Files | F3 Notes | Alt+Tab switch windows\nNotes: Ctrl+S save, Ctrl+O open, Ctrl+N new document\nFiles and Notes run as separate protected processes.\nType help below for shell commands.\n";
    while (*s) putc(*s++);
}
int64_t open(uint64_t pid,const gui::Config& config) {
    if (!pixels) return abi::missing;
    if (config.slot<1 || config.slot>2 || !config.title[0] || !ascii(config.title,sizeof(config.title))) return abi::invalid;
    int current=owned(pid); auto& app=apps[config.slot];
    if ((current>=0 && (current!=int(config.slot) || app.initialized)) || (app.owner && app.owner!=pid)) return abi::full;
    app.owner=pid; app.initialized=true; app.resize=true;
    strings::copy(app.title,config.title,sizeof(app.title)); focus_window(config.slot); return config.slot;
}
int64_t present(uint64_t pid,const gui::Command* commands,size_t count) {
    int id=owned(pid); if (id<0 || !apps[id].initialized) return abi::denied;
    if (count>gui::command_limit) return abi::invalid;
    for (size_t i=0;i<count;++i) {
        const auto& c=commands[i];
        if (c.x < -4096 || c.x>4096 || c.y < -4096 || c.y>4096) return abi::invalid;
        if (c.kind==gui::Draw::rectangle) {
            if (c.width<0 || c.width>1920 || c.height<0 || c.height>1080) return abi::invalid;
        } else if (c.kind!=gui::Draw::text || !ascii(c.text,sizeof(c.text))) return abi::invalid;
    }
    auto& app=apps[id]; for (size_t i=0;i<count;++i) app.commands[i]=commands[i];
    app.count=count; dirty=true; return 0;
}
int64_t event(uint64_t pid,gui::Event& result) {
    int id=owned(pid); if (id<0 || !apps[id].initialized) return abi::denied;
    auto& app=apps[id]; result={};
    if (app.resize) { app.resize=false; result.type=gui::Type::resize; result.width=windows[id].w; result.height=windows[id].h-30; }
    else if (app.closing) { app.closing=false; result.type=gui::Type::close; }
    else if (app.overflow) { app.overflow=false; app.tail=app.head; result.type=gui::Type::input_lost; }
    else if (app.tail!=app.head) { result=app.events[app.tail]; app.tail=(app.tail+1)%64; }
    else return abi::again;
    return 0;
}
void release(uint64_t pid) {
    int id=owned(pid); if (id<0) return;
    auto& app=apps[id]; app.owner=0; app.initialized=app.resize=app.closing=app.overflow=false;
    app.count=app.head=app.tail=0; hide(id); if (dragging==id) dragging=-1;
}
int64_t launch(unsigned slot,const char* path) {
    if (!pixels) return abi::missing;
    if (slot<1 || slot>2 || strings::length(path)>=abi::path_max) return abi::invalid;
    auto& app=apps[slot];
    if (!app.owner) {
        int64_t pid=process::launch(slot==1 ? "/bin/files.elf" : "/bin/notes.elf",path);
        if (pid<0) { launch_error[slot]="Cannot start app. Check available processes."; focus_window(slot); return pid; }
        app.owner=pid; launch_error[slot]=nullptr;
    } else if (*path || slot==1) {
        if ((app.head+1)%64==app.tail || app.overflow) return abi::full;
        gui::Event event{}; event.type=gui::Type::open_document; strings::copy(event.path,path,sizeof(event.path)); queue(slot,event);
    }
    focus_window(slot); return app.owner;
}
void enqueue(int dx,int dy,unsigned buttons,int key) {
    unsigned next=(input_head+1)%256;
    if (next==input_tail) { input_overflow=true; return; }
    inputs[input_head]={dx,dy,buttons,key}; input_head=next;
}
void mouse(int dx,int dy,unsigned buttons) { if (pixels) enqueue(dx,dy,buttons,0); }
bool key(int c) { if (!pixels) return false; enqueue(0,0,0,c); return true; }
void flush() { if (pixels) { focus_window(0); render(); } }
void service() {
    if (!pixels) return;
    while (input_tail!=input_head) {
        Input event=inputs[input_tail]; input_tail=(input_tail+1)%256;
        if (event.key) {
            int key=event.key;
            if (key==gui::switch_app) {
                for (int i=0;i<3;++i) if (windows[order[i]].visible && order[i]!=focus) { focus_window(order[i]); break; }
            } else if (key>=gui::terminal_app && key<=gui::notes_app) {
                if (key==gui::terminal_app) focus_window(0); else launch(key-gui::terminal_app);
            } else if (focus==0 && key<128) console::input(key);
            else if (focus>0) { gui::Event e{}; e.type=gui::Type::key; e.key=key; queue(focus,e); }
        } else {
            mx=clamp(mx+event.dx,0,width-1); my=clamp(my+event.dy,44,height-1);
            bool down=event.buttons&1; if (down && !left) click();
            if (down && dragging>=0) {
                auto& w=windows[dragging]; w.x=clamp(mx-grab_x,0,width-w.w); w.y=clamp(my-grab_y,44,height-40-w.h);
            }
            if (!down) dragging=-1; left=down; dirty=true;
        }
    }
    if (input_overflow) {
        left=false; dragging=-1; input_overflow=false;
        for (int i=1;i<3;++i) if (apps[i].owner) apps[i].overflow=true;
    }
    if (dirty && arch::ticks()-last_render>=3) {
        asm volatile("sti" : : : "memory"); render(); asm volatile("cli" : : : "memory");
    }
}
}
