#include "hashmap.h"
#include "gif.h"

u16
hash_str(char* key)
{
    u16 sum = 0;
    for (; *key != 0; key++) {
        sum += *key;
        sum *= *key;
    }

    return sum;
}

void
hashmap_clear(Hashmap* hashmap)
{
    for (int i = 0; i < hashmap->capacity; i++) {
        hashmap->records[i].type = EMPTY;
        hashmap->records[i].key = NULL;
        hashmap->records[i].value = NULL;
    }
    hashmap->length = 0;
}

Hashmap
hashmap_make(size_t capacity, Allocator* allocator)
{
    Hashmap hashmap;
    hashmap.records =
      allocator->alloc(sizeof(HashmapRecord) * capacity, allocator->context);
    hashmap.capacity = capacity;
    hashmap.length = 0;
    for (int i = 0; i < hashmap.capacity; i++) {
        hashmap.records[i].type = EMPTY;
        hashmap.records[i].key = NULL;
        hashmap.records[i].value = NULL;
    }
    return hashmap;
}

bool
hashmap_insert(Hashmap* hashmap, char* key, void* value)
{
    if (value == NULL)
        return false;

    u16 idx = hash_str(key) % hashmap->capacity;
    for (int i = 0; i < hashmap->capacity; i++) {
        HashmapRecord* record =
          &hashmap->records[(idx + i) % hashmap->capacity];

        if (record->type == EMPTY || record->type == DELETED) {
            record->key = key;
            record->value = value;
            record->type = FILLED;
            hashmap->length++;
            return true;
        } else if (record->type == FILLED && strcmp(record->key, key) == 0) {
            return false;
        }
    }

    return false;
}

void*
hashmap_get(Hashmap* hashmap, char* key)
{
    u16 hash = hash_str(key) % hashmap->capacity;
    for (int i = 0; i < hashmap->capacity; i++) {
        u16 idx = (hash + i) % hashmap->capacity;
        HashmapRecord* record = &hashmap->records[idx];
        if (record->type == EMPTY) {
            return NULL;
        }
        if (record->type == DELETED) {
            continue;
        }

        if (strcmp(key, record->key) == 0) {
            return record->value;
        }
    }

    return NULL;
}

void*
hashmap_delete(Hashmap* hashmap, char* key)
{
    u16 hash = hash_str(key) % hashmap->capacity;
    for (int i = 0; i < hashmap->capacity; i++) {
        u16 idx = (hash + i) % hashmap->capacity;
        HashmapRecord* record = &hashmap->records[idx];
        if (record->type == EMPTY) {
            return NULL;
        }
        if (record->type == DELETED) {
            continue;
        }

        if (strcmp(key, record->key) == 0) {
            void* temp = record->value;
            record->type = DELETED;
            record->key = NULL;
            record->value = NULL;
            return temp;
        }
    }

    return NULL;
}

void
hashmap_print(Hashmap* hashmap)
{
    printf("----START----\n");
    for (int i = 0; i < hashmap->capacity; i++) {
        HashmapRecord* record = &hashmap->records[i];
        if (record->type == EMPTY)
            printf("(%d) EMPTY\n", i);
        else if (record->type == DELETED)
            printf("(%d) DELETED\n", i);
        else
            printf("(%d) %s: %s\n", i, record->key, (char*)record->value);
    }
    printf("----END----\n");
}

//
// int
// main(void)
// {
//     Arena arena;
//     void* base = malloc(1024);
//     arena_init(&arena, base, 1024);
//     Allocator allocator = arena_alloc_init(&arena);
//
//     Hashmap hashmap = hashmap_make(10, &allocator);
//     char* names[10] = { "Arda",  "Cemal", "Orhan", "Kemal",    "Abdulkerim",
//                         "Semih", "Cemil", "Seda",  "Muhittin", "Asu" };
//     hashmap_print(&hashmap);
//     for (int i = 0; i < 10; i++) {
//         hashmap_insert(&hashmap, names[i], names[i]);
//         hashmap_print(&hashmap);
//         char* val = hashmap_get(&hashmap, names[i]);
//         assert(strcmp(val, names[i]) == 0);
//         hashmap_delete(&hashmap, names[i]);
//         hashmap_print(&hashmap);
//         val = hashmap_get(&hashmap, names[i]);
//         assert(val == NULL);
//     }
//
//     free(base);
//     return 0;
// }
