#include "gui.hpp"
namespace {
int failures=0;
void check(bool condition) { if (!condition) ++failures; }
}
extern "C" int main(const char* args) {
    gui::Config config{}; config.slot=1; os::copy(config.title,"GUI validation",sizeof(config.title));
    check(os::call(abi::window_open,0xffffffff80000000ULL)==abi::invalid);
    config.slot=0; check(os::call(abi::window_open,uint64_t(&config))==abi::invalid); config.slot=1;
    if (os::call(abi::window_open,uint64_t(&config))<0) { os::print("GUICHECK FAIL: no free window\n"); return 1; }
    check(os::call(abi::window_open,uint64_t(&config))==abi::full);
    gui::Command command{}; command.kind=gui::Draw::rectangle;
    command.x=-10; command.y=-10; command.width=40; command.height=40; command.color=0x67dec8;
    check(os::call(abi::window_present,uint64_t(&command),1)==0);
    if (os::equal(args,"fault")) { asm volatile("ud2"); return 1; }
    gui::Event event{};
    check(os::call(abi::window_event,uint64_t(&event),0)==0 && event.type==gui::Type::resize);
    check(os::call(abi::window_event,uint64_t(&event),0)==abi::again);
    if (os::equal(args,"wait")) {
        os::print("GUIWAIT ready\n");
        while (ui::next(event)) if (event.type==gui::Type::close) return 0;
        return 1;
    }
    check(os::call(abi::window_present,0xffffffff80000000ULL,1)==abi::invalid);
    check(os::call(abi::window_present,uint64_t(&command),UINT64_MAX)==abi::invalid);
    command.width=INT32_MAX; check(os::call(abi::window_present,uint64_t(&command),1)==abi::invalid);
    command.width=40; command.x=INT32_MIN; check(os::call(abi::window_present,uint64_t(&command),1)==abi::invalid);
    command.x=0; command.kind=gui::Draw(99); check(os::call(abi::window_present,uint64_t(&command),1)==abi::invalid);
    command.kind=gui::Draw::text;
    for (char& c:command.text) c='a';
    check(os::call(abi::window_present,uint64_t(&command),1)==abi::invalid);
    check(os::call(abi::window_event,uint64_t(main),1)==abi::invalid);
    check(os::call(abi::window_event,uint64_t(&event),2)==abi::invalid);
    // The child has no window and cannot present or receive this process's input.
    int64_t pid=os::call(abi::spawn,uint64_t("/bin/check.elf"),uint64_t("gui-owner"),0);
    check(pid>0 && os::call(abi::wait,pid)==0);
    check(os::call(abi::window_present,uint64_t(&command),0)==0);
    check(os::call(abi::window_close)==0);
    check(os::call(abi::window_present,uint64_t(&command),0)==abi::denied);
    check(os::call(abi::window_event,uint64_t(&event),0)==abi::denied);
    // The same slot must be available immediately after closing it.
    check(os::call(abi::window_open,uint64_t(&config))>0);
    os::print(failures ? "GUICHECK FAIL\n" : "GUICHECK PASS: ownership, pointer validation, bounded drawing, event lifecycle\n");
    return failures;
}
