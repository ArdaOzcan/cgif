#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;

#define WIDTH 64
#define HEIGHT 64
#define LZW_MIN_CODE_LENGTH 6
#define COLOR_RES 2
#define GCT_SIZE_N 5

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

#define arena_push_array(arena, type, length)                                  \
    (type*)arena_push_size(arena, sizeof(type) * length)

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

#define array_header(a) ((ArrayHeader*)(a) - 1)
#define array_len(a) (array_header(a)->length)

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

void
gif_write_header(Arena* gif_data)
{
    arena_copy_size(gif_data, "GIF89a", 6);
}

void
gif_write_logical_screen_descriptor(Arena* gif_data,
                                    u16 width,
                                    u16 height,
                                    bool gct,
                                    u8 colorResolution,
                                    bool sort,
                                    u8 gctSize,
                                    u8 background,
                                    u8 pixelAspectRatio)
{
    arena_copy_size(gif_data, &width, sizeof(u16));
    arena_copy_size(gif_data, &height, sizeof(u16));

    u8 packed = 0;
    // 0b1010 0101
    packed |= gct << 7;                     // 1000 0000
    packed |= (colorResolution & 0x7) << 4; // 0111 0000
    packed |= sort << 3;                    // 0000 1000
    packed |= (gctSize & 0x7);              // 0000 0111

    arena_copy_size(gif_data, &packed, sizeof(u8));
    arena_copy_size(gif_data, &background, sizeof(u8));
    arena_copy_size(gif_data, &pixelAspectRatio, sizeof(u8));
}

#define get_lsb_mask(length) ((1 << length) - 1)

void
gif_write_global_color_table(Arena* gif_data, const Color256RGB* colors)
{
    u8 N = ((u8*)gif_data->base)[10] & get_lsb_mask(3);
    u8 colorAmount = 1 << (N + 1);
    for (u8 i = 0; i < colorAmount; i++) {
        arena_copy_size(gif_data, &colors[i], sizeof(Color256RGB));
    }
}

void
gif_write_img_descriptor(Arena* gif_data,
                         u16 left,
                         u16 top,
                         u16 width,
                         u16 height,
                         u8 local_color_table)
{
    arena_copy_size(gif_data, ",", sizeof(char));
    arena_copy_size(gif_data, &left, sizeof(u16));
    arena_copy_size(gif_data, &top, sizeof(u16));
    arena_copy_size(gif_data, &width, sizeof(u16));
    arena_copy_size(gif_data, &height, sizeof(u16));
    arena_copy_size(gif_data, &local_color_table, sizeof(u8));
}

void
gif_write_img_data(Arena* gif_data,
                   u8 lzwMinCodeSize,
                   u8* bytes,
                   size_t bytes_length)
{
    arena_copy_size(gif_data, &lzwMinCodeSize, sizeof(u8));

    size_t bytes_left = bytes_length;
    while (bytes_left) {
        u8 block_length = bytes_left >= GIF_MAX_BLOCK_LENGTH
                            ? GIF_MAX_BLOCK_LENGTH
                            : bytes_left;
        arena_copy_size(gif_data, &block_length, sizeof(u8));
        arena_copy_size(gif_data,
                        &bytes[bytes_length - bytes_left],
                        block_length * sizeof(u8));
        bytes_left -= block_length;
    }

    const u8 terminator = '\0';
    arena_copy_size(gif_data, &terminator, sizeof(u8));
}

void
gif_write_trailer(Arena* arena)
{
    const u8 trailer = 0x3B;
    arena_copy_size(arena, &trailer, 1);
}

typedef struct
{
    u8* array;
    u8 current_byte;
    u8 current_bit_index;
} BitArray;

void
bit_array_init(BitArray* bitArray, u8* buffer)
{
    bitArray->array = buffer;
    bitArray->current_bit_index = 0;
    bitArray->current_byte = 0;
}

void
bit_array_push(BitArray* bit_array, u16 data, u8 bit_amount)
{
    u8 bits_left = bit_amount;
    u8 split_bit_amount = 0;
    while (bit_array->current_bit_index + bits_left > 8) {
        split_bit_amount = 8 - bit_array->current_bit_index;

        u8 mask = get_lsb_mask(split_bit_amount);
        bit_array->current_byte |= (data & mask)
                                   << bit_array->current_bit_index;
        bit_array->current_bit_index += split_bit_amount;
        bits_left -= split_bit_amount;
        data >>= split_bit_amount;

        array_append(bit_array->array, bit_array->current_byte);
        bit_array->current_byte = 0;
        bit_array->current_bit_index = 0;
    }

    u8 mask = get_lsb_mask(bits_left);
    bit_array->current_byte |= (data & mask) << bit_array->current_bit_index;
    bit_array->current_bit_index += bits_left;
}

void
bit_array_pad_last_byte(BitArray* bitArray)
{
    array_append(bitArray->array, bitArray->current_byte);
    bitArray->current_byte = 0;
    bitArray->current_bit_index = 0;
}

typedef struct
{
    char** array;
} Dictionary;

#define dict_len(dict) (array_len((dict)->array))

int
dict_find(Dictionary dict, const char* value, size_t valueLength)
{
    for (size_t i = 0; i < dict_len(&dict); i++) {
        const char* str = dict.array[i];
        if (str[valueLength] != '\0')
            continue;
        int result = memcmp(str, value, valueLength);
        if (result == 0)
            return i;
    }

    return -1;
}

void
dict_add(Allocator* allocator,
         Dictionary* dictionary,
         const char* value,
         size_t valueLength)
{
    char* copy = make(char, valueLength + 1, allocator);
    memcpy(copy, value, valueLength);
    copy[valueLength] = '\0';

    array_append(dictionary->array, copy);

    int dictLength = dict_len(dictionary);

    assert(dictLength <= LZW_DICT_MAX_CAP);
}

Dictionary
dict_init(size_t capacity, Allocator* allocator)
{
    char** arr = array_init(sizeof(const char*), LZW_DICT_MIN_CAP, allocator);
    return (Dictionary){ .array = arr };
}

void
dict_print(Dictionary dict)
{
    printf("{\n");
    for (int i = 0; i < dict_len(&dict); i++) {
        printf("%d: \"%s\", \n", i, dict.array[i]);
    }
    printf("}\n");
}

u8*
gif_compress_lzw(Allocator* allocator,
                 u8 min_code_size,
                 const u8* indices,
                 size_t indices_len,
                 size_t* compressed_len)
{
    Dictionary dict = dict_init(LZW_DICT_MIN_CAP, allocator);
    const size_t clear_code = 1 << min_code_size;
    const size_t eoi_code = clear_code + 1;

    for (u8 i = 0; i <= eoi_code; i++) {
        char data[2];
        data[0] = '0' + i;
        data[1] = '\0';
        dict_add(allocator, &dict, data, 2);
    }

    BitArray bit_array;

    u8* bit_array_buf = array_init(sizeof(u8), BIT_ARRAY_MIN_CAP, allocator);
    bit_array_init(&bit_array, bit_array_buf);

    // Starts from min + 1 because min_code_size is for colors only
    // special codes (clear code and end of instruction code) are
    // not included
    u8 code_size = min_code_size + 1;
    bit_array_push(&bit_array, clear_code, code_size);

    char* input_buf = array_init(sizeof(char), INPUT_BUFFER_CAP, allocator);
    for (size_t i = 0; i < indices_len; i++) {
        array_append(input_buf, '0' + indices[i]);
        input_buf[array_len(input_buf)] = '\0';

        int result = dict_find(dict, input_buf, array_len(input_buf));
        // printf("INPUT: %s\n", input_buf);

        if (result < 0) {
            dict_add(allocator, &dict, input_buf, array_len(input_buf));

            // printf("#%zu: %s\n",
            // dict_len(&dict) - 1,
            // dict.array[dict_len(&dict) - 1]);

            int idx = dict_find(dict, input_buf, array_len(input_buf) - 1);
            if (idx < 0) {
                // printf("%s was not found in dictionary.\n", input_buf);
                assert(idx >= 0);
            }

            bit_array_push(&bit_array, idx, code_size);
            // printf("OUTPUT: %d\n", idx);

            input_buf[0] = input_buf[array_len(input_buf) - 1];
            input_buf[1] = '\0';
            array_len(input_buf) = 1;

            if (dict_len(&dict) > (1 << code_size)) {
                code_size++;
            }
        }
    }

    int idx = dict_find(dict, input_buf, array_len(input_buf));
    assert(idx >= 0);

    bit_array_push(&bit_array, idx, code_size);

    bit_array_push(&bit_array, eoi_code, code_size);
    bit_array_pad_last_byte(&bit_array);
    *compressed_len = array_len(bit_array.array);
    return bit_array.array;
}

void
gif_write_img_extension(Arena* gif_data)
{
    // Dummy bytes for now
    u8 bytes[] = {
        0x21, 0xf9, 0x04, 0x01, 0x0a, 0x00, 0x1f, 0x00,
    };

    for (int i = 0; i < sizeof(bytes) / sizeof(u8); i++) {
        arena_copy_size(gif_data, &bytes[i], sizeof(u8));
    }
}

typedef struct
{
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
} GIFMetadata;

void
gif_export(GIFMetadata metadata,
           const Color256RGB* colors,
           const u8* indices,
           const char* out_path)
{
    Arena gif_data;
    void* gif_base = malloc(GIF_ALLOC_SIZE);
    arena_init(&gif_data, gif_base, GIF_ALLOC_SIZE);

    gif_write_header(&gif_data);
    gif_write_logical_screen_descriptor(&gif_data,
                                        metadata.width,
                                        metadata.height,
                                        metadata.has_gct,
                                        metadata.color_resolution,
                                        metadata.sort,
                                        metadata.gct_size_n,
                                        metadata.background,
                                        metadata.pixel_aspect_ratio);

    gif_write_global_color_table(&gif_data, colors);
    gif_write_img_extension(&gif_data);
    gif_write_img_descriptor(&gif_data,
                             metadata.left,
                             metadata.top,
                             metadata.width,
                             metadata.height,
                             metadata.local_color_table);

    Arena lzw_arena;
    void* lzw_base = malloc(LZW_ALLOC_SIZE);
    arena_init(&lzw_arena, lzw_base, LZW_ALLOC_SIZE);
    Allocator lzw_alloc = arena_alloc_init(&lzw_arena);

    size_t compressed_len = 0;
    u8* compressed = gif_compress_lzw(&lzw_alloc,
                                      metadata.min_code_size,
                                      indices,
                                      metadata.width * metadata.height,
                                      &compressed_len);

    gif_write_img_data(
      &gif_data, metadata.min_code_size, compressed, compressed_len);
    gif_write_trailer(&gif_data);

    FILE* file = fopen(out_path, "w");
    if (file) {
        fwrite(gif_data.base, sizeof(char), gif_data.used, file);
        fclose(file);
    }

    printf("GIF Arena used: %zu\n", gif_data.used);
    printf("LZW Arena used: %zu\n", lzw_arena.used);
    printf("\n");
    free(gif_base);
    free(lzw_base);
}

int
main()
{
    Color256RGB colors[] = {
        { 0, 0, 0 },       { 7, 64, 48 },     { 50, 50, 68 },
        { 85, 86, 100 },   { 56, 115, 154 },  { 0, 137, 203 },
        { 82, 120, 108 },  { 105, 118, 121 }, { 151, 128, 112 },
        { 32, 171, 238 },  { 120, 153, 183 }, { 92, 161, 207 },
        { 199, 155, 111 }, { 180, 165, 147 }, { 219, 204, 176 },
        { 242, 241, 222 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }, { 255, 255, 255 }, { 255, 255, 255 },
        { 255, 255, 255 }
    };

    u8 indices[] = {
        9,  9,  9,  9,  9,  9,  9,  9,  10, 10, 10, 10, 10, 10, 9,  9,  9,  9,
        10, 10, 10, 10, 10, 10, 10, 10, 10, 5,  5,  5,  10, 14, 12, 15, 14, 10,
        5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
        5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  9,  9,  9,  9,  9,  9,  10, 10,
        10, 10, 9,  9,  9,  9,  9,  10, 10, 10, 10, 10, 10, 10, 10, 10, 5,  5,
        5,  5,  5,  5,  14, 13, 3,  3,  12, 14, 5,  5,  5,  5,  5,  5,  5,  5,
        5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
        5,  5,  9,  9,  9,  9,  10, 10, 9,  9,  9,  9,  9,  9,  9,  10, 10, 10,
        10, 10, 10, 10, 10, 5,  5,  5,  5,  5,  5,  5,  5,  5,  14, 8,  12, 8,
        3,  14, 10, 5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
        5,  5,  5,  5,  5,  5,  5,  14, 15, 5,  5,  5,  9,  9,  10, 10, 9,  9,
        9,  9,  9,  9,  10, 10, 10, 10, 10, 10, 10, 10, 10, 5,  5,  5,  5,  5,
        5,  5,  5,  5,  5,  10, 12, 8,  12, 12, 8,  12, 14, 5,  5,  5,  5,  5,
        5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  10, 14, 15,
        12, 15, 5,  5,  9,  9,  9,  9,  9,  9,  9,  10, 10, 10, 10, 10, 10, 10,
        10, 10, 5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  13, 12, 12,
        13, 12, 8,  8,  15, 10, 5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
        5,  5,  5,  5,  5,  5,  5,  14, 15, 7,  8,  14, 10, 5,  9,  9,  9,  9,
        9,  10, 10, 10, 10, 10, 10, 10, 10, 10, 5,  5,  5,  5,  5,  5,  5,  5,
        5,  5,  5,  5,  5,  5,  5,  14, 8,  13, 13, 12, 8,  8,  14, 13, 5,  5,
        5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  12, 14,
        8,  12, 12, 8,  14, 5,  9,  9,  10, 10, 10, 10, 10, 10, 10, 10, 10, 5,
        5,  5,  5,  5,  5,  5,  5,  5,  10, 10, 10, 5,  5,  5,  5,  5,  5,  14,
        8,  14, 13, 12, 12, 8,  13, 14, 5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
        5,  5,  5,  5,  5,  5,  5,  10, 14, 8,  12, 14, 13, 5,  14, 10, 10, 10,
        10, 10, 10, 10, 10, 10, 10, 5,  5,  5,  5,  5,  5,  5,  5,  10, 10, 10,
        5,  5,  5,  5,  5,  5,  5,  5,  5,  13, 8,  14, 13, 12, 13, 8,  12, 15,
        10, 5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  10, 14,
        8,  8,  13, 14, 13, 8,  13, 10, 10, 10, 10, 10, 10, 10, 5,  5,  5,  5,
        5,  5,  5,  5,  10, 10, 10, 10, 5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
        10, 12, 8,  14, 13, 8,  13, 8,  8,  14, 13, 5,  5,  5,  5,  5,  5,  5,
        5,  5,  5,  5,  5,  5,  5,  10, 15, 12, 8,  12, 14, 13, 14, 12, 8,  5,
        10, 10, 10, 10, 5,  5,  5,  5,  5,  5,  5,  10, 10, 10, 10, 10, 5,  5,
        5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  13, 8,  8,  13, 13, 8,  13, 12,
        8,  13, 14, 10, 5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  10, 14,
        12, 7,  8,  12, 13, 12, 14, 13, 8,  5,  10, 10, 5,  5,  5,  5,  5,  5,
        5,  10, 10, 10, 10, 10, 5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
        5,  5,  14, 8,  8,  12, 13, 8,  8,  12, 8,  12, 15, 10, 5,  5,  5,  5,
        5,  5,  5,  5,  5,  5,  5,  10, 15, 12, 7,  8,  12, 13, 12, 14, 13, 12,
        8,  10, 10, 5,  5,  5,  5,  5,  10, 10, 10, 10, 10, 5,  5,  5,  5,  5,
        5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  14, 8,  8,  8,  13, 8,
        8,  12, 8,  8,  15, 13, 5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  10, 14,
        12, 7,  8,  8,  13, 12, 12, 13, 12, 8,  8,  10, 10, 5,  5,  5,  10, 10,
        10, 10, 10, 5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
        5,  5,  5,  5,  12, 8,  8,  8,  12, 12, 7,  8,  7,  12, 14, 13, 10, 5,
        5,  5,  5,  5,  5,  5,  5,  10, 14, 13, 8,  7,  8,  12, 12, 8,  13, 12,
        12, 8,  8,  5,  5,  5,  10, 10, 10, 10, 10, 5,  5,  5,  5,  5,  5,  5,
        5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  10, 12, 8,  8,
        8,  12, 7,  8,  7,  12, 13, 13, 13, 10, 13, 5,  10, 5,  13, 5,  10, 14,
        15, 8,  7,  8,  8,  12, 8,  8,  12, 12, 8,  8,  12, 5,  5,  10, 10, 10,
        10, 10, 5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
        5,  5,  5,  5,  5,  5,  5,  12, 8,  8,  8,  12, 6,  3,  3,  12, 13, 13,
        13, 13, 13, 10, 13, 13, 10, 13, 10, 14, 13, 7,  7,  8,  8,  8,  8,  12,
        12, 8,  8,  8,  10, 5,  10, 10, 10, 10, 5,  5,  5,  5,  5,  5,  5,  5,
        5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  8,
        12, 8,  7,  8,  8,  3,  12, 13, 14, 15, 13, 13, 13, 13, 14, 13, 13, 13,
        13, 13, 12, 6,  3,  8,  8,  7,  8,  12, 8,  8,  6,  8,  5,  5,  10, 10,
        5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
        5,  5,  5,  5,  5,  5,  5,  5,  5,  8,  8,  12, 7,  8,  7,  12, 14, 15,
        15, 15, 14, 13, 14, 14, 13, 13, 14, 14, 13, 14, 13, 8,  3,  7,  8,  3,
        12, 12, 8,  7,  8,  12, 5,  5,  10, 5,  5,  5,  5,  5,  5,  5,  5,  5,
        5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
        5,  12, 7,  8,  8,  3,  14, 15, 10, 10, 15, 15, 14, 14, 15, 14, 13, 15,
        15, 14, 13, 15, 14, 13, 7,  3,  8,  7,  8,  8,  7,  3,  12, 10, 5,  5,
        5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
        5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  10, 3,  7,  8,  14, 15, 6,
        6,  3,  1,  14, 14, 13, 14, 14, 13, 14, 14, 13, 13, 14, 15, 15, 13, 3,
        3,  6,  8,  7,  3,  3,  12, 5,  5,  5,  5,  5,  5,  5,  5,  5,  7,  3,
        3,  3,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
        5,  5,  5,  13, 12, 3,  12, 15, 6,  6,  15, 15, 1,  1,  13, 14, 13, 14,
        13, 14, 13, 13, 13, 15, 7,  7,  14, 13, 8,  3,  7,  3,  7,  12, 8,  5,
        5,  5,  5,  5,  5,  5,  7,  3,  3,  3,  3,  5,  5,  5,  5,  5,  5,  5,
        5,  5,  5,  10, 10, 13, 13, 13, 13, 13, 10, 5,  5,  5,  13, 3,  15, 14,
        6,  15, 6,  0,  0,  1,  6,  14, 13, 13, 14, 13, 8,  13, 7,  1,  3,  3,
        7,  14, 13, 8,  3,  3,  8,  8,  5,  5,  5,  5,  5,  5,  7,  3,  3,  3,
        3,  7,  5,  5,  5,  5,  5,  5,  5,  5,  5,  10, 13, 14, 14, 15, 15, 15,
        15, 15, 14, 13, 5,  5,  5,  15, 15, 14, 6,  13, 0,  0,  0,  1,  1,  13,
        13, 13, 13, 13, 8,  7,  1,  15, 13, 0,  3,  7,  14, 13, 3,  8,  8,  8,
        5,  5,  5,  5,  5,  7,  3,  3,  2,  3,  5,  5,  5,  5,  5,  5,  5,  5,
        5,  10, 13, 14, 14, 15, 15, 15, 15, 15, 15, 15, 15, 14, 13, 10, 10, 15,
        15, 15, 3,  6,  1,  0,  6,  1,  6,  8,  13, 14, 13, 8,  13, 3,  15, 0,
        0,  0,  1,  3,  14, 14, 8,  8,  8,  10, 5,  5,  5,  5,  7,  3,  2,  2,
        3,  5,  5,  5,  5,  5,  5,  5,  5,  5,  10, 14, 15, 15, 15, 15, 15, 14,
        14, 15, 15, 15, 15, 15, 15, 14, 13, 14, 14, 15, 14, 3,  6,  1,  1,  6,
        13, 8,  14, 15, 13, 7,  13, 1,  6,  1,  0,  0,  1,  6,  14, 15, 13, 13,
        8,  5,  5,  5,  5,  5,  3,  2,  2,  2,  7,  5,  5,  5,  5,  5,  5,  5,
        5,  10, 14, 15, 15, 14, 15, 15, 14, 14, 14, 14, 14, 14, 14, 15, 15, 15,
        13, 10, 13, 14, 15, 14, 3,  6,  13, 14, 8,  13, 13, 13, 13, 7,  13, 6,
        1,  6,  1,  0,  14, 6,  15, 15, 14, 7,  8,  5,  5,  5,  5,  5,  2,  2,
        2,  7,  5,  5,  5,  5,  5,  5,  5,  5,  10, 14, 15, 14, 14, 14, 15, 14,
        14, 14, 13, 13, 14, 15, 15, 14, 14, 14, 13, 7,  10, 13, 14, 15, 15, 15,
        14, 13, 13, 14, 13, 13, 14, 8,  8,  14, 6,  1,  6,  13, 6,  14, 15, 14,
        14, 7,  10, 5,  5,  5,  5,  5,  2,  2,  7,  5,  5,  5,  5,  5,  5,  5,
        5,  10, 14, 14, 14, 14, 13, 14, 14, 13, 14, 13, 13, 14, 14, 15, 15, 14,
        14, 14, 13, 7,  7,  13, 13, 14, 14, 14, 14, 13, 12, 14, 14, 14, 12, 13,
        7,  13, 14, 13, 6,  10, 15, 15, 14, 13, 14, 6,  5,  5,  5,  5,  5,  5,
        2,  3,  5,  5,  5,  5,  5,  5,  5,  5,  10, 14, 14, 14, 14, 13, 13, 14,
        13, 14, 14, 13, 13, 14, 15, 14, 14, 14, 15, 15, 13, 8,  7,  8,  8,  13,
        14, 13, 13, 8,  12, 15, 15, 12, 14, 12, 8,  8,  12, 14, 15, 15, 14, 13,
        12, 13, 10, 13, 5,  5,  5,  5,  5,  5,  2,  7,  5,  5,  5,  5,  5,  5,
        5,  5,  14, 15, 14, 14, 13, 13, 14, 13, 13, 14, 13, 14, 14, 14, 14, 14,
        14, 15, 15, 14, 13, 10, 7,  7,  8,  13, 13, 13, 14, 3,  3,  8,  8,  8,
        3,  13, 13, 13, 13, 13, 12, 13, 10, 10, 10, 10, 7,  10, 5,  5,  5,  5,
        5,  5,  3,  5,  5,  5,  5,  5,  5,  5,  5,  13, 15, 14, 13, 14, 13, 13,
        14, 13, 14, 13, 13, 14, 13, 13, 14, 14, 15, 15, 14, 14, 13, 8,  10, 7,
        8,  8,  14, 15, 15, 12, 0,  3,  8,  0,  2,  8,  14, 14, 13, 13, 13, 10,
        10, 7,  10, 7,  13, 5,  5,  5,  5,  5,  5,  5,  7,  5,  5,  5,  5,  5,
        5,  5,  10, 14, 15, 14, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 14,
        14, 14, 14, 14, 14, 14, 8,  8,  10, 7,  7,  14, 13, 15, 15, 14, 2,  2,
        3,  5,  13, 15, 15, 15, 14, 13, 13, 7,  10, 7,  7,  13, 10, 5,  5,  5,
        5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  13, 14, 14, 14, 13, 13,
        13, 13, 13, 13, 13, 13, 13, 13, 14, 14, 14, 14, 13, 13, 13, 8,  8,  8,
        10, 9,  13, 15, 14, 13, 14, 13, 7,  3,  2,  7,  13, 14, 14, 14, 13, 14,
        13, 13, 7,  7,  10, 13, 5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
        5,  5,  5,  10, 14, 14, 13, 14, 13, 13, 13, 13, 13, 13, 13, 13, 13, 14,
        13, 13, 13, 13, 13, 13, 8,  13, 13, 8,  8,  9,  10, 13, 14, 13, 13, 6,
        2,  0,  0,  2,  7,  7,  10, 13, 13, 13, 14, 14, 7,  10, 10, 5,  5,  5,
        5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  10, 14, 14, 14, 13, 14,
        13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 8,  8,  8,  13, 13, 14,
        13, 8,  7,  7,  9,  7,  10, 13, 6,  2,  0,  2,  2,  0,  2,  3,  5,  10,
        7,  14, 15, 14, 7,  10, 5,  5,  5,  5,  10, 9,  10, 5,  5,  5,  5,  5,
        5,  5,  5,  5,  14, 15, 14, 13, 13, 13, 13, 13, 13, 8,  13, 13, 13, 13,
        13, 13, 13, 8,  8,  13, 13, 13, 14, 15, 13, 8,  7,  3,  10, 10, 10, 6,
        3,  7,  10, 7,  7,  7,  3,  2,  7,  5,  5,  13, 13, 7,  9,  14, 9,  9,
        9,  10, 10, 5,  5,  9,  5,  5,  5,  5,  5,  5,  5,  10, 15, 14, 13, 13,
        13, 13, 8,  8,  8,  13, 13, 14, 14, 14, 13, 13, 8,  13, 13, 14, 13, 14,
        15, 13, 13, 8,  7,  3,  7,  3,  9,  10, 7,  10, 7,  10, 7,  10, 7,  7,
        3,  7,  7,  10, 7,  7,  10, 5,  5,  5,  5,  5,  5,  5,  5,  5,  10, 9,
        5,  5,  5,  5,  10, 14, 14, 14, 14, 13, 13, 8,  8,  13, 13, 14, 14, 15,
        15, 14, 14, 14, 13, 14, 15, 14, 14, 13, 14, 13, 8,  8,  6,  3,  3,  3,
        3,  7,  10, 10, 10, 10, 10, 6,  10, 7,  10, 10, 5,  7,  7,  9,  5,  5,
        14, 5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  13, 14, 13, 13,
        14, 14, 8,  8,  13, 14, 14, 14, 15, 14, 14, 14, 13, 13, 14, 14, 14, 13,
        15, 14, 13, 8,  8,  13, 8,  3,  2,  7,  7,  3,  3,  7,  10, 9,  9,  10,
        10, 9,  10, 7,  7,  3,  10, 5,  9,  5,  5,  9,  14, 9,  5,  5,  5,  5,
        5,  5,  5,  5,  5,  5,  14, 13, 13, 8,  13, 13, 8,  13, 13, 14, 13, 13,
        14, 13, 13, 13, 13, 14, 14, 13, 13, 13, 14, 14, 13, 13, 13, 13, 8,  3,
        2,  7,  3,  7,  3,  3,  7,  3,  7,  3,  3,  7,  5,  3,  3,  7,  5,  5,
        5,  10, 5,  5,  5,  5,  9,  5,  5,  5,  5,  5,  5,  5,  5,  5,  13, 13,
        8,  13, 13, 13, 8,  13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 14, 13, 13,
        13, 14, 13, 13, 13, 13, 13, 8,  7,  3,  2,  3,  7,  7,  3,  3,  3,  7,
        3,  7,  5,  5,  7,  3,  3,  5,  5,  5,  5,  9,  5,  5,  5,  5,  5,  9,
        5,  5,  5,  5,  5,  5,  5,  5,  13, 8,  8,  8,  13, 8,  8,  8,  8,  13,
        13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 8,  13, 8,
        3,  3,  2,  3,  8,  3,  7,  7,  3,  5,  5,  10, 10, 5,  3,  3,  6,  13,
        10, 5,  5,  5,  14, 5,  5,  5,  5,  5,  9,  14, 9,  9,  5,  5,  5,  5,
        13, 8,  7,  8,  13, 8,  8,  8,  8,  8,  8,  8,  13, 13, 13, 13, 13, 13,
        13, 13, 13, 13, 13, 13, 8,  8,  13, 7,  3,  2,  3,  2,  8,  7,  3,  7,
        7,  5,  10, 5,  10, 7,  3,  3,  8,  13, 5,  10, 5,  5,  5,  9,  5,  5,
        5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  13, 8,  7,  8,  8,  8,  8,  7,
        7,  7,  7,  8,  8,  13, 8,  8,  13, 8,  8,  13, 13, 13, 13, 8,  8,  13,
        8,  7,  3,  2,  3,  2,  7,  7,  3,  3,  5,  5,  10, 5,  5,  7,  3,  3,
        8,  13, 5,  9,  5,  5,  5,  9,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
        5,  5,  8,  8,  7,  7,  7,  8,  8,  7,  7,  3,  13, 8,  8,  8,  8,  7,
        8,  8,  8,  13, 13, 13, 8,  13, 13, 8,  8,  3,  2,  2,  7,  2,  3,  7,
        7,  3,  5,  6,  10, 5,  5,  3,  3,  7,  7,  8,  5,  5,  9,  5,  5,  5,
        10, 5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  8,  8,  3,  7,  3,  8,
        7,  7,  13, 7,  13, 13, 13, 8,  7,  8,  8,  7,  8,  13, 13, 8,  8,  13,
        8,  8,  7,  3,  2,  2,  3,  2,  3,  8,  7,  3,  7,  5,  10, 5,  7,  3,
        7,  3,  3,  8,  5,  5,  5,  10, 5,  5,  14, 5,  5,  5,  5,  5,  5,  5,
        5,  5,  5,  5,  8,  8,  3,  3,  3,  7,  8,  13, 13, 8,  7,  13, 14, 13,
        8,  8,  7,  7,  8,  8,  8,  8,  8,  8,  8,  7,  7,  3,  2,  2,  3,  2,
        3,  8,  7,  3,  7,  10, 5,  7,  3,  3,  7,  2,  3,  8,  5,  5,  5,  9,
        5,  5,  9,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  8,  8,  7,  3,
        7,  13, 13, 8,  7,  13, 8,  13, 13, 8,  8,  7,  8,  13, 13, 8,  8,  13,
        13, 13, 8,  7,  3,  3,  2,  2,  2,  3,  3,  7,  7,  3,  7,  5,  7,  7,
        7,  3,  3,  2,  3,  5,  5,  5,  5,  5,  5,  5,  5,  14, 10, 5,  5,  5,
        5,  5,  5,  5,  5,  5,  8,  8,  8,  3,  3,  13, 13, 8,  7,  13, 8,  7,
        8,  8,  7,  7,  3,  13, 13, 8,  8,  8,  7,  8,  8,  7,  7,  3,  3,  2,
        2,  2,  2,  3,  7,  3,  6,  7,  7,  7,  10, 7,  3,  2,  3,  5,  5,  5,
        5,  5,  9,  5,  5,  5,  5,  9,  14, 9,  5,  5,  5,  5,  5,  5,  5,  8,
        8,  3,  3,  3,  13, 8,  3,  13, 13, 8,  7,  8,  8,  7,  3,  8,  13, 7,
        8,  8,  7,  7,  7,  3,  3,  3,  3,  2,  3,  2,  3,  2,  3,  3,  7,  3,
        7,  7,  3,  3,  2,  3,  3,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
        5,  5,  14, 5,  5,  5,  5,  5,  5,  8,  8,  3,  3,  3,  7,  8,  3,  7,
        13, 13, 3,  7,  7,  8,  3,  3,  7,  7,  7,  7,  7,  3,  3,  3,  3,  3,
        2,  3,  2,  3,  2,  3,  2,  3,  3,  7,  7,  3,  3,  3,  2,  3,  3,  5,
        5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  10, 5,  5,  5,  5,
        5,  3,  7,  7,  3,  3,  3,  3,  3,  3,  7,  7,  13, 3,  7,  7,  7,  3,
        3,  7,  7,  3,  3,  3,  3,  3,  3,  2,  3,  2,  3,  3,  7,  3,  3,  7,
        3,  7,  3,  3,  3,  2,  2,  3,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,
        5,  5,  5,  5,  5,  5,  7,  5,  5,  5,  5,  3,  3,  7,  7,  3,  3,  3,
        3,  2,  3,  3,  7,  7,  3,  7,  7,  3,  3,  3,  3,  3,  3,  3,  3,  2,
        2,  2,  2,  3,  3,  7,  3,  7,  7,  7,  3,  3,  3,  7,  7,  2,  3,  3,
        5,  5,  5,  5,  5,  5,  5,  5,  5,  9,  9,  9,  9,  9,  9,  9,  7,  5,
        5,  5,  5,  7,  3,  3,  7,  7,  3,  3,  3,  3,  2,  3,  7,  13, 7,  3,
        7,  7,  3,  2,  3,  3,  2,  3,  2,  2,  2,  3,  3,  3,  7,  3,  7,  3,
        7,  3,  7,  6,  7,  10, 7,  2,  3,  5,  5,  5,  5,  5,  5,  5,  9,  9,
        9,  9,  9,  9,  9,  9,  5,  5,  3,  5,  5,  5,  5,  5,  2,  3,  3,  7,
        7,  3,  3,  3,  2,  2,  3,  7,  13, 7,  7,  7,  3,  2,  2,  3,  3,  3,
        3,  3,  3,  3,  3,  3,  3,  7,  7,  7,  7,  7,  6,  8,  10, 5,  2,  3,
        5,  5,  5,  5,  5,  9,  9,  9,  9,  5,  5,  5,  5,  5,  5,  5,  5,  5,
        3,  5,  5,  5,  5,  5,  7,  3,  3,  10, 7,  3,  3,  3,  3,  2,  2,  7,
        7,  7,  3,  3,  7,  3,  2,  2,  3,  7,  7,  3,  3,  3,  7,  7,  7,  7,
        8,  7,  8,  8,  10, 10, 9,  2,  3,  5,  5,  5,  5,  9,  9,  9,  5,  5,
        5,  5,  5,  5,  5,  5,  5,  9,  9,  9,  3,  5,  5,  5,  5,  5,  5,  2,
        3,  7,  10, 7,  3,  3,  3,  2,  2,  3,  10, 7,  3,  3,  3,  3,  3,  2,
        3,  3,  10, 7,  10, 7,  7,  8,  7,  8,  7,  8,  10, 10, 10, 9,  5,  3,
        5,  5,  5,  9,  9,  5,  5,  5,  5,  5,  5,  5,  9,  9,  9,  9,  9,  9,
        9,  9,  3,  7,  5,  5,  5,  5,  5,  7,  2,  3,  10, 7,  7,  3,  3,  3,
        2,  2,  3,  10, 7,  3,  3,  3,  3,  2,  2,  3,  7,  10, 6,  10, 3,  3,
        7,  10, 8,  10, 10, 10, 9,  10, 3,  5,  5,  5,  5,  5,  5,  5,  5,  5,
        9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  3,  3,  5,  5,  5,  5,
        5,  5,  2,  3,  7,  10, 7,  7,  3,  3,  3,  3,  2,  10, 7,  3,  3,  3,
        2,  3,  2,  2,  2,  3,  7,  3,  2,  2,  3,  7,  10, 9,  10, 9,  7,  3,
        5,  5,  5,  5,  5,  5,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  5,  5,
        5,  5,  5,  5,  2,  3,  7,  5,  5,  5,  5,  5,  7,  2,  3,  10, 10, 7,
        7,  3,  3,  2,  2,  3,  10, 7,  7,  3,  3,  2,  3,  2,  2,  2,  2,  2,
        3,  5,  10, 3,  7,  5,  10, 5,  7,  5,  5,  5,  5,  5,  9,  9,  9,  9,
        9,  9,  9,  5,  5,  5,  5,  5,  9,  9,  9,  9,  9,  9,  2,  3,  3,  5,
        5,  5,  5,  5,  5,  2,  2,  3,  10, 10, 7,  3,  3,  3,  3,  2,  10, 10,
        7,  7,  2,  3,  2,  3,  2,  2,  3,  10, 9,  9,  5,  3,  2,  3,  5,  3,
        3,  5,  5,  5,  9,  9,  9,  9,  9,  5,  5,  9,  9,  9,  9,  9,  9,  9,
        9,  9,  9,  9,  9,  9,  2,  2,  3,  3,  7,  5,  5,  5,  5,  2,  2,  2,
        7,  10, 3,  3,  3,  3,  2,  2,  2,  10, 10, 7,  7,  2,  3,  3,  7,  2,
        2,  5,  9,  9,  3,  2,  2,  2,  3,  3,  7,  5,  9,  9,  9,  9,  5,  5,
        9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  2,  2,
        3,  3,  3,  3,  7,  5,  7,  3,  2,  2,  3,  7,  10, 3,  3,  3,  2,  2,
        2,  3,  10, 7,  7,  3,  3,  3,  7,  3,  2,  10, 5,  9,  2,  2,  2,  3,
        3,  3,  5,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
        9,  9,  9,  9,  9,  9,  9,  9,  3,  2,  2,  3,  3,  3,  3,  3,  3,  3,
        3,  2,  2,  3,  10, 10, 3,  2,  2,  2,  3,  2,  3,  10, 7,  7,  3,  3,
        3,  7,  3,  3,  5,  3,  2,  2,  3,  3,  3,  3,  5,  9,  9,  9,  9,  9,
        9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
        5,  3,  2,  2,  2,  3,  2,  3,  3,  3,  3,  3,  3,  3,  7,  10, 3,  2,
        3,  3,  3,  2,  2,  3,  3,  7,  3,  3,  3,  7,  7,  3,  5,  2,  2,  2,
        3,  3,  3,  3,  7,  5,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
        9,  9,  9,  9,  9,  9,  9,  9,  9,  9
    };

    GIFMetadata metadata = (GIFMetadata){ .background = 0x10,
                                          .color_resolution = COLOR_RES,
                                          .sort = 0,
                                          .local_color_table = 0,
                                          .pixel_aspect_ratio = 0,
                                          .min_code_size = LZW_MIN_CODE_LENGTH,
                                          .gct_size_n = GCT_SIZE_N,
                                          .left = 0,
                                          .top = 0,
                                          .width = WIDTH,
                                          .height = HEIGHT,
                                          .has_gct = true };

    gif_export(metadata, colors, indices, "out/out.gif");
    return 0;
}
