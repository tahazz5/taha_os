#include "../kernel/pages.hpp"
#include <cassert>
#include <cstdio>
#include <set>
#include <random>
static PageAllocator pool, full, empty, fragmented;
using Result = PageAllocator::AddResult;
int main() {
    assert(empty.allocate() == 0);
    assert(empty.add(4096, 4096) == Result::sealed);
    assert(!pool.release(4096));
    assert(pool.add(UINT64_MAX - 10, 100) == Result::invalid);
    assert(pool.add(0, 4096) == Result::added);
    assert(pool.total() == 0);
    assert(pool.add(4097, 16383) == Result::added);
    assert(pool.total() == 3);
    assert(pool.add(4096, 8192) == Result::overlap);
    assert(pool.total() == 3); // overlap rejection must be atomic
    auto a = pool.allocate(), b = pool.allocate(), c = pool.allocate();
    assert(a == 8192 && b == 12288 && c == 16384);
    assert(pool.allocate() == 0);
    assert(pool.add(65536, 4096) == Result::sealed);
    assert(!pool.release(a + 1));
    assert(!pool.release(PageAllocator::address_limit));
    assert(!pool.release(4096));
    assert(pool.release(b));
    assert(!pool.release(b));
    assert(pool.allocate() == b);
    assert(pool.release(a) && pool.release(b) && pool.release(c));
    assert(pool.available() == pool.total());
    assert(full.add(0, PageAllocator::address_limit + 4096) == Result::truncated);
    assert(full.total() == PageAllocator::capacity - 1);
    for (size_t i = 1; i < PageAllocator::capacity; ++i) assert(full.allocate() == i * 4096);
    assert(full.allocate() == 0);
    assert(full.release(4096));
    assert(full.allocate() == 4096); // hint must recover after full exhaustion
    assert(fragmented.add(0x100000, 128 * 4096) == Result::added);
    assert(fragmented.add(0x800000, 64 * 4096) == Result::added);
    std::mt19937 rng(42);
    std::set<uint64_t> live;
    for (unsigned i = 0; i < 10000; ++i) {
        if (!live.empty() && rng() % 2) {
            auto it = live.begin(); std::advance(it, rng() % live.size());
            assert(fragmented.release(*it)); live.erase(it);
        } else {
            auto page = fragmented.allocate();
            if (page) {
                assert((page >= 0x100000 && page < 0x180000) || (page >= 0x800000 && page < 0x840000));
                assert(live.insert(page).second);
            } else assert(live.size() == fragmented.total());
        }
        assert(fragmented.available() + live.size() == fragmented.total());
    }
    puts("Allocator tests passed (boundaries, overlap, exhaustion, randomized reuse)");
}
