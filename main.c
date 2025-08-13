#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;

#define ArraySize(arr, type) sizeof(arr) / sizeof(type)

#define WIDTH 3
#define HEIGHT 4
#define LZW_MIN_BIT 2
#define COLOR_RES 2
#define GCT_SIZE_POW 1

#define KILOBYTE 1024

// Normally 4096
#define MAX_DICT_LENGTH 256
#define GIF_ALLOC_SIZE 1 * KILOBYTE
#define LZW_ALLOC_SIZE 32 * MAX_DICT_LENGTH

typedef struct
{
    void* base;
    size_t used;
    size_t size;
} Arena;

void
InitArena(Arena* arena, void* base, size_t size)
{
    arena->base = base;
    arena->size = size;
    arena->used = 0;
}

void*
PushSize(Arena* arena, size_t size)
{
    arena->used += size;
    if (arena->used > arena->size) {
        printf("Arena is full\n");
        return NULL;
    }

    return arena->base + arena->used - size;
}

#define PushArray(arena, type, length)                                         \
    (type*)PushSize(arena, sizeof(type) * length)

void
CopySize(Arena* arena, const void* data, size_t size)
{
    memcpy(PushSize(arena, size), data, size);
}

typedef struct
{
    u8 r;
    u8 g;
    u8 b;
} Color256RGB;

void
WriteGIFHeader(Arena* arena)
{
    memcpy(PushSize(arena, 6), "GIF89a", 6);
}

void
WriteGIFLogicalScreenDescriptor(Arena* arena,
                                u16 width,
                                u16 height,
                                bool gct,
                                u8 colorResolution,
                                bool sort,
                                u8 gctSize,
                                u8 background,
                                u8 pixelAspectRatio)
{
    CopySize(arena, &width, sizeof(u16));
    CopySize(arena, &height, sizeof(u16));

    u8 packed = 0;
    packed |= gct << 7;                     // 1000 0000
    packed |= (colorResolution & 0x7) << 4; // 0111 0000
    packed |= sort << 3;                    // 0000 1000
    packed |= (gctSize & 0x7);              // 0000 0111

    CopySize(arena, &packed, sizeof(u8));
    CopySize(arena, &background, sizeof(u8));
    CopySize(arena, &pixelAspectRatio, sizeof(u8));
}

void
WriteGIFGlobalColorTable(Arena* arena, Color256RGB* colors)
{
    u8 N = ((u8*)arena->base)[10] & 0x3;
    u8 colorAmount = 1 << (N + 1);
    for (u8 i = 0; i < colorAmount; i++) {
        CopySize(arena, &colors[i], sizeof(Color256RGB));
    }
}

void
WriteGIFImageDescriptor(Arena* arena,
                        u16 left,
                        u16 top,
                        u16 width,
                        u16 height,
                        u8 localColorTable)
{
    CopySize(arena, ",", sizeof(char));
    CopySize(arena, &left, sizeof(u16));
    CopySize(arena, &top, sizeof(u16));
    CopySize(arena, &width, sizeof(u16));
    CopySize(arena, &height, sizeof(u16));
    CopySize(arena, &localColorTable, sizeof(u8));
}

void
WriteGIFImageData(Arena* arena,
                  u8 lzwMinCodeSize,
                  u8* pixels,
                  size_t pixelsLength)
{
    CopySize(arena, &lzwMinCodeSize, sizeof(u8));

    size_t counter = 0;
    while (pixelsLength > counter) {
        u8 blockLength = pixelsLength >= 255 ? 255 : pixelsLength;
        CopySize(arena, &blockLength, sizeof(u8));
        CopySize(arena, &pixels[counter], blockLength * sizeof(u8));
        counter += blockLength;
    }

    const u8 terminator = '\0';
    CopySize(arena, &terminator, sizeof(u8));
}

void
WriteGIFTrailer(Arena* arena)
{
    const u8 trailer = 0x3B;
    CopySize(arena, &trailer, 1);
}

typedef struct
{
    u8* buffer;
    u8 totalByteLength;

    u8 currentByte;
    u8 currentBitIndex;
} BitArray;

void
InitBitArray(BitArray* bitArray, u8* buffer)
{
    bitArray->buffer = buffer;
    bitArray->currentBitIndex = 0;
    bitArray->totalByteLength = 0;
    bitArray->currentByte = 0;
}

u16
GetMSBMask(u16 length)
{
    u16 nBits = (1 << length) - 1;
    return nBits << (8 - length);
}

u16
GetLSBMask(u16 length)
{
    u16 nBits = (1 << length) - 1;
    return nBits;
}

void
AddBits(BitArray* bitArray, u16 data, u8 bitAmount)
{
    u8 bitsLeft = bitAmount;
    u8 splitBitAmount = 0;
    while (bitArray->currentBitIndex + bitsLeft > 8) {
        splitBitAmount = 8 - bitArray->currentBitIndex;

        u8 mask = GetLSBMask(splitBitAmount);
        bitArray->currentByte |= (data & mask) << bitArray->currentBitIndex;
        bitArray->currentBitIndex += splitBitAmount;
        bitsLeft -= splitBitAmount;
        data >>= splitBitAmount;

        bitArray->buffer[bitArray->totalByteLength] = bitArray->currentByte;
        bitArray->totalByteLength++;
        bitArray->currentByte = 0;
        bitArray->currentBitIndex = 0;
    }

    u8 mask = GetLSBMask(bitsLeft);
    bitArray->currentByte |= (data & mask) << bitArray->currentBitIndex;
    bitArray->currentBitIndex += bitsLeft;
}

void
PadLastByte(BitArray* bitArray)
{
    bitArray->buffer[bitArray->totalByteLength] = bitArray->currentByte;
    bitArray->totalByteLength++;
    bitArray->currentByte = 0;
    bitArray->currentBitIndex = 0;
}

int
FindInArray(u8* arr, const u8* value, size_t arrLength, size_t typeSize)
{
    for (size_t i = 0; i < arrLength; i++) {
        u8* element = &arr[typeSize * i];
        int result = memcmp(element, value, typeSize);
        if (result == 0)
            return i;
    }

    return -1;
}

int
FindInDictionary(const char** dict,
                 size_t dictLength,
                 const char* value,
                 size_t valueLength)
{
    for (size_t i = 0; i < dictLength; i++) {
        const char* str = dict[i];
        if (strlen(str) != valueLength)
            continue;
        int result = memcmp(str, value, valueLength);
        if (result == 0)
            return i;
    }

    return -1;
}

void
AddToDictionary(Arena* arena,
                const char** dictionary,
                size_t dictionaryLength,
                const char* value,
                size_t valueLength)
{
    char* inputCopy = PushArray(arena, char, 256);
    memcpy(inputCopy, value, valueLength);
    inputCopy[valueLength] = '\0';

    dictionary[dictionaryLength] = inputCopy;
}

void
CompressLZW(Arena* arena,
            u8 minimumCodeSize,
            const u8* indices,
            size_t indicesLength,
            u8* compressed,
            size_t* compressedLength)
{
    const char** dictionary = PushArray(arena, const char*, MAX_DICT_LENGTH);
    size_t dictionaryLength = (1 << minimumCodeSize) + 2;
    const size_t clearCode = 1 << minimumCodeSize;
    const size_t eoiCode = clearCode + 1;
    for (u8 i = 0; i < dictionaryLength; i++) {
        char* data = PushArray(arena, char, 2);
        data[0] = '0' + i;
        data[1] = '\0';
        dictionary[i] = data;
    }

    BitArray bitArray;
    InitBitArray(&bitArray, compressed);
    u8 codeSize = minimumCodeSize + 1;
    AddBits(&bitArray, clearCode, codeSize);

    size_t inputBufferLength = 0;
    char* inputBuffer = PushArray(arena, char, 256);
    for (size_t i = 0; i < indicesLength; i++) {
        inputBuffer[inputBufferLength] = '0' + indices[i];
        inputBuffer[inputBufferLength + 1] = '\0';
        inputBufferLength++;

        int result = FindInDictionary(
          dictionary, dictionaryLength, inputBuffer, inputBufferLength);
        printf("INPUT: %s\n", inputBuffer);

        if (result < 0) {
            AddToDictionary(arena,
                            dictionary,
                            dictionaryLength,
                            inputBuffer,
                            inputBufferLength);
            dictionaryLength++;

            // printf("#%zu: %s\n",
            //        dictionaryLength - 1,
            //        dictionary[dictionaryLength - 1]);

            int idx = FindInDictionary(
              dictionary, dictionaryLength, inputBuffer, inputBufferLength - 1);
            assert(idx >= 0);

            AddBits(&bitArray, idx, codeSize);

            inputBuffer[0] = inputBuffer[inputBufferLength - 1];
            inputBuffer[1] = '\0';
            inputBufferLength = 1;

            if (dictionaryLength > (1 << codeSize)) {
                codeSize++;
            }
        }
    }

    int idx = FindInDictionary(
      dictionary, dictionaryLength, inputBuffer, inputBufferLength);
    assert(idx >= 0);

    AddBits(&bitArray, idx, codeSize);

    AddBits(&bitArray, eoiCode, codeSize);
    PadLastByte(&bitArray);
    *compressedLength = bitArray.totalByteLength;
}

int
main()
{
    Arena gifData;
    void* gifBase = malloc(GIF_ALLOC_SIZE);
    InitArena(&gifData, gifBase, GIF_ALLOC_SIZE);

    WriteGIFHeader(&gifData);
    WriteGIFLogicalScreenDescriptor(
      &gifData, WIDTH, HEIGHT, true, COLOR_RES, false, GCT_SIZE_POW, 1, 0);

    Color256RGB colors[] = {
        { 255, 0, 0 },
        { 0, 255, 0 },
        { 0, 0, 255 },
        { 255, 255, 255 },
    };
    WriteGIFGlobalColorTable(&gifData, colors);
    WriteGIFImageDescriptor(&gifData, 0, 0, WIDTH, HEIGHT, 0);

    Arena lzwArena;
    void* lzwBase = malloc(LZW_ALLOC_SIZE);
    InitArena(&lzwArena, lzwBase, LZW_ALLOC_SIZE);

    u8 indices[] = { 1, 1, 1, 0, 0, 0, 3, 3, 3, 2, 2, 2 };
    u8* compressed = PushArray(&lzwArena, u8, 1024);
    size_t compressedLength = 0;
    CompressLZW(&lzwArena,
                LZW_MIN_BIT,
                indices,
                ArraySize(indices, u8),
                compressed,
                &compressedLength);
    for (int i = 0; i < compressedLength; i++) {
        printf("%02x ", compressed[i]);
    }
    printf("\n");

    WriteGIFImageData(&gifData, LZW_MIN_BIT, compressed, compressedLength);
    WriteGIFTrailer(&gifData);

    FILE* file = fopen("out.gif", "w");

    fwrite(gifData.base, sizeof(char), gifData.used, file);

    fclose(file);

    BitArray bitArray;
    InitBitArray(&bitArray, PushArray(&lzwArena, u8, 16));
    AddBits(&bitArray, 4, 3);
    AddBits(&bitArray, 1, 3);
    AddBits(&bitArray, 6, 3);
    AddBits(&bitArray, 0, 3);
    AddBits(&bitArray, 8, 4);
    AddBits(&bitArray, 3, 4);
    AddBits(&bitArray, 10, 4);
    AddBits(&bitArray, 2, 4);
    AddBits(&bitArray, 12, 4);
    AddBits(&bitArray, 5, 4);
    PadLastByte(&bitArray);
    for (int i = 0; i < bitArray.totalByteLength; i++) {
        printf("%02x ", bitArray.buffer[i]);
    }
    printf("\n");

    printf("GIF Arena used: %zu\n", gifData.used);
    printf("LZE Arena used: %zu\n", lzwArena.used);
    printf("\n");
    free(gifBase);
    free(lzwBase);
    return 0;
}
