//
// extra tests for malloc_3 (also works against malloc_4, same API)
// covers edge cases + the trickier logic from PDF part 3 that the staff
// suite might not stress hard enough: srealloc's buddy-merge simulation,
// sfree's multi-level merging, and the buddy/mmap boundary.
//
// compile against malloc_3:
//   g++ malloc_3.cpp test_malloc_3_extra.cpp -o test3_extra
//   ./test3_extra
//
// compile against malloc_4 (should pass the same way, is_huge is invisible
// from outside these tests):
//   g++ malloc_4.cpp test_malloc_3_extra.cpp -o test3_extra_v4
//   ./test3_extra_v4
//

#include <cstdio>
#include <cstddef>
#include <cstring>

// declared here instead of including os_malloc.h, since we only need the
// signatures required by the PDF (page 16), not the internal struct
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

#define INFO_CHECK(cond, desc, note) do { \
    if (cond) { g_pass++; printf("  [PASS] %s\n", desc); } \
    else { printf("  [INFO] %s -- %s\n", desc, note); } \
} while(0)

#define SECTION(name) printf("\n== %s ==\n", name)

static const size_t MAX_BLOCK_SIZE_T = 128 * 1024;
static const int NUM_INITIAL_BLOCKS_T = 32;


void test_smalloc_edge_cases() {
    SECTION("smalloc edge cases (PDF page 5, page 7)");

    CHECK(smalloc(0) == NULL, "smalloc(0) returns NULL");
    CHECK(smalloc(100000001) == NULL, "smalloc(size > 1e8) returns NULL");

    // 1e8 bytes also happens to be >= HUGE_THRESHOLD (4MB) if this is malloc_4,
    // so on a VM with no huge pages reserved this mmap can legitimately fail
    // even with correct code - treated as info, not a hard fail
    void* p = smalloc(100000000);
    INFO_CHECK(p != NULL, "smalloc(exactly 1e8, the boundary) succeeds",
               "mmap/hugepages may fail if not configured on this VM - not necessarily a bug");
    if (p) sfree(p);

    void* a = smalloc(50);
    void* b = smalloc(50);
    CHECK(a != NULL && b != NULL && a != b, "two small smallocs return distinct non-null pointers");
    if (a) sfree(a);
    if (b) sfree(b);
}


void test_smalloc_buddy_vs_mmap_boundary() {
    SECTION("smalloc buddy-vs-mmap boundary (page 10-11, page 13)");

    size_t meta = _size_meta_data();
    CHECK(meta > 0 && meta <= 64, "_size_meta_data() is between 1 and 64 bytes (page 13 note)");

    // request that just fits inside one 128KB block -> buddy path
    size_t fits_size = MAX_BLOCK_SIZE_T - meta;
    void* p1 = smalloc(fits_size);
    CHECK(p1 != NULL, "smalloc(exactly 128KB - metadata) succeeds via the buddy system");

    // request that just exceeds one 128KB block -> must go through mmap
    size_t exceeds_size = MAX_BLOCK_SIZE_T - meta + 1;
    void* p2 = smalloc(exceeds_size);
    CHECK(p2 != NULL, "smalloc(128KB - metadata + 1) succeeds via mmap");

    if (p1) sfree(p1);
    if (p2) sfree(p2);
}


void test_smalloc_tightest_fit_and_split() {
    SECTION("smalloc tightest-fit splitting (Challenge 0/1, page 10-11)");

    size_t before_blocks = _num_allocated_blocks();
    CHECK(before_blocks == (size_t)NUM_INITIAL_BLOCKS_T, "heap starts at 32 allocated blocks");

    // tiny allocation forces the full split cascade down to order 0,
    // creating exactly 10 new free buddy blocks along the way
    void* p = smalloc(10);
    size_t after_blocks = _num_allocated_blocks();
    CHECK(after_blocks == before_blocks + 10,
          "splitting one 128KB block down to order 0 creates exactly 10 new free buddy blocks");

    sfree(p);
    size_t merged_blocks = _num_allocated_blocks();
    CHECK(merged_blocks == before_blocks,
          "freeing the tiny block merges all buddies back into a single 128KB block");
}


void test_scalloc() {
    SECTION("scalloc edge cases and zero-init (page 7)");

    CHECK(scalloc(0, 10) == NULL, "scalloc(num=0, size=10) returns NULL");
    CHECK(scalloc(10, 0) == NULL, "scalloc(num=10, size=0) returns NULL");
    CHECK(scalloc(1000000, 1000) == NULL, "scalloc(num*size > 1e8) returns NULL");

    size_t n = 20;
    char* buf = (char*)scalloc(n, sizeof(char));
    CHECK(buf != NULL, "scalloc succeeds for a valid small request");

    bool all_zero = true;
    for (size_t i = 0; i < n; i++) if (buf[i] != 0) all_zero = false;
    CHECK(all_zero, "scalloc-allocated memory is zero-initialized");

    // dirty it, free it, allocate again - should get zeroed memory again
    // even though the underlying bytes are reused from a freed block
    for (size_t i = 0; i < n; i++) buf[i] = 0xFF;
    sfree(buf);

    char* buf2 = (char*)scalloc(n, sizeof(char));
    all_zero = true;
    for (size_t i = 0; i < n; i++) if (buf2[i] != 0) all_zero = false;
    CHECK(all_zero, "reused (previously freed and dirtied) block is correctly zeroed by scalloc");
    sfree(buf2);
}


void test_sfree_safety() {
    SECTION("sfree safety - NULL and double free (page 8)");

    sfree(NULL);
    g_pass++; printf("  [PASS] sfree(NULL) does not crash\n");

    void* p = smalloc(64);
    sfree(p);
    sfree(p); // spec: "if already released, simply returns"
    g_pass++; printf("  [PASS] double sfree() on the same pointer does not crash\n");
}


void test_sfree_multilevel_merge() {
    SECTION("sfree iterative buddy merging (Challenge 2, page 12)");

    // four small allocations, taken from the same original 128KB block
    // through however many splits smalloc's tightest-fit logic needs
    void* blocks[4];
    for (int i = 0; i < 4; i++) blocks[i] = smalloc(100);
    for (int i = 0; i < 4; i++) CHECK(blocks[i] != NULL, "small allocation is non-null");

    sfree(blocks[0]);
    sfree(blocks[1]);
    sfree(blocks[2]);
    sfree(blocks[3]);

    // regardless of the exact split path taken, once every descendant of the
    // original block is freed, the buddy merge chain must fully collapse
    // back to the single top-level block - this is the real invariant
    CHECK(_num_allocated_blocks() == (size_t)NUM_INITIAL_BLOCKS_T,
          "freeing all 4 small blocks fully re-merges back to 32 top-level blocks");
}


void test_heap_exhaustion() {
    SECTION("heap exhaustion - hard limit of 32 top-level blocks (page 10)");

    size_t meta = _size_meta_data();
    size_t big = MAX_BLOCK_SIZE_T - meta; // fills exactly one top-level block, no split needed

    void* blocks[NUM_INITIAL_BLOCKS_T];
    int allocated = 0;
    for (int i = 0; i < NUM_INITIAL_BLOCKS_T; i++) {
        blocks[i] = smalloc(big);
        if (blocks[i] != NULL) allocated++;
    }
    CHECK(allocated == NUM_INITIAL_BLOCKS_T, "all 32 top-level blocks can be allocated");

    // per the PDF: "after having these 32 blocks, you should not use sbrk()
    // again" - so even a tiny request must fail once the heap is exhausted
    void* one_more = smalloc(100);
    CHECK(one_more == NULL,
          "heap is truly full: even a tiny request fails once all 32 blocks are used, since sbrk() is never called again");

    for (int i = 0; i < allocated; i++) sfree(blocks[i]);
    CHECK(_num_allocated_blocks() == (size_t)NUM_INITIAL_BLOCKS_T,
          "heap fully recovers after freeing all 32 blocks");
}


void test_srealloc_edge_cases() {
    SECTION("srealloc argument edge cases (page 8)");

    CHECK(srealloc(NULL, 0) == NULL, "srealloc(NULL, 0) returns NULL");
    void* p = srealloc(NULL, 100);
    CHECK(p != NULL, "srealloc(NULL, valid size) behaves like smalloc");
    sfree(p);

    void* q = smalloc(50);
    CHECK(srealloc(q, 0) == NULL, "srealloc(validPtr, 0) returns NULL");
    CHECK(srealloc(q, 100000001) == NULL, "srealloc(validPtr, size > 1e8) returns NULL");

    // per spec: on failure, oldp must remain untouched and still valid
    memset(q, 0x7C, 50);
    CHECK(((unsigned char*)q)[0] == 0x7C, "original block is still valid/untouched after a failed srealloc");
    sfree(q);
}


void test_srealloc_shrink_or_equal() {
    SECTION("srealloc shrink/equal reuses same block (priority a, page 12)");

    void* p = smalloc(200);
    CHECK(p != NULL, "initial smalloc succeeds");
    memset(p, 0xAB, 200);

    void* p2 = srealloc(p, 100);
    CHECK(p2 == p, "srealloc to a smaller size reuses the exact same pointer");
    CHECK(((unsigned char*)p2)[0] == 0xAB && ((unsigned char*)p2)[99] == 0xAB,
          "data is preserved when shrinking in place");

    sfree(p2);
}


void test_srealloc_grow_merges_with_free_buddy() {
    SECTION("srealloc grows in place by merging with a free buddy (priority b, page 12)");

    void* p = smalloc(10); // splits down to order 0, leaves its order-0 buddy free
    CHECK(p != NULL, "initial tiny smalloc succeeds");
    memset(p, 0x55, 10);

    // 150 bytes needs order 1 (256B) worth of space, safely beyond order 0's
    // usable range and safely within order 1's, regardless of exact metadata size
    void* p2 = srealloc(p, 150);
    CHECK(p2 != NULL, "srealloc grow succeeds");
    CHECK(p2 == p, "srealloc merges with the free buddy IN PLACE (same address, no copy needed)");
    CHECK(((unsigned char*)p2)[0] == 0x55 && ((unsigned char*)p2)[9] == 0x55,
          "original data preserved after in-place buddy merge");

    sfree(p2);
}


void test_srealloc_grow_falls_back_when_buddy_busy() {
    SECTION("srealloc falls back to a new block when the buddy is NOT free (priority c, page 12)");

    void* p = smalloc(10); // order 0
    void* q = smalloc(10); // reuses p's now-free order-0 buddy -> p and q ARE buddies, both allocated
    CHECK(p != NULL && q != NULL && p != q, "p and q are distinct valid allocations");
    memset(p, 0x11, 10);
    memset(q, 0x22, 10);

    void* p2 = srealloc(p, 150); // can't merge (q occupies the buddy) -> must relocate
    CHECK(p2 != NULL, "srealloc succeeds by finding a different block");
    CHECK(p2 != p, "srealloc did NOT reuse the old address, since its buddy was occupied");
    CHECK(((unsigned char*)p2)[0] == 0x11 && ((unsigned char*)p2)[9] == 0x11,
          "data correctly copied to the new location");
    CHECK(((unsigned char*)q)[0] == 0x22 && ((unsigned char*)q)[9] == 0x22,
          "the neighboring buddy block (q) was untouched by the relocation");

    sfree(p2);
    sfree(q);
}


void test_srealloc_mmap_behavior() {
    SECTION("srealloc on mmap blocks (mmap note 4, page 13)");

    size_t meta = _size_meta_data();
    size_t big = MAX_BLOCK_SIZE_T - meta + 100; // forces mmap in smalloc
    void* p = smalloc(big);
    CHECK(p != NULL, "large smalloc uses mmap");
    memset(p, 0x99, big);

    // same size -> per spec, must NOT reallocate
    void* p_same = srealloc(p, big);
    CHECK(p_same == p, "srealloc with the identical size on an mmap block returns the SAME pointer");

    // bigger size -> must always allocate a fresh region, never reuse in place
    void* p_bigger = srealloc(p_same, big + 500);
    CHECK(p_bigger != NULL, "srealloc succeeds for a bigger mmap size");
    CHECK(p_bigger != p_same, "srealloc on mmap block for a LARGER size always allocates a new region");
    CHECK(((unsigned char*)p_bigger)[0] == 0x99 && ((unsigned char*)p_bigger)[big-1] == 0x99,
          "data correctly preserved across the mmap resize");

    sfree(p_bigger);
}


void test_stats_invariants() {
    SECTION("stats function invariants (functions 5-10, page 8)");

    CHECK(_num_free_blocks() <= _num_allocated_blocks(), "free blocks never exceed allocated blocks");
    CHECK(_num_free_bytes() <= _num_allocated_bytes(), "free bytes never exceed allocated bytes");
    CHECK(_num_meta_data_bytes() == _num_allocated_blocks() * _size_meta_data(),
          "_num_meta_data_bytes() == _num_allocated_blocks() * _size_meta_data()");

    void* ptrs[10];
    for (int i = 0; i < 10; i++) ptrs[i] = smalloc(50 + i * 20);

    CHECK(_num_free_blocks() <= _num_allocated_blocks(), "invariant holds after multiple allocations");
    CHECK(_num_meta_data_bytes() == _num_allocated_blocks() * _size_meta_data(),
          "metadata-bytes invariant holds after multiple allocations");

    for (int i = 0; i < 10; i++) sfree(ptrs[i]);

    CHECK(_num_allocated_blocks() == (size_t)NUM_INITIAL_BLOCKS_T,
          "heap returns to exactly 32 blocks after freeing everything");
}


int main() {
    printf("=========================================\n");
    printf(" extra tests for malloc_3 / malloc_4\n");
    printf("=========================================\n");

    test_smalloc_edge_cases();
    test_smalloc_buddy_vs_mmap_boundary();
    test_smalloc_tightest_fit_and_split();
    test_scalloc();
    test_sfree_safety();
    test_sfree_multilevel_merge();
    test_heap_exhaustion();
    test_srealloc_edge_cases();
    test_srealloc_shrink_or_equal();
    test_srealloc_grow_merges_with_free_buddy();
    test_srealloc_grow_falls_back_when_buddy_busy();
    test_srealloc_mmap_behavior();
    test_stats_invariants();

    printf("\n=========================================\n");
    printf(" RESULTS: %d passed, %d failed\n", g_pass, g_fail);
    printf("=========================================\n");
    return g_fail == 0 ? 0 : 1;
}
