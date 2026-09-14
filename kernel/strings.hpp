#pragma once
#include <stddef.h>
namespace strings {
inline size_t length(const char* s) { size_t n = 0; while (s[n]) ++n; return n; }
inline bool equal(const char* a, const char* b) { while (*a && *a == *b) { ++a; ++b; } return *a == *b; }
inline void copy(char* d, const char* s, size_t cap) {
    if (!cap) return;
    size_t n = 0; while (n + 1 < cap && s[n]) { d[n] = s[n]; ++n; } d[n] = 0;
}
inline void bytes(void* d, const void* s, size_t n) {
    for (size_t i = 0; i < n; ++i) static_cast<unsigned char*>(d)[i] = static_cast<const unsigned char*>(s)[i];
}
}
