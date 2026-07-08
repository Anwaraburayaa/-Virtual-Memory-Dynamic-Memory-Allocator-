//
// Created by Omar Atme on 08/07/2026.
//



struct MallocMetadata {
    size_t size;
    bool is_free;

    MallocMetadata* next;
    MallocMetadata* prev;
};
