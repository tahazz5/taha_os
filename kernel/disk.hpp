#pragma once
#include <stdint.h>
namespace disk {
bool initialize();
bool read(uint32_t sector, void* buffer);
bool write(uint32_t sector, const void* buffer);
bool flush();
uint32_t sectors();
}
