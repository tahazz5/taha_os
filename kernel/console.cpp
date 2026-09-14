#include "console.hpp"
#include "arch.hpp"
#include "display.hpp"
namespace {
char input_buffer[1024];
unsigned input_head = 0, input_tail = 0;
bool input_overflow = false, interrupt_pending = false;
}
namespace console {
static void enqueue(char c) {
    unsigned head = __atomic_load_n(&input_head, __ATOMIC_RELAXED);
    unsigned next = (head + 1) & 1023;
    if (next == __atomic_load_n(&input_tail, __ATOMIC_ACQUIRE))
        __atomic_store_n(&input_overflow, true, __ATOMIC_RELEASE);
    else {
        input_buffer[head] = c;
        __atomic_store_n(&input_head, next, __ATOMIC_RELEASE);
    }
}
void input(char c) {
    if (c == 3) __atomic_store_n(&interrupt_pending, true, __ATOMIC_RELEASE);
    else enqueue(c);
}
bool take_interrupt() { return __atomic_exchange_n(&interrupt_pending, false, __ATOMIC_ACQ_REL); }
void deliver_interrupt() { enqueue(3); }
void receive_interrupt() {
    while (arch::in(0x3fd) & 1) input(arch::in(0x3f8));
}
int read_char() {
    if (__atomic_exchange_n(&input_overflow, false, __ATOMIC_ACQ_REL)) {
        __atomic_store_n(&input_tail, __atomic_load_n(&input_head, __ATOMIC_ACQUIRE), __ATOMIC_RELEASE);
        return -2;
    }
    unsigned tail = __atomic_load_n(&input_tail, __ATOMIC_RELAXED);
    if (tail == __atomic_load_n(&input_head, __ATOMIC_ACQUIRE)) return -1;
    unsigned char c = input_buffer[tail];
    __atomic_store_n(&input_tail, (tail + 1) & 1023, __ATOMIC_RELEASE);
    return c;
}
void init() {
    arch::out(0x3f9, 0); arch::out(0x3fb, 0x80); arch::out(0x3f8, 1); arch::out(0x3f9, 0);
    arch::out(0x3fb, 3); arch::out(0x3fa, 0xc7); arch::out(0x3fc, 0x0b);
}
void putc(char c) {
    if (c == '\n') putc('\r');
    display::putc(c);
    arch::out(0xe9, c);
    for (unsigned wait = 0; wait < 100000; ++wait)
        if (arch::in(0x3fd) & 0x20) { arch::out(0x3f8, c); return; }
}
void write(const char* s) { while (*s) putc(*s++); }
void number(uint64_t n, unsigned base) {
    if (base < 2 || base > 16) return;
    char digits[65]; unsigned size = 0;
    do { digits[size++] = "0123456789abcdef"[n % base]; n /= base; } while (n);
    while (size) putc(digits[--size]);
}
void hex(uint64_t n) { write("0x"); number(n, 16); }
[[noreturn]] void panic(const char* message) {
    asm volatile("cli");
    write("\nPANIC: "); write(message); write("\n"); arch::halt();
}
}
