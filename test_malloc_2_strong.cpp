//
// strong tests for malloc_2 (metadata + free-list reuse, sbrk-only)
// PDF: Part 2, pages 6-9
//
// compile:
//   g++ malloc_2.cpp test_malloc_2_strong.cpp -o test2_strong
//   ./test2_strong
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


void test_smalloc_illegal_args() {
    SECTION("smalloc illegal arguments (page 7)");

    size_t before = _num_allocated_blocks();
    CHECK(smalloc(0) == NULL, "smalloc(0) returns NULL");
    CHECK(smalloc(100000001) == NULL, "smalloc(size > 1e8) returns NULL");
    CHECK(_num_allocated_blocks() == before, "rejected requests do not create a block");
}


void test_scalloc_illegal_args() {
    SECTION("scalloc illegal arguments (page 7)");

    CHECK(scalloc(0, 10) == NULL, "scalloc(num=0, size=10) returns NULL");
    CHECK(scalloc(10, 0) == NULL, "scalloc(num=10, size=0) returns NULL");
    CHECK(scalloc(1000000, 1000) == NULL, "scalloc(num*size > 1e8) returns NULL");
}


void test_scalloc_zero_init() {
    SECTION("scalloc zero-initialization (page 7)");

    size_t n = 25;
    char* buf = (char*)scalloc(n, sizeof(char));
    CHECK(buf != NULL, "scalloc succeeds for a valid request");

    bool all_zero = true;
    for (size_t i = 0; i < n; i++) if (buf[i] != 0) all_zero = false;
    CHECK(all_zero, "scalloc-allocated memory is zero-initialized");

    for (size_t i = 0; i < n; i++) buf[i] = 0xFF;
    sfree(buf);

    char* buf2 = (char*)scalloc(n, sizeof(char));
    CHECK(buf2 == buf, "the freed block is reused for the next scalloc of the same size");
    all_zero = true;
    for (size_t i = 0; i < n; i++) if (buf2[i] != 0) all_zero = false;
    CHECK(all_zero, "reused (dirtied) block is correctly re-zeroed by scalloc");
    sfree(buf2);
}


void test_sfree_safety() {
    SECTION("sfree safety - NULL and double free (page 8)");

    sfree(NULL);
    g_pass++; printf("  [PASS] sfree(NULL) does not crash\n");

    void* p = smalloc(64);
    size_t free_before = _num_free_blocks();
    sfree(p);
    CHECK(_num_free_blocks() == free_before + 1, "sfree marks exactly one additional block as free");

    sfree(p); // double free - spec: "if already released, simply returns"
    CHECK(_num_free_blocks() == free_before + 1, "double sfree() does not double-count the block as free again");
}


void test_reuse_first_fit_ascending() {
    SECTION("blocks are reused in ascending-address order (page 9, note 2)");

    // three distinct blocks, allocated in order -> their addresses are
    // naturally ascending, since this design only ever grows via sbrk
    void* a = smalloc(30);
    void* b = smalloc(30);
    void* c = smalloc(30);
    CHECK(a < b && b < c, "sequential allocations land at ascending addresses");

    sfree(a);
    sfree(c);
    // both a and c are free and big enough; per the ascending-order rule,
    // a new request for 30 bytes must reuse 'a' (the lower address), not 'c'
    void* d = smalloc(30);
    CHECK(d == a, "a new allocation reuses the LOWEST free address, not just any free block");

    sfree(b);
    sfree(c);
    sfree(d);
}


void test_oversized_block_reuse_and_fragmentation() {
    SECTION("oversized free block is reused whole, size field never shrinks (page 9, notes 1 & 12)");
    // NOTE: this test must run on a pristine heap (nothing allocated before it
    // anywhere in this program run) - see the ordering in main().

    void* big = smalloc(1000);
    CHECK(big != NULL, "large allocation succeeds");

    size_t free_bytes_before = _num_free_bytes();
    sfree(big);
    CHECK(_num_free_bytes() == free_bytes_before + 1000, "freeing restores exactly 1000 free bytes");

    size_t allocated_bytes_before = _num_allocated_bytes();
    void* small = smalloc(10); // much smaller than the freed 1000-byte block
    CHECK(small == big, "the tiny request reuses the SAME oversized freed block (page 9 note 3)");
    CHECK(_num_allocated_bytes() == allocated_bytes_before,
          "reusing an existing block does not change total allocated bytes (metadata size field never shrinks, note 1)");
    CHECK(_num_free_bytes() == free_bytes_before,
          "free bytes drops by the FULL 1000 bytes, not just the 10 requested (note 12) - the whole block left is used");

    sfree(small);
}


void test_srealloc_edge_cases() {
    SECTION("srealloc argument edge cases (page 8)");

    CHECK(srealloc(NULL, 0) == NULL, "srealloc(NULL, 0) returns NULL");
    void* p = srealloc(NULL, 100);
    CHECK(p != NULL, "srealloc(NULL, valid size) behaves like smalloc");

    CHECK(srealloc(p, 0) == NULL, "srealloc(validPtr, 0) returns NULL");
    CHECK(srealloc(p, 100000001) == NULL, "srealloc(validPtr, size > 1e8) returns NULL");

    // per spec: "do not free oldp if srealloc() fails"
    memset(p, 0x5A, 100);
    CHECK(((unsigned char*)p)[0] == 0x5A, "original block remains valid/untouched after a failed srealloc");
    sfree(p);
}


void test_srealloc_shrink_reuses_same_block() {
    SECTION("srealloc: size <= current size reuses same block (page 8)");

    void* p = smalloc(300);
    memset(p, 0x77, 300);

    void* p2 = srealloc(p, 150);
    CHECK(p2 == p, "srealloc to a smaller size returns the SAME pointer");
    CHECK(((unsigned char*)p2)[0] == 0x77 && ((unsigned char*)p2)[149] == 0x77,
          "data preserved when reusing the block in place");

    void* p3 = srealloc(p2, 300); // exactly equal to original size -> still reuse
    CHECK(p3 == p2, "srealloc to the EXACT same size still reuses the same block");

    sfree(p3);
}


void test_srealloc_grow_copies_and_frees_old() {
    SECTION("srealloc grow: allocates new space, copies data, frees old block (page 8)");
    // NOTE: sizes chosen larger than anything the pristine-heap test above could
    // have left free (max 1000), so this can't accidentally reuse that leftover
    // block instead of genuinely forcing a move - see ordering in main().

    void* p = smalloc(1200);
    memset(p, 0x33, 1200);

    size_t free_blocks_before = _num_free_blocks();
    void* p2 = srealloc(p, 5000); // bigger than current block, forces a move
    CHECK(p2 != NULL, "srealloc grow succeeds");
    CHECK(p2 != p, "srealloc to a larger size does NOT reuse the old (too-small) block");

    bool data_ok = true;
    for (int i = 0; i < 1200; i++) if (((unsigned char*)p2)[i] != 0x33) data_ok = false;
    CHECK(data_ok, "original 1200 bytes of data correctly copied into the new block");

    CHECK(_num_free_blocks() == free_blocks_before + 1,
          "the old block was automatically freed after the move, becoming a new free block");

    sfree(p2);
}


void test_stats_invariants() {
    SECTION("stats function invariants (functions 5-10, page 8)");

    CHECK(_num_meta_data_bytes() == _num_allocated_blocks() * _size_meta_data(),
          "_num_meta_data_bytes() == _num_allocated_blocks() * _size_meta_data()");
    CHECK(_num_free_blocks() <= _num_allocated_blocks(), "free blocks never exceed allocated blocks");
    CHECK(_num_free_bytes() <= _num_allocated_bytes(), "free bytes never exceed allocated bytes");

    void* ptrs[5];
    for (int i = 0; i < 5; i++) ptrs[i] = smalloc(20 * (i + 1));
    CHECK(_num_meta_data_bytes() == _num_allocated_blocks() * _size_meta_data(),
          "metadata-bytes invariant holds after multiple fresh allocations");

    for (int i = 0; i < 5; i++) sfree(ptrs[i]);
    CHECK(_num_free_blocks() == _num_allocated_blocks(),
          "after freeing everything just allocated, all currently tracked blocks are free");
}


int main() {
    printf("=========================================\n");
    printf(" strong tests for malloc_2\n");
    printf("=========================================\n");

    // illegal-arg tests create zero blocks, safe to run anytime
    test_smalloc_illegal_args();
    test_scalloc_illegal_args();

    // these two need a pristine (or at least controlled) heap state to make
    // absolute assertions - must run before any test below leaves stray free
    // blocks that could get reused ahead of what these tests expect
    test_oversized_block_reuse_and_fragmentation();
    test_srealloc_grow_copies_and_frees_old();

    // remaining tests use relative/delta-based assertions, so they're safe
    // to run regardless of what leftover free blocks exist by this point
    test_scalloc_zero_init();
    test_sfree_safety();
    test_reuse_first_fit_ascending();
    test_srealloc_edge_cases();
    test_srealloc_shrink_reuses_same_block();
    test_stats_invariants();

    printf("\n=========================================\n");
    printf(" RESULTS: %d passed, %d failed\n", g_pass, g_fail);
    printf("=========================================\n");
    return g_fail == 0 ? 0 : 1;
}
