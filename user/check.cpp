#include "api.hpp"
#include "../shared/gui.hpp"
extern "C" int main(const char* args) {
    int failures = 0;
    if (os::call(abi::console_write, 0xffffffff80000000ULL, 8) != abi::invalid) ++failures;
    if (os::call(abi::console_write, 0x7fffff, UINT64_MAX) != abi::invalid) ++failures;
    if (os::call(abi::console_write, 0x7fffff, 2) != abi::invalid) ++failures;
    if (os::call(abi::file_read, uint64_t("/etc/welcome"), uint64_t(main), 1) != abi::invalid) ++failures;
    if (os::call(static_cast<abi::Call>(999)) != abi::invalid) ++failures;
    if (os::call(abi::kill, 1) != abi::denied) ++failures;
    if (os::writefile("/bin/hello.elf", "bad", 3) != abi::denied) ++failures;
    gui::Command command{}; gui::Event event{};
    if (os::call(abi::window_present,uint64_t(&command),0)!=abi::denied) ++failures;
    if (os::call(abi::window_event,uint64_t(&event),0)!=abi::denied) ++failures;
    if (os::equal(args,"gui-owner")) {
        gui::Config config{}; config.slot=1;
        os::copy(config.title,"Ownership probe",sizeof(config.title));
        if (os::call(abi::window_open,uint64_t(&config))!=abi::full) ++failures;
        // Closing without an owned window must leave the parent's window intact.
        if (os::call(abi::window_close)!=0) ++failures;
    }
    os::print(failures ? "USERCHECK FAIL\n" : "USERCHECK PASS: pointer validation and permissions\n");
    return failures;
}
