#include "elf.hpp"
#include "strings.hpp"
namespace elf {
struct Header {
    uint8_t magic[16]; uint16_t type, machine; uint32_t version;
    uint64_t entry, phoff, shoff; uint32_t flags;
    uint16_t ehsize, phsize, phnum, shsize, shnum, shstrings;
};
struct Segment { uint32_t type, flags; uint64_t offset, address, physical, filesz, memsz, align; };
static_assert(sizeof(Header) == 64 && sizeof(Segment) == 56);
bool load(paging::AddressSpace& space, const uint8_t* bytes, size_t size, uint64_t& entry) {
    if (!bytes || size < sizeof(Header)) return false;
    Header h; strings::bytes(&h, bytes, sizeof(h));
    if (h.magic[0] != 0x7f || h.magic[1] != 'E' || h.magic[2] != 'L' || h.magic[3] != 'F' ||
        h.magic[4] != 2 || h.magic[5] != 1 || h.magic[6] != 1 || h.type != 2 || h.machine != 62 ||
        h.version != 1 || h.ehsize != sizeof(Header) || h.phsize != sizeof(Segment) ||
        !h.phnum || h.phnum > 16 || h.phoff > size || size - h.phoff < h.phnum * sizeof(Segment)) return false;
    bool entry_ok = false;
    Segment segments[16];
    for (unsigned i = 0; i < h.phnum; ++i) {
        auto& p = segments[i]; strings::bytes(&p, bytes + h.phoff + i * sizeof(Segment), sizeof(p));
        if (p.type == 2 || p.type == 3) return false;
        if (p.type != 1 || !p.memsz) continue;
        if (p.filesz > p.memsz || p.offset > size || p.filesz > size - p.offset ||
            p.address < paging::user_min || p.address >= 0x700000 || p.memsz > 0x700000 - p.address ||
            (p.flags & 3) == 3 || !(p.flags & 4) || (p.flags & ~7u) ||
            p.address % 4096 != p.offset % 4096 ||
            (p.align > 1 && ((p.align & (p.align - 1)) || p.address % p.align != p.offset % p.align))) return false;
        uint64_t start = p.address & ~uint64_t(4095), end = (p.address + p.memsz + 4095) & ~uint64_t(4095);
        for (unsigned j = 0; j < i; ++j) if (segments[j].type == 1 && segments[j].memsz) {
            uint64_t left = segments[j].address & ~uint64_t(4095);
            uint64_t right = (segments[j].address + segments[j].memsz + 4095) & ~uint64_t(4095);
            if (start < right && end > left) return false;
        }
        if ((p.flags & 1) && h.entry >= p.address && h.entry < p.address + p.filesz) entry_ok = true;
    }
    if (!entry_ok || !paging::create(space)) return false;
    for (unsigned i = 0; i < h.phnum; ++i) {
        auto& p = segments[i]; if (p.type != 1 || !p.memsz) continue;
        uint64_t end = (p.address + p.memsz + 4095) & ~uint64_t(4095);
        for (uint64_t address = p.address & ~uint64_t(4095); address < end; address += 4096)
            if (!paging::map(space, address, p.flags & 2, p.flags & 1)) { paging::destroy(space); return false; }
        if (!paging::copy_to(space, p.address, bytes + p.offset, p.filesz, true)) { paging::destroy(space); return false; }
    }
    // A 64 KiB user stack with an unmapped page below it.
    for (uint64_t address = 0x7f0000; address < paging::user_end; address += 4096)
        if (!paging::map(space, address, true, false)) { paging::destroy(space); return false; }
    entry = h.entry; return true;
}
}
