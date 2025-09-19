#include "../test/test-images/cat16.h"
#include "../test/test-images/cat256.h"
#include "../test/test-images/cat64.h"
#include "../test/test-images/woman256.h"
#include <gifbuf/gifbuf.h>

#include <stdbool.h>

#include <stdio.h>
#include <stdlib.h>

unsigned char*
read_file_to_buffer(const char* filename, size_t* file_size)
{
    FILE* file = fopen(filename, "rb");
    if (!file) {
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    *file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    unsigned char* buffer = (unsigned char*)malloc(*file_size);
    if (!buffer) {
        fclose(file);
        return NULL;
    }

    if (fread(buffer, 1, *file_size, file) != *file_size) {
        free(buffer);
        fclose(file);
        return NULL;
    }

    fclose(file);
    return buffer;
};

int
_main(void)
{
    size_t size = 0;
    unsigned char* bytes = read_file_to_buffer("test/test-images/woman256.gif", &size);
    GIFObject imported_gif = { 0 };

    gif_import(bytes, &imported_gif);

    free(bytes);
    free(imported_gif.indices);
    free(imported_gif.color_table);

    return 0;
}

#include "raylib.h"
#include <stdint.h>
#include <stdlib.h>

int
main(void)
{
    const int screenWidth = 512;
    const int screenHeight = 512;

    InitWindow(screenWidth, screenHeight, "Pixel buffer example");

    size_t size = 0;
    unsigned char* bytes =
      read_file_to_buffer("test/test-images/bird512.gif", &size);
    GIFObject gif_object = { 0 };
    gif_import(bytes, &gif_object);

    uint32_t* pixels = malloc(gif_object.metadata.width *
                              gif_object.metadata.height * sizeof(uint32_t));

    // Fill with something (gradient)
    for (int y = 0; y < screenHeight; y++) {
        for (int x = 0; x < screenWidth; x++) {
            unsigned char r =
              gif_object
                .color_table[gif_object.indices[y * screenWidth + x]][0];
            unsigned char g =
              gif_object
                .color_table[gif_object.indices[y * screenWidth + x]][1];
            unsigned char b =
              gif_object
                .color_table[gif_object.indices[y * screenWidth + x]][2];
            unsigned char a = 255;
            pixels[y * screenWidth + x] = ((uint32_t)a << 24) |
                                          ((uint32_t)b << 16) |
                                          ((uint32_t)g << 8) | ((uint32_t)r);
        }
    }

    // Create a raylib Image from pixel buffer
    Image image = { .data = pixels,
                    .width = screenWidth,
                    .height = screenHeight,
                    .mipmaps = 1,
                    .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };

    // Upload to GPU
    Texture2D texture = LoadTextureFromImage(image);

    // Now we can free the CPU buffer if we don’t need to update per-frame
    free(pixels);

    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(BLACK);

        DrawTexture(texture, 0, 0, WHITE);

        EndDrawing();
    }

    UnloadTexture(texture);
    CloseWindow();

    return 0;
}

int
__main(void)
{
    GIFMetadata metadata = { 0 };
    size_t size = 0;
    unsigned char* file_data =
      read_file_to_buffer("test/test-images/woman256.gif", &size);

    size_t cursor = 0;

    cursor += gif_read_header(file_data, &metadata.version);
    cursor += gif_read_logical_screen_descriptor(file_data + cursor, &metadata);
    GIFColor* colors =
      malloc(sizeof(GIFColor) * (1 << (metadata.gct_size_n + 1)));
    cursor += gif_read_global_color_table(
      file_data + cursor, metadata.gct_size_n, colors);
    metadata.min_code_size = 8;
    metadata.has_graphic_control = false;

    GIFGraphicControl graphic_control = { 0 };

    GIFObject gif_object = { .color_table = woman256_colors,
                             .indices = woman256_indices,
                             .graphic_control = graphic_control,
                             .metadata = metadata };
    gif_export(gif_object, 4096, 254, "out/test_woman_256.gif");
    return 0;
}
