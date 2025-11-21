//#define BASE_IMPLEMENTATION
#include "base.h"

// #include "mdc.h"

#include <stdio.h>
#include <stdlib.h>

#define ITER 128
#define N 40000

#define __STR1(x) #x
#define __STR(x) __STR1(x)
#define PLEASE_UNROLL(N) _Pragma(__STR(GCC unroll N))


#define SM_STARTING_BUCKETS_LOG2 3 // the amount of starting buckets
// Amount of entries in a bucket
// With 10 entries each bucket takes 256 bytes of memory which fits nicely into 4 cache lines
#define SM_BUCKET_LEN 8
#define SM_OVERFLOW_SIZE 4
#define SM_LOAD_FACTOR_PERCENT 75
#define SM_N_BUCKETS(log2) (1 << (log2))

typedef enum {
    INSERTION_FULL,
    INSERTION_OVERRIDE,
    INSERTION_SUCCESS,
} StrMapInsertionResult;

typedef struct strmap_result_t {
    bool ok;
    u64 value;
} StrMapResult;

typedef struct strmap_bucket_t {
    // NOTE: Nothing here is _owned_. The lifetime of the underlying keys and values is not managed.
    Str8 keys[SM_BUCKET_LEN];
    u64 values[SM_BUCKET_LEN];
} StrMapBucket;

typedef struct strmap_t {
    StrMapBucket *buckets;
    u32 buckets_log2; // we store 2**buckets_log2 number of buckets
    u32 len; // total items stored in the map
} StrMap;


typedef struct str8_slice_t {
    Str8 *p;
    u64 len;
} Str8Slice;


void strmap_init(StrMap *map, u32 starting_buckets_log2);
void strmap_free(StrMap *map);
void strmap_put(StrMap *map, Str8 key, u64 value);
StrMapResult strmap_get(StrMap *map, Str8 key);

/* === impl === */
void strmap_init(StrMap *map, u32 starting_buckets_log2)
{
    map->len = 0;
    map->buckets_log2 = starting_buckets_log2;

    u64 n_buckets = SM_N_BUCKETS(map->buckets_log2);
    map->buckets = malloc(n_buckets * sizeof(StrMapBucket));

    for (u64 i = 0; i < n_buckets; i++) {
        StrMapBucket *bucket = &map->buckets[i];

        PLEASE_UNROLL(SM_BUCKET_LEN)
        for (u64 j = 0; j < SM_BUCKET_LEN; j++) {
            bucket->keys[j].str = NULL;
        }
    }
}

void strmap_free(StrMap *map)
{
    free(map->buckets);
}

static u32 str8_hash(Str8 str)
{
    /* gigahafting kok (legger dermed ikke så mye lit til det) */
    u32 A = 1327217885;
    u32 k = 0;

    // TODO: instead of reading byte for byte we can read 64 bytes at a time
    for (size_t i = 0; i < str.len; i++) {
	    k += (k << 5) + ((u8 *)str.str)[i];
    }

    return k * A;
}

static StrMapInsertionResult bucket_insert(StrMapBucket *bucket, Str8 key, u64 value, u32 hash)
{

    /* Must check each entry in case the key is already stored */
    s64 found = -1;

    for (u64 i = 0; i < SM_BUCKET_LEN; i++) {
        Str8 entry_key = bucket->keys[i];
        if (entry_key.str == NULL) {
            found = i;
        }
        if (entry_key.str != NULL && STR8_EQUAL(key, entry_key)) {
            bucket->values[i] = value;
            return INSERTION_OVERRIDE;
        }
    }

    if (found == -1) {
        return INSERTION_FULL;
    }
    
    bucket->keys[found] = key;
    bucket->values[found] = value;
    return INSERTION_SUCCESS;
}

static void strmap_increase(StrMap *map)
{
    map->buckets_log2++;
    assert(map->buckets_log2 < 32);

    u64 old_n_buckets = N_BUCKETS(map->buckets_log2 - 1);
    u64 n_buckets = N_BUCKETS(map->buckets_log2);
    StrMapBucket *new_buckets = malloc(n_buckets * sizeof(StrMapBucket));
    /* Init the new buckets */
    for (u64 i = 0; i < n_buckets; i++) {
        StrMapBucket *bucket = &new_buckets[i];

        PLEASE_UNROLL(SM_BUCKET_LEN)
        for (u64 j = 0; j < SM_BUCKET_LEN; j++) {
            bucket->keys[j].str = NULL;
        }
    }

    /* Move all entries to the new buckets */
    // TODO: what if we get a collision here
    for (u64 i = 0; i < old_n_buckets; i++) {
        StrMapBucket *bucket = &map->buckets[i];
        for (u64 j = 0; j < SM_BUCKET_LEN; j++) {
            Str8 entry_key = bucket->keys[j];
            if (entry_key.str != NULL) {
                u32 hash = str8_hash(entry_key);
                u32 idx = hash >> (32 - map->buckets_log2);
                bucket_insert(&new_buckets[idx], entry_key, bucket->values[j], hash);
            }
        }
    }

    free(map->buckets);
    map->buckets = new_buckets;
}

void strmap_put(StrMap *map, Str8 key, u64 value)
{
    // Stolen from map.jai
    // Without dividing, we want to test:
    //    (filled / allocated >= 70/100)
    // Therefore, we say
    //    (filled * 100 >= allocated * 70)
    u64 entries_allocated = N_BUCKETS(map->buckets_log2) * SM_BUCKET_LEN;
    if ((map->len + 1) * 100 >= entries_allocated * SM_LOAD_FACTOR_PERCENT) {
        strmap_increase(map);
    }

    u32 hash = str8_hash(key);
    u32 idx = hash >> (32 - map->buckets_log2);
    StrMapInsertionResult rc = bucket_insert(&map->buckets[idx], key, value, hash);
    if (rc == INSERTION_FULL) {
        strmap_increase(map);
        strmap_put(map, key, value);
    }

    if (rc == INSERTION_SUCCESS) {
        map->len++;
    }
}

StrMapResult strmap_get(StrMap *map, Str8 key)
{
    u32 hash = str8_hash(key);
    u32 idx = hash >> (32 - map->buckets_log2);
    StrMapBucket bucket = map->buckets[idx];
    for (u64 i = 0; i < SM_BUCKET_LEN; i++) {
        Str8 entry_key = bucket.keys[i];
        if (entry_key.str != NULL && STR8_EQUAL(key, entry_key)) {
            return (StrMapResult){ .ok = true, .value = bucket.values[i] };
        }
    }

    return (StrMapResult){ .ok = false };
}


/* === */

extern int increased_map;


Str8Slice get_names(Arena *arena)
{
    int unique_names = 40000;
    Str8 *names = m_arena_alloc_array(arena, Str8, unique_names);

    FILE *file = fopen("firstnames_unique.csv", "r");
    if (file == NULL) {
        printf("Error: Could not open firstnames.csv\n");
        return (Str8Slice){ .p = names, .len = 0 };
    }

    int names_read = 0;
    size_t storage_size = arena->max_pages * arena->page_size -arena->offset;
    u8 *name_storage = m_arena_alloc(arena, storage_size);
    u8 *current_pos = name_storage;
    u8 *storage_end = name_storage + storage_size;

    int c;
    while ((c = fgetc(file)) != EOF && c != '\n') {
        // skip header line
    }

    // read each line char by char until first ';'
    // this is a bit stupid because each fgetc call probably performs a syscall but whatever
    while (names_read < unique_names && (c = fgetc(file)) != EOF) {
        u8 *name_start = current_pos;
        size_t name_len = 0;

        // read until semicolon
        while (c != EOF && c != ';') {
            // check if we have space for this char + null terminator
            if (current_pos + 1 >= storage_end) {
                printf("Warning: ran out of name storage space at %d names\n", names_read);
                fclose(file);
                return (Str8Slice){ .p = names, .len = names_read };
            }
            *current_pos = (u8)c;
            current_pos++;
            name_len++;
            c = fgetc(file);
        }

        // add null terminator
        *current_pos = 0;
        current_pos++;

        names[names_read].str = name_start;
        names[names_read].len = name_len;
        names_read++;

        // skip rest of line if we haven't reached newline yet
        while (c != EOF && c != '\n') {
            c = fgetc(file);
        }
    }

    fclose(file);
    return (Str8Slice){ .p = names, .len = names_read };
}

void stress_generic(Str8Slice n)
{
    Str8 *names = n.p;

    printf("Read %zu names\n", n.len);

    HashMap map;
    hashmap_init(&map);

    int increased_map_prev = increased_map;

    /*
     * INSTRUMENT_HASHMAP ifdef
     *
     * Data we're interested in: how much memory does the map use?
     * How slow is it?
     * How many hits and how many misses
     */

    for (u64 c = 0; c < ITER; c++) {
        for (u64 i = 0; i < N; i++) {
            hashmap_put(&map, names[i].str, names[i].len, (void *)(i + 1), sizeof(u64),false);
            if (increased_map_prev != increased_map) {
                increased_map_prev = increased_map;
                printf("increase map size @ %zu:%zu\n", c, i);
            }
            //printf("%s\n", names[i].str);
        }

        for (u64 i = 0; i < N; i++) {
            void *result = hashmap_get(&map, names[i].str, names[i].len);
            if (!(result != NULL && (u64)result == (i+1))) {
                printf("bad %zu\n", i);
            }
        }
    }

    hashmap_free(&map);
}

void stress_new(Str8Slice n)
{
    Str8 *names = n.p;

    StrMap map;
    strmap_init(&map, SM_STARTING_BUCKETS_LOG2);

    for (u64 c = 0; c < ITER; c++) {
        for (u64 i = 0; i < N; i++) {
            strmap_put(&map, names[i], i);
        }

        for (u64 i = 0; i < N; i++) {
            StrMapResult result = strmap_get(&map, names[i]);
            if (!(result.ok && result.value == i)) {
                printf("bad %zu\n", i);
            }
        }
    }


    strmap_free(&map);
}



int main(void)
{
    Arena arena;
    m_arena_init_dynamic(&arena, 256, 256); // allocate 1mb up front
    Str8Slice n = get_names(&arena);

    //stress_generic(n);

    stress_new(n);

    //size_t bucket_size = sizeof(StrMapBucket);
    //bool is_factor_of_cache_size = bucket_size % 64 == 0;
    //printf("%zu %d\n", bucket_size, is_factor_of_cache_size);


    m_arena_release(&arena);
}
