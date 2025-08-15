#include "test-images/cat16.h"
#include "gif.h"

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
                                          .width = 16,
                                          .height = 16,
                                          .has_gct = true,
                                          .image_extension = false };

    gif_export(metadata, cat16_colors, cat16_indices, "out/out16_test.gif");
    return 0;
}
