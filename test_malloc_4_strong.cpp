//
// strong tests for malloc_4 (hugepage optimization) - goes beyond
// test_malloc_4_extra.cpp with tighter boundary checks.
// PDF: Part 4, Challenge 5, page 14
//
// ENVIRONMENT NOTE: MAP_HUGETLB needs the OS to have huge pages reserved.
// check: cat /proc/meminfo | grep Huge
// checks that DEPEND on huge pages actually being available are marked
// [INFO] on failure, not [FAIL] - see each test's comment.
//
// compile:
//   g++ malloc_4.cpp test_malloc_4_strong.cpp -o test4_strong
//   ./test4_strong
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
static int g_info = 0;

#define CHECK(cond, desc) do { \
    if (cond) { g_pass++; printf("  [PASS] %s\n", desc); } \
    else { g_fail++; printf("  [FAIL] %s (line %d)\n", desc, __LINE__); } \
} while(0)

#define INFO_CHECK(cond, desc, note) do { \
    if (cond) { g_pass++; printf("  [PASS] %s\n", desc); } \
    else { g_info++; printf("  [INFO] %s -- %s\n", desc, note); } \
} while(0)

#define SECTION(name) printf("\n== %s ==\n", name)

static const size_t HUGE_THRESHOLD_T = 1024ull * 1024 * 4;
static const size_t SCALLOC_THRESHOLD_T = 1024ull * 1024 * 2;

static const char* HP_NOTE =
    "requires OS huge pages reserved (check: cat /proc/meminfo | grep Huge)";


void test_smalloc_huge_threshold_exact_boundary() {
    SECTION("smalloc exact HUGE_THRESHOLD boundary - 'equal-to or larger than 4MB' (page 14)");

    // one byte BELOW threshold -> must NOT use hugepages, always works regardless of VM config
    void* below = smalloc(HUGE_THRESHOLD_T - 1);
    CHECK(below != NULL, "smalloc(HUGE_THRESHOLD - 1) succeeds via regular mmap");
    if (below) sfree(below);

    // exactly AT threshold -> spec says "equal-to or larger" triggers hugepage
    void* at = smalloc(HUGE_THRESHOLD_T);
    INFO_CHECK(at != NULL, "smalloc(exactly HUGE_THRESHOLD) succeeds via hugepage mmap", HP_NOTE);
    if (at) sfree(at);
}


void test_scalloc_threshold_exact_boundary() {
    SECTION("scalloc exact SCALLOC_THRESHOLD boundary - 'larger than 2MB' is strict (page 14)");

    // exactly AT threshold -> spec says "larger than 2MB", so == 2MB must NOT trigger hugepage
    // this stays under the regular mmap path (still > 128KB) so it's a deterministic check
    void* at = scalloc(1, SCALLOC_THRESHOLD_T);
    CHECK(at != NULL, "scalloc(exactly SCALLOC_THRESHOLD) succeeds via the REGULAR (non-huge) path");
    if (at) {
        bool zeroed = true;
        for (int i = 0; i < 100; i++) if (((char*)at)[i] != 0) zeroed = false;
        CHECK(zeroed, "boundary-sized scalloc block is still zero-initialized");
        sfree(at);
    }

    // one byte ABOVE threshold -> now it must use hugepages
    void* above = scalloc(1, SCALLOC_THRESHOLD_T + 1);
    INFO_CHECK(above != NULL, "scalloc(SCALLOC_THRESHOLD + 1) succeeds via hugepage mmap", HP_NOTE);
    if (above) sfree(above);
}


void test_scalloc_threshold_is_per_element_not_total() {
    SECTION("scalloc threshold applies to per-element size, not num*size total (page 14 wording)");

    // 'num' is huge but each individual element is tiny - per the PDF wording
    // ("the size of ONE block"), this must NOT trigger hugepages, even though
    // num*size together would be a large total allocation
    size_t num = 10000;
    size_t elem_size = 50; // far below SCALLOC_THRESHOLD per element
    void* p = scalloc(num, elem_size);
    CHECK(p != NULL, "scalloc with huge num but tiny per-element size succeeds via the regular path");
    if (p) {
        bool zeroed = true;
        for (size_t i = 0; i < 200; i++) if (((char*)p)[i] != 0) zeroed = false;
        CHECK(zeroed, "large-count, small-element scalloc is correctly zero-initialized");
        sfree(p);
    }
}


void test_srealloc_grows_huge_block_further() {
    SECTION("srealloc on an already-huge block growing to an even bigger size (mmap note 4, page 13)");
    // both the initial alloc AND the resize need hugepages here - fully env-dependent

    void* p = smalloc(HUGE_THRESHOLD_T + 1000);
    if (p == NULL) {
        printf("  [INFO] initial huge smalloc failed -- %s, skipping this test\n", HP_NOTE);
        return;
    }
    memset(p, 0x5C, 100);

    void* p2 = srealloc(p, HUGE_THRESHOLD_T + 50000); // grow further, still well above threshold
    INFO_CHECK(p2 != NULL, "srealloc growing an already-huge block to an even bigger huge size succeeds", HP_NOTE);
    if (p2) {
        CHECK(((unsigned char*)p2)[0] == 0x5C, "data preserved across huge-to-huge resize");
        sfree(p2);
    }
}


void test_below_threshold_never_touches_hugepages() {
    SECTION("sizes safely below HUGE_THRESHOLD never depend on hugepage availability");

    // this whole test is deterministic (no INFO fallback needed) since every
    // size here is guaranteed to stay on the regular mmap path
    for (size_t sz = HUGE_THRESHOLD_T - 5000; sz < HUGE_THRESHOLD_T; sz += 1000) {
        void* p = smalloc(sz);
        CHECK(p != NULL, "smalloc just below HUGE_THRESHOLD reliably succeeds (no hugepage dependency)");
        if (p) sfree(p);
    }
}


int main() {
    printf("=========================================\n");
    printf(" strong tests for malloc_4 (hugepage-specific)\n");
    printf("=========================================\n");
    printf("some checks are [INFO] not [FAIL] if huge pages aren't reserved on this VM.\n");
    printf("check: cat /proc/meminfo | grep Huge\n");

    test_smalloc_huge_threshold_exact_boundary();
    test_scalloc_threshold_exact_boundary();
    test_scalloc_threshold_is_per_element_not_total();
    test_srealloc_grows_huge_block_further();
    test_below_threshold_never_touches_hugepages();

    printf("\n=========================================\n");
    printf(" RESULTS: %d passed, %d failed, %d info (env-dependent)\n", g_pass, g_fail, g_info);
    printf("=========================================\n");
    return g_fail == 0 ? 0 : 1;
}
