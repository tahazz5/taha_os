#include "fs.hpp"
#include "strings.hpp"
namespace {
struct Node { abi::Entry entry; bool used; uint8_t data[abi::file_max]; };
Node nodes[fs::max_nodes], backup;
constexpr uint32_t bank_sectors = 8192, max_payload = fs::max_nodes * (sizeof(abi::Entry) + abi::file_max);
alignas(8) uint8_t payload[max_payload + 512];
struct Header { char magic[8]; uint64_t generation; uint32_t size, checksum; uint8_t padding[488]; };
static_assert(sizeof(Header) == 512 && sizeof(abi::Entry) == 104);
fs::Storage storage{};
bool mounted = false;
uint64_t generation = 0;
unsigned active_bank = 1;

bool valid_path(const char* p) {
    if (!p || p[0] != '/') return false;
    size_t n = strings::length(p);
    if (n >= abi::path_max) return false;
    if (n == 1) return true;
    if (p[n - 1] == '/') return false;
    size_t component = 1;
    for (size_t i = 1; i <= n; ++i) {
        if (!p[i] || p[i] == '/') {
            size_t length = i - component;
            if (!length || (length == 1 && p[component] == '.') ||
                (length == 2 && p[component] == '.' && p[component + 1] == '.')) return false;
            component = i + 1;
        } else if (p[i] < 33 || p[i] > 126) return false;
    }
    return true;
}
int find(const char* path) {
    for (unsigned i = 0; i < fs::max_nodes; ++i)
        if (nodes[i].used && strings::equal(path, nodes[i].entry.path)) return i;
    return -1;
}
int empty() { for (unsigned i = 0; i < fs::max_nodes; ++i) if (!nodes[i].used) return i; return -1; }
int parent(const char* path) {
    char p[abi::path_max]; strings::copy(p, path, sizeof(p));
    size_t n = strings::length(p);
    while (n && p[n - 1] != '/') --n;
    p[n > 1 ? n - 1 : 1] = 0;
    return find(p);
}
bool home(const char* path) {
    const char* prefix = "/home/";
    for (unsigned i = 0; i < 6; ++i) if (path[i] != prefix[i]) return false;
    return true;
}
uint32_t checksum(const uint8_t* data, size_t n) {
    uint32_t sum = 2166136261u;
    for (size_t i = 0; i < n; ++i) { sum ^= data[i]; sum *= 16777619u; }
    return sum;
}
bool header_valid(const Header& h) {
    const char magic[8] = {'T','A','H','A','F','S','1',0};
    for (unsigned i = 0; i < 8; ++i) if (h.magic[i] != magic[i]) return false;
    return h.size <= max_payload && h.generation;
}
bool load_payload(unsigned bank, const Header& header) {
    for (uint32_t sector = 0; sector < (header.size + 511) / 512; ++sector)
        if (!storage.read(2 + bank * bank_sectors + sector, payload + sector * 512)) return false;
    if (checksum(payload, header.size) != header.checksum) return false;
    // Validate the whole snapshot before applying any entry, including paths,
    // duplicates, directory ancestry and lengths. Nothing may replace /bin.
    size_t cursor = 0; unsigned total = 0;
    abi::Entry parsed[fs::max_nodes];
    while (cursor < header.size) {
        if (header.size - cursor < sizeof(abi::Entry) || total == fs::max_nodes) return false;
        strings::bytes(&parsed[total], payload + cursor, sizeof(abi::Entry)); cursor += sizeof(abi::Entry);
        auto& e = parsed[total];
        if (e.path[abi::path_max - 1] || !valid_path(e.path) || !home(e.path) || e.readonly ||
            e.directory > 1 || e.size > abi::file_max || (e.directory && e.size) || e.size > header.size - cursor) return false;
        for (unsigned i = 0; i < total; ++i) if (strings::equal(parsed[i].path, e.path)) return false;
        cursor += e.size; ++total;
    }
    unsigned free = 0; for (auto& node : nodes) if (!node.used) ++free;
    if (total > free) return false;
    for (unsigned i = 0; i < total; ++i) {
        char path[abi::path_max]; strings::copy(path, parsed[i].path, sizeof(path));
        size_t n = strings::length(path); while (n && path[n - 1] != '/') --n; path[n - 1] = 0;
        bool found = strings::equal(path, "/home");
        for (unsigned j = 0; j < total; ++j)
            if (parsed[j].directory && strings::equal(parsed[j].path, path)) found = true;
        if (!found) return false;
    }
    cursor = 0;
    for (unsigned i = 0; i < total; ++i) {
        auto& node = nodes[empty()]; node.entry = parsed[i]; node.used = true;
        cursor += sizeof(abi::Entry);
        strings::bytes(node.data, payload + cursor, node.entry.size); cursor += node.entry.size;
    }
    return true;
}
int64_t change(const char* path, const void* data, size_t size, bool directory) {
    if (!valid_path(path) || size > abi::file_max) return abi::invalid;
    int p = parent(path);
    if (p < 0 || !nodes[p].entry.directory) return abi::missing;
    if (nodes[p].entry.readonly) return abi::denied;
    int index = find(path);
    if (index >= 0 && (nodes[index].entry.readonly || nodes[index].entry.directory || directory)) return abi::denied;
    if (index < 0) index = empty();
    if (index < 0) return abi::full;
    backup = nodes[index];
    auto& node = nodes[index]; node.entry = {}; node.used = true;
    strings::copy(node.entry.path, path, sizeof(node.entry.path));
    node.entry.size = size; node.entry.directory = directory;
    if (size) strings::bytes(node.data, data, size);
    if (mounted && home(path) && !fs::sync()) { node = backup; return abi::io; }
    return size;
}
}
namespace fs {
void initialize() {
    for (auto& node : nodes) { node.used = false; node.entry = {}; }
    storage = {}; mounted = false; generation = 0; active_bank = 1;
    seed("/", nullptr, 0, true); seed("/bin", nullptr, 0, true);
    seed("/etc", nullptr, 0, true); seed("/home", nullptr, 0, true, false);
    seed("/tmp", nullptr, 0, true, false);
}
bool seed(const char* path, const void* data, size_t size, bool directory, bool readonly) {
    if (!valid_path(path) || size > abi::file_max || find(path) >= 0) return false;
    int index = empty(); if (index < 0) return false;
    auto& n = nodes[index]; n.used = true; n.entry = {};
    strings::copy(n.entry.path, path, sizeof(n.entry.path));
    n.entry.size = size; n.entry.directory = directory; n.entry.readonly = readonly;
    if (size) strings::bytes(n.data, data, size);
    return true;
}
bool mount(Storage device) {
    if (mounted || !device.read || !device.write || !device.flush) return false;
    storage = device;
    uint8_t label[512];
    if (!storage.read(0, label)) return false;
    const char signature[] = "TAHADK01";
    for (unsigned i = 0; i < 8; ++i) if (label[i] != signature[i]) return false;
    for (unsigned i = 8; i < 512; ++i) if (label[i]) return false;
    Header headers[2];
    if (!storage.read(1, &headers[0]) || !storage.read(1 + bank_sectors, &headers[1])) return false;
    unsigned first = headers[1].generation > headers[0].generation ? 1 : 0;
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        unsigned bank = first ^ attempt;
        if (header_valid(headers[bank]) && load_payload(bank, headers[bank])) {
            mounted = true; active_bank = bank; generation = headers[bank].generation; return true;
        }
    }
    const auto* bytes = reinterpret_cast<const uint8_t*>(headers);
    for (size_t i = 0; i < sizeof(headers); ++i) if (bytes[i]) return false;
    mounted = true; generation = 0; active_bank = 1; return true;
}
bool persistent() { return mounted; }
bool sync() {
    if (!mounted || generation == UINT64_MAX) return false;
    size_t size = 0;
    for (auto& node : nodes) if (node.used && home(node.entry.path)) {
        strings::bytes(payload + size, &node.entry, sizeof(node.entry)); size += sizeof(node.entry);
        strings::bytes(payload + size, node.data, node.entry.size); size += node.entry.size;
    }
    for (size_t i = size; i < ((size + 511) & ~size_t(511)); ++i) payload[i] = 0;
    Header header{};
    unsigned bank = active_bank ^ 1;
    // Invalidate target first, commit payload next, and publish metadata last.
    if (!storage.write(1 + bank * bank_sectors, &header) || !storage.flush()) return false;
    for (size_t sector = 0; sector < (size + 511) / 512; ++sector)
        if (!storage.write(2 + bank * bank_sectors + sector, payload + sector * 512)) return false;
    if (!storage.flush()) return false;
    strings::copy(header.magic, "TAHAFS1", sizeof(header.magic));
    header.generation = generation + 1; header.size = size; header.checksum = checksum(payload, size);
    if (!storage.write(1 + bank * bank_sectors, &header) || !storage.flush()) return false;
    active_bank = bank; generation = header.generation; return true;
}
int64_t read(const char* path, void* data, size_t capacity) {
    int i = find(path); if (i < 0) return abi::missing;
    auto& n = nodes[i]; if (n.entry.directory) return abi::invalid;
    size_t size = n.entry.size < capacity ? n.entry.size : capacity;
    strings::bytes(data, n.data, size); return size;
}
const uint8_t* file(const char* path, size_t& size) {
    int i = find(path); if (i < 0 || nodes[i].entry.directory) return nullptr;
    size = nodes[i].entry.size; return nodes[i].data;
}
int64_t write(const char* path, const void* data, size_t size) { return change(path, data, size, false); }
int64_t mkdir(const char* path) { return change(path, nullptr, 0, true); }
int64_t remove(const char* path) {
    int i = find(path); if (i < 0) return abi::missing;
    if (nodes[i].entry.readonly || strings::equal(path, "/home") || strings::equal(path, "/tmp")) return abi::denied;
    for (auto& node : nodes) if (node.used && parent(node.entry.path) == i) return abi::denied;
    backup = nodes[i]; nodes[i].used = false;
    if (mounted && home(path) && !sync()) { nodes[i] = backup; return abi::io; }
    return 0;
}
int64_t stat(const char* path, abi::Entry& entry) {
    int i = find(path); if (i < 0) return abi::missing;
    entry = nodes[i].entry; return 0;
}
int64_t list(const char* directory, abi::Entry* entries, size_t capacity) {
    int p = find(directory); if (p < 0 || !nodes[p].entry.directory) return abi::missing;
    size_t count = 0;
    for (auto& node : nodes) if (node.used && !strings::equal(node.entry.path, directory) && parent(node.entry.path) == p) {
        if (count == capacity) break;
        entries[count++] = node.entry;
    }
    return count;
}
}
