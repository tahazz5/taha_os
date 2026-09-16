#include "gui.hpp"
namespace {
abi::Entry entries[64];
int entry_count=0,page=0;
char directory[96]="/home",preview[2048]{},preview_path[96]{};
bool viewing=false;
const char* status="";
ui::Dialog dialog;
void refresh() {
    entry_count=os::call(abi::list,uint64_t(directory),uint64_t(entries),64);
    page=0; viewing=false; status="";
}
void render() {
    using namespace ui;
    begin(); rectangle(0,0,width,height,background);
    button(12,6,32,"Up"); button(52,6,48,"Home"); button(108,6,72,"Refresh");
    button(188,6,48,"Prev"); button(244,6,48,"Next"); button(300,6,64,"Folder");
    label(12,34,viewing ? preview_path : directory,muted,background,(width-24)/8);
    if (viewing) { button(12,54,120,"Edit in Notes"); body(preview,82); }
    else if (entry_count<0) label(12,58,"Cannot read directory");
    else if (!entry_count) label(12,58,"Empty directory",muted);
    else for (int i=0;page+i<entry_count && 74+i*20<height-30;++i) {
        const char* name=entries[page+i].path;
        for (const char* p=name;*p;++p) if (*p=='/') name=p+1;
        label(12,58+i*20,entries[page+i].directory ? "+" : "-",accent);
        label(32,58+i*20,name,text,background,(width-44)/8);
    }
    label(12,height-22,status,accent,background,(width-24)/8);
    dialog.draw(); present();
}
void click(int px,int y) {
    int x=px-12;
    if (y>=6 && y<30) {
        if (x>=0 && x<32) {
            size_t n=os::length(directory); while (n>1 && directory[n-1]!='/') --n;
            if (n>1) --n; directory[n]=0; refresh();
        } else if (x>=40 && x<88) { os::copy(directory,"/home",sizeof(directory)); refresh(); }
        else if (x>=96 && x<168) refresh();
        else if ((x>=176 && x<224) || (x>=232 && x<280)) {
            int per_page=(ui::height-105)/20+1;
            if (x<224) page=page>per_page ? page-per_page : 0;
            else if (page+per_page<entry_count) page+=per_page;
            viewing=false;
        } else if (x>=288 && x<352) {
            char path[96]; os::copy(path,directory,sizeof(path)); size_t n=os::length(path);
            if (n>1 && n+1<sizeof(path)) { path[n++]='/'; path[n]=0; }
            dialog.start(ui::Dialog::path,"Create folder",path);
        }
    } else if (viewing && y>=54 && y<78 && x>=0 && x<120) {
        if (os::call(abi::desktop_launch,2,uint64_t(preview_path))<0) status="Cannot open Notes: app or input queue is full";
    } else if (!viewing && y>=58 && y<ui::height-46 && x>=0) {
        int index=page+(y-58)/20;
        if (index<entry_count) {
            if (entries[index].directory) { os::copy(directory,entries[index].path,sizeof(directory)); refresh(); }
            else {
                int64_t n=entries[index].size>=sizeof(preview) ? abi::full : os::readfile(entries[index].path,preview,sizeof(preview)-1);
                if (n<0) os::copy(preview,"Preview unavailable. Use Edit in Notes for larger text files.",sizeof(preview));
                else preview[n]=0;
                os::copy(preview_path,entries[index].path,sizeof(preview_path)); viewing=true;
            }
        }
    }
}
}
extern "C" int main(const char* args) {
    if (!ui::open(1,"Files")) return 1;
    if (*args) os::copy(directory,args,sizeof(directory)); refresh();
    gui::Event event{};
    while (ui::next(event)) {
        if (event.type==gui::Type::close) return 0;
        if (event.type==gui::Type::input_lost) { status="Input queue full; please retry the last action"; dialog.kind=ui::Dialog::none; }
        else if (dialog.kind!=ui::Dialog::none) {
            auto result=dialog.handle(event);
            if (result==ui::Dialog::cancel) dialog.kind=ui::Dialog::none;
            else if (result==ui::Dialog::accept) {
                if (dialog.value[0]!='/' || os::call(abi::mkdir,uint64_t(dialog.value))<0) dialog.error="Cannot create folder: check path and parent";
                else { dialog.kind=ui::Dialog::none; refresh(); }
            }
        } else if (event.type==gui::Type::click) click(event.x,event.y);
        else if (event.type==gui::Type::open_document) { if (event.path[0]) os::copy(directory,event.path,sizeof(directory)); refresh(); }
        else if (event.type==gui::Type::key && event.key==18) refresh(); // Ctrl+R
        render();
    }
    return 1;
}
