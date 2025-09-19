#ifndef GIFBUF_H
#define GIFBUF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint8_t GIFColor[3];

typedef enum
{
    GIF87a,
    GIF89a
} GIFVersion;

typedef struct
{
    GIFVersion version;
    uint16_t left;
    uint16_t top;
    uint16_t width;
    uint16_t height;
    bool has_gct;
    uint8_t color_resolution;
    bool sort;
    uint8_t gct_size_n;
    uint8_t background;
    uint8_t pixel_aspect_ratio;
    uint8_t local_color_table;
    uint8_t min_code_size;
    bool has_graphic_control;
} GIFMetadata;

typedef struct
{
    uint8_t disposal_method;
    bool user_input_flag;
    bool transparent_color_flag;
    uint16_t delay_time;
    uint8_t transparent_color_index;
} GIFGraphicControl;

typedef struct
{
    GIFMetadata metadata;
    GIFColor* color_table;
    GIFGraphicControl graphic_control;
    uint8_t* indices;
} GIFObject;

void
gif_import(const uint8_t* file_data, GIFObject* gif_object);

void
gif_export(GIFObject gif_object,
           size_t lzw_hashmap_max_length,
           size_t max_block_length,
           const char* out_path);

size_t
gif_read_header(const uint8_t* header, GIFVersion* version);
size_t
gif_read_logical_screen_descriptor(const uint8_t* bytes, GIFMetadata* metadata);
size_t
gif_read_global_color_table(const uint8_t* bytes, uint8_t N, GIFColor* colors);
size_t
gif_read_graphic_control_extension(const uint8_t* bytes,
                                   GIFGraphicControl* graphic_control);
size_t
gif_read_img_descriptor(const uint8_t* bytes, GIFMetadata* metadata);

#endif // GIFBUF_H
