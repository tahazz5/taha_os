#include <stddef.h>
extern "C" void* memcpy(void* destination, const void* source, size_t count) {
    auto* d = static_cast<unsigned char*>(destination);
    auto* s = static_cast<const unsigned char*>(source);
    for (size_t i = 0; i < count; ++i) d[i] = s[i];
    return destination;
}
extern "C" void* memset(void* destination, int value, size_t count) {
    auto* d = static_cast<unsigned char*>(destination);
    for (size_t i = 0; i < count; ++i) d[i] = value;
    return destination;
}
extern "C" void* memmove(void* destination, const void* source, size_t count) {
    auto* d = static_cast<unsigned char*>(destination);
    auto* s = static_cast<const unsigned char*>(source);
    if (reinterpret_cast<size_t>(d) < reinterpret_cast<size_t>(s))
        for (size_t i = 0; i < count; ++i) d[i] = s[i];
    else while (count) { --count; d[count] = s[count]; }
    return destination;
}
extern "C" int memcmp(const void* a, const void* b, size_t n) {
    auto* left = static_cast<const unsigned char*>(a);
    auto* right = static_cast<const unsigned char*>(b);
    for (size_t i = 0; i < n; ++i) if (left[i] != right[i]) return int(left[i]) - right[i];
    return 0;
}
