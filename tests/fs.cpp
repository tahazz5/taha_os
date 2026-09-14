#include "../kernel/fs.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <vector>
static std::vector<unsigned char> disk(16 * 1024 * 1024);
static int writes_left = -1;
bool read_sector(uint32_t sector, void* bytes) {
    if (uint64_t(sector) * 512 + 512 > disk.size()) return false;
    std::memcpy(bytes, disk.data() + sector * 512, 512); return true;
}
bool write_sector(uint32_t sector, const void* bytes) {
    if (writes_left == 0 || uint64_t(sector) * 512 + 512 > disk.size()) return false;
    if (writes_left > 0) --writes_left;
    std::memcpy(disk.data() + sector * 512, bytes, 512); return true;
}
bool flush() { return true; }
void mount() { fs::initialize(); assert(fs::mount({read_sector, write_sector, flush})); }
int main() {
    std::memcpy(disk.data(), "TAHADK01", 8);
    mount();
    assert(fs::write("/bin/oops", "x", 1) == abi::denied);
    assert(fs::write("/home/../oops", "x", 1) == abi::invalid);
    assert(fs::write("/home//oops", "x", 1) == abi::invalid);
    assert(fs::write("/home/missing/file", "x", 1) == abi::missing);
    assert(fs::mkdir("/home/docs") == 0);
    assert(fs::write("/home/docs/note", "hello", 5) == 5);
    assert(fs::write("/tmp/transient", "x", 1) == 1);
    assert(fs::remove("/home/docs") == abi::denied);
    assert(fs::remove("/home") == abi::denied);
    abi::Entry entries[64];
    assert(fs::list("/home/docs", entries, 64) == 1);
    assert(std::strcmp(entries[0].path, "/home/docs/note") == 0);
    char buffer[64]{};
    mount();
    assert(fs::read("/home/docs/note", buffer, sizeof(buffer)) == 5);
    assert(std::memcmp(buffer, "hello", 5) == 0);
    assert(fs::read("/tmp/transient", buffer, sizeof(buffer)) == abi::missing);
    writes_left = 1; // Invalidation succeeds, then payload write fails.
    assert(fs::write("/home/docs/note", "lost", 4) == abi::io);
    assert(fs::read("/home/docs/note", buffer, sizeof(buffer)) == 5);
    assert(std::memcmp(buffer, "hello", 5) == 0);
    writes_left = -1;
    mount();
    assert(fs::read("/home/docs/note", buffer, sizeof(buffer)) == 5);
    assert(fs::write("/home/docs/note", "new", 3) == 3);
    // Corrupt the newest payload; mount must fall back to the old bank.
    uint64_t g0, g1;
    std::memcpy(&g0, disk.data() + 512 + 8, 8);
    std::memcpy(&g1, disk.data() + 8193 * 512 + 8, 8);
    disk[(g1 > g0 ? 8194 : 2) * 512 + 1] ^= 0x80;
    mount();
    assert(fs::read("/home/docs/note", buffer, sizeof(buffer)) == 5);
    assert(fs::remove("/home/docs/note") == 0);
    assert(fs::remove("/home/docs") == 0);
    mount();
    assert(fs::read("/home/docs/note", buffer, sizeof(buffer)) == abi::missing);
    std::fill(disk.begin(), disk.end(), 0x5a);
    fs::initialize(); assert(!fs::mount({read_sector, write_sector, flush}));
    assert(!fs::persistent()); assert(!fs::sync());
    assert(disk[512] == 0x5a);
    puts("Filesystem tests passed (paths, persistence, rollback, corrupt-bank recovery)");
}
