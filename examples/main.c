#include "../test/test-images/cat16.h"
#include "../test/test-images/cat256.h"
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
main(void)
{
    GIFMetadata metadata = (GIFMetadata){ .version = GIF87a,
                                          .background = 0x0f,
                                          .color_resolution = 6,
                                          .sort = 0,
                                          .local_color_table = 0,
                                          .pixel_aspect_ratio = 0,
                                          .min_code_size = 4,
                                          .gct_size_n = 3,
                                          .left = 0,
                                          .top = 0,
                                          .width = 5,
                                          .height = 5,
                                          .has_gct = true,
                                          .image_extension = false };

    // gif_export(metadata, cat16_colors, cat16_indices, "out/out16_test.gif");

    uint8_t out_indices[] = { 0, 1, 2, 3, 4, 5, 6,  7, 7, 0, 15, 15, 4,
                              5, 6, 7, 7, 0, 1, 15, 4, 5, 6, 7,  0 };
    gif_export(metadata, cat256_colors, out_indices, "out/test_indices.gif");

    size_t size = 0;
    unsigned char* bytes = read_file_to_buffer("out/test_indices.gif", &size);
    uint8_t* in_indices = malloc(metadata.width * metadata.height);
    size_t indices_length = 0;

    gif_import(&bytes[73], &metadata, in_indices);

    for (int i = 0; i < metadata.width * metadata.height; i++) {
        printf("%d: %d - %d\n", i, in_indices[i], out_indices[i]);
    }

    return 0;
}
