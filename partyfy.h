#ifndef PARTYFY
#define PARTYFY

#include <stdio.h>
#include <stdlib.h>
#include "libspotify/api.h"

void play();

char* search(char* search);

/**
 * Return JSON for query results of search
 * Remember to free memory of return value when done with it.
 *
 * Returns NULL on error.
 */
char* search_to_json(sp_search *search);

/**
 * Append the JSON representing the track to param json.
 * 
 * Returns TRUE if success - FALSE otherwise
 */
int track_to_json(sp_track* track, char** json, int* json_size);

/**
 * Appends the source to dest, escaping double quotes and resizing dest as necessary.
 *
 * NOTE* does not cleanse dest. It only cleanses the appended string.
 *
 * Assumes no other characters besides double quotes and apostrophies 
 * exist in source that require escaping.
 */
void append_string_cleanse(char** dest, int* dest_size, const char* source);

/**
 * Allows string concatenation without worrying about buffer overflows.
 * Refactors dest size by powers of 2 as necessary.
 *
 * In case it fails to allocate memory for new size, it tries
 * to allocate the bare minimum. If that fails, it prints an error
 * and returns.
 */
void strcat_resize(char** dest, int* dest_size, const char* source);

/**
 * Prints the Title - Artists for each item in the search result
 */
static void print_search(sp_search *search);

bool is_playing();

void upvote(sp_link* link);

char* print_queue();

void pop_queue();


#endif
