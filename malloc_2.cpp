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

struct MallocMetadata {
    size_t size;
    bool is_free;

    MallocMetadata* next;
    MallocMetadata* prev;
};


MallocMetadata* global_head = NULL;




void* smalloc(size_t size) {
    if (size == 0 || size > 100000000) {
        return NULL;
    }

    MallocMetadata* curr = global_head;
    MallocMetadata* last = NULL;


    while (curr != NULL) {
        if (curr->is_free && curr->size >= size) {
            curr->is_free = false;
            return (void*)(curr + 1);
        }
        last = curr;
        curr = curr->next;
    }


    size_t total_size = size + sizeof(MallocMetadata);
    void* allocated = sbrk(total_size);

    if (allocated == (void*)(-1)) {
        return NULL;
    }


    MallocMetadata* new_metadata = (MallocMetadata*)allocated;
    new_metadata->size = size;
    new_metadata->is_free = false;
    new_metadata->next = NULL;
    new_metadata->prev = last;


    if (last != NULL) {
        last->next = new_metadata;
    }else {
        global_head = new_metadata;
    }

    return (void*)(new_metadata + 1);
}


void* scalloc(size_t num, size_t size) {
    void* allocated = smalloc(size * num);
    if (allocated == NULL) {
        return NULL;
    }

    std::memset(allocated, 0, size * num);
    return allocated;
}


void sfree(void* p) {
    if (p == NULL) {
        return;
    }


    MallocMetadata* ptr = (MallocMetadata*)p;
    ptr--;

    if (ptr->is_free == true) {
        return;
    }

    ptr->is_free = true;
}



void* srealloc(void* oldp, size_t size) {
    if (size == 0 || size > 100000000) {
        return NULL;
    }

    if (oldp == NULL) {
        return smalloc(size);
    }

    MallocMetadata* old_metadata = (MallocMetadata*)oldp;
    old_metadata--;

    if (size <= old_metadata->size) {
        return oldp;
    }

    void* ptr = smalloc(size);

    if (ptr == NULL) {
        return NULL;
    }

    std::memmove(ptr, oldp, old_metadata->size);

    sfree(oldp);

    return ptr;

}



size_t _num_free_blocks() {
    MallocMetadata* ptr = global_head;
    size_t num_free_blocks = 0;

    while (ptr != NULL) {
        if (ptr->is_free == true) {
            num_free_blocks++;
        }
        ptr = ptr->next;
    }

    return num_free_blocks;
}


size_t _num_free_bytes() {
    MallocMetadata* ptr = global_head;
    size_t num_free_bytes = 0;

    while (ptr != NULL) {
        if (ptr->is_free == true) {
            num_free_bytes += ptr->size;
        }
        ptr = ptr->next;
    }

    return num_free_bytes;
}

size_t _num_allocated_blocks() {
    MallocMetadata* ptr = global_head;
    size_t num_allocated_blocks = 0;

    while (ptr != NULL) {
        num_allocated_blocks++;
        ptr = ptr->next;
    }

    return num_allocated_blocks;
}


size_t _num_allocated_bytes() {
    MallocMetadata* ptr = global_head;
    size_t num_allocated_bytes = 0;

    while (ptr != NULL) {
        num_allocated_bytes += ptr->size;
        ptr = ptr->next;
    }

    return num_allocated_bytes;
}


size_t _num_meta_data_bytes() {
    return _num_allocated_blocks() * _size_meta_data();
}


size_t _size_meta_data() {
    return sizeof(MallocMetadata);
}