#ifndef INTERFACE_H
#define INTERFACE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
} Color256RGB;

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
gif_export(GIFMetadata metadata,
           const Color256RGB* colors,
           const uint8_t* indices,
           const char* out_path);


#endif // INTERFACE_H
