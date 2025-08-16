#include "core.h"

#include <stdio.h>
#include <stdbool.h>

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
cstr_from_dynstr(const char* src, Allocator* allocator)
{
    char* cstr = make(char, array_len(src) + 1, allocator);
    memcpy(cstr, src, array_len(src));
    cstr[array_len(src)] = '\0';
    return cstr;
}

char*
dynstr_from_cstr(const char* cstr, size_t capacity, Allocator* allocator)
{
    size_t len = strlen(cstr);

    char* arr = array(char, capacity, allocator);
    array_ensure_capacity(arr, len + 1, sizeof(char));
    memcpy(arr, cstr, len);
    arr[len] = '\0';

    return arr;
}

char*
dynstr_init(size_t capacity, Allocator* a)
{
    char* arr = array(char, capacity, a);
    array_append(arr, '\0');

    return arr;
}

void
dynstr_append_c(char* dest, char src)
{
    array_ensure_capacity(dest, 1, sizeof(char));
    size_t dest_str_len = dynstr_len(dest);
    dest[dest_str_len] = src;
    dest[dest_str_len + 1] = '\0';
    array_header(dest)->length += 1;
}

void
dynstr_append(char* dest, const char* src)
{
    size_t len = strlen(src);
    array_ensure_capacity(dest, len, sizeof(char));
    memcpy(dest + len, src, len);
    dest[dynstr_len(dest)] = '\0';
}

void
dynstr_set(char* dest, const char* src)
{
    size_t src_len = dynstr_len(src);
    int diff = src_len - dynstr_len(dest);
    if (diff > 0) {
        array_ensure_capacity(dest, diff, sizeof(char));
    }

    memcpy(dest, src, src_len);
    dest[src_len] = '\0';
    array_len(dest) = src_len + 1;
}

void
dynstr_clear(char* str)
{
    array_len(str) = 1;
    str[0] = '\0';
}

#define FNV_OFFSET 14695981039346656037UL
#define FNV_PRIME 1099511628211UL

// From: https://benhoyt.com/writings/hash-table-in-c/
// Return 64-bit FNV-1a hash for key (NUL-terminated). See description:
// https://en.wikipedia.org/wiki/Fowler–Noll–Vo_hash_function
uint64_t hash_str(const char* key) {
    uint64_t hash = FNV_OFFSET;
    for (const char* p = key; *p; p++) {
        hash ^= (uint64_t)(unsigned char)(*p);
        hash *= FNV_PRIME;
    }
    return hash;
}

void
hashmap_clear(Hashmap* hashmap)
{
    for (int i = 0; i < hashmap->capacity; i++) {
        hashmap->records[i].type = EMPTY;
        hashmap->records[i].key = NULL;
        hashmap->records[i].value = NULL;
    }
    hashmap->length = 0;
}

Hashmap
hashmap_init(size_t capacity, Allocator* allocator)
{
    Hashmap hashmap;
    hashmap.records =
      allocator->alloc(sizeof(HashmapRecord) * capacity, allocator->context);
    hashmap.capacity = capacity;
    hashmap.length = 0;
    for (int i = 0; i < hashmap.capacity; i++) {
        hashmap.records[i].type = EMPTY;
        hashmap.records[i].key = NULL;
        hashmap.records[i].value = NULL;
    }
    return hashmap;
}

bool
hashmap_insert(Hashmap* hashmap, char* key, void* value)
{
    if (value == NULL)
        return false;

    u16 idx = hash_str(key) % hashmap->capacity;
    for (int i = 0; i < hashmap->capacity; i++) {
        HashmapRecord* record =
          &hashmap->records[(idx + i) % hashmap->capacity];

        if (record->type == EMPTY || record->type == DELETED) {
            record->key = key;
            record->value = value;
            record->type = FILLED;
            hashmap->length++;
            return true;
        } else if (record->type == FILLED && strcmp(record->key, key) == 0) {
            return false;
        }
    }

    return false;
}

void*
hashmap_get(Hashmap* hashmap, char* key)
{
    u16 hash = hash_str(key) % hashmap->capacity;
    for (int i = 0; i < hashmap->capacity; i++) {
        u16 idx = (hash + i) % hashmap->capacity;
        HashmapRecord* record = &hashmap->records[idx];
        if (record->type == EMPTY) {
            return NULL;
        }
        if (record->type == DELETED) {
            continue;
        }

        if (strcmp(key, record->key) == 0) {
            return record->value;
        }
    }

    return NULL;
}

void*
hashmap_delete(Hashmap* hashmap, char* key)
{
    u16 hash = hash_str(key) % hashmap->capacity;
    for (int i = 0; i < hashmap->capacity; i++) {
        u16 idx = (hash + i) % hashmap->capacity;
        HashmapRecord* record = &hashmap->records[idx];
        if (record->type == EMPTY) {
            return NULL;
        }
        if (record->type == DELETED) {
            continue;
        }

        if (strcmp(key, record->key) == 0) {
            void* temp = record->value;
            record->type = DELETED;
            record->key = NULL;
            record->value = NULL;
            return temp;
        }
    }

    return NULL;
}

void
hashmap_print(Hashmap* hashmap)
{
    printf("----START----\n");
    for (int i = 0; i < hashmap->capacity; i++) {
        HashmapRecord* record = &hashmap->records[i];
        if (record->type == EMPTY)
            printf("(%d) EMPTY\n", i);
        else if (record->type == DELETED)
            printf("(%d) DELETED\n", i);
        else
            printf("(%d) %s: %s\n", i, record->key, (char*)record->value);
    }
    printf("----END----\n");
}

//
// int
// main(void)
// {
//     Arena arena;
//     void* base = malloc(1024);
//     arena_init(&arena, base, 1024);
//     Allocator allocator = arena_alloc_init(&arena);
//
//     Hashmap hashmap = hashmap_make(10, &allocator);
//     char* names[10] = { "Arda",  "Cemal", "Orhan", "Kemal",    "Abdulkerim",
//                         "Semih", "Cemil", "Seda",  "Muhittin", "Asu" };
//     hashmap_print(&hashmap);
//     for (int i = 0; i < 10; i++) {
//         hashmap_insert(&hashmap, names[i], names[i]);
//         hashmap_print(&hashmap);
//         char* val = hashmap_get(&hashmap, names[i]);
//         assert(strcmp(val, names[i]) == 0);
//         hashmap_delete(&hashmap, names[i]);
//         hashmap_print(&hashmap);
//         val = hashmap_get(&hashmap, names[i]);
//         assert(val == NULL);
//     }
//
//     free(base);
//     return 0;
// }
