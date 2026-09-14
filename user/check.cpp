#include "api.hpp"
extern "C" int main(const char*) {
    int failures = 0;
    if (os::call(abi::console_write, 0xffffffff80000000ULL, 8) != abi::invalid) ++failures;
    if (os::call(abi::console_write, 0x7fffff, UINT64_MAX) != abi::invalid) ++failures;
    if (os::call(abi::console_write, 0x7fffff, 2) != abi::invalid) ++failures;
    if (os::call(abi::file_read, uint64_t("/etc/welcome"), uint64_t(main), 1) != abi::invalid) ++failures;
    if (os::call(static_cast<abi::Call>(999)) != abi::invalid) ++failures;
    if (os::call(abi::kill, 1) != abi::denied) ++failures;
    if (os::writefile("/bin/hello.elf", "bad", 3) != abi::denied) ++failures;
    os::print(failures ? "USERCHECK FAIL\n" : "USERCHECK PASS: pointer validation and permissions\n");
    return failures;
}
