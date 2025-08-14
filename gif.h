#ifndef GIF_H
#define GIF_H

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;

#define KILOBYTE 1024
#define MEGABYTE 1024 * 1024

#define GIF_ALLOC_SIZE 1 * MEGABYTE
#define LZW_ALLOC_SIZE 64 * KILOBYTE

#define GIF_MAX_BLOCK_LENGTH 254
#define INPUT_BUFFER_CAP 256

#define LZW_DICT_MAX_CAP 4 * KILOBYTE
#define LZW_DICT_MIN_CAP 2 * KILOBYTE

#define BIT_ARRAY_MIN_CAP 2 * KILOBYTE

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

void
arena_init(Arena* arena, void* base, size_t size);

void*
arena_push_size(Arena* arena, size_t size);

#define arena_push_array(arena, type, length)                                  \
    (type*)arena_push_size(arena, sizeof(type) * length)

void
arena_copy_size(Arena* arena, const void* data, size_t size);

void*
ArenaAlloc_(size_t bytes, void* context);

void
ArenaFree_(size_t bytes, void* ptr, void* context);

#define arena_alloc_init(arena)                                                \
    (Allocator)                                                                \
    {                                                                          \
        ArenaAlloc_, ArenaFree_, arena                                         \
    }

typedef struct
{
    size_t capacity;
    size_t length;

    Allocator* allocator;
} ArrayHeader;

void*
array_init(size_t item_size, size_t capacity, Allocator* allocator);

#define array_header(a) ((ArrayHeader*)(a) - 1)
#define array_len(a) (array_header(a)->length)

void*
array_check_cap(void* arr, size_t added_count, size_t item_size);

#define array_append(a, v)                                                     \
    ((a) = array_check_cap(a, 1, sizeof(v)),                                   \
     (a)[array_header(a)->length] = (v),                                       \
     &(a)[array_header(a)->length++])

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

typedef struct
{
    u8 r;
    u8 g;
    u8 b;
} Color256RGB;


typedef enum {
    GIF87a,
    GIF89a
} GIFVersion;

void
gif_write_header(Arena* gif_data, GIFVersion version);

void
gif_write_logical_screen_descriptor(Arena* gif_data,
                                    u16 width,
                                    u16 height,
                                    bool gct,
                                    u8 colorResolution,
                                    bool sort,
                                    u8 gctSize,
                                    u8 background,
                                    u8 pixelAspectRatio);

#define get_lsb_mask(length) ((1 << length) - 1)

void
gif_write_global_color_table(Arena* gif_data, const Color256RGB* colors);

void
gif_write_img_descriptor(Arena* gif_data,
                         u16 left,
                         u16 top,
                         u16 width,
                         u16 height,
                         u8 local_color_table);

void
gif_write_img_data(Arena* gif_data,
                   u8 lzwMinCodeSize,
                   u8* bytes,
                   size_t bytes_length);

void
gif_write_trailer(Arena* arena);

typedef struct
{
    u8* array;
    u8 current_byte;
    u8 current_bit_index;
} BitArray;

void
bit_array_init(BitArray* bitArray, u8* buffer);

void
bit_array_push(BitArray* bit_array, u16 data, u8 bit_amount);

void
bit_array_pad_last_byte(BitArray* bitArray);

typedef struct
{
    char** array;
} Dictionary;

#define dict_len(dict) (array_len((dict)->array))

int
dict_find(Dictionary dict, const char* value, size_t valueLength);

void
dict_add(Allocator* allocator,
         Dictionary* dictionary,
         const char* value,
         size_t valueLength);

Dictionary
dict_init(size_t capacity, Allocator* allocator);

void
dict_print(Dictionary dict);

u8*
gif_compress_lzw(Allocator* allocator,
                 u8 min_code_size,
                 const u8* indices,
                 size_t indices_len,
                 size_t* compressed_len);

void
gif_write_img_extension(Arena* gif_data);

typedef struct
{
    GIFVersion version;
    u16 left;
    u16 top;
    u16 width;
    u16 height;
    bool has_gct;
    u8 color_resolution;
    bool sort;
    u8 gct_size_n;
    u8 background;
    u8 pixel_aspect_ratio;
    u8 local_color_table;
    u8 min_code_size;
    bool image_extension;
} GIFMetadata;

void
gif_export(GIFMetadata metadata,
           const Color256RGB* colors,
           const u8* indices,
           const char* out_path);

#endif
