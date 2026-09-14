#include "paging.hpp"
#include "arch.hpp"
namespace {
constexpr uint64_t present = 1, large = 1 << 7, address_mask = 0x000ffffffffff000ULL;
constexpr size_t max_tables = 4096;
uint64_t tables[max_tables], table_root = 0, offset = 0;
size_t count = 0;
PageAllocator* pool = nullptr;
uint64_t* direct(uint64_t physical) { return reinterpret_cast<uint64_t*>(offset + physical); }
uint64_t allocate_table() {
    if (count == max_tables) return 0;
    uint64_t frame = pool->allocate();
    if (frame) tables[count++] = frame;
    return frame;
}
uint64_t clone(uint64_t original, unsigned level) {
    uint64_t copy = allocate_table();
    if (!copy) return 0;
    auto* source = direct(original);
    auto* target = direct(copy);
    for (unsigned i = 0; i < 512; ++i) {
        uint64_t entry = source[i];
        if ((entry & present) && level > 1 && !(level <= 3 && (entry & large))) {
            uint64_t child = clone(entry & address_mask, level - 1);
            if (!child) return 0;
            entry = (entry & ~address_mask) | child;
        }
        target[i] = entry;
    }
    return copy;
}
bool unmap(uint64_t address) {
    uint64_t* table = direct(table_root);
    for (unsigned level = 4; level > 1; --level) {
        auto& entry = table[(address >> (12 + 9 * (level - 1))) & 511];
        if (!(entry & present)) return true;
        if (entry & large) {
            // Split inherited 1 GiB/2 MiB mappings when a guard falls inside one.
            if (level == 4) return false;
            uint64_t child = allocate_table();
            if (!child) return false;
            uint64_t step = uint64_t(1) << (12 + 9 * (level - 2));
            uint64_t base = (entry & address_mask) & ~(step * 512 - 1);
            uint64_t flags = entry & ~address_mask;
            if (level == 2) {
                flags &= ~large;
                if (entry & (1 << 12)) flags |= 1 << 7; // huge PAT -> 4 KiB PAT
            } else if (entry & (1 << 12)) flags |= 1 << 12;
            for (unsigned i = 0; i < 512; ++i) direct(child)[i] = (base + i * step) | flags;
            // Retain access restrictions and cache settings on the new pointer.
            entry = child | (entry & (0x800000000000003fULL));
        }
        table = direct(entry & address_mask);
    }
    table[(address >> 12) & 511] = 0;
    return true;
}
void rollback() {
    while (count) pool->release(tables[--count]);
    table_root = 0;
}
}
namespace paging {
bool initialize(PageAllocator& allocator, uint64_t direct_map) {
    if (table_root) return false;
    uint64_t cr4, original;
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    if (cr4 & (1 << 12)) return false; // only four-level paging supported
    asm volatile("mov %%cr3, %0" : "=r"(original));
    pool = &allocator; offset = direct_map;
    table_root = clone(original & address_mask, 4);
    if (!table_root) { rollback(); return false; }
    char* guards[] = {kernel_stack_guard, df_stack_guard, nmi_stack_guard, mc_stack_guard};
    for (auto* guard : guards) {
        if (!unmap(reinterpret_cast<uint64_t>(guard))) { rollback(); return false; }
    }
    if (!unmap(0)) { rollback(); return false; }
    // Clear global translations too; changing CR3 alone need not flush them.
    uint64_t without_global = cr4 & ~uint64_t(1 << 7);
    asm volatile("mov %0, %%cr4; mov %1, %%cr3; mov %2, %%cr4"
        : : "r"(without_global), "r"(table_root), "r"(cr4) : "memory");
    uint64_t cr0; asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 1 << 16; // Honor read-only mappings in supervisor mode.
    asm volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
    return true;
}
uint64_t root() { return table_root; }
size_t table_count() { return count; }
bool mapped(uint64_t address) {
    if (!table_root) return false;
    uint64_t* table = direct(table_root);
    for (unsigned level = 4; level; --level) {
        uint64_t entry = table[(address >> (12 + 9 * (level - 1))) & 511];
        if (!(entry & present)) return false;
        if (level == 1 || (level <= 3 && (entry & large))) return true;
        table = direct(entry & address_mask);
    }
    return false;
}
}

namespace {
constexpr uint64_t user_bit = 4, nx = uint64_t(1) << 63;
uint64_t user_frame(paging::AddressSpace& space) {
    if (space.count == 256) return 0;
    uint64_t frame = pool->allocate();
    if (frame) {
        space.frames[space.count++] = frame;
        for (unsigned i = 0; i < 512; ++i) direct(frame)[i] = 0;
    }
    return frame;
}
uint64_t* user_pte(const paging::AddressSpace& space, uint64_t address) {
    if (!space.cr3 || address < paging::user_min || address >= paging::user_end) return nullptr;
    uint64_t* table = direct(space.cr3);
    for (unsigned level = 4; level > 1; --level) {
        uint64_t entry = table[(address >> (12 + 9 * (level - 1))) & 511];
        if ((entry & 5) != 5 || (entry & large)) return nullptr;
        table = direct(entry & address_mask);
    }
    return &table[(address >> 12) & 511];
}
}
namespace paging {
bool create(AddressSpace& space) {
    if (space.cr3) return false;
    space.count = 0;
    space.cr3 = user_frame(space);
    if (!space.cr3) return false;
    for (unsigned i = 256; i < 512; ++i) direct(space.cr3)[i] = direct(table_root)[i] & ~user_bit;
    return true;
}
bool map(AddressSpace& space, uint64_t address, bool writable, bool executable) {
    if (address < user_min || address >= user_end || address % 4096 || (writable && executable)) return false;
    auto* table = direct(space.cr3);
    for (unsigned level = 4; level > 1; --level) {
        auto& entry = table[(address >> (12 + 9 * (level - 1))) & 511];
        if (!(entry & present)) {
            uint64_t frame = user_frame(space); if (!frame) return false;
            entry = frame | 7;
        }
        if (!(entry & user_bit) || (entry & large)) return false;
        table = direct(entry & address_mask);
    }
    auto& entry = table[(address >> 12) & 511];
    if (entry & present) return false;
    uint64_t frame = user_frame(space); if (!frame) return false;
    entry = frame | 5 | (writable ? 2 : 0) | (executable ? 0 : nx);
    return true;
}
void destroy(AddressSpace& space) {
    // Caller must switch away before releasing a live root.
    while (space.count) pool->release(space.frames[--space.count]);
    space.cr3 = 0;
}
void activate(const AddressSpace& space) {
    uint64_t cr3 = space.cr3 ? space.cr3 : table_root;
    asm volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
}
bool user_range(const AddressSpace& space, uint64_t address, size_t size, bool writing) {
    if (address < user_min || address >= user_end || size > user_end - address) return false;
    for (size_t i = 0; i < size;) {
        auto* pte = user_pte(space, address + i);
        if (!pte || (*pte & 5) != 5 || (writing && !(*pte & 2))) return false;
        size_t step = 4096 - ((address + i) & 4095);
        i += step < size - i ? step : size - i;
    }
    return true;
}
bool copy_from(const AddressSpace& space, void* to, uint64_t from, size_t size) {
    if (!user_range(space, from, size, false)) return false;
    auto* target = static_cast<uint8_t*>(to);
    for (size_t i = 0; i < size;) {
        uint64_t entry = *user_pte(space, from + i);
        auto* source = reinterpret_cast<uint8_t*>(direct(entry & address_mask)) + ((from + i) & 4095);
        size_t step = 4096 - ((from + i) & 4095); if (step > size - i) step = size - i;
        for (size_t j = 0; j < step; ++j) target[i + j] = source[j];
        i += step;
    }
    return true;
}
bool copy_to(const AddressSpace& space, uint64_t to, const void* from, size_t size, bool loading) {
    if (!user_range(space, to, size, !loading)) return false;
    auto* source = static_cast<const uint8_t*>(from);
    for (size_t i = 0; i < size;) {
        uint64_t entry = *user_pte(space, to + i);
        auto* target = reinterpret_cast<uint8_t*>(direct(entry & address_mask)) + ((to + i) & 4095);
        size_t step = 4096 - ((to + i) & 4095); if (step > size - i) step = size - i;
        for (size_t j = 0; j < step; ++j) target[j] = source[i + j];
        i += step;
    }
    return true;
}
}
