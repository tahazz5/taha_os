#include "gui.hpp"
#include "../shared/text_editor.hpp"
namespace {
desktop::Editor document;
char load_buffer[abi::file_max];
char path[96]="/home/desktop.txt",pending_path[96]{};
unsigned top=0;
bool readonly=false,finished=false;
const char* status="Ctrl+S Save | Ctrl+O Open | Ctrl+N New";
enum class Action { none,new_document,open_document,ask_open,close };
enum class Purpose { open,save_as };
Action pending_action=Action::none;
Purpose purpose=Purpose::open;
ui::Dialog dialog;
bool load(const char* source) {
    abi::Entry info{};
    if (os::call(abi::stat,uint64_t(source),uint64_t(&info))<0 || info.directory) { status="Cannot open: file missing or is a folder"; return false; }
    int64_t n=os::readfile(source,load_buffer,sizeof(load_buffer));
    if (n<0 || info.size>sizeof(load_buffer) || n!=info.size || !document.load(load_buffer,n)) {
        status="Cannot open: only ASCII text up to 32 KiB"; return false;
    }
    os::copy(path,source,sizeof(path)); readonly=info.readonly; top=0;
    status=readonly ? "Read-only file | Save As to create a copy" : "Ctrl+S Save | Ctrl+O Open | Ctrl+N New";
    return true;
}
void prompt(Purpose type,const char* initial) {
    purpose=type;
    dialog.start(ui::Dialog::path,type==Purpose::open ? "Open document" : "Save document as",initial);
}
void complete() {
    Action action=pending_action; pending_action=Action::none; dialog.kind=ui::Dialog::none;
    if (action==Action::new_document) {
        document.load("",0); path[0]=0; readonly=false; top=0; status="New document | Ctrl+S to choose a filename";
    } else if (action==Action::open_document) load(pending_path);
    else if (action==Action::ask_open) prompt(Purpose::open,"/home/");
    else if (action==Action::close) finished=true;
}
void request(Action action,const char* source="") {
    pending_action=action; os::copy(pending_path,source,sizeof(pending_path));
    if (document.modified) dialog.start(ui::Dialog::unsaved,"Save changes before continuing?");
    else complete();
}
bool save_to(const char* target) {
    if (os::writefile(target,document.data,document.size)<0) {
        status="Save failed: check path, permissions, or disk"; dialog.error=status; return false;
    }
    os::copy(path,target,sizeof(path)); document.mark_saved(); readonly=false;
    status="Saved"; complete(); return true;
}
void save() {
    if (!path[0] || readonly) prompt(Purpose::save_as,"/home/untitled.txt");
    else save_to(path);
}
void submit() {
    if (dialog.kind==ui::Dialog::unsaved) { save(); return; }
    if (dialog.kind==ui::Dialog::overwrite) { save_to(dialog.value); return; }
    if (dialog.value[0]!='/') { dialog.error="Use an absolute path, e.g. /home/note.txt"; return; }
    if (purpose==Purpose::open) {
        if (load(dialog.value)) dialog.kind=ui::Dialog::none;
        else dialog.error=status;
    } else {
        abi::Entry entry{};
        if (os::call(abi::stat,uint64_t(dialog.value),uint64_t(&entry))==0 && !os::equal(dialog.value,path)) {
            dialog.kind=ui::Dialog::overwrite; dialog.title="Replace existing file?"; dialog.error="";
        } else save_to(dialog.value);
    }
}
void render() {
    using namespace ui;
    begin(); rectangle(0,0,width,height,background);
    button(12,6,48,"Save"); button(68,6,72,"Save As"); button(148,6,48,"Open"); button(204,6,40,"New");
    button(252,6,48,"Undo"); button(308,6,48,"Redo");
    label(12,38,path[0] ? path : "Untitled",text,background,(width-48)/8);
    if (document.modified) label(width-24,38,"*",accent);
    unsigned cols=(width-24)/8,visible=(height-102)/16,caret_row,caret_col;
    document.position(document.cursor,cols,caret_row,caret_col);
    if (caret_row<top) top=caret_row;
    if (caret_row>=top+visible) top=caret_row-visible+1;
    size_t start=document.at(top,0,cols);
    unsigned row=top;
    while (start<document.size && row<top+visible) {
        char line[241]; unsigned n=0;
        while (start<document.size && document.data[start]!='\n' && n<cols) {
            char c=document.data[start++]; line[n++]=c=='\t' ? ' ' : c;
        }
        line[n]=0; label(12,66+(row-top)*16,line); ++row;
        // A newline after a full wrapped row occupies its own visual line.
        if (n<cols && start<document.size && document.data[start]=='\n') ++start;
    }
    if (dialog.kind==Dialog::none && !readonly) rectangle(12+caret_col*8,66+(caret_row-top)*16,2,16,accent);
    rectangle(0,height-26,width,26,0x182431); label(12,height-21,status,muted,0x182431,(width-24)/8);
    dialog.draw(); present();
}
void click(int px,int y) {
    int x=px-12;
    if (y>=6 && y<30) {
        if (x>=0 && x<48) save();
        else if (x>=56 && x<128) prompt(Purpose::save_as,path[0] ? path : "/home/untitled.txt");
        else if (x>=136 && x<184) request(Action::ask_open);
        else if (x>=192 && x<232) request(Action::new_document);
        else if (x>=240 && x<288 && !readonly) status=document.undo() ? "Undo | Ctrl+Y to redo" : "Nothing to undo";
        else if (x>=296 && x<344 && !readonly) status=document.redo() ? "Redo | Ctrl+Z to undo" : "Nothing to redo";
    } else if (y>=66 && y<ui::height-36 && x>=0 && x<ui::width-24)
        document.cursor=document.at(top+(y-66)/16,x/8,(ui::width-24)/8);
}
void key(int c) {
    unsigned cols=(ui::width-24)/8;
    if (c==19) save();
    else if (c==gui::save_as) prompt(Purpose::save_as,path[0] ? path : "/home/untitled.txt");
    else if (c==15) request(Action::ask_open);
    else if (c==14) request(Action::new_document);
    else if (c==26 && !readonly) status=document.undo() ? "Undo | Ctrl+Y to redo" : "Nothing to undo";
    else if (c==25 && !readonly) status=document.redo() ? "Redo | Ctrl+Z to undo" : "Nothing to redo";
    else if (c==gui::left_key && document.cursor) --document.cursor;
    else if (c==gui::right_key && document.cursor<document.size) ++document.cursor;
    else if (c==gui::up_key) document.vertical(-1,cols);
    else if (c==gui::down_key) document.vertical(1,cols);
    else if (c==gui::home_key) document.home();
    else if (c==gui::end_key) document.end();
    else if (c==gui::page_up) document.vertical(-int((ui::height-102)/16),cols);
    else if (c==gui::page_down) document.vertical((ui::height-102)/16,cols);
    else if (!readonly) {
        if (c==8) document.backspace();
        else if (c==gui::delete_key) document.remove();
        else if (c=='\n' || c=='\t' || (c>=32 && c<=126)) {
            if (!document.insert(c)) status="Document full (32 KiB)";
        }
    }
}
}
extern "C" int main(const char* args) {
    if (!ui::open(2,"Notes")) return 1;
    if (*args) { if (!load(args)) path[0]=0; }
    else {
        abi::Entry info{};
        if (os::call(abi::stat,uint64_t(path),uint64_t(&info))==0 && !load(path)) path[0]=0;
    }
    gui::Event event{};
    while (!finished && ui::next(event)) {
        if (event.type==gui::Type::input_lost) { dialog.kind=ui::Dialog::none; pending_action=Action::none; status="Input queue full; please retry the last action"; }
        else if (event.type==gui::Type::open_document) {
            // Keep the current dialog and unsaved-operation target intact.
            if (dialog.kind!=ui::Dialog::none) status="Finish the current dialog before opening another file";
            else request(Action::open_document,event.path);
        } else if (event.type==gui::Type::close) {
            if (dialog.kind==ui::Dialog::none) request(Action::close);
        } else if (dialog.kind!=ui::Dialog::none) {
            auto result=dialog.handle(event);
            if (result==ui::Dialog::cancel) { dialog.kind=ui::Dialog::none; pending_action=Action::none; }
            else if (result==ui::Dialog::accept) submit();
            else if (result==ui::Dialog::discard) complete();
        } else if (event.type==gui::Type::click) click(event.x,event.y);
        else if (event.type==gui::Type::key) key(event.key);
        if (!finished) render();
    }
    return finished ? 0 : 1;
}
