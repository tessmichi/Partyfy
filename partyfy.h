#ifndef PARTYFY
#define PARTYFY

#include <stdio.h>
#include <stdlib.h>
#include "libspotify/api.h"

void play(sp_session* session);

char* search(sp_session* session, char* search);

bool is_playing(sp_session* session);

void upvote(sp_session* session, sp_link* link);

void print_queue();

void pop_queue();


#endif
