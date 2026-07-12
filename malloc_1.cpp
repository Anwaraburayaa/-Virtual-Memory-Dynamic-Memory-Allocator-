//
// Created by Omar Atme on 08/07/2026.
//


#include <unistd.h>
#include <cstring>
#include <cstdint>
#include <sys/mman.h>
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


void* smalloc(size_t size) {
    if (size == 0 || size > 100000000) {
        return NULL;
    }

    void* allocated = sbrk(size);

    if (allocated == (void*)(-1)) {
        return NULL;
    }

    return allocated;
}


