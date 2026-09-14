#include "api.hpp"
extern "C" int main(const char* argument) {
    unsigned short cs; asm volatile("mov %%cs, %0" : "=r"(cs));
    os::print("Hello from an ELF process in ring "); os::number(cs & 3); os::print("!\n");
    if (*argument) { os::print("Argument: "); os::print(argument); os::print("\n"); }
    return 0;
}
