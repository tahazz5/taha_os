#include "api.hpp"
extern "C" int main(const char* argument) {
    // No yield or sleep: only timer preemption lets the shell keep running.
    char message[160]; os::copy(message, "CPU worker started ", sizeof(message));
    os::copy(message + 19, argument, sizeof(message) - 19);
    size_t size = os::length(message); message[size++] = '\n'; os::write(message, size);
    for (;;) asm volatile("pause");
}
