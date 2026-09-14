#pragma once
#include <stdint.h>
namespace console {
void init();
void receive_interrupt();
void input(char c);
bool take_interrupt();
void deliver_interrupt();
int read_char();
void putc(char c);
void write(const char* text);
void number(uint64_t n, unsigned base = 10);
void hex(uint64_t n);
[[noreturn]] void panic(const char* message);
}
