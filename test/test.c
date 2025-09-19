#include "munit.h"

#include "test-images/cat16.h"
#include "test-images/cat256.h"
#include "test-images/cat64.h"
#include "test-images/woman256.h"
#include "test-images/test.h"
#include <gifbuf/gifbuf.h>

#include <stdio.h>
#include <stdlib.h>

// Helper function to read a file's content into a dynamically allocated buffer
// Returns the buffer and sets the size in *file_size.
// Returns NULL on failure.
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
}

static void
dump_hex(const uint8_t* data, size_t length)
{
    for (int row = 0; row < length / 8; row++) {
        printf("%08x\t", row * 8);
        for (int i = 0; i < 8; i++) {
            printf("%02x ", data[row * 8 + i]);
        }
        printf("\t");
        for (int i = 0; i < 8; i++) {
            uint8_t c = data[row * 8 + i];
            if (33 < c && c < 126)
                printf("%c", c);
            else
                printf(".");
        }
        printf("\n");
    }
}

static MunitResult
assert_binary_files_equal(const char* fp1, const char* fp2)
{
    size_t file1_size, file2_size;

    // Load the contents of the first file
    unsigned char* file1_content = read_file_to_buffer(fp1, &file1_size);
    munit_assert_not_null(file1_content);

    // Load the contents of the second file
    unsigned char* file2_content = read_file_to_buffer(fp2, &file2_size);
    munit_assert_not_null(file2_content);

    // First, assert that the file sizes are the same. This is a quick check
    // and prevents comparing buffers of different sizes, which would fail
    // anyway.
    munit_assert_size(file1_size, ==, file2_size);

    // Now, assert that the contents of the buffers are identical.
    munit_assert_memory_equal(file1_size, file1_content, file2_content);

    // Don't forget to free the allocated memory.
    free(file1_content);
    free(file2_content);

    return MUNIT_OK;
}

static MunitResult
test_encode_256(const MunitParameter params[], void* user_data_or_fixture)
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
                                          .has_graphic_control = false,
                                          .has_gct = true };

    GIFObject gif_object = { .color_table = cat256_colors,
                             .indices = cat256_indices,
                             .metadata = metadata };
    gif_export(gif_object, 4096, 254, "out/out256_test.gif");
    assert_binary_files_equal("out/out256_test.gif",
                              "test/test-images/cat256.gif");

    return MUNIT_OK;
}

static MunitResult
test_encode_woman_256(const MunitParameter params[], void* user_data_or_fixture)
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
    gif_export(gif_object, 4097, 254, "out/test_woman_256.gif");

    assert_binary_files_equal("out/test_woman_256.gif",
                              "test/test-images/woman256.gif");

    return MUNIT_OK;
}

static MunitResult
test_encode_test_256(const MunitParameter params[], void* user_data_or_fixture)
{
    GIFMetadata metadata = { 0 };
    size_t size = 0;
    unsigned char* file_data =
      read_file_to_buffer("test/test-images/test.gif", &size);

    size_t cursor = 0;

    cursor += gif_read_header(file_data, &metadata.version);
    cursor += gif_read_logical_screen_descriptor(file_data + cursor, &metadata);
    GIFColor* colors =
      malloc(sizeof(GIFColor) * (1 << (metadata.gct_size_n + 1)));
    cursor += gif_read_global_color_table(
      file_data + cursor, metadata.gct_size_n, colors);
    metadata.min_code_size = 8;
    metadata.has_graphic_control = true;

    GIFGraphicControl graphic_control = { 0 };
    cursor +=
      gif_read_graphic_control_extension(file_data + cursor, &graphic_control);

    GIFObject gif_object = { .color_table = test_colors,
                             .indices = test_indices,
                             .graphic_control = graphic_control,
                             .metadata = metadata };
    gif_export(gif_object, 4097, 254, "out/test_test.gif");

    assert_binary_files_equal("out/test_test.gif",
                              "test/test-images/test.gif");

    return MUNIT_OK;
}

static MunitResult
test_encode_64(const MunitParameter params[], void* user_data_or_fixture)
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
                                          .has_graphic_control = true,
                                          .has_gct = true };

    GIFGraphicControl graphic_control =
      (GIFGraphicControl){ .disposal_method = 0,
                           .user_input_flag = false,
                           .transparent_color_flag = true,
                           .delay_time = 10,
                           .transparent_color_index = 0x1f };

    GIFObject gif_object = { .metadata = metadata,
                             .color_table = cat64_colors,
                             .indices = cat64_indices,
                             .graphic_control = graphic_control };

    gif_export(gif_object, 4096, 254, "out/out64_test.gif");
    assert_binary_files_equal("out/out64_test.gif",
                              "test/test-images/cat64.gif");

    return MUNIT_OK;
}

static MunitResult
test_encode_16(const MunitParameter params[], void* user_data_or_fixture)
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
                                          .has_graphic_control = false };

    GIFObject gif_object = { .color_table = cat16_colors,
                             .indices = cat16_indices,
                             .metadata = metadata };
    gif_export(gif_object, 4096, 254, "out/out16_test.gif");
    assert_binary_files_equal("out/out16_test.gif",
                              "test/test-images/cat16.gif");

    return MUNIT_OK;
}

static MunitResult
test_decode_16(const MunitParameter params[], void* user_data_or_fixture)
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
                                          .has_graphic_control = false };

    GIFObject gif_object = { .color_table = cat16_colors,
                             .indices = cat16_indices,
                             .metadata = metadata };
    gif_export(gif_object, 4096, 254, "out/out16_test.gif");

    size_t size = 0;
    unsigned char* bytes = read_file_to_buffer("out/out16_test.gif", &size);
    GIFObject imported_gif = { 0 };

    gif_import(bytes, &imported_gif);

    munit_assert_memory_equal(metadata.width * metadata.height *
                                sizeof(uint8_t),
                              imported_gif.indices,
                              cat16_indices);
    free(bytes);
    free(imported_gif.indices);
    free(imported_gif.color_table);

    return MUNIT_OK;
}

static MunitResult
test_decode_64(const MunitParameter params[], void* user_data_or_fixture)
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
                                          .has_graphic_control = true,
                                          .has_gct = true };

    GIFObject gif_object = { .color_table = cat64_colors,
                             .indices = cat64_indices,
                             .metadata = metadata };
    gif_export(gif_object, 4096, 254, "out/out64_test.gif");

    size_t size = 0;
    unsigned char* bytes = read_file_to_buffer("out/out64_test.gif", &size);
    GIFObject imported_gif = { 0 };

    gif_import(bytes, &imported_gif);

    munit_assert_memory_equal(metadata.width * metadata.height *
                                sizeof(uint8_t),
                              imported_gif.indices,
                              cat64_indices);
    free(bytes);
    free(imported_gif.indices);
    free(imported_gif.color_table);

    return MUNIT_OK;
}

static MunitResult
test_decode_256(const MunitParameter params[], void* user_data_or_fixture)
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
                                          .has_graphic_control = false,
                                          .has_gct = true };

    GIFObject gif_object = { .color_table = cat256_colors,
                             .indices = cat256_indices,
                             .metadata = metadata };
    gif_export(gif_object, 4096, 254, "out/out256_test.gif");

    size_t size = 0;
    unsigned char* bytes = read_file_to_buffer("out/out256_test.gif", &size);
    GIFObject imported_gif = { 0 };

    gif_import(bytes, &imported_gif);

    munit_assert_memory_equal(metadata.width * metadata.height *
                                sizeof(uint8_t),
                              imported_gif.indices,
                              cat256_indices);
    free(bytes);
    free(imported_gif.indices);
    free(imported_gif.color_table);

    return MUNIT_OK;
}

static MunitResult
test_read_metadata(const MunitParameter params[], void* user_data_or_fixture)
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
                                          .has_graphic_control = true,
                                          .has_gct = true };
    GIFMetadata old_metadata = metadata;
    GIFObject gif_object = { .metadata = metadata,
                             .indices = cat64_indices,
                             .color_table = cat64_colors };

    gif_export(gif_object, 4096, 254, "out/test_read_metadata.gif");

    size_t size = 0;
    unsigned char* file_data = read_file_to_buffer("out/out64_test.gif", &size);

    size_t cursor = 0;

    cursor += gif_read_header(file_data, &metadata.version);
    cursor += gif_read_logical_screen_descriptor(file_data + cursor, &metadata);
    GIFColor* colors =
      malloc(sizeof(GIFColor) * (1 << (metadata.gct_size_n + 1)));
    cursor += gif_read_global_color_table(
      file_data + cursor, metadata.gct_size_n, colors);

    munit_assert_memory_equal(sizeof(cat64_colors), cat64_colors, colors);

    if (metadata.has_graphic_control) {
        GIFGraphicControl graphic_control;
        cursor += gif_read_graphic_control_extension(file_data + cursor,
                                                     &graphic_control);
    }
    cursor += gif_read_img_descriptor(file_data + cursor, &metadata);

    munit_assert_memory_equal(sizeof(GIFMetadata), &metadata, &old_metadata);

    free(colors);
    free(file_data);

    return 0;
}

static MunitTest tests[] = {
    {
      "test_encode_16",       /* name */
      test_encode_16,         /* test */
      NULL,                   /* setup */
      NULL,                   /* tear_down */
      MUNIT_TEST_OPTION_NONE, /* options */
      NULL                    /* parameters */
    },
    {
      "test_decode_16",       /* name */
      test_decode_16,         /* test */
      NULL,                   /* setup */
      NULL,                   /* tear_down */
      MUNIT_TEST_OPTION_NONE, /* options */
      NULL                    /* parameters */
    },
    {
      "test_encode_64",       /* name */
      test_encode_64,         /* test */
      NULL,                   /* setup */
      NULL,                   /* tear_down */
      MUNIT_TEST_OPTION_NONE, /* options */
      NULL                    /* parameters */
    },
    {
      "test_decode_64",       /* name */
      test_decode_64,         /* test */
      NULL,                   /* setup */
      NULL,                   /* tear_down */
      MUNIT_TEST_OPTION_NONE, /* options */
      NULL                    /* parameters */
    },
    {
      "test_encode_256",      /* name */
      test_encode_256,        /* test */
      NULL,                   /* setup */
      NULL,                   /* tear_down */
      MUNIT_TEST_OPTION_NONE, /* options */
      NULL                    /* parameters */
    },
    {
      "test_decode_256",      /* name */
      test_decode_256,        /* test */
      NULL,                   /* setup */
      NULL,                   /* tear_down */
      MUNIT_TEST_OPTION_NONE, /* options */
      NULL                    /* parameters */
    },
    {
      "test_encode_test_256", /* name */
      test_encode_test_256,   /* test */
      NULL,                    /* setup */
      NULL,                    /* tear_down */
      MUNIT_TEST_OPTION_NONE,  /* options */
      NULL                     /* parameters */
    },
    {
      "test_encode_woman_256", /* name */
      test_encode_woman_256,   /* test */
      NULL,                    /* setup */
      NULL,                    /* tear_down */
      MUNIT_TEST_OPTION_NONE,  /* options */
      NULL                     /* parameters */
    },
    {
      "test_read_metadata",   /* name */
      test_read_metadata,     /* test */
      NULL,                   /* setup */
      NULL,                   /* tear_down */
      MUNIT_TEST_OPTION_NONE, /* options */
      NULL                    /* parameters */
    },
    /* Mark the end of the array with an entry where the test
     * function is NULL */
    { NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};

static const MunitSuite suite = {
    "/gif-compare",         /* name */
    tests,                  /* tests */
    NULL,                   /* suites */
    1,                      /* iterations */
    MUNIT_SUITE_OPTION_NONE /* options */
};

int
main(int argc, char* argv[])
{
    return munit_suite_main(&suite, NULL, argc, argv);
}
