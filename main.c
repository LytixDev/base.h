//#define BASE_IMPLEMENTATION
#include "base.h"

#include <stdio.h>
//#include <stdlib.h>

/*
 * 1. Get a bunch of strings into an array
 * 2. Craete StrMap
 */

typedef struct str8_slice_t {
    Str8 *p;
    u64 len;
} Str8Slice;


extern int increased_map;


Str8Slice get_names(Arena *arena)
{
    int unique_names = 40000;
    Str8 *names = m_arena_alloc_array(arena, Str8, unique_names);

    FILE *file = fopen("firstnames.csv", "r");
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

void stress_generic(void)
{
    Arena arena;
    m_arena_init_dynamic(&arena, 256, 256); // allocate 1mb up front

    Str8Slice n = get_names(&arena);
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

    for (u64 c = 0; c < 32; c++) {
        for (u64 i = 0; i < 40000; i++) {
            hashmap_put(&map, names[i].str, names[i].len, (void *)(i + 1), sizeof(u64),false);
            if (increased_map_prev != increased_map) {
                increased_map_prev = increased_map;
                printf("increase map size @ %zu:%zu\n", c, i);
            }
            //printf("%s\n", names[i].str);
        }
    }
}



int main(void)
{
    // first stress test old
    stress_generic();

    // then create a new beautiful Str8Map
    // then stress test new
}
