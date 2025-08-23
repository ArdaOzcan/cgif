#include <assert.h>
#include <gifbuf/gifbuf.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ccore.h"

#define GIF_ALLOC_SIZE 1 * MEGABYTE
#define LZW_ALLOC_SIZE 1 * MEGABYTE

#define GIF_MAX_BLOCK_LENGTH 254
#define INPUT_BUFFER_CAP 256

#define LZW_DICT_MAX_CAP 4096
#define LZW_DICT_MIN_CAP 2048

#define BIT_ARRAY_MIN_CAP 2 * KILOBYTE

#define LSB_MASK(length) ((1 << (length)) - 1)

typedef struct
{
    u8* array;
    u8 next_byte;
    u8 current_bit_idx;
} BitArray;

BitArray
bit_array_init(u8* buffer)
{
    BitArray bit_array;
    bit_array.array = buffer;
    bit_array.current_bit_idx = 0;
    bit_array.next_byte = 0;
    return bit_array;
}

#ifndef min
#define min(a, b)                                                              \
    ({                                                                         \
        __typeof__(a) _a = (a);                                                \
        __typeof__(b) _b = (b);                                                \
        _a < _b ? _a : _b;                                                     \
    })
#endif

// Return value is always aligned to the least significant bit.
u32
bit_array_read(const u8* bytes,
               size_t start_byte_idx,
               u8 start_bit_idx,
               u8 bit_amount)
{
    assert(start_bit_idx < 8);
    assert(bit_amount > 0);
    u32 result = 0;
    size_t byte_idx = start_byte_idx;

    u8 bits_read = 0;
    u8 bits_in_first_byte = min(8 - start_bit_idx, bit_amount);

    result |= (bytes[byte_idx] >> start_bit_idx) & LSB_MASK(bits_in_first_byte);
    bits_read += bits_in_first_byte;
    byte_idx++;

    while (bit_amount - bits_read > 8) {
        result |= bytes[byte_idx] << bits_read;
        bits_read += 8;
        byte_idx++;
    }

    result |= (bytes[byte_idx] & LSB_MASK(bit_amount - bits_read)) << bits_read;

    return result;
}

void
bit_array_push(BitArray* bit_array, u16 data, u8 bit_amount)
{
    u8 bits_left = bit_amount;
    u8 split_bit_amount = 0;
    u8 mask = 0;
    while (bit_array->current_bit_idx + bits_left > 8) {
        split_bit_amount = 8 - bit_array->current_bit_idx;

        mask = LSB_MASK(split_bit_amount);
        bit_array->next_byte |= (data & mask) << bit_array->current_bit_idx;
        bit_array->current_bit_idx += split_bit_amount;
        bits_left -= split_bit_amount;
        data >>= split_bit_amount;

        array_append(bit_array->array, bit_array->next_byte);
        // printf("%zu: 0x%02x\n",
        //        array_len(bit_array->array),
        //        bit_array->current_byte);
        bit_array->next_byte = 0;
        bit_array->current_bit_idx = 0;
    }

    mask = LSB_MASK(bits_left);
    bit_array->next_byte |= (data & mask) << bit_array->current_bit_idx;
    bit_array->current_bit_idx += bits_left;
}

void
bit_array_pad_last_byte(BitArray* bit_array)
{
    array_append(bit_array->array, bit_array->next_byte);
    bit_array->next_byte = 0;
    bit_array->current_bit_idx = 0;
}

void
lzw_hashmap_reset(Hashmap* hashmap, u16 eoi_code, Allocator* allocator)
{
    for (u16 i = 0; i <= eoi_code; i++) {
        char* key = make(char, 2, allocator);
        key[0] = '0' + i;
        key[1] = '\0';
        u16* val = make(u16, 1, allocator);
        *val = i;
        hashmap_insert(hashmap, key, val);
    }
}

void
gif_decompress_lzw(const u8* bytes,
                   u8 min_code_size,
                   u8* indices,
                   size_t* out_indices_length,
                   Allocator* allocator)
{
    const size_t clear_code = 1 << min_code_size;
    const size_t eoi_code = clear_code + 1;
    const char** code_table = array(const char*, 128, allocator);

    for (int i = 0; i <= eoi_code; i++) {
        char* str = make(char, 2, allocator);
        str[0] = '0' + i;
        str[1] = '\0';
        array_append(code_table, str);
    }

    // lzw_hashmap_reset(&hashmap, eoi_code, allocator);

    u8 code_size = min_code_size + 1;
    size_t start_byte_idx = 0;
    u8 start_bit_idx = 0;

    // Read initial clear code
    u16 code = bit_array_read(bytes, start_byte_idx, start_bit_idx, code_size);
    start_bit_idx += code_size;
    while (start_bit_idx >= 8) {
        start_byte_idx++;
        start_bit_idx %= 8;
    }

    if (code != clear_code) {
        printf("First byte should be the clear code!!\n");
    }

    code = bit_array_read(bytes, start_byte_idx, start_bit_idx, code_size);
    start_bit_idx += code_size;
    while (start_bit_idx >= 8) {
        start_byte_idx++;
        start_bit_idx %= 8;
    }
    size_t indices_len = 0;
    for (int j = 0; code_table[code][j] != '\0'; j++) {
        indices[indices_len++] = code_table[code][j] - '0';
    }

    u8 previous_code = code;
    while ((code = bit_array_read(
              bytes, start_byte_idx, start_bit_idx, code_size)) != eoi_code) {

        // printf("Code %u (%b from byte 0x%02x 0b%08b) was read at %zu:%u. ",
        //        code,
        //        code,
        //        bytes[start_byte_idx],
        //        bytes[start_byte_idx],
        //        start_byte_idx,
        //        start_bit_idx);

        start_bit_idx += code_size;
        while (start_bit_idx >= 8) {
            start_byte_idx++;
            start_bit_idx %= 8;
        }
        // printf("Location is now %zu:%u.\n", start_byte_idx, start_bit_idx);

        char k = 0;
        if (code < array_len(code_table)) {
            k = code_table[code][0];
            // printf("%hu was in code table.\n", code);
        } else {
            k = code_table[previous_code][0];
            // printf("%hu was not in code table.\n", code);
        }

        size_t previous_code_length = strlen(code_table[previous_code]);
        char* new_entry = make(char, previous_code_length + 2, allocator);
        memcpy(new_entry, code_table[previous_code], previous_code_length);
        new_entry[previous_code_length] = k;
        new_entry[previous_code_length + 1] = '\0';

        if (code < array_len(code_table)) {
            for (int j = 0; code_table[code][j] != '\0'; j++) {
                indices[indices_len++] = code_table[code][j] - '0';
                // printf("Added %d to indices\n", indices[indices_len - 1]);
            }
        } else {
            for (int j = 0; new_entry[j] != '\0'; j++) {
                indices[indices_len++] = new_entry[j] - '0';
                // printf("Added %d to indices\n", indices[indices_len - 1]);
            }
        }

        array_append(code_table, new_entry);
        if (array_len(code_table) >= (1 << code_size)) {
            code_size++;
        }
        // printf("Added %s to code table. (New size: %zu)\n",
        //        new_entry,
        //        array_len(code_table));
        previous_code = code;
    }

    *out_indices_length = array_len(indices);
}

u8*
gif_compress_lzw(Allocator* allocator,
                 u8 min_code_size,
                 const u8* indices,
                 size_t indices_len,
                 size_t* compressed_len)
{
    Hashmap hashmap = hashmap_init(LZW_DICT_MAX_CAP, allocator);
    const size_t clear_code = 1 << min_code_size;
    const size_t eoi_code = clear_code + 1;

    lzw_hashmap_reset(&hashmap, eoi_code, allocator);

    u8* bit_array_buf = array(u8, BIT_ARRAY_MIN_CAP, allocator);
    BitArray bit_array = bit_array_init(bit_array_buf);

    // Starts from min + 1 because min_code_size is for colors only
    // special codes (clear code and end of instruction code) are
    // not included
    u8 code_size = min_code_size + 1;
    bit_array_push(&bit_array, clear_code, code_size);

    char* input_buf = dynstr_init(INPUT_BUFFER_CAP, allocator);
    dynstr_append_c(input_buf, '0' + indices[0]);

    char* appended = dynstr_init(INPUT_BUFFER_CAP, allocator);
    for (size_t i = 1; i < indices_len; i++) {
        char k = '0' + indices[i];

        dynstr_set(appended, input_buf);
        dynstr_append_c(appended, k);

        char* result = hashmap_get(&hashmap, appended);

        if (result != NULL) {
            dynstr_append_c(input_buf, k);
            // printf("INPUT: %s\n", input_buf);
        } else {
            char* key = cstr_from_dynstr(appended, allocator);
            u16* val = make(u16, 1, allocator);
            *val = hashmap.length;

            hashmap_insert(&hashmap, key, val);
            // printf("'%s': %d\n", key, *val);

            u16* idx = hashmap_get(&hashmap, input_buf);

            assert(idx != NULL);

            bit_array_push(&bit_array, *idx, code_size);
            // printf("Pushed %b to the bit array for index %d\n",
            //        *idx & LSB_MASK(code_size),
            //        indices[i]);

            dynstr_clear(input_buf);
            dynstr_append_c(input_buf, k);

            if (hashmap.length >= LZW_DICT_MAX_CAP) {
                // printf("---CLEAR---\n");
                bit_array_push(&bit_array, clear_code, code_size);
                hashmap_clear(&hashmap);
                lzw_hashmap_reset(&hashmap, eoi_code, allocator);
                code_size = min_code_size + 1;
            } else if (hashmap.length > (1 << code_size)) {
                code_size++;
            }
        }
    }

    u16* idx = hashmap_get(&hashmap, input_buf);

    assert(idx != NULL);

    bit_array_push(&bit_array, *idx, code_size);

    bit_array_push(&bit_array, eoi_code, code_size);
    bit_array_pad_last_byte(&bit_array);
    *compressed_len = array_len(bit_array.array);
    printf("Dictionary length: %zu\n", hashmap.length);
    return bit_array.array;
}

void
gif_write_header(VArena* gif_data, GIFVersion version)
{
    switch (version) {
        case GIF87a:
            varena_push_copy(gif_data, "GIF87a", 6);
            break;
        case GIF89a:
            varena_push_copy(gif_data, "GIF89a", 6);
            break;
    }
}

void
gif_write_logical_screen_descriptor(VArena* gif_data,
                                    u16 width,
                                    u16 height,
                                    bool gct,
                                    u8 color_resolution,
                                    bool sort,
                                    u8 gct_size,
                                    u8 background,
                                    u8 pixel_aspect_ratio)
{
    varena_push_copy(gif_data, &width, sizeof(u16));
    varena_push_copy(gif_data, &height, sizeof(u16));

    u8 packed = 0;
    packed |= gct << 7;                      // 1000 0000
    packed |= (color_resolution & 0x7) << 4; // 0111 0000
    packed |= sort << 3;                     // 0000 1000
    packed |= (gct_size & 0x7);              // 0000 0111

    varena_push_copy(gif_data, &packed, sizeof(u8));
    varena_push_copy(gif_data, &background, sizeof(u8));
    varena_push_copy(gif_data, &pixel_aspect_ratio, sizeof(u8));
}

void
gif_write_global_color_table(VArena* gif_data, const Color256RGB* colors)
{
    u8 N = ((u8*)gif_data->base)[10] & LSB_MASK(3);
    u8 colorAmount = 1 << (N + 1);
    for (u8 i = 0; i < colorAmount; i++) {
        varena_push_copy(gif_data, &colors[i], sizeof(Color256RGB));
    }
}

void
gif_write_img_extension(VArena* gif_data)
{
    // Dummy bytes for now
    u8 bytes[] = {
        0x21, 0xf9, 0x04, 0x01, 0x0a, 0x00, 0x1f, 0x00,
    };

    for (int i = 0; i < sizeof(bytes) / sizeof(u8); i++) {
        varena_push_copy(gif_data, &bytes[i], sizeof(u8));
    }
}

void
gif_write_img_descriptor(VArena* gif_data,
                         u16 left,
                         u16 top,
                         u16 width,
                         u16 height,
                         u8 local_color_table)
{
    varena_push_copy(gif_data, ",", sizeof(char));
    varena_push_copy(gif_data, &left, sizeof(u16));
    varena_push_copy(gif_data, &top, sizeof(u16));
    varena_push_copy(gif_data, &width, sizeof(u16));
    varena_push_copy(gif_data, &height, sizeof(u16));
    varena_push_copy(gif_data, &local_color_table, sizeof(u8));
}

void
gif_write_img_data(VArena* gif_data,
                   u8 lzw_min_code,
                   u8* bytes,
                   size_t bytes_length)
{
    varena_push_copy(gif_data, &lzw_min_code, sizeof(u8));

    size_t bytes_left = bytes_length;
    while (bytes_left) {
        u8 block_length = bytes_left >= GIF_MAX_BLOCK_LENGTH
                            ? GIF_MAX_BLOCK_LENGTH
                            : bytes_left;
        varena_push_copy(gif_data, &block_length, sizeof(u8));
        varena_push_copy(gif_data,
                         &bytes[bytes_length - bytes_left],
                         block_length * sizeof(u8));
        bytes_left -= block_length;
    }

    const u8 terminator = '\0';
    varena_push_copy(gif_data, &terminator, sizeof(u8));
}

void
gif_write_trailer(VArena* gif_data)
{
    const u8 trailer = 0x3B;
    varena_push_copy(gif_data, &trailer, 1);
}

void
gif_import(const u8* file_data,
           GIFMetadata* metadata,
           u8* indices,
           size_t* indices_length)
{
    VArena lzw_arena;
    varena_init(&lzw_arena, LZW_ALLOC_SIZE);
    Allocator lzw_alloc = varena_allocator(&lzw_arena);

    gif_decompress_lzw(file_data, metadata->min_code_size, indices, indices_length, &lzw_alloc);
    varena_destroy(&lzw_arena);
}

void
gif_export(GIFMetadata metadata,
           const Color256RGB* colors,
           const u8* indices,
           const char* out_path)
{
    VArena gif_data;
    varena_init_ex(&gif_data, GIF_ALLOC_SIZE, system_page_size(), 1);

    gif_write_header(&gif_data, metadata.version);
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
    if (metadata.image_extension) {
        gif_write_img_extension(&gif_data);
    }
    gif_write_img_descriptor(&gif_data,
                             metadata.left,
                             metadata.top,
                             metadata.width,
                             metadata.height,
                             metadata.local_color_table);

    VArena lzw_arena;
    varena_init(&lzw_arena, LZW_ALLOC_SIZE);
    Allocator lzw_alloc = varena_allocator(&lzw_arena);

    size_t compressed_len = 0;
    u8* compressed = gif_compress_lzw(&lzw_alloc,
                                      metadata.min_code_size,
                                      indices,
                                      metadata.width * metadata.height,
                                      &compressed_len);

    gif_write_img_data(
      &gif_data, metadata.min_code_size, compressed, compressed_len);
    gif_write_trailer(&gif_data);

    FILE* file = fopen(out_path, "wb");
    if (file) {
        fwrite(gif_data.base, sizeof(char), gif_data.used, file);
        fclose(file);
    }
    printf("GIF Arena used: %.2f%% (%llu/%llu KB)\n",
           100.0f * gif_data.used / gif_data.size,
           gif_data.used / KILOBYTE,
           gif_data.size / KILOBYTE);
    printf("LZW Arena used: %.2f%% (%llu/%llu KB)\n",
           100.0f * lzw_arena.used / lzw_arena.size,
           lzw_arena.used / KILOBYTE,
           lzw_arena.size / KILOBYTE);
    printf("\n");

    varena_destroy(&gif_data);
    varena_destroy(&lzw_arena);
}
