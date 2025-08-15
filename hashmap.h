#ifndef HASHMAP_H
#define HASHMAP_H

#include "gif.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

typedef enum
  : u8
{
    FILLED,
    EMPTY,
    DELETED
} HashmapRecordType;

typedef struct
{
    HashmapRecordType type;
    char* key;
    void* value;
} HashmapRecord;

typedef struct
{
    HashmapRecord* records;
    size_t capacity;
    size_t length;
} Hashmap;

u16
hash_str(char* key);

void
hashmap_clear(Hashmap* hashmap);

Hashmap
hashmap_make(size_t capacity, Allocator* allocator);

bool
hashmap_insert(Hashmap* hashmap, char* key, void* value);

void*
hashmap_get(Hashmap* hashmap, char* key);

void*
hashmap_delete(Hashmap* hashmap, char* key);

void
hashmap_print(Hashmap* hashmap);

size_t
hashmap_len(Hashmap* hashmap);

#endif
