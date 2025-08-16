#include "mem.h"

#include <stdio.h>

Arena
arena_init(void* base, size_t size)
{
    Arena arena;
    arena.base = base;
    arena.size = size;
    arena.used = 0;
    return arena;
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
arena_alloc_(size_t bytes, void* context)
{
    return arena_push_size((Arena*)context, bytes);
}

void
arena_free_(size_t bytes, void* ptr, void* context)
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
array_ensure_capacity(void* arr, size_t added_count, size_t item_size)
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

char*
string_from_cstr(const char* cstr, size_t capacity, Allocator* allocator)
{
    size_t len = strlen(cstr);

    char* arr = array(char, capacity, allocator);
    array_ensure_capacity(arr, len + 1, sizeof(char));
    memcpy(arr, cstr, len);
    arr[len] = '\0';

    return arr;
}

char*
string_init(size_t capacity, Allocator* a)
{
    char* arr = array(char, capacity, a);
    array_append(arr, '\0');

    return arr;
}

void
string_append_c(char* dest, char src)
{
    array_ensure_capacity(dest, 1, sizeof(char));
    size_t dest_str_len = string_len(dest);
    dest[dest_str_len] = src;
    dest[dest_str_len + 1] = '\0';
    array_header(dest)->length += 1;
}

void
string_append(char* dest, const char* src)
{
    size_t len = strlen(src);
    array_ensure_capacity(dest, len, sizeof(char));
    memcpy(dest + len, src, len);
    dest[string_len(dest)] = '\0';
}

void
string_copy(char* dest, const char* src)
{
    size_t src_len = strlen(src);
    size_t diff = string_len(dest) - src_len;
    if (diff > 0) {
        array_ensure_capacity(dest, diff, sizeof(char));
    }

    memcpy(dest, src, src_len);
    dest[src_len] = '\0';
    array_len(dest) = src_len + 1;
}

void
string_clear(char* str)
{
    array_len(str) = 1;
    str[0] = '\0';
}
