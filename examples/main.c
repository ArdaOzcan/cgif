#include "../test/test-images/cat256.h"
#include <gifbuf/gifbuf.h>

#include <stdbool.h>

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
                                          .width = 256,
                                          .height = 256,
                                          .image_extension = false,
                                          .has_gct = true };

    gif_export(metadata, cat256_colors, cat256_indices, "out/out256_test.gif");
    return 0;
}
