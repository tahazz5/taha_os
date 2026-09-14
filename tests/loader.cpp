#include "../kernel/elf.hpp"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <vector>
#include <cstdio>
struct Page { unsigned char bytes[4096]{}; bool write, execute; };
static std::map<uint64_t, Page> test_pages;
static int fail_after = -1, destroys = 0;
namespace paging {
bool create(AddressSpace& space) { assert(!space.cr3); space.cr3 = 1; return true; }
bool map(AddressSpace&, uint64_t address, bool write, bool execute) {
    if (fail_after == 0) return false;
    if (fail_after > 0) --fail_after;
    assert(!test_pages.count(address)); test_pages[address].write = write; test_pages[address].execute = execute; return true;
}
void destroy(AddressSpace& space) { test_pages.clear(); space.cr3 = 0; ++destroys; }
bool copy_to(const AddressSpace&, uint64_t address, const void* source, size_t count, bool loading) {
    assert(loading);
    for (size_t i = 0; i < count; ++i) {
        assert(test_pages.count((address + i) & ~uint64_t(4095)));
        test_pages[(address + i) & ~uint64_t(4095)].bytes[(address + i) & 4095] = static_cast<const unsigned char*>(source)[i];
    }
    return true;
}
}
static void put(std::vector<uint8_t>& bytes, size_t offset, uint64_t value) {
    assert(offset + 8 <= bytes.size()); std::memcpy(bytes.data() + offset, &value, 8);
}
static uint64_t get(const std::vector<uint8_t>& bytes, size_t offset) {
    uint64_t value; std::memcpy(&value, bytes.data() + offset, 8); return value;
}
int main(int argc, char** argv) {
    assert(argc == 2);
    std::ifstream file(argv[1], std::ios::binary);
    std::vector<uint8_t> good{std::istreambuf_iterator<char>(file), {}};
    assert(!good.empty());
    paging::AddressSpace space{}; uint64_t entry = 0;
    assert(elf::load(space, good.data(), good.size(), entry));
    assert(entry >= paging::user_min && entry < 0x700000);
    assert(test_pages.at(entry & ~uint64_t(4095)).execute);
    assert(!test_pages.at(entry & ~uint64_t(4095)).write);
    assert(test_pages.at(0x7ff000).write && !test_pages.at(0x7ff000).execute);
    assert(!test_pages.count(0x7ef000));
    paging::destroy(space);
    auto reject = [&](std::vector<uint8_t> bad) {
        assert(!elf::load(space, bad.data(), bad.size(), entry));
        assert(!space.cr3 && test_pages.empty());
    };
    auto bad = good; bad[0] = 0; reject(bad);
    bad.assign(good.begin(), good.begin() + 32); reject(bad);
    bad = good; put(bad, 32, UINT64_MAX); reject(bad); // overflowed phoff
    auto phoff = get(good, 32);
    bad = good; bad[phoff + 4] = 7; reject(bad); // W+X
    bad = good; put(bad, phoff + 16, 0xffffffff80000000ULL); reject(bad);
    bad = good; put(bad, phoff + 40, UINT64_MAX); reject(bad); // memsz
    bad = good; put(bad, phoff + 32, UINT64_MAX); reject(bad); // filesz
    bad = good; put(bad, phoff + 48, 3); reject(bad); // invalid alignment
    bad = good; put(bad, 24, 0x7ff000); reject(bad); // stack entry point
    bad = good; put(bad, phoff + 56 + 16, get(good, phoff + 16)); reject(bad); // overlap
    int before = destroys; fail_after = 2; reject(good);
    assert(destroys == before + 1); fail_after = -1;
    puts("ELF loader tests passed (permissions, malformed images, bounds, rollback)");
}
