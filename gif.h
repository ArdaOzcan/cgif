#ifndef GIF_H
#define GIF_H

#include "core.h"

#include <stdbool.h>

#define GIF_ALLOC_SIZE 1 * MEGABYTE
#define LZW_ALLOC_SIZE 2 * MEGABYTE

#define GIF_MAX_BLOCK_LENGTH 254
#define INPUT_BUFFER_CAP 256

#define LZW_DICT_MAX_CAP 4096
#define LZW_DICT_MIN_CAP 2048

#define BIT_ARRAY_MIN_CAP 2 * KILOBYTE

typedef struct
{
    u8 r;
    u8 g;
    u8 b;
} Color256RGB;

typedef enum
{
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

BitArray
bit_array_init(u8* buffer);

void
bit_array_push(BitArray* bit_array, u16 data, u8 bit_amount);

void
bit_array_pad_last_byte(BitArray* bitArray);

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
