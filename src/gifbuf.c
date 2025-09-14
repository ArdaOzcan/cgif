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

#define DEBUG_LOG

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
gif_decompress_lzw(const u8* compressed,
                   u8 min_code_size,
                   u8* out_indices,
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

    u8 code_size = min_code_size + 1;
    size_t start_byte_idx = 0;
    u8 start_bit_idx = 0;

    int _temp_i = 0;

    // Read initial clear code
    u16 code =
      bit_array_read(compressed, start_byte_idx, start_bit_idx, code_size);
#ifdef DEBUG_LOG
    printf("READ Code[%d]: 0b%0*b (%d)\n", _temp_i, code_size, code, code);
    printf("Code size: %hhu\n", code_size);
#endif
    _temp_i++;
    start_bit_idx += code_size;
    start_byte_idx += start_bit_idx / 8;
    start_bit_idx %= 8;

    if (code != clear_code) {
        printf("First byte should be the clear code!!\n");
    }

    size_t indices_len = 0;
    // Initial code after clear (in order to set previous_code)
    code = bit_array_read(compressed, start_byte_idx, start_bit_idx, code_size);
#ifdef DEBUG_LOG
    printf("READ Code[%d]: 0b%0*b (%d)\n", _temp_i, code_size, code, code);
    printf("Code size: %hhu\n", code_size);
#endif
    _temp_i++;

    start_bit_idx += code_size;
    start_byte_idx += start_bit_idx / 8;
    start_bit_idx %= 8;

    for (int j = 0; code_table[code][j] != '\0'; j++) {
        out_indices[indices_len++] = code_table[code][j] - '0';
    }

    u16 previous_code = code;

    while ((code = bit_array_read(
              compressed, start_byte_idx, start_bit_idx, code_size)) !=
           eoi_code) {
#ifdef DEBUG_LOG
        printf("~~~~~~~~\n");
        printf("READ Code[%d]: 0b%0*b (%d)\n", _temp_i, code_size, code, code);
        printf("from Byte[%zu]: \n", start_byte_idx);
        printf("    0b%08b (0x%x)\n",
               compressed[start_byte_idx],
               compressed[start_byte_idx]);
        for (int i = -5; i < 7 - start_bit_idx; i++)
            printf(" ");
        printf("-^\n");
        printf(
          "    Code size: %hhu, Start Bit Index: %d\n", code_size, start_bit_idx);
#endif
        _temp_i++;

        start_bit_idx += code_size;
        start_byte_idx += start_bit_idx / 8;
        start_bit_idx %= 8;

        if (code == clear_code) {
            array_len(code_table) = eoi_code;
            code_size = min_code_size + 1;
            continue;
        }

        size_t previous_code_length = strlen(code_table[previous_code]);
        char* new_entry = make(char, previous_code_length + 2, allocator);
        memcpy(new_entry, code_table[previous_code], previous_code_length);

        const char* used_val = NULL;
        char k = 0;

        if (code < array_len(code_table)) {
            k = code_table[code][0];
            used_val = code_table[code];
        } else {
            k = code_table[previous_code][0];
            used_val = new_entry;
        }

        new_entry[previous_code_length] = k;
        new_entry[previous_code_length + 1] = '\0';

        for (int j = 0; used_val[j] != '\0'; j++) {
            out_indices[indices_len++] = used_val[j] - '0';
#ifdef DEBUG_LOG
            printf("READ: Index[%zu]: %hhu\n",
                   indices_len - 1,
                   out_indices[indices_len - 1]);
#endif
        }

        array_append(code_table, new_entry);
        if (array_len(code_table) >= (1 << code_size)) {
            code_size++;
        }

        previous_code = code;
    }

    printf("Encountered EOI code (%zu) at byte %zu from 0x%02x, completed "
           "decompression\n",
           eoi_code,
           start_byte_idx,
           compressed[start_byte_idx]);
    printf("Dictionary length was: %zu\n", array_len(code_table));
}

u8*
gif_compress_lzw(Allocator* allocator,
                 u8 min_code_size,
                 const u8* indices,
                 size_t indices_len,
                 size_t* compressed_len)
{
    Hashmap hashmap = { 0 };
    hashmap_init(&hashmap, LZW_DICT_MAX_CAP, allocator);
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
    int _temp_i = 0;
#ifdef DEBUG_LOG
    printf("WRITE Code[%d]: 0b%0*zb (%zu)\n",
           _temp_i,
           code_size,
           clear_code,
           clear_code);
    // printf("Code size: %hhu\n", code_size);
#endif
    _temp_i++;

    char* input_buf = dynstr_init(INPUT_BUFFER_CAP, allocator);
    dynstr_append_c(input_buf, '0' + indices[0]);

    char* appended = dynstr_init(INPUT_BUFFER_CAP, allocator);
    for (size_t i = 1; i < indices_len; i++) {
        char k = '0' + indices[i];
#ifdef DEBUG_LOG
        printf(
          "WRITE: Index[%zu]: %hhu ('%c')\n", i, indices[i], '0' + indices[i]);
#endif

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
            _temp_i++;
#ifdef DEBUG_LOG
            printf("~~~~~~~~\n");
            printf(
              "WRITE Code[%d]: 0b%0*b (%d)\n", _temp_i, code_size, *idx, *idx);
            printf("to Byte[%zu]: \n", array_len(bit_array.array) - 1);
            printf(
              "    0b%08b (0x%x)\n", bit_array.next_byte, bit_array.next_byte);
            for (int i = -6; i < 7 - bit_array.current_bit_idx; i++)
                printf(" ");
            printf("^");
            for (int i = 0; i < code_size; i++)
                printf("-");
            printf("\n    Code size: %hhu, Current Bit Index: %d\n",
                   code_size,
                   bit_array.current_bit_idx);
            printf("Dict['%s'] = %d\n", key, *val);
#endif

            dynstr_clear(input_buf);
            dynstr_append_c(input_buf, k);

            if (hashmap.length >= LZW_DICT_MAX_CAP) {
                bit_array_push(&bit_array, clear_code, code_size);
                _temp_i++;
#ifdef DEBUG_LOG
                printf("---CLEAR---\n");
                printf("WRITE Code[%d]: 0b%0*zb (%zu)\n",
                       _temp_i,
                       code_size,
                       clear_code,
                       clear_code);
                printf("Code size: %hhu\n", code_size);
#endif
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
    _temp_i++;
#ifdef DEBUG_LOG
    printf("WRITE Code[%d]: 0b%0*b (%d)\n", _temp_i, code_size, *idx, *idx);
    printf("Code size: %hhu\n", code_size);
#endif

    bit_array_push(&bit_array, eoi_code, code_size);
    _temp_i++;
#ifdef DEBUG_LOG
    printf("WRITE Code[%d]: 0b%0*zb (%zu)\n",
           _temp_i,
           code_size,
           eoi_code,
           eoi_code);
    printf("Code size: %hhu\n", code_size);
#endif
    bit_array_pad_last_byte(&bit_array);
    *compressed_len = array_len(bit_array.array);
    printf("Dictionary length: %zu\n", hashmap.length);
    return bit_array.array;
}

size_t
gif_read_header(const u8* header, GIFVersion* version)
{
    if (memcmp(header, "GIF87a", 6) == 0) {
        *version = GIF87a;
    } else if (memcmp(header, "GIF89a", 6) == 0) {
        *version = GIF89a;
    }

    return 6;
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

size_t
gif_read_logical_screen_descriptor(const u8* bytes, GIFMetadata* metadata)
{
    size_t cursor = 0;
    memcpy(&metadata->width, bytes + cursor, sizeof(u16));
    cursor += sizeof(u16);

    memcpy(&metadata->height, bytes + cursor, sizeof(u16));
    cursor += sizeof(u16);

    u8 packed = 0;
    memcpy(&packed, bytes + cursor, sizeof(u8));
    cursor += sizeof(u8);
    metadata->has_gct = packed >> 7;
    metadata->color_resolution = (packed >> 4) & LSB_MASK(3);
    metadata->sort = (packed >> 3) & LSB_MASK(1);
    metadata->gct_size_n = packed & LSB_MASK(3);

    memcpy(&metadata->background, bytes + cursor, sizeof(u8));
    cursor += sizeof(u8);

    memcpy(&metadata->pixel_aspect_ratio, bytes + cursor, sizeof(u8));
    cursor += sizeof(u8);

    return cursor;
}

void
gif_write_logical_screen_descriptor(VArena* gif_data,
                                    const GIFMetadata* metadata)
{
    varena_push_copy(gif_data, &metadata->width, sizeof(u16));
    varena_push_copy(gif_data, &metadata->height, sizeof(u16));

    u8 packed = 0;
    packed |= metadata->has_gct << 7;                  // 1000 0000
    packed |= (metadata->color_resolution & 0x7) << 4; // 0111 0000
    packed |= metadata->sort << 3;                     // 0000 1000
    packed |= (metadata->gct_size_n & 0x7);            // 0000 0111

    varena_push_copy(gif_data, &packed, sizeof(u8));
    varena_push_copy(gif_data, &metadata->background, sizeof(u8));
    varena_push_copy(gif_data, &metadata->pixel_aspect_ratio, sizeof(u8));
}

size_t
gif_read_global_color_table(const u8* bytes, u8 N, GIFColor* colors)
{
    u8 color_amount = 1 << (N + 1);
    for (u8 i = 0; i < color_amount; i++) {
        memcpy(colors[i], bytes + i * sizeof(GIFColor), sizeof(GIFColor));
    }

    return color_amount * sizeof(GIFColor);
}

void
gif_write_global_color_table(VArena* gif_data, const GIFColor* colors)
{
    u8 N = ((u8*)gif_data->base)[10] & LSB_MASK(3);
    u8 colorAmount = 1 << (N + 1);
    for (u8 i = 0; i < colorAmount; i++) {
        varena_push_copy(gif_data, &colors[i], sizeof(GIFColor));
    }
}

size_t
gif_read_img_extension(const u8* bytes, u8* output)
{
    // Read the dummy bytes for now.
    memcpy(output, bytes, 8 * sizeof(u8));
    return 8;
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

size_t
gif_read_img_descriptor(const u8* bytes, GIFMetadata* metadata)
{
    size_t cursor = 0;
    if (',' == *(bytes + cursor)) {
        cursor += sizeof(char);
    } else {
        printf("Expected %02x at the beginning of image descriptor section.\n",
               ',');
    }

    memcpy(&metadata->left, bytes + cursor, sizeof(u16));
    cursor += sizeof(u16);

    memcpy(&metadata->top, bytes + cursor, sizeof(u16));
    cursor += sizeof(u16);

    memcpy(&metadata->width, bytes + cursor, sizeof(u16));
    cursor += sizeof(u16);

    memcpy(&metadata->height, bytes + cursor, sizeof(u16));
    cursor += sizeof(u16);

    memcpy(&metadata->local_color_table, bytes + cursor, sizeof(u8));
    cursor += sizeof(u8);

    return cursor;
}

void
gif_write_img_descriptor(VArena* gif_data, const GIFMetadata* metadata)
{
    varena_push_copy(gif_data, ",", sizeof(char));
    varena_push_copy(gif_data, &metadata->left, sizeof(u16));
    varena_push_copy(gif_data, &metadata->top, sizeof(u16));
    varena_push_copy(gif_data, &metadata->width, sizeof(u16));
    varena_push_copy(gif_data, &metadata->height, sizeof(u16));
    varena_push_copy(gif_data, &metadata->local_color_table, sizeof(u8));
}

// Reads the data blocks and writes all bytes to a continous buffer.
size_t
gif_read_img_data(const u8* in_bytes, u8 lzw_min_code, u8* out_bytes)
{
    size_t read_cursor = 0;
    size_t write_cursor = 0;
    memcpy(&lzw_min_code, in_bytes + read_cursor, sizeof(u8));
    read_cursor += sizeof(u8);

    u8 block_length = 0;
    memcpy(&block_length, in_bytes + read_cursor, sizeof(u8));
    read_cursor += sizeof(u8);
    while (block_length > 0) {
        memcpy(out_bytes + write_cursor, &in_bytes[read_cursor], block_length);

        write_cursor += block_length;
        read_cursor += block_length;

        memcpy(&block_length, in_bytes + read_cursor, sizeof(u8));
        read_cursor += sizeof(u8);
    }

    printf("gif_read_img_data: Compressed data byte length was: %zu (without "
           "block lengths)\n",
           write_cursor);

    return read_cursor;
}

// Writes the compressed indices
// into data blocks as specified in the GIF specs.
void
gif_write_img_data(VArena* gif_data,
                   u8 lzw_min_code,
                   u8* bytes,
                   size_t bytes_length)
{
    varena_push_copy(gif_data, &lzw_min_code, sizeof(u8));
    size_t start = gif_data->used;

    size_t bytes_left = bytes_length;
    while (bytes_left) {
        u8 block_length = bytes_left >= GIF_MAX_BLOCK_LENGTH
                            ? GIF_MAX_BLOCK_LENGTH
                            : bytes_left;
        varena_push_copy(gif_data, &block_length, sizeof(u8));
        varena_push_copy(gif_data,
                         &bytes[bytes_length - bytes_left],
                         block_length * sizeof(u8));
        // printf("Block Length: %u\n", block_length);
        // printf("Written to byte 0x%04zx\n", gif_data->used - 1);
        bytes_left -= block_length;
    }

    const u8 terminator = '\0';
    varena_push_copy(gif_data, &terminator, sizeof(u8));
}

size_t
gif_read_trailer(const u8* bytes)
{
    u8 trailer = 0x3B;
    memcpy(&trailer, bytes, 1);
    if (trailer != 0x3B) {
        fprintf(stderr, "GIF Trailer was not provided. Ignoring...\n");
    }
    return 1;
}

void
gif_write_trailer(VArena* gif_data)
{
    const u8 trailer = 0x3B;
    varena_push_copy(gif_data, &trailer, 1);
}

void
gif_import(const u8* file_data, GIFObject* gif_object)
{
    VArena lzw_arena;
    varena_init(&lzw_arena, LZW_ALLOC_SIZE);
    Allocator lzw_alloc = varena_allocator(&lzw_arena);

    size_t cursor = 0;

    cursor += gif_read_header(file_data, &gif_object->metadata.version);
    cursor += gif_read_logical_screen_descriptor(file_data + cursor,
                                                 &gif_object->metadata);
    gif_object->metadata.min_code_size = gif_object->metadata.gct_size_n + 1;
    gif_object->color_table =
      calloc((1 << (gif_object->metadata.gct_size_n + 1)), sizeof(GIFColor));
    cursor += gif_read_global_color_table(file_data + cursor,
                                          gif_object->metadata.gct_size_n,
                                          gif_object->color_table);

    if (gif_object->metadata.image_extension) {
        u8* img_extension = make(u8, 8, &lzw_alloc);
        cursor += gif_read_img_extension(file_data + cursor, img_extension);
    }
    cursor +=
      gif_read_img_descriptor(file_data + cursor, &gif_object->metadata);

    size_t pixel_amount =
      gif_object->metadata.width * gif_object->metadata.height;

    u8* compressed = array(u8, pixel_amount, &lzw_alloc);
    gif_read_img_data(
      file_data + cursor, gif_object->metadata.min_code_size, compressed);

    gif_object->indices = calloc(pixel_amount, sizeof(u8));
    gif_decompress_lzw(compressed,
                       gif_object->metadata.min_code_size,
                       gif_object->indices,
                       &lzw_alloc);
    varena_destroy(&lzw_arena);
}

void
gif_export(GIFObject gif_object, const char* out_path)
{
    VArena gif_data;
    varena_init_ex(&gif_data, GIF_ALLOC_SIZE, system_page_size(), 1);

    gif_write_header(&gif_data, gif_object.metadata.version);
    gif_write_logical_screen_descriptor(&gif_data, &gif_object.metadata);

    gif_write_global_color_table(&gif_data, gif_object.color_table);
    if (gif_object.metadata.image_extension) {
        gif_write_img_extension(&gif_data);
    }
    gif_write_img_descriptor(&gif_data, &gif_object.metadata);

    VArena lzw_arena;
    varena_init(&lzw_arena, LZW_ALLOC_SIZE);
    Allocator lzw_alloc = varena_allocator(&lzw_arena);

    size_t compressed_len = 0;
    u8* compressed =
      gif_compress_lzw(&lzw_alloc,
                       gif_object.metadata.min_code_size,
                       gif_object.indices,
                       gif_object.metadata.width * gif_object.metadata.height,
                       &compressed_len);

    gif_write_img_data(
      &gif_data, gif_object.metadata.min_code_size, compressed, compressed_len);
    gif_write_trailer(&gif_data);

    FILE* file = fopen(out_path, "wb");
    if (file) {
        fwrite(gif_data.base, sizeof(char), gif_data.used, file);
        fclose(file);
    }
    printf("GIF exported to %s successfully.\n", out_path);
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
