#include "api.hpp"
extern "C" int main(const char* argument) {
    if (os::equal(argument, "kernel")) {
        asm volatile("movq (%0), %%rax" : : "r"(0xffffffff80000000ULL) : "rax", "memory");
    } else if (os::equal(argument, "io")) {
        asm volatile("outb %%al, $0xe9" : : "a"('!'));
    } else if (os::equal(argument, "nx")) {
        unsigned char code[] = {0xc3};
        asm volatile("jmp *%0" : : "r"(code) : "memory");
    } else if (os::equal(argument, "text")) {
        asm volatile("movb $0, (%0)" : : "r"(reinterpret_cast<uint64_t>(main)) : "memory");
    } else asm volatile("ud2");
    return 99;
}
