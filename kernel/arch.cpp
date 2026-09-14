#include "arch.hpp"
#include "console.hpp"
#include "process.hpp"
#include "keyboard.hpp"
#include <stddef.h>

namespace {
struct __attribute__((packed)) Descriptor { uint16_t limit; uint64_t base; };
struct __attribute__((packed)) Tss {
    uint32_t reserved0;
    uint64_t rsp[3];
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3, iomap;
};
struct __attribute__((packed)) Gate {
    uint16_t low, selector;
    uint8_t ist, flags;
    uint16_t mid;
    uint32_t high, reserved;
};
static_assert(sizeof(Tss) == 104 && offsetof(Tss, ist) == 36);
static_assert(sizeof(Gate) == 16 && sizeof(Descriptor) == 10);
alignas(16) Tss tss{};
alignas(16) uint64_t gdt[7] = {0, 0x00af9a000000ffff, 0x00cf92000000ffff, 0, 0, 0x00affa000000ffff, 0x00cff2000000ffff};
alignas(16) Gate idt[256];
uint64_t timer_ticks = 0, breakpoints = 0;
extern "C" void load_gdt(const Descriptor*);
extern "C" uintptr_t isr_table[256];
extern "C" char df_stack_top[], nmi_stack_top[], mc_stack_top[];
void delay() { arch::out(0x80, 0); }
void pic_write(uint16_t port, uint8_t value) { arch::out(port, value); delay(); }
}
namespace arch {
void install_tables() {
    uint32_t a, b, c, d;
    asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000001), "c"(0));
    if (!(d & (1 << 20))) console::panic("NX-capable CPU required");
    asm volatile("rdmsr" : "=a"(a), "=d"(d) : "c"(0xc0000080));
    a |= 1 << 11;
    asm volatile("wrmsr" : : "a"(a), "d"(d), "c"(0xc0000080));
    uint64_t cr0; asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 1 << 3; // FPU/SIMD is unsupported; fault rather than leak process state.
    asm volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
    tss.rsp[0] = reinterpret_cast<uint64_t>(kernel_stack_top);
    tss.ist[0] = reinterpret_cast<uint64_t>(df_stack_top);
    tss.ist[1] = reinterpret_cast<uint64_t>(nmi_stack_top);
    tss.ist[2] = reinterpret_cast<uint64_t>(mc_stack_top);
    tss.iomap = sizeof(Tss);
    uint64_t base = reinterpret_cast<uint64_t>(&tss);
    gdt[3] = (sizeof(Tss) - 1) | ((base & 0xffffff) << 16) |
        (uint64_t(0x89) << 40) | (((base >> 24) & 0xff) << 56);
    gdt[4] = base >> 32;
    Descriptor gdtr{sizeof(gdt) - 1, reinterpret_cast<uint64_t>(gdt)};
    load_gdt(&gdtr);
    for (unsigned i = 0; i < 256; ++i) {
        uint64_t a = isr_table[i];
        uint8_t ist = i == 8 ? 1 : i == 2 ? 2 : i == 18 ? 3 : 0;
        idt[i] = {uint16_t(a), 8, ist, uint8_t(i == 128 ? 0xee : 0x8e), uint16_t(a >> 16), uint32_t(a >> 32), 0};
    }
    Descriptor idtr{sizeof(idt) - 1, reinterpret_cast<uint64_t>(idt)};
    asm volatile("lidt %0" : : "m"(idtr) : "memory");
}
void start_timer() {
    // Remap the PIC; enable the PIT and COM1 receive interrupts.
    pic_write(0x20, 0x11); pic_write(0xa0, 0x11);
    pic_write(0x21, 0x20); pic_write(0xa1, 0x28);
    pic_write(0x21, 4); pic_write(0xa1, 2);
    pic_write(0x21, 1); pic_write(0xa1, 1);
    pic_write(0x21, 0xec); pic_write(0xa1, 0xff);
    out(0x3f9, 1);
    constexpr uint16_t divisor = 11932; // approximately 100 Hz
    out(0x43, 0x36); out(0x40, divisor & 0xff); out(0x40, divisor >> 8);
    asm volatile("sti" : : : "memory");
}
uint64_t ticks() { return __atomic_load_n(&timer_ticks, __ATOMIC_RELAXED); }
uint64_t breakpoint_count() { return breakpoints; }
}
extern "C" void interrupt_dispatch(arch::InterruptFrame* frame) {
    if (frame->vector == 32) {
        __atomic_fetch_add(&timer_ticks, 1, __ATOMIC_RELAXED);
        arch::out(0x20, 0x20);
        process::timer(frame);
        return;
    }
    if (frame->vector == 33) { keyboard::interrupt(); arch::out(0x20, 0x20); return; }
    if (frame->vector == 36) { console::receive_interrupt(); arch::out(0x20, 0x20); return; }
    if (frame->vector == 128) { process::syscall(frame); return; }
    if ((frame->cs & 3) == 3) { process::fault(frame); return; }
    // Spurious IRQ7/IRQ15 are possible even when masked. Only an in-service
    // slave interrupt gets a slave EOI; IRQ15 always acknowledges the master.
    if (frame->vector == 39 || frame->vector == 47) {
        uint16_t port = frame->vector == 39 ? 0x20 : 0xa0;
        arch::out(port, 0x0b);
        if (arch::in(port) & 0x80) arch::out(port, 0x20);
        if (frame->vector == 47) arch::out(0x20, 0x20);
        return;
    }
    if (frame->vector == 3) { ++breakpoints; return; }
    uint64_t cr2; asm volatile("mov %%cr2, %0" : "=r"(cr2));
    using namespace console;
    write("\nPANIC: exception vector="); number(frame->vector);
    if (frame->vector == 6) write(" (invalid opcode)");
    if (frame->vector == 8) write(" (double fault, IST1)");
    if (frame->vector == 13) write(" (general protection)");
    if (frame->vector == 14) write(" (page fault)");
    write(" rip="); hex(frame->rip); write(" error="); hex(frame->error);
    write(" cr2="); hex(cr2); write(" rsp="); hex(frame->rsp); write("\n");
    arch::halt();
}
