#pragma once
#include <stdint.h>
namespace arch {
inline void out(uint16_t port, uint8_t value) { asm volatile("outb %0, %1" : : "a"(value), "Nd"(port)); }
inline uint8_t in(uint16_t port) { uint8_t value; asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port)); return value; }
[[noreturn]] inline void halt() { for (;;) asm volatile("cli; hlt"); }
void install_tables();
void start_timer();
uint64_t ticks();
uint64_t breakpoint_count();
// Layout matches interrupts.S; all GPRs survive every interrupt.
struct InterruptFrame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector, error, rip, cs, flags, rsp, ss;
};
static_assert(sizeof(InterruptFrame) == 22 * 8);
}
extern "C" char kernel_stack_guard[], kernel_stack_top[];
extern "C" char df_stack_guard[], nmi_stack_guard[], mc_stack_guard[];
extern "C" [[noreturn]] void trigger_stack_fault();

extern "C" bool test_interrupt_registers();
