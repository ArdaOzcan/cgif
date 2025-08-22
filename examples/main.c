#include "../test/test-images/cat16.h"
#include "../test/test-images/cat64.h"
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
    GIFMetadata metadata = (GIFMetadata){ .version = GIF89a,
                                          .background = 0x10,
                                          .color_resolution = 2,
                                          .sort = 0,
                                          .local_color_table = 0,
                                          .pixel_aspect_ratio = 0,
                                          .min_code_size = 6,
                                          .gct_size_n = 5,
                                          .left = 0,
                                          .top = 0,
                                          .width = 64,
                                          .height = 64,
                                          .image_extension = true,
                                          .has_gct = true };

    gif_export(metadata, cat64_colors, cat64_indices, "out/out64_test.gif");

    size_t size = 0;
    unsigned char* bytes = read_file_to_buffer("out/out64_test.gif", &size);
    uint8_t* imported_indices = malloc(metadata.width * metadata.height);

    gif_import(&bytes[0xe1], &metadata, imported_indices);

    free(imported_indices);

    return 0;
}
