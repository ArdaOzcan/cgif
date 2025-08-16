#include "gif.h"
#include "hashmap.h"
#include "mem.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

void
gif_write_header(Arena* gif_data, GIFVersion version)
{
    switch (version) {
        case GIF87a:
            arena_copy_size(gif_data, "GIF87a", 6);
            break;
        case GIF89a:
            arena_copy_size(gif_data, "GIF89a", 6);
            break;
    }
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
    packed |= gct << 7;                     // 1000 0000
    packed |= (colorResolution & 0x7) << 4; // 0111 0000
    packed |= sort << 3;                    // 0000 1000
    packed |= (gctSize & 0x7);              // 0000 0111

    arena_copy_size(gif_data, &packed, sizeof(u8));
    arena_copy_size(gif_data, &background, sizeof(u8));
    arena_copy_size(gif_data, &pixelAspectRatio, sizeof(u8));
}

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
                   u8 lzw_min_code,
                   u8* bytes,
                   size_t bytes_length)
{
    arena_copy_size(gif_data, &lzw_min_code, sizeof(u8));

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

BitArray
bit_array_init(u8* buffer)
{
    BitArray bit_array;
    bit_array.array = buffer;
    bit_array.current_bit_index = 0;
    bit_array.current_byte = 0;
    return bit_array;
}

void
bit_array_push(BitArray* bit_array, u16 data, u8 bit_amount)
{
    u8 bits_left = bit_amount;
    u8 split_bit_amount = 0;
    u8 mask = 0;
    while (bit_array->current_bit_index + bits_left > 8) {
        split_bit_amount = 8 - bit_array->current_bit_index;

        mask = get_lsb_mask(split_bit_amount);
        bit_array->current_byte |= (data & mask)
                                   << bit_array->current_bit_index;
        bit_array->current_bit_index += split_bit_amount;
        bits_left -= split_bit_amount;
        data >>= split_bit_amount;

        array_append(bit_array->array, bit_array->current_byte);
        // printf("%zu: 0x%02x\n",
        //        array_len(bit_array->array),
        //        bit_array->current_byte);
        bit_array->current_byte = 0;
        bit_array->current_bit_index = 0;
    }

    mask = get_lsb_mask(bits_left);
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
        // printf("INPUT: %s\n", input_buf);

        if (result != NULL) {
            dynstr_append_c(input_buf, k);
        } else {
            char* key = cstr_from_dynstr(appended, allocator);
            u16* val = make(u16, 1, allocator);
            *val = hashmap.length;

            hashmap_insert(&hashmap, key, val);
            // printf("'%s': %d\n", key, *val);

            u16* idx = hashmap_get(&hashmap, input_buf);

            assert(idx != NULL);

            bit_array_push(&bit_array, *idx, code_size);

            dynstr_clear(input_buf);
            dynstr_append_c(input_buf, k);

            if (hashmap.length >= LZW_DICT_MAX_CAP) {
                printf("---CLEAR---\n");
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

void
gif_export(GIFMetadata metadata,
           const Color256RGB* colors,
           const u8* indices,
           const char* out_path)
{
    void* gif_base = malloc(GIF_ALLOC_SIZE);
    Arena gif_data = arena_init(gif_base, GIF_ALLOC_SIZE);

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

    void* lzw_base = malloc(LZW_ALLOC_SIZE);
    Arena lzw_arena = arena_init(lzw_base, LZW_ALLOC_SIZE);
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
