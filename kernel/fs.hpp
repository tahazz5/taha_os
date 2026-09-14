#pragma once
#include "../shared/abi.hpp"
namespace fs {
constexpr unsigned max_nodes = 64;
struct Storage {
    bool (*read)(uint32_t, void*);
    bool (*write)(uint32_t, const void*);
    bool (*flush)();
};
void initialize();
bool seed(const char* path, const void* data, size_t size, bool directory = false, bool readonly = true);
// Mount only our known format or an empty disk. Unknown/corrupt disks are not overwritten.
bool mount(Storage storage);
bool persistent();
int64_t read(const char* path, void* data, size_t capacity);
int64_t write(const char* path, const void* data, size_t size);
int64_t mkdir(const char* path);
int64_t remove(const char* path);
int64_t list(const char* directory, abi::Entry* entries, size_t capacity);
int64_t stat(const char* path, abi::Entry& entry);
bool sync();
const uint8_t* file(const char* path, size_t& size);
}
