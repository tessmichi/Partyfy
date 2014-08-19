#ifndef PARTYFY
#define PARTYFY

#include <stdio.h>
#include <stdlib.h>
#include "libspotify/api.h"

struct songInQueue
{
	int nVotes;
	sp_track* song;
	songInQueue* next;
	songInQueue* prev
}

void play();

char* search(char* search);

bool is_playing();

void upvote(sp_link* link);

char* print_queue();

void pop_queue();


#endif
