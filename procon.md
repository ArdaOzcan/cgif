# Option 1: Enforce String struct with length

```c
typedef struct {
char * str;
size_t length;
} String;
```

## Pros:

- Easy to implement
- Easy to understand
- Nice addition to library

## Cons:

- We would need a wrapper function for null-terminated strings
  to find the length with strlen()
- Harder to use on other projects (well not if we do the wrapper mentioned)

# Option 2: User provides hash function

User can have its own String type. Hash the string type accordingly.

## Pros:

- Keeps the core library more usage-agnostic
- More flexible hashing.

## Cons:

- Adds complexity to hashmap API.
- Hashmap would have to take void*, not type safe
  e.g. a hashmap_insert(&hashmap, (void *)random_integer) would probably segfault when hashed.
