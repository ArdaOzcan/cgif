#include <assert.h>
#include <gifbuf/gifbuf.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ccore.h"
#include "clog.h"

#define GIF_ALLOC_SIZE 1 * MEGABYTE
#define LZW_ALLOC_SIZE 16 * MEGABYTE

#define INPUT_BUFFER_CAP 256

#define LZW_DICT_MIN_CAP 2048

#define BIT_ARRAY_MIN_CAP 2 * KILOBYTE

#define LSB_MASK(length) ((1 << (length)) - 1)

typedef struct
{
    u8* array;
    u8 next_byte;
    u8 current_bit_idx;
} BitArray;

typedef struct
{
    size_t byte_idx;
    u8 bit_idx;
} BitArrayReader;

void
bit_array_init(BitArray* bit_array, u8* buffer)
{
    bit_array->array = buffer;
    bit_array->current_bit_idx = 0;
    bit_array->next_byte = 0;
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
bit_array_read(const u8* bytes, BitArrayReader* reader, u8 bit_amount)
{
    assert(reader->bit_idx < 8);
    assert(bit_amount > 0);
    u32 result = 0;
    size_t byte_idx = reader->byte_idx;

    u8 bits_read = 0;
    u8 bits_in_first_byte = min(8 - reader->bit_idx, bit_amount);

    result |=
      (bytes[byte_idx] >> reader->bit_idx) & LSB_MASK(bits_in_first_byte);
    bits_read += bits_in_first_byte;
    byte_idx++;

    while (bit_amount - bits_read > 8) {
        result |= bytes[byte_idx] << bits_read;
        bits_read += 8;
        byte_idx++;
    }

    result |= (bytes[byte_idx] & LSB_MASK(bit_amount - bits_read)) << bits_read;

    reader->bit_idx += bit_amount;
    reader->byte_idx += reader->bit_idx / 8;
    reader->bit_idx %= 8;

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
        // CLOG_DEBUG("%zu: 0x%02x",
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
    for (u16 i = 0; i <= eoi_code - 2; i++) {
        u8* key_string = array(u8, 1, allocator);
        array_append(key_string, i);

        u16* val = make(u16, 1, allocator);
        *val = i;

        ByteString* key = make(ByteString, 1, allocator);
        key->ptr = (char*)key_string;
        key->length = 1;
        if (hashmap_insert(hashmap, key, val) == 1) {
            fprintf(
              stderr, "Key %.*s was already in hashmap.\n", 1, (char*)key);
        }
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
    const u8** code_table = array(const u8*, 128, allocator);

    for (int i = 0; i <= eoi_code; i++) {
        u8* str = array(u8, 1, allocator);
        array_append(str, i);
        array_append(code_table, str);
    }

    u8 code_size = min_code_size + 1;
    BitArrayReader bit_reader = { 0 };
    // Read initial clear code
    u16 code = bit_array_read(compressed, &bit_reader, code_size);

    int _temp_i = 0;

    CLOG_DEBUG("READ Code[%d]: 0b%0*b (%d)", _temp_i, code_size, code, code);
    CLOG_DEBUG("Code size: %hhu", code_size);
    _temp_i++;

    if (code != clear_code) {
        CLOG_DEBUG("First byte should be the clear code!!");
    }

    size_t indices_len = 0;
    // Initial code after clear (in order to set previous_code)
    code = bit_array_read(compressed, &bit_reader, code_size);

    CLOG_DEBUG("READ Code[%d]: 0b%0*b (%d)", _temp_i, code_size, code, code);
    CLOG_DEBUG("Code size: %hhu", code_size);
    _temp_i++;

    for (int j = 0; j < array_len(code_table[code]); j++) {
        out_indices[indices_len++] = code_table[code][j];
    }

    u16 previous_code = code;

    while ((code = bit_array_read(compressed, &bit_reader, code_size)) !=
           eoi_code) {

        CLOG_DEBUG("~~~~~~~~");
        CLOG_DEBUG(
          "READ Code[%d]: 0b%0*b (%d)", _temp_i, code_size, code, code);
        CLOG_DEBUG("from Byte[%zu]: ", bit_reader.byte_idx);
        CLOG_DEBUG("    0b%08b (0x%x)",
                   compressed[bit_reader.byte_idx],
                   compressed[bit_reader.byte_idx]);
        for (int i = -5; i < 7 - bit_reader.bit_idx; i++)
            clog_printf_debug(" ");
        clog_printf_debug("-^\n");
        CLOG_DEBUG("    Code size: %hhu, Start Bit Index: %d",
                   code_size,
                   bit_reader.bit_idx);
        _temp_i++;

        if (code == clear_code) {
            array_header(code_table)->length = eoi_code;
            code_size = min_code_size + 1;
            continue;
        }

        if (code_table[previous_code] == NULL) {
            fprintf(stderr,
                    "Previous code %d was not in the code table.\n",
                    previous_code);
        }
        assert(code_table[previous_code] != NULL);
        size_t previous_code_length = array_len(code_table[previous_code]);
        u8* new_entry = array(u8, previous_code_length + 1, allocator);
        for (int i = 0; i < previous_code_length; i++)
            array_append(new_entry, code_table[previous_code][i]);

        const u8* used_val = NULL;
        char k = 0;

        if (code < array_len(code_table)) {
            k = code_table[code][0];
            used_val = code_table[code];
        } else {
            k = code_table[previous_code][0];
            used_val = new_entry;
        }

        // CLOG_DEBUG("k = %x", k);

        array_append(new_entry, k);

        for (int j = 0; j < array_len(used_val); j++) {
            out_indices[indices_len++] = used_val[j];
            CLOG_DEBUG("READ: Index[%zu]: %hhu",
                       indices_len - 1,
                       out_indices[indices_len - 1]);
        }

        array_append(code_table, new_entry);
        if (array_len(code_table) >= (1 << code_size) && code_size < 12) {
            code_size++;
        }

        previous_code = code;
    }

    CLOG_DEBUG("Encountered EOI code (%zu) at byte %zu from 0x%02x, completed "
               "decompression",
               eoi_code,
               bit_reader.byte_idx,
               compressed[bit_reader.byte_idx]);
    CLOG_DEBUG("Dictionary length was: %zu", array_len(code_table));
}

u8*
gif_compress_lzw(Allocator* allocator,
                 size_t lzw_hashmap_max_length,
                 u8 min_code_size,
                 const u8* indices,
                 size_t indices_len,
                 size_t* compressed_len)
{
    Hashmap hashmap = { 0 };
    hashmap_byte_string_init(&hashmap, lzw_hashmap_max_length, allocator);
    const size_t clear_code = 1 << min_code_size;
    const size_t eoi_code = clear_code + 1;

    lzw_hashmap_reset(&hashmap, eoi_code, allocator);
    CLOG_DEBUG("Hashmap length after reset: %zu", hashmap.length);

    u8* bit_array_buf = array(u8, BIT_ARRAY_MIN_CAP, allocator);
    BitArray bit_array = { 0 };
    bit_array_init(&bit_array, bit_array_buf);

    // Starts from min + 1 because min_code_size is for colors only
    // special codes (clear code and end of instruction code) are
    // not included
    u8 code_size = min_code_size + 1;
    bit_array_push(&bit_array, clear_code, code_size);
    int _temp_i = 0;

    CLOG_DEBUG("WRITE Code[%d]: 0b%0*zb (%zu)",
               _temp_i,
               code_size,
               clear_code,
               clear_code);
    _temp_i++;

    u8* input_buf = array(u8, INPUT_BUFFER_CAP, allocator);
    array_append(input_buf, indices[0]);

    u8* appended = array(u8, INPUT_BUFFER_CAP, allocator);
    for (size_t i = 1; i < indices_len; i++) {
        char k = indices[i];
        CLOG_DEBUG("Input Buffer (Length=%zu): ", array_len(input_buf));
        if (clog_log_level_get() <= CLOG_LOG_LEVEL_DEBUG)
            clog_print_array_u8(input_buf, array_len(input_buf));

        array_assign(appended, input_buf);
        array_append(appended, k);

        char* result = hashmap_byte_string_get(
          &hashmap,
          (ByteString){ .ptr = (char*)appended,
                        .length = array_len(appended) });

        if (result != NULL) {
            array_append(input_buf, k);
            // CLOG_DEBUG("INPUT: %s", input_buf);
            continue;
        }

        u16* idx = hashmap_byte_string_get(
          &hashmap,
          (ByteString){ .ptr = (char*)input_buf,
                        .length = array_len(input_buf) });

        if (idx == NULL && clog_log_level_get() <= CLOG_LOG_LEVEL_ERROR) {
            CLOG_ERROR("Key was not present in hashmap: ");
            clog_print_array_u8(input_buf, array_len(input_buf));
        }
        assert(idx != NULL);
        assert(*idx < lzw_hashmap_max_length);

        bit_array_push(&bit_array, *idx, code_size);
        _temp_i++;

        array_header(input_buf)->length = 0;
        array_append(input_buf, k);

        size_t next_code = hashmap.length + 2;

        // +2 for CLEAR and EOI codes.
        if (next_code >= lzw_hashmap_max_length) {
            bit_array_push(&bit_array, clear_code, code_size);

            CLOG_DEBUG("---CLEAR---");
            CLOG_DEBUG("WRITE Code[%d]: 0b%0*zb (%zu)",
                       _temp_i,
                       code_size,
                       clear_code,
                       clear_code);
            CLOG_DEBUG("Code size: %hhu", code_size);

            _temp_i++;
            hashmap_clear(&hashmap);
            lzw_hashmap_reset(&hashmap, eoi_code, allocator);
            code_size = min_code_size + 1;
            continue;
        } else if (next_code >= (1 << code_size)) {
            code_size++;
        }

        u8* appended_copy = (u8*)array_copy(appended, allocator);
        ByteString* key = make(ByteString, 1, allocator);
        key->length = array_len(appended);
        key->ptr = (char*)appended_copy;

        u16* val = make(u16, 1, allocator);
        *val = next_code;

        hashmap_insert(&hashmap, key, val);

        CLOG_DEBUG("~~~~FOUND~~~~");
        clog_printf_debug("Dict['");
        for (int i = 0; i < key->length; i++) {
            clog_printf_debug("<%d>", (unsigned char)key->ptr[i]);
        }
        clog_printf_debug("'] = %d\n", *val);
        CLOG_DEBUG(
          "WRITE Code[%d]: 0b%0*b (%d)", _temp_i, code_size, *idx, *idx);
        CLOG_DEBUG("to Byte[%zu]: ", array_len(bit_array.array) - 1);
        clog_printf_debug(
          "    0b%08b (0x%x)\n", bit_array.next_byte, bit_array.next_byte);
        for (int i = -6; i < 7 - bit_array.current_bit_idx; i++)
            clog_printf_debug(" ");
        clog_printf_debug("^");
        for (int i = 0; i < code_size; i++)
            clog_printf_debug("-");
        clog_printf_debug("\n");
        clog_printf_debug("    Code size: %hhu, Current Bit Index: %d\n",
                          code_size,
                          bit_array.current_bit_idx);
        CLOG_DEBUG("~~~~~~~~~~~~~");
    }

    u16* idx = hashmap_byte_string_get(
      &hashmap,
      (ByteString){ .ptr = (char*)input_buf, .length = array_len(input_buf) });

    assert(idx != NULL);

    bit_array_push(&bit_array, *idx, code_size);

    CLOG_DEBUG("WRITE Code[%d]: 0b%0*b (%d)", _temp_i, code_size, *idx, *idx);
    CLOG_DEBUG("Code size: %hhu", code_size);
    _temp_i++;

    bit_array_push(&bit_array, eoi_code, code_size);

    CLOG_DEBUG(
      "WRITE Code[%d]: 0b%0*zb (%zu)", _temp_i, code_size, eoi_code, eoi_code);
    CLOG_DEBUG("Code size: %hhu", code_size);
    _temp_i++;

    bit_array_pad_last_byte(&bit_array);
    *compressed_len = array_len(bit_array.array);
    CLOG_DEBUG("Dictionary length: %zu", hashmap.length);

    // CLOG_DEBUG("0x%x",
    //        bit_array.array[5608 - 73 - 22]);

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
    u16 color_amount = 1 << (N + 1);
    for (int i = 0; i < color_amount; i++) {
        memcpy(colors[i], bytes + i * sizeof(GIFColor), sizeof(GIFColor));
    }

    return color_amount * sizeof(GIFColor);
}

void
gif_write_global_color_table(VArena* gif_data, const GIFColor* colors)
{
    u8 N = ((u8*)gif_data->base)[10] & LSB_MASK(3);
    size_t color_amount = 1 << (N + 1);
    size_t i = 0;
    for (i = 0; i < color_amount; i++) {
        varena_push_copy(gif_data, &colors[i], sizeof(GIFColor));
    }
}

size_t
gif_read_graphic_control_extension(const u8* bytes, GIFGraphicControl* output)
{
    const u8 introducer = 0x21;
    const u8 control_label = 0xf9;
    const u8 block_size = 0x4;
    const u8 terminator = 0x00;

    size_t cursor = 0;
    if (bytes[cursor] != introducer) {
        fprintf(stderr,
                "Graphic Control Extension should start with byte 0x%x\n",
                introducer);
    }
    cursor += sizeof(u8);
    if (bytes[cursor] != control_label) {
        fprintf(stderr,
                "Graphic Control Extension should start with bytes 0x%x 0x%x\n",
                introducer,
                control_label);
    }
    cursor += sizeof(u8);
    if (bytes[cursor] != block_size) {
        fprintf(stderr,
                "Graphic Control Extension should have block size of %x\n",
                block_size);
    }
    cursor += sizeof(u8);

    u8 packed = bytes[cursor];
    cursor += sizeof(u8);

    output->disposal_method = (packed >> 3) & LSB_MASK(3);
    output->user_input_flag = (packed >> 1) & LSB_MASK(1);
    output->transparent_color_flag = packed & LSB_MASK(1);

    output->delay_time = bytes[cursor];
    cursor += sizeof(u16);
    output->transparent_color_index = bytes[cursor];
    cursor += sizeof(u8);

    if (bytes[cursor] != terminator) {
        fprintf(stderr,
                "Graphic Control Extension should end with terminator %x\n",
                terminator);
    }
    cursor += sizeof(u8);

    assert(cursor == 8);
    return cursor;
}

void
gif_write_graphics_control_extension(VArena* gif_data,
                                     GIFGraphicControl control)
{
    // u8 bytes[] = {
    //     0x21, 0xf9, 0x04, 0x01, 0x0a, 0x00, 0x1f, 0x00,
    // };
    // for (int i = 0; i < sizeof(bytes) / sizeof(u8); i++) {
    //     varena_push_copy(gif_data, &bytes[i], sizeof(u8));
    // }

    u8 introducer = 0x21;
    u8 control_label = 0xf9;
    u8 block_size = 0x4;
    varena_push_copy(gif_data, &introducer, sizeof(u8));
    varena_push_copy(gif_data, &control_label, sizeof(u8));
    varena_push_copy(gif_data, &block_size, sizeof(u8));

    u8 packed = 0;
    packed |= (control.disposal_method & LSB_MASK(3)) << 3;
    packed |= (control.user_input_flag & LSB_MASK(1)) << 1;
    packed |= (control.transparent_color_flag & LSB_MASK(1));
    varena_push_copy(gif_data, &packed, sizeof(u8));
    varena_push_copy(gif_data, &control.delay_time, sizeof(u16));
    varena_push_copy(gif_data, &control.transparent_color_index, sizeof(u8));

    u8 terminator = 0x00;
    varena_push_copy(gif_data, &terminator, sizeof(u8));
}

size_t
gif_read_img_descriptor(const u8* bytes, GIFMetadata* metadata)
{
    size_t cursor = 0;
    if (',' == *(bytes + cursor)) {
        cursor += sizeof(char);
    } else {
        CLOG_DEBUG(
          "Expected %02x at the beginning of image descriptor section.", ',');
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
gif_read_img_data(const u8* in_bytes, u8* lzw_min_code, u8* out_bytes)
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

    CLOG_DEBUG(
      "gif_read_img_data: Compressed data byte length was: %zu (without "
      "block lengths)",
      write_cursor);

    return read_cursor;
}

// Writes the compressed indices
// into data blocks as specified in the GIF specs.
void
gif_write_img_data(VArena* gif_data,
                   u8 lzw_min_code,
                   size_t max_block_length,
                   u8* bytes,
                   size_t bytes_length)
{
    varena_push_copy(gif_data, &lzw_min_code, sizeof(u8));
    size_t start = gif_data->used;

    size_t bytes_left = bytes_length;
    while (bytes_left) {
        u8 block_length =
          bytes_left >= max_block_length ? max_block_length : bytes_left;
        varena_push_copy(gif_data, &block_length, sizeof(u8));
        varena_push_copy(gif_data,
                         &bytes[bytes_length - bytes_left],
                         block_length * sizeof(u8));
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
        CLOG_ERROR("GIF Trailer was not provided. Ignoring...\n");
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
    if (file_data == NULL) {
        CLOG_ERROR("File data was NULL. Aborting GIF import\n");
        return;
    }

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

    if (file_data[cursor] == '!') {
        GIFGraphicControl graphic_control = { 0 };
        cursor += gif_read_graphic_control_extension(file_data + cursor,
                                                     &graphic_control);
    }

    cursor +=
      gif_read_img_descriptor(file_data + cursor, &gif_object->metadata);

    size_t pixel_amount =
      gif_object->metadata.width * gif_object->metadata.height;

    u8* compressed = array(u8, pixel_amount, &lzw_alloc);
    gif_read_img_data(
      file_data + cursor, &gif_object->metadata.min_code_size, compressed);

    gif_object->indices = calloc(pixel_amount, sizeof(u8));
    gif_decompress_lzw(compressed,
                       gif_object->metadata.min_code_size,
                       gif_object->indices,
                       &lzw_alloc);
    varena_destroy(&lzw_arena);
}

void
gif_export(GIFObject gif_object,
           size_t lzw_hashmap_max_length,
           size_t max_block_length,
           const char* out_path)
{
    VArena gif_data;
    varena_init_ex(&gif_data, GIF_ALLOC_SIZE, system_page_size(), 1);

    gif_write_header(&gif_data, gif_object.metadata.version);
    gif_write_logical_screen_descriptor(&gif_data, &gif_object.metadata);

    gif_write_global_color_table(&gif_data, gif_object.color_table);
    if (gif_object.metadata.has_graphic_control) {
        gif_write_graphics_control_extension(&gif_data,
                                             gif_object.graphic_control);
    }
    gif_write_img_descriptor(&gif_data, &gif_object.metadata);

    VArena lzw_arena;
    varena_init(&lzw_arena, LZW_ALLOC_SIZE);
    Allocator lzw_alloc = varena_allocator(&lzw_arena);

    CLOG_INFO("Before compress: %zu", gif_data.used);
    size_t compressed_len = 0;
    u8* compressed =
      gif_compress_lzw(&lzw_alloc,
                       lzw_hashmap_max_length,
                       gif_object.metadata.min_code_size,
                       gif_object.indices,
                       gif_object.metadata.width * gif_object.metadata.height,
                       &compressed_len);

    gif_write_img_data(&gif_data,
                       gif_object.metadata.min_code_size,
                       max_block_length,
                       compressed,
                       compressed_len);
    gif_write_trailer(&gif_data);

    FILE* file = fopen(out_path, "wb");
    if (file) {
        fwrite(gif_data.base, sizeof(char), gif_data.used, file);
        fclose(file);
    }
    CLOG_INFO("GIF exported to %s successfully.", out_path);
    CLOG_INFO("GIF Arena used: %.2f%% (%llu/%llu KB)",
              100.0f * gif_data.used / gif_data.size,
              gif_data.used / KILOBYTE,
              gif_data.size / KILOBYTE);
    CLOG_INFO("LZW Arena used: %.2f%% (%llu/%llu KB)",
              100.0f * lzw_arena.used / lzw_arena.size,
              lzw_arena.used / KILOBYTE,
              lzw_arena.size / KILOBYTE);

    varena_destroy(&gif_data);
    varena_destroy(&lzw_arena);
}
