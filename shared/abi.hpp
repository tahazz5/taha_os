#pragma once
#include <stdint.h>
#include <stddef.h>
namespace abi {
constexpr size_t path_max = 96, file_max = 32768;
enum Call : uint64_t { console_write, console_read, exit, spawn, wait, sleep, yield, ticks,
    file_read, file_write, list, mkdir, unlink, stat, processes, kill, diagnostic, sync, reboot };
struct Entry { char path[path_max]; uint32_t size; uint8_t directory, readonly; uint8_t reserved[2]; };
struct Process { uint64_t pid, parent, ticks; uint32_t state; char name[32]; uint32_t reserved; };
constexpr int64_t invalid = -1, missing = -2, full = -3, denied = -4, io = -5;
}
