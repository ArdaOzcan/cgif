#ifndef MEM_H
#define MEM_H

#include <stdint.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;

#define KILOBYTE 1024
#define MEGABYTE 1024 * 1024

typedef struct
{
    void* (*alloc)(size_t bytes, void* context);
    void (*free)(size_t bytes, void* ptr, void* context);
    void* context;
} Allocator;

#define make(T, n, a) ((T*)((a)->alloc(sizeof(T) * n, (a)->context)))

typedef struct
{
    void* base;
    size_t used;
    size_t size;
} Arena;

Arena
arena_init(void* base, size_t size);

void*
arena_push_size(Arena* arena, size_t size);

#define arena_push_array(arena, type, length)                                  \
    (type*)arena_push_size(arena, sizeof(type) * length)

void
arena_copy_size(Arena* arena, const void* data, size_t size);

void*
arena_alloc_(size_t bytes, void* context);

void
arena_free_(size_t bytes, void* ptr, void* context);

#define arena_alloc_init(arena)                                                \
    (Allocator)                                                                \
    {                                                                          \
        arena_alloc_, arena_free_, arena                                         \
    }

typedef struct
{
    size_t capacity;
    size_t length;

    Allocator* allocator;
} ArrayHeader;

void*
array_init(size_t item_size, size_t capacity, Allocator* allocator);

#define array(type, cap, alloc) array_init(sizeof(type), cap, alloc)
#define array_header(a) ((ArrayHeader*)(a) - 1)
#define array_len(a) (array_header(a)->length)

void*
array_ensure_capacity(void* arr, size_t added_count, size_t item_size);

#define array_append(a, v)                                                     \
    ((a) = array_ensure_capacity(a, 1, sizeof(v)),                             \
     (a)[array_len(a)] = (v),                                                  \
     &(a)[array_len(a)++])

#define array_remove(a, i)                                                     \
    do {                                                                       \
        ArrayHeader* h = array_header(a);                                      \
        if (i == h->length - 1) {                                              \
            h->length -= 1;                                                    \
        } else if (h->length > 1) {                                            \
            void* ptr = &a[i];                                                 \
            void* last = &a[h->length - 1];                                    \
            h->length -= 1;                                                    \
            memcpy(ptr, last, sizeof(*a));                                     \
        }                                                                      \
    } while (0)

#define array_pop_back(a) (a[--array_header(a)->length])


#define dynstr_len(str) (array_len(str) - 1)

char*
cstr_from_dynstr(const char* src, Allocator* allocator);

char*
dynstr_from_cstr(const char* cstr, size_t capacity, Allocator* allocator);

char*
dynstr_init(size_t capacity, Allocator* a);

void
dynstr_append_c(char * dest, char src);

void
dynstr_append(char * dest, const char * src);

void
dynstr_clear(char* str);

void
dynstr_set(char * dest, const char * src);

#endif // MEM_H
