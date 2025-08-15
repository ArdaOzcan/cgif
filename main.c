#include "test-images/cat64.h"
#include "gif.h"

#include <stdbool.h>

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

    gif_export(metadata, cat64_colors, cat64_indices, "out/out.gif");
    return 0;
}
