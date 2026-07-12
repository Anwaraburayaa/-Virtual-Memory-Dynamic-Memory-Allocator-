//
// strong tests for malloc_3 (buddy allocator) - goes beyond test_malloc_3_extra.cpp
// with deeper multi-level merges and stress scenarios.
// works against malloc_4.cpp too (same base API).
//
// compile:
//   g++ malloc_3.cpp test_malloc_3_strong.cpp -o test3_strong
//   ./test3_strong
//

#include <cstdio>
#include <cstddef>
#include <cstring>

void* smalloc(size_t size);
void* scalloc(size_t num, size_t size);
void sfree(void* p);
void* srealloc(void* oldp, size_t size);
size_t _num_free_blocks();
size_t _num_free_bytes();
size_t _num_allocated_blocks();
size_t _num_allocated_bytes();
size_t _num_meta_data_bytes();
size_t _size_meta_data();

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, desc) do { \
    if (cond) { g_pass++; printf("  [PASS] %s\n", desc); } \
    else { g_fail++; printf("  [FAIL] %s (line %d)\n", desc, __LINE__); } \
} while(0)

#define SECTION(name) printf("\n== %s ==\n", name)

static const size_t MAX_BLOCK_SIZE_T = 128 * 1024;
static const int NUM_INITIAL_BLOCKS_T = 32;


// helper: confirms the heap is back to a fully-merged, pristine state
void assert_heap_clean(const char* where) {
    CHECK(_num_allocated_blocks() == (size_t)NUM_INITIAL_BLOCKS_T, where);
}


void test_illegal_args_all_functions() {
    SECTION("illegal arguments across all functions (pages 5,7,8)");

    CHECK(smalloc(0) == NULL, "smalloc(0) -> NULL");
    CHECK(smalloc(100000001) == NULL, "smalloc(> 1e8) -> NULL");
    CHECK(scalloc(0, 100) == NULL, "scalloc(num=0) -> NULL");
    CHECK(scalloc(100, 0) == NULL, "scalloc(size=0) -> NULL");
    CHECK(scalloc(1000000, 1000) == NULL, "scalloc(num*size > 1e8) -> NULL");
    CHECK(srealloc(NULL, 0) == NULL, "srealloc(NULL, 0) -> NULL");
    CHECK(srealloc(NULL, 100000001) == NULL, "srealloc(NULL, > 1e8) -> NULL");

    assert_heap_clean("no illegal call left any block behind");
}


void test_deep_split_then_full_scramble_merge() {
    SECTION("deep split into 8 same-order blocks, freed in scrambled order, fully re-merges");

    // force splitting one 128KB block all the way down repeatedly, taking
    // 8 small allocations - by tightest-fit these all land at low orders,
    // spanning several buddy levels within the same original 128KB region
    void* blocks[8];
    for (int i = 0; i < 8; i++) blocks[i] = smalloc(50);
    bool all_ok = true;
    for (int i = 0; i < 8; i++) if (blocks[i] == NULL) all_ok = false;
    CHECK(all_ok, "all 8 small allocations succeed");

    // free in a scrambled (non-sequential, non-reverse) order to stress the
    // buddy-detection logic across multiple merge levels at once
    int order[8] = {3, 0, 5, 1, 7, 2, 6, 4};
    for (int i = 0; i < 8; i++) sfree(blocks[order[i]]);

    assert_heap_clean("scrambled-order freeing still fully collapses back to 32 top-level blocks");
}


void test_srealloc_two_level_merge() {
    SECTION("srealloc merges across TWO buddy levels, not just one");

    // p ends up at order 0 after a deep split, with its order-0 buddy free.
    // requesting a size that needs order 2 (512B) forces the merge loop to
    // run TWICE: order0->order1, then order1->order2 - this is the part of
    // Step 1/2 that a single-iteration test wouldn't catch.
    void* p = smalloc(10); // order 0, buddy free immediately after
    CHECK(p != NULL, "initial tiny smalloc succeeds");
    memset(p, 0x66, 10);

    // 400 bytes needs order 2 (512B usable) worth of space - safely beyond
    // order 1's usable range (256B - up to 64B metadata = 192B worst case)
    void* p2 = srealloc(p, 400);
    CHECK(p2 != NULL, "two-level-merge srealloc succeeds");
    CHECK(p2 == p, "address unchanged - merged via buddies without needing to relocate");
    CHECK(((unsigned char*)p2)[0] == 0x66 && ((unsigned char*)p2)[9] == 0x66,
          "data survives a two-level buddy merge");

    sfree(p2);
    assert_heap_clean("heap fully recovers after the two-level-merge test");
}


void test_stress_many_allocations_no_corruption() {
    SECTION("stress test - many varied allocations, interleaved frees, no data corruption");

    const int N = 40;
    void* ptrs[N];
    size_t sizes[N];

    // allocate a spread of sizes, small to medium, to exercise many orders at once
    for (int i = 0; i < N; i++) {
        sizes[i] = 20 + (i * 37) % 900; // pseudo-varied, deterministic sizes
        ptrs[i] = smalloc(sizes[i]);
    }
    bool all_ok = true;
    for (int i = 0; i < N; i++) if (ptrs[i] == NULL) all_ok = false;
    CHECK(all_ok, "40 varied-size allocations all succeed");

    // stamp each block with a unique, size-matching pattern
    for (int i = 0; i < N; i++) memset(ptrs[i], (i % 250) + 1, sizes[i]);

    // free every third one, freeing up buddies unevenly (interleaved pattern)
    for (int i = 0; i < N; i += 3) {
        sfree(ptrs[i]);
        ptrs[i] = NULL;
    }

    // verify every SURVIVING block's data is still intact - this is the real
    // test: a bug in split/merge bookkeeping would corrupt a neighbor here
    bool no_corruption = true;
    for (int i = 0; i < N; i++) {
        if (ptrs[i] == NULL) continue;
        unsigned char expected = (i % 250) + 1;
        unsigned char* c = (unsigned char*)ptrs[i];
        for (size_t j = 0; j < sizes[i]; j++) {
            if (c[j] != expected) { no_corruption = false; break; }
        }
    }
    CHECK(no_corruption, "all surviving blocks retain correct data after interleaved frees");

    // clean up the rest
    for (int i = 0; i < N; i++) if (ptrs[i] != NULL) sfree(ptrs[i]);

    assert_heap_clean("heap fully recovers after the stress test");
}


void test_srealloc_relocation_preserves_all_neighbors() {
    SECTION("srealloc relocation does not corrupt unrelated blocks");

    // three same-order allocations: p, q (p's buddy), and r (unrelated, from
    // a different original 128KB block, forced by first exhausting the first
    // block's order-0 slots)
    void* p = smalloc(10);
    void* q = smalloc(10); // p's buddy, per smalloc's reuse-immediate-buddy behavior
    void* r = smalloc(10); // needs a fresh split elsewhere since p/q's order is full

    CHECK(p != NULL && q != NULL && r != NULL, "three small allocations succeed");
    memset(p, 0xAA, 10);
    memset(q, 0xBB, 10);
    memset(r, 0xCC, 10);

    // grow p - its buddy q is occupied, so this must relocate elsewhere
    void* p2 = srealloc(p, 300);
    CHECK(p2 != NULL && p2 != p, "srealloc relocates p since its buddy q is busy");

    // q and r must be completely untouched by p's relocation
    CHECK(((unsigned char*)q)[0] == 0xBB, "q's data is untouched after p's relocation");
    CHECK(((unsigned char*)r)[0] == 0xCC, "r's data is untouched after p's relocation");
    CHECK(((unsigned char*)p2)[0] == 0xAA, "p's own data correctly followed it to the new location");

    sfree(p2);
    sfree(q);
    sfree(r);
    assert_heap_clean("heap fully recovers after the relocation-safety test");
}


void test_mmap_boundary_exact() {
    SECTION("exact mmap/buddy boundary (page 10-11)");

    size_t meta = _size_meta_data();

    void* fits = smalloc(MAX_BLOCK_SIZE_T - meta); // exactly fills one top-level block
    CHECK(fits != NULL, "request exactly filling one 128KB block uses the buddy system");
    sfree(fits);
    assert_heap_clean("buddy-path allocation cleans up fully");

    void* overflow = smalloc(MAX_BLOCK_SIZE_T - meta + 1); // one byte too many -> mmap
    CHECK(overflow != NULL, "one byte over the limit correctly falls through to mmap");
    sfree(overflow);
}


int main() {
    printf("=========================================\n");
    printf(" strong tests for malloc_3 / malloc_4\n");
    printf("=========================================\n");

    test_illegal_args_all_functions();
    test_deep_split_then_full_scramble_merge();
    test_srealloc_two_level_merge();
    test_stress_many_allocations_no_corruption();
    test_srealloc_relocation_preserves_all_neighbors();
    test_mmap_boundary_exact();

    printf("\n=========================================\n");
    printf(" RESULTS: %d passed, %d failed\n", g_pass, g_fail);
    printf("=========================================\n");
    return g_fail == 0 ? 0 : 1;
}
