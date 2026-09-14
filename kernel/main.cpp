#include <limine.h>
#include <stdint.h>
#include "pages.hpp"
#include "arch.hpp"
#include "console.hpp"
#include "paging.hpp"
#include "process.hpp"
#include "programs.hpp"
#include "fs.hpp"
#include "disk.hpp"
#include "display.hpp"
#include "keyboard.hpp"

__attribute__((used, section(".limine_requests_start")))
static volatile LIMINE_REQUESTS_START_MARKER;
__attribute__((used, section(".limine_requests")))
static volatile LIMINE_BASE_REVISION(3);
__attribute__((used, section(".limine_requests")))
static volatile limine_memmap_request memory_request = {LIMINE_MEMMAP_REQUEST, 0, nullptr};
__attribute__((used, section(".limine_requests")))
static volatile limine_hhdm_request hhdm_request = {LIMINE_HHDM_REQUEST, 0, nullptr};
__attribute__((used, section(".limine_requests")))
static volatile limine_framebuffer_request framebuffer_request = {LIMINE_FRAMEBUFFER_REQUEST, 0, nullptr};
__attribute__((used, section(".limine_requests_end")))
static volatile LIMINE_REQUESTS_END_MARKER;

static PageAllocator pages;
using namespace console;
using arch::halt;
static bool equal(const char* a, const char* b) { while (*a && *a == *b) { ++a; ++b; } return *a == *b; }

static void cpu() {
    uint32_t a, b, c, d;
    asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0), "c"(0));
    char vendor[13];
    for (unsigned i = 0; i < 4; ++i) { vendor[i] = b >> (i * 8); vendor[i+4] = d >> (i*8); vendor[i+8] = c >> (i*8); }
    vendor[12] = 0; write("CPU: "); write(vendor); write(" | max CPUID leaf: "); number(a); write("\n");
}
static void memory() {
    write("Managed pages: "); number(pages.total()); write(" | free: "); number(pages.available());
    write(" | page size: 4096 bytes\n");
}
static bool selftest() {
    uint64_t cr3; asm volatile("mov %%cr3, %0" : "=r"(cr3));
    if (cr3 != paging::root() || paging::mapped(0) ||
        paging::mapped(reinterpret_cast<uint64_t>(kernel_stack_guard)) ||
        paging::mapped(reinterpret_cast<uint64_t>(df_stack_guard)) ||
        paging::mapped(reinterpret_cast<uint64_t>(nmi_stack_guard)) ||
        paging::mapped(reinterpret_cast<uint64_t>(mc_stack_guard))) return false;
    auto breaks = arch::breakpoint_count();
    if (!test_interrupt_registers()) return false;
    if (arch::breakpoint_count() != breaks + 1) return false;
    auto before = pages.available();
    uint64_t page = pages.allocate();
    if (!page) return false;
    auto* data = reinterpret_cast<volatile uint64_t*>(hhdm_request.response->offset + page);
    for (unsigned i = 0; i < 512; ++i) data[i] = 0x5441484100000000ULL + i;
    bool ok = true;
    for (unsigned i = 0; i < 512; ++i) if (data[i] != 0x5441484100000000ULL + i) ok = false;
    return pages.release(page) && pages.available() == before && ok;
}
extern "C" void kernel_diagnostic(const char* line) {
    if (equal(line, "help")) write("help  info  cpu  mem  memmap  selftest  uptime  vm  clear  halt\n");
    else if (equal(line, "info")) write("TahaOS 0.4 | x86_64 | C++20 | Limine | serial monitor\n");
    else if (equal(line, "cpu")) cpu();
    else if (equal(line, "mem")) memory();
    else if (equal(line, "memmap")) {
        auto* map = memory_request.response;
        for (uint64_t i = 0; i < map->entry_count; ++i) {
            auto* e = map->entries[i]; hex(e->base); write(" bytes="); number(e->length); write(" type="); number(e->type); write("\n");
        }
    }
    else if (equal(line, "selftest")) write(selftest() ? "SELFTEST PASS\n" : "SELFTEST FAIL\n");
    else if (equal(line, "uptime")) { write("Timer ticks: "); number(arch::ticks()); write(" (100 Hz nominal)\n"); }
    else if (equal(line, "vm")) {
        write("Owned CR3: "); hex(paging::root()); write(" | table pages: "); number(paging::table_count());
        write(" | stack guards: 4 | null page: unmapped\n");
    }
#ifdef TAHA_TEST_FAULTS
    else if (equal(line, "fault ud")) { asm volatile("ud2"); }
    else if (equal(line, "fault page")) {
        asm volatile("movq $1, (%0)" : : "r"(kernel_stack_guard) : "memory");
    }
    else if (equal(line, "fault stack")) trigger_stack_fault();
    else if (equal(line, "fault text")) {
        asm volatile("movb $0, (%0)" : : "r"(reinterpret_cast<uint64_t>(cpu)) : "memory");
    }
#endif
    else if (equal(line, "clear")) write("\033[2J\033[H");
    else if (equal(line, "halt")) {
        if (fs::persistent() && !fs::sync()) { write("Storage I/O failed.\n"); return; }
        write("System halted.\n"); halt();
    }
    else if (*line) write("Unknown command. Type help.\n");
}
extern "C" [[noreturn]] void kmain() {
    asm volatile("cli");
    console::init();
    write("\nTahaOS 0.4\n");
    if (!LIMINE_BASE_REVISION_SUPPORTED) { write("PANIC: unsupported boot protocol\n"); halt(); }
    if (framebuffer_request.response && framebuffer_request.response->framebuffer_count)
        display::initialize(framebuffer_request.response->framebuffers[0]);
    arch::install_tables();
    write("GDT/TSS/IDT ready | kernel stack active\n");
    if (!memory_request.response || !hhdm_request.response) { write("PANIC: missing memory information\n"); halt(); }
    bool capped = false;
    auto* map = memory_request.response;
    for (uint64_t i = 0; i < map->entry_count; ++i) {
        auto* e = map->entries[i];
        if (e->type == LIMINE_MEMMAP_USABLE) {
            auto result = pages.add(e->base, e->length);
            if (result == PageAllocator::AddResult::truncated) capped = true;
            else if (result != PageAllocator::AddResult::added) panic("invalid or overlapping memory map");
        }
    }
    if (!paging::initialize(pages, hhdm_request.response->offset)) panic("page table initialization failed");
    write("Owned page tables active | stack guards installed\n");
    cpu(); memory();
    if (capped) write("Usable memory above 4 GiB is reserved.\n");
    if (!selftest()) { write("SELFTEST FAIL\n"); halt(); }
    fs::initialize();
    install_programs();
    if (disk::initialize() && fs::mount({disk::read, disk::write, disk::flush}))
        write("Storage: TahaFS persistent /home\n");
    else write("Storage: RAM only (no valid TahaFS disk)\n");
    process::initialize();
    if (keyboard::initialize()) write("PS/2 keyboard ready (US layout)\n");
    arch::start_timer();
    uint64_t first_tick = arch::ticks();
    // Bound the initial check independently of IRQs: missing interrupts must
    // report failure instead of leaving boot stuck in HLT forever.
    for (uint64_t spin = 0; spin < 100000000 && arch::ticks() == first_tick; ++spin)
        asm volatile("pause");
    if (arch::ticks() == first_tick) panic("timer interrupt did not arrive");
    write("TIMER PASS\n");
    write("SELFTEST PASS\nType help for commands.\n");
    process::start();
}
