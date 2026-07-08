//
// Created by Omar Atme on 08/07/2026.
//


#include <unistd.h>

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


