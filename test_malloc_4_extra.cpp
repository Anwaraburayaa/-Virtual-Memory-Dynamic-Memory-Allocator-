//
// extra tests specifically for malloc_4 (Part 4, hugepage optimization)
// PDF: Part 4, Challenge 5, page 14
//
// IMPORTANT ENVIRONMENT NOTE:
// MAP_HUGETLB requires the OS to have huge pages reserved ahead of time.
// most VMs default to 0 reserved huge pages. check with:
//   cat /proc/meminfo | grep Huge
// if HugePages_Total is 0, the hugepage-triggering checks below may show
// mmap failures that are NOT bugs in your code, just missing VM config.
// to reserve some for testing (as root, temporary, not persistent):
//   sudo sysctl -w vm.nr_hugepages=64
//
// compile:
//   g++ malloc_4.cpp test_malloc_4_extra.cpp -o test4_extra
//   ./test4_extra
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

static const char* HUGEPAGE_NOTE =
    "requires OS huge pages reserved (check: cat /proc/meminfo | grep Huge); "
    "may legitimately fail on this VM without config, see file header comment";


void test_scalloc_hugepage_threshold() {
    SECTION("scalloc hugepage threshold (Challenge 5, page 14)");

    // per-element size well below the 2MB threshold -> normal path, must always work
    void* p1 = scalloc(4, 1000);
    CHECK(p1 != NULL, "scalloc with per-element size below SCALLOC_THRESHOLD succeeds via the normal path");
    if (p1) sfree(p1);

    // "if the size of one block is larger than 2MB" -> hugepage path
    size_t above = SCALLOC_THRESHOLD_T + 1;
    void* p2 = scalloc(1, above);
    INFO_CHECK(p2 != NULL, "scalloc with per-element size just above SCALLOC_THRESHOLD succeeds",
               HUGEPAGE_NOTE);

    if (p2) {
        char* c = (char*)p2;
        bool all_zero = true;
        for (size_t i = 0; i < 100; i++) if (c[i] != 0) all_zero = false;
        CHECK(all_zero, "hugepage-allocated scalloc memory is still zero-initialized");
        sfree(p2);
    }
}


void test_smalloc_hugepage_threshold() {
    SECTION("smalloc hugepage threshold (Challenge 5, page 14)");

    size_t before_blocks = _num_allocated_blocks();

    size_t huge_size = HUGE_THRESHOLD_T + 1000;
    void* p = smalloc(huge_size);
    INFO_CHECK(p != NULL, "smalloc just above HUGE_THRESHOLD (4MB) succeeds via hugepage mmap",
               HUGEPAGE_NOTE);

    if (p) {
        memset(p, 0x42, 100); // touch the memory to confirm it's actually usable, not just a valid pointer
        CHECK(((unsigned char*)p)[0] == 0x42, "hugepage-allocated memory is writable and readable");

        sfree(p);
        CHECK(_num_allocated_blocks() == before_blocks,
              "freeing the hugepage block returns _num_allocated_blocks() to its prior value (no leak in bookkeeping)");
    }
}


void test_below_huge_threshold_stays_regular_mmap() {
    SECTION("sizes below HUGE_THRESHOLD stay on regular mmap (Challenge 5 boundary)");

    // above 128KB (needs mmap) but below 4MB (must NOT use hugepages) -
    // this one has no environment dependency, so it's a hard CHECK, not INFO
    size_t regular_mmap_size = HUGE_THRESHOLD_T - 1000;
    void* p = smalloc(regular_mmap_size);
    CHECK(p != NULL, "smalloc just below HUGE_THRESHOLD succeeds via regular mmap, always works regardless of VM hugepage config");

    if (p) {
        memset(p, 0x33, 50);
        CHECK(((unsigned char*)p)[0] == 0x33, "regular (non-huge) large mmap block is usable");
        sfree(p);
    }
}


int main() {
    printf("=========================================\n");
    printf(" extra tests for malloc_4 (hugepage-specific)\n");
    printf("=========================================\n");
    printf("some checks below are marked [INFO] instead of [FAIL] because they\n");
    printf("depend on huge pages being reserved on this VM.\n");
    printf("check: cat /proc/meminfo | grep Huge\n");

    test_scalloc_hugepage_threshold();
    test_smalloc_hugepage_threshold();
    test_below_huge_threshold_stays_regular_mmap();

    printf("\n=========================================\n");
    printf(" RESULTS: %d passed, %d failed, %d info (env-dependent)\n", g_pass, g_fail, g_info);
    printf("=========================================\n");
    return g_fail == 0 ? 0 : 1;
}
