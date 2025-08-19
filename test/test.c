#include <gifbuf/gifbuf.h>
#include "test-images/cat16.h"
#include "test-images/cat64.h"
#include "test-images/cat256.h"
#include "munit.h"

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
                                          .image_extension = false,
                                          .has_gct = true };

    gif_export(metadata, cat256_colors, cat256_indices, "out/out256_test.gif");
    assert_binary_files_equal("out/out256_test.gif", "test/test-images/cat256.gif");

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
                                          .image_extension = true,
                                          .has_gct = true };

    gif_export(metadata, cat64_colors, cat64_indices, "out/out64_test.gif");
    assert_binary_files_equal("out/out64_test.gif", "test/test-images/cat64.gif");

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
                                          .image_extension = false };

    gif_export(metadata, cat16_colors, cat16_indices, "out/out16_test.gif");
    assert_binary_files_equal("out/out16_test.gif", "test/test-images/cat16.gif");

    return MUNIT_OK;
}

static MunitResult
test_decode_16(const MunitParameter params[], void* user_data_or_fixture)
{
    return MUNIT_FAIL;
}

static MunitTest tests[] = {
    {
      "test_encode_16",          /* name */
      test_encode_16,            /* test */
      NULL,                   /* setup */
      NULL,                   /* tear_down */
      MUNIT_TEST_OPTION_NONE, /* options */
      NULL                    /* parameters */
    },
    {
      "test_decode_16",          /* name */
      test_decode_16,            /* test */
      NULL,                   /* setup */
      NULL,                   /* tear_down */
      MUNIT_TEST_OPTION_NONE, /* options */
      NULL                    /* parameters */
    },
    {
      "test_encode_64",          /* name */
      test_encode_64,            /* test */
      NULL,                   /* setup */
      NULL,                   /* tear_down */
      MUNIT_TEST_OPTION_NONE, /* options */
      NULL                    /* parameters */
    },
    {
      "test_encode_256",         /* name */
      test_encode_256,           /* test */
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
