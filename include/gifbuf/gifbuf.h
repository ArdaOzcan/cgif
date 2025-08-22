#ifndef GIFBUF_H
#define GIFBUF_H

#include <stdbool.h>
#include <stdint.h>

typedef uint8_t Color256RGB[3];

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
    bool image_extension;
} GIFMetadata;

void
gif_import(const uint8_t* file_data,
           GIFMetadata* metadata,
           uint8_t* pixels);

void
gif_export(GIFMetadata metadata,
           const Color256RGB* colors,
           const uint8_t* indices,
           const char* out_path);

#endif // GIFBUF_H
