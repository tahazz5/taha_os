#pragma once
#include "api.hpp"
#include "../shared/gui.hpp"
namespace ui {
constexpr uint32_t background=0x101923,text=0xdce6f2,muted=0x94a6ba,accent=0x67dec8;
inline int width=480,height=558;
inline gui::Command commands[gui::command_limit];
inline size_t count=0;
inline bool overflow=false;
inline void begin() { count=0; overflow=false; }
inline gui::Command* append() {
    if (count==gui::command_limit) { overflow=true; return nullptr; }
    commands[count]={}; return &commands[count++];
}
inline void rectangle(int x,int y,int w,int h,uint32_t color) {
    auto* c=append(); if (!c) return;
    c->kind=gui::Draw::rectangle; c->x=x; c->y=y; c->width=w; c->height=h; c->color=color;
}
inline void label(int x,int y,const char* s,uint32_t color=text,uint32_t bg=background,size_t max=240) {
    while (*s && max) {
        auto* c=append(); if (!c) return;
        c->kind=gui::Draw::text; c->x=x; c->y=y; c->color=color; c->background=bg;
        size_t n=0;
        while (*s && max && n<sizeof(c->text)-1) {
            char value=*s++; c->text[n++]=value>=32 && value<=126 ? value : '?'; --max;
        }
        c->text[n]=0; x+=n*8;
    }
}
inline void button(int x,int y,int w,const char* title) {
    rectangle(x,y,w,24,0x30495a); rectangle(x+1,y+1,w-2,22,0x213746);
    label(x+6,y+4,title,accent,0x213746,(w-12)/8);
}
inline bool present() { return !overflow && os::call(abi::window_present,uint64_t(commands),count)==0; }
inline bool open(unsigned slot,const char* title) {
    gui::Config config{}; config.slot=slot; os::copy(config.title,title,sizeof(config.title));
    int64_t result=os::call(abi::window_open,uint64_t(&config));
    if (result<0) { os::print("Cannot open graphical window.\n"); os::error(result); return false; }
    return true;
}
inline bool next(gui::Event& event) {
    if (os::call(abi::window_event,uint64_t(&event),1)<0) return false;
    if (event.type==gui::Type::resize) { width=event.width; height=event.height; }
    return true;
}
inline void body(const char* s,int top) {
    int y=top; char line[241];
    while (*s && y+16<height-12) {
        unsigned n=0,cols=(width-24)/8;
        while (*s && *s!='\n' && n<cols) { char c=*s++; line[n++]=c>=32 && c<=126 ? c : ' '; }
        line[n]=0; label(12,y,line); y+=16;
        if (*s=='\n') ++s;
    }
}
struct Dialog {
    enum Kind { none,path,unsaved,overwrite } kind=none;
    enum Result { pending,accept,discard,cancel };
    char value[96]{};
    const char* title="";
    const char* error="";
    bool selected=true;
    void start(Kind type,const char* heading,const char* initial="") {
        kind=type; title=heading; error=""; selected=true; os::copy(value,initial,sizeof(value));
    }
    void draw() const {
        if (kind==none) return;
        int w=width-24; if (w>536) w=536;
        int x=(width-w)/2,y=(height-184)/2;
        rectangle(x+6,y+6,w,184,0x080e16); rectangle(x,y,w,184,0x213041);
        label(x+16,y+16,title,text,0x213041,(w-32)/8);
        if (kind==unsaved) label(x+16,y+52,"Your document has unsaved changes.",muted,0x213041,(w-32)/8);
        else {
            rectangle(x+16,y+48,w-32,28,background);
            size_t max=(w-48)/8,n=os::length(value); const char* s=value;
            if (n>max) s+=n-max;
            label(x+24,y+54,s,text,selected && kind==path ? 0x285260 : background,max);
            if (!selected && kind==path) rectangle(x+24+os::length(s)*8,y+54,2,16,accent);
        }
        label(x+16,y+92,error,accent,0x213041,(w-32)/8);
        button(x+16,y+134,64,kind==unsaved ? "Save" : "OK");
        if (kind==unsaved) button(x+96,y+134,80,"Discard");
        button(x+192,y+134,80,"Cancel");
    }
    Result handle(const gui::Event& event) {
        if (event.type==gui::Type::key) {
            int c=event.key; size_t n=os::length(value);
            if (c==27) return cancel;
            if (c=='\n') return accept;
            if (kind!=path) return pending;
            if (c==1) selected=true;
            else if (c==8) {
                if (selected) value[0]=0; else if (n) value[n-1]=0;
                selected=false;
            } else if (c>=32 && c<=126) {
                if (selected) { n=0; value[0]=0; selected=false; }
                if (n<sizeof(value)-1) { value[n]=c; value[n+1]=0; }
            }
        } else if (event.type==gui::Type::click) {
            int w=width-24; if (w>536) w=536;
            int x=event.x-(width-w)/2-16,y=event.y-(height-184)/2;
            if (y>=130 && y<168) {
                if (x>=0 && x<64) return accept;
                if (x>=80 && x<160 && kind==unsaved) return discard;
                if (x>=176 && x<256) return cancel;
            }
        }
        return pending;
    }
};
}
