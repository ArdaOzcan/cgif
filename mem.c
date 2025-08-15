#include "mem.h"

#include <stdio.h>

void
arena_init(Arena* arena, void* base, size_t size)
{
    arena->base = base;
    arena->size = size;
    arena->used = 0;
}

void*
arena_push_size(Arena* arena, size_t size)
{
    arena->used += size;
    if (arena->used > arena->size) {
        printf("Arena is full\n");
        return NULL;
    }

    return arena->base + arena->used - size;
}

void
arena_copy_size(Arena* arena, const void* data, size_t size)
{
    memcpy(arena_push_size(arena, size), data, size);
}

void*
ArenaAlloc_(size_t bytes, void* context)
{
    return arena_push_size((Arena*)context, bytes);
}

void
ArenaFree_(size_t bytes, void* ptr, void* context)
{
}

void*
array_init(size_t item_size, size_t capacity, Allocator* allocator)
{
    size_t size = item_size * capacity + sizeof(ArrayHeader);
    ArrayHeader* header = allocator->alloc(size, allocator->context);

    void* ptr = NULL;
    if (header) {
        header->capacity = capacity;
        printf("Array initialized with capacity %zu\n", capacity);
        header->length = 0;
        header->allocator = allocator;
        ptr = header + 1;
    }

    return ptr;
}

void*
array_check_cap(void* arr, size_t added_count, size_t item_size)
{
    ArrayHeader* header = array_header(arr);

    size_t desired_capacity = header->length + added_count;
    if (desired_capacity > header->capacity) {
        // Realloc array
        size_t new_capacity = 2 * header->capacity;
        while (new_capacity < desired_capacity) {
            new_capacity *= 2;
        }

        size_t new_size = sizeof(ArrayHeader) + new_capacity * item_size;
        ArrayHeader* new_header =
          header->allocator->alloc(new_size, header->allocator->context);

        if (new_header) {
            size_t old_size =
              sizeof(ArrayHeader) + header->capacity * item_size;
            printf("Reallocing array from %zu bytes to %zu bytes.\n",
                   old_size,
                   new_size);
            memcpy(new_header, header, old_size);

            if (header->allocator->free) {
                header->allocator->free(
                  old_size, header, header->allocator->context);
            }

            new_header->capacity = new_capacity;
            return new_header + 1;
        } else {
            return NULL;
        }
    }

    return arr;
}

