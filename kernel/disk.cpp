#include "disk.hpp"
#include "arch.hpp"
namespace {
uint32_t capacity = 0;
uint16_t input() { uint16_t v; asm volatile("inw %1, %0" : "=a"(v) : "Nd"(uint16_t(0x1f0))); return v; }
void output(uint16_t v) { asm volatile("outw %0, %1" : : "a"(v), "Nd"(uint16_t(0x1f0))); }
void delay() { for (int i = 0; i < 4; ++i) arch::in(0x3f6); }
bool ready(bool data) {
    for (unsigned i = 0; i < 1000000; ++i) {
        uint8_t status = arch::in(0x1f7);
        if (!status || status == 0xff) return false;
        if (!(status & 0x80)) {
            if (status & 0x21) return false;
            if (!data || (status & 8)) return true;
        }
        asm volatile("pause");
    }
    return false;
}
bool select(uint32_t sector, uint8_t command) {
    if (sector >= capacity || sector >= (1u << 28) || !ready(false)) return false;
    arch::out(0x1f6, 0xe0 | ((sector >> 24) & 15)); delay();
    arch::out(0x1f2, 1); arch::out(0x1f3, sector);
    arch::out(0x1f4, sector >> 8); arch::out(0x1f5, sector >> 16);
    arch::out(0x1f7, command); delay();
    return ready(true);
}
}
namespace disk {
bool initialize() {
    arch::out(0x3f6, 2); // PIO only: disable device IRQs.
    arch::out(0x1f6, 0xa0); delay();
    arch::out(0x1f2, 0); arch::out(0x1f3, 0); arch::out(0x1f4, 0); arch::out(0x1f5, 0);
    arch::out(0x1f7, 0xec); delay();
    if (!ready(true) || arch::in(0x1f4) || arch::in(0x1f5)) return false;
    uint16_t identify[256]; for (auto& word : identify) word = input();
    if (!(identify[49] & (1 << 9))) return false;
    capacity = uint32_t(identify[60]) | (uint32_t(identify[61]) << 16);
    return capacity >= 16385;
}
bool read(uint32_t sector, void* buffer) {
    if (!select(sector, 0x20)) return false;
    auto* bytes = static_cast<uint8_t*>(buffer);
    for (unsigned i = 0; i < 256; ++i) {
        uint16_t v = input(); bytes[2 * i] = v; bytes[2 * i + 1] = v >> 8;
    }
    delay(); return ready(false);
}
bool write(uint32_t sector, const void* buffer) {
    if (!select(sector, 0x30)) return false;
    auto* bytes = static_cast<const uint8_t*>(buffer);
    for (unsigned i = 0; i < 256; ++i) output(uint16_t(bytes[2 * i]) | uint16_t(bytes[2 * i + 1]) << 8);
    delay(); return ready(false);
}
bool flush() { if (!capacity || !ready(false)) return false; arch::out(0x1f7, 0xe7); delay(); return ready(false); }
uint32_t sectors() { return capacity; }
}
