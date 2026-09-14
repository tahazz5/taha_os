#pragma once
#include "../shared/abi.hpp"
namespace os {
inline int64_t call(abi::Call n, uint64_t a = 0, uint64_t b = 0, uint64_t c = 0) {
    uint64_t result;
    asm volatile("int $0x80" : "=a"(result) : "a"(n), "D"(a), "S"(b), "d"(c) : "memory", "cc");
    return int64_t(result);
}
inline size_t length(const char* s) { size_t n = 0; while (s[n]) ++n; return n; }
inline bool equal(const char* a, const char* b) { while (*a && *a == *b) { ++a; ++b; } return *a == *b; }
inline void copy(char* d, const char* s, size_t cap) {
    if (!cap) return; size_t n = 0; while (n + 1 < cap && s[n]) { d[n] = s[n]; ++n; } d[n] = 0;
}
inline void write(const char* s, size_t n) {
    while (n) { size_t chunk = n < 4096 ? n : 4096; call(abi::console_write, uint64_t(s), chunk); s += chunk; n -= chunk; }
}
inline void print(const char* s) { write(s, length(s)); }
inline size_t decimal(char* out, uint64_t value) {
    char digits[21]; unsigned count = 0;
    do { digits[count++] = '0' + value % 10; value /= 10; } while (value);
    for (unsigned i = 0; i < count; ++i) out[i] = digits[count - i - 1];
    out[count] = 0; return count;
}
inline void number(uint64_t value) {
    char digits[21]; unsigned count = 0;
    do { digits[count++] = '0' + value % 10; value /= 10; } while (value);
    char out[21]; for (unsigned i = 0; i < count; ++i) out[i] = digits[count - i - 1]; write(out, count);
}
inline void error(int64_t code) {
    if (code >= 0) return;
    if (code == abi::missing) print("Not found.\n");
    else if (code == abi::denied) print("Permission denied or directory not empty.\n");
    else if (code == abi::full) print("Capacity reached.\n");
    else if (code == abi::io) print("Storage I/O failed.\n");
    else print("Invalid argument or executable.\n");
}
inline int64_t readfile(const char* path, void* data, size_t capacity) { return call(abi::file_read, uint64_t(path), uint64_t(data), capacity); }
inline int64_t writefile(const char* path, const void* data, size_t size) { return call(abi::file_write, uint64_t(path), uint64_t(data), size); }
}
