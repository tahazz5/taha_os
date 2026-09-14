#pragma once
#include <stdint.h>
#include <stddef.h>

// Single-core allocator: interrupt handlers must not allocate. Two bitmaps
// distinguish reserved, free, and allocated frames below the 4 GiB ceiling.
class PageAllocator {
public:
    static constexpr uint64_t page_size = 4096;
    static constexpr size_t capacity = 1 << 20;
    static constexpr uint64_t address_limit = capacity * page_size;
    enum class AddResult { added, truncated, invalid, overlap, sealed };

    AddResult add(uint64_t base, uint64_t length) {
        if (sealed_) return AddResult::sealed;
        if (length > UINT64_MAX - base || base > UINT64_MAX - 4095)
            return AddResult::invalid;
        uint64_t end = (base + length) & ~uint64_t(4095);
        uint64_t start = (base + 4095) & ~uint64_t(4095);
        if (!start) start = page_size;
        bool truncated = end > address_limit;
        if (end > address_limit) end = address_limit;
        // Validate the entire range before changing state: malformed maps must
        // never make a live physical frame available to another allocation.
        for (uint64_t p = start / page_size; p < end / page_size; ++p)
            if (managed_[p / 64] & (uint64_t(1) << (p % 64))) return AddResult::overlap;
        for (uint64_t p = start / page_size; p < end / page_size; ++p) {
            managed_[p / 64] |= uint64_t(1) << (p % 64);
            free_[p / 64] |= uint64_t(1) << (p % 64);
            ++count_;
        }
        return truncated ? AddResult::truncated : AddResult::added;
    }
    uint64_t allocate() {
        sealed_ = true;
        while (hint_ < words && !free_[hint_]) ++hint_;
        if (hint_ == words) return 0;
        unsigned bit = __builtin_ctzll(free_[hint_]);
        free_[hint_] &= ~(uint64_t(1) << bit);
        ++allocated_;
        return (hint_ * 64 + bit) * page_size;
    }
    bool release(uint64_t address) {
        if (!address || address % page_size || address >= address_limit) return false;
        size_t frame = address / page_size, word = frame / 64;
        uint64_t bit = uint64_t(1) << (frame % 64);
        if (!(managed_[word] & bit) || (free_[word] & bit)) return false;
        free_[word] |= bit;
        --allocated_;
        if (word < hint_) hint_ = word;
        return true;
    }
    size_t total() const { return count_; }
    size_t available() const { return count_ - allocated_; }
private:
    static constexpr size_t words = capacity / 64;
    uint64_t managed_[words]{}, free_[words]{};
    size_t count_ = 0, allocated_ = 0, hint_ = 0;
    bool sealed_ = false;
};
