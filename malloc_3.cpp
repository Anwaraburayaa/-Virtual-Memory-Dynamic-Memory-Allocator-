//
// Created by Omar Atme on 09/07/2026.
//


#define MAX_ORDER 10
#define MAX_BLOCK_SIZE (128 *1024)
#define NUM_INITIAL_BLOCKS 32
#define ALLOCATION_SIZE (NUM_INITIAL_BLOCKS * MAX_BLOCK_SIZE)


#include "os_malloc.h"

struct MallocMetadata {
    size_t size;
    bool is_free;

    MallocMetadata* next;
    MallocMetadata* prev;
};


static MallocMetadata* free_blocks[MAX_ORDER + 1];
static MallocMetadata* mmap_blocks = NULL;
static bool is_initialized = false;

static size_t num_blocks = 0;
static size_t num_mmap_blocks = 0;




bool initialize_metadata() {
    for (size_t i = 0; i <= MAX_ORDER; i++) {
        free_blocks[i] = NULL;
    }

    void* curr_break = sbrk(0);
    uintptr_t curr_addr = (uintptr_t)curr_break;


    size_t offset = curr_addr % ALLOCATION_SIZE;

    if (offset != 0) {
        offset = ALLOCATION_SIZE - offset;
        void* res = sbrk(offset);
        if (res == (void*)(-1)) {
            return false;
        }
    }


    void* memory_list = sbrk(ALLOCATION_SIZE);
    if (memory_list == (void*)(-1)){
        return false;
    }

    MallocMetadata* prev = NULL;
    MallocMetadata* curr = (MallocMetadata*)memory_list;

    for (size_t i = 0; i < NUM_INITIAL_BLOCKS; i++) {
        curr->size = MAX_BLOCK_SIZE;
        curr->is_free = true;
        curr->next = NULL;
        curr->prev = prev;

        if (prev != NULL) {
            prev->next = curr;
        }

        prev = curr;
        curr = (MallocMetadata*)((char*)curr + MAX_BLOCK_SIZE);
    }

    free_blocks[MAX_ORDER] = (MallocMetadata*)memory_list;
    is_initialized = true;
    num_blocks = 32;
    return true;
}


void insertToListByAddress(MallocMetadata* block, int order) {
    MallocMetadata* curr = free_blocks[order];

    //The list is empty
    if (curr == NULL) {
        block->next = NULL;
        block->prev = NULL;
        free_blocks[order] = block;
        return;
    }

    //Addition at the head
    if ((uintptr_t)block < (uintptr_t)curr) {
        block->next = curr;
        block->prev = NULL;
        curr->prev = block;
        free_blocks[order] = block;
        return;
    }

    //We need to walk through the list to find the correct spot
    while (curr->next != NULL && (uintptr_t)curr->next < (uintptr_t)block) {
        curr = curr->next;
    }

    block->next = curr->next;
    block->prev = curr;

    if (curr->next != NULL) {
        curr->next->prev = block;
    }
    curr->next = block;
}


void* smalloc(size_t size) {
    if (is_initialized == false) {
        if (!initialize_metadata()) {
            return NULL;
        }
    }

    if (size == 0 || size > 100000000) {
        return NULL;
    }

    size_t total_size = size + _size_meta_data();

    if (total_size > MAX_BLOCK_SIZE) {
        void* res = mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (res == MAP_FAILED) {
            return NULL;
        }

        MallocMetadata* ptr = (MallocMetadata*)res;
        ptr->size = total_size;
        ptr->is_free = false;
        ptr->prev = NULL;
        ptr->next = mmap_blocks;

        if (mmap_blocks != NULL) {
            mmap_blocks->prev = ptr;
        }

        mmap_blocks = ptr;
        num_mmap_blocks++;
        return (void*)(ptr + 1);
    }



    int order_wanted = 0;
    size_t current_size = 128;


    //we find the order of the minimal size that is bigger than the total size
    while (current_size < total_size && order_wanted <= MAX_ORDER) {
        order_wanted++;
        current_size *= 2;
    }

    if (order_wanted > MAX_ORDER) {
        return NULL;
    }

    int order_found = order_wanted;

    //although we found the right order, the list at this order might be empty, so we search in bigger orders until we
    //find a non-empty list
    while (order_found <= MAX_ORDER && free_blocks[order_found] == NULL) {
        order_found++;
    }


    //if the condition is true, the heap is full
    if (order_found > MAX_ORDER) {
        return NULL;
    }


    MallocMetadata* target_block = free_blocks[order_found];
    free_blocks[order_found] = target_block->next;

    if (free_blocks[order_found] != NULL) {
        free_blocks[order_found]->prev = NULL;
    }

    target_block->next = NULL;
    target_block->prev = NULL;


    while (order_found > order_wanted) {
        target_block->size = target_block->size / 2;
        size_t new_size = target_block->size;

        MallocMetadata* right_block = (MallocMetadata*)((char*)target_block + new_size);
        right_block->size = new_size;
        right_block->is_free = true;

        int lower_order = order_found - 1;
        insertToListByAddress(right_block, lower_order);

        num_blocks++;
        order_found--;
    }

    target_block->is_free = false;

    return (void*)(target_block + 1);

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

}



void* srealloc(void* oldp, size_t size) {

}



size_t _num_free_blocks() {
    size_t count = 0;

    for (int i = 0; i <= MAX_ORDER; i++) {
        MallocMetadata* curr = free_blocks[i];
        while (curr != NULL) {
            count++;
            curr = curr->next;
        }
    }
    return count;
}


size_t _num_free_bytes() {
    size_t total_bytes = 0;

    for (int i = 0; i <= MAX_ORDER; i++) {
        MallocMetadata* curr = free_blocks[i];
        while (curr != NULL) {
            total_bytes += (curr->size - _size_meta_data());
            curr = curr->next;
        }
    }
    return total_bytes;
}

size_t _num_allocated_blocks() {
    return num_blocks + num_mmap_blocks;
}


size_t _num_allocated_bytes() {
   
}


size_t _num_meta_data_bytes() {
    return _num_allocated_blocks() * _size_meta_data();
}


size_t _size_meta_data() {
    return sizeof(MallocMetadata);
}
