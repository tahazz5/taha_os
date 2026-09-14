#include "api.hpp"
static char buffer[abi::file_max];
extern "C" int main(const char* path) {
    if (!*path) { os::print("Usage: run cat /absolute/path\n"); return 1; }
    auto n = os::readfile(path, buffer, sizeof(buffer));
    if (n < 0) { os::error(n); return 1; }
    os::write(buffer, n); return 0;
}
