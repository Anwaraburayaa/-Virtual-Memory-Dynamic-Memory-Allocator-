//
// strong tests for malloc_1 (naive smalloc via sbrk, no metadata, no free)
// PDF: Part 1, page 5
//
// malloc_1.cpp only defines smalloc() - no sfree/scalloc/srealloc/stats
// exist in this file, so this test only ever calls smalloc().
//
// compile:
//   g++ malloc_1.cpp test_malloc_1_strong.cpp -o test1_strong
//   ./test1_strong
//

#include <cstdio>
#include <cstddef>
#include <cstring>

void* smalloc(size_t size);

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, desc) do { \
    if (cond) { g_pass++; printf("  [PASS] %s\n", desc); } \
    else { g_fail++; printf("  [FAIL] %s (line %d)\n", desc, __LINE__); } \
} while(0)

#define SECTION(name) printf("\n== %s ==\n", name)


void test_illegal_args() {
    SECTION("illegal arguments (page 5)");

    CHECK(smalloc(0) == NULL, "smalloc(0) returns NULL");
    CHECK(smalloc(100000001) == NULL, "smalloc(size > 1e8) returns NULL");
}


void test_boundary() {
    SECTION("boundary - exactly 1e8 is legal (not 'more than')");

    // sbrk doesn't commit physical memory until touched, so a 100MB request
    // should succeed even on a modest VM
    void* p = smalloc(100000000);
    CHECK(p != NULL, "smalloc(exactly 1e8) succeeds");
}


void test_basic_write_read() {
    SECTION("basic allocation is usable memory");

    int* arr = (int*)smalloc(10 * sizeof(int));
    CHECK(arr != NULL, "smalloc for an int array succeeds");
    for (int i = 0; i < 10; i++) arr[i] = i * 7;
    bool ok = true;
    for (int i = 0; i < 10; i++) if (arr[i] != i * 7) ok = false;
    CHECK(ok, "written values are read back correctly");
}


void test_no_metadata_overhead() {
    SECTION("no metadata overhead - consecutive blocks are exactly adjacent (page 5)");

    // malloc_1 has NO metadata struct at all (that's introduced in Part 2) -
    // it's a raw sbrk(size) with nothing else, so two consecutive allocations
    // must be EXACTLY 'size' bytes apart, with zero header in between
    char* p1 = (char*)smalloc(64);
    char* p2 = (char*)smalloc(128);
    CHECK(p1 != NULL && p2 != NULL, "two consecutive smallocs both succeed");
    CHECK(p2 - p1 == 64, "second block starts EXACTLY 64 bytes after the first (no metadata gap)");
}


void test_multiple_distinct_allocations() {
    SECTION("multiple allocations never overlap");

    void* ptrs[20];
    for (int i = 0; i < 20; i++) ptrs[i] = smalloc(10 + i);
    bool all_ok = true;
    for (int i = 0; i < 20; i++) if (ptrs[i] == NULL) all_ok = false;
    CHECK(all_ok, "20 sequential allocations all succeed");

    // verify no two allocations were given the same address
    bool distinct = true;
    for (int i = 0; i < 20 && distinct; i++)
        for (int j = i + 1; j < 20; j++)
            if (ptrs[i] == ptrs[j]) distinct = false;
    CHECK(distinct, "no two allocations share the same address");

    // write a unique byte pattern into each block, then verify none overwrote a neighbor
    for (int i = 0; i < 20; i++) memset(ptrs[i], i, 10 + i);
    bool no_corruption = true;
    for (int i = 0; i < 20; i++) {
        unsigned char* c = (unsigned char*)ptrs[i];
        for (int j = 0; j < 10 + i; j++) if (c[j] != (unsigned char)i) no_corruption = false;
    }
    CHECK(no_corruption, "writing to each block does not corrupt any neighboring block");
}


int main() {
    printf("=========================================\n");
    printf(" strong tests for malloc_1\n");
    printf("=========================================\n");

    test_illegal_args();
    test_boundary();
    test_basic_write_read();
    test_no_metadata_overhead();
    test_multiple_distinct_allocations();

    printf("\n=========================================\n");
    printf(" RESULTS: %d passed, %d failed\n", g_pass, g_fail);
    printf("=========================================\n");
    return g_fail == 0 ? 0 : 1;
}
