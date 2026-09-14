#pragma once
#include <stdint.h>
#include <stddef.h>
#include "paging.hpp"
namespace elf {
bool load(paging::AddressSpace& space, const uint8_t* bytes, size_t size, uint64_t& entry);
}
