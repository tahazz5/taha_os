#pragma once
#include <stdint.h>
#include <stddef.h>
#include "pages.hpp"
namespace paging {
// Clone boot mappings into owned frames, then protect stack guard pages.
// Bootloader memory is intentionally retained until its data is copied too.
bool initialize(PageAllocator& allocator, uint64_t direct_map);
uint64_t root();
size_t table_count();
bool mapped(uint64_t address);
}

namespace paging {
constexpr uint64_t user_min = 0x400000, user_end = 0x800000;
struct AddressSpace {
    uint64_t cr3 = 0;
    uint64_t frames[256]{};
    size_t count = 0;
};
bool create(AddressSpace& space);
bool map(AddressSpace& space, uint64_t address, bool writable, bool executable);
void destroy(AddressSpace& space);
void activate(const AddressSpace& space);
bool copy_from(const AddressSpace& space, void* to, uint64_t from, size_t size);
bool copy_to(const AddressSpace& space, uint64_t to, const void* from, size_t size, bool loading = false);
bool user_range(const AddressSpace& space, uint64_t address, size_t size, bool writing);
}
