#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "mongoose.h"
#include "sp_key.h"
#include "libspotify/api.h"

static sp_track *g_currenttrack;

static void send_reply(struct mg_connection *conn) {
	if(!strcmp(conn->uri, "/search")) {
		mg_printf_data(conn, "Search %s", conn->query_string);
		//Call search function
	} else if(!strcmp(conn->uri, "/upvote")) {
		mg_printf_data(conn, "Upvote %s", conn->query_string);
		//call upvote function
	} else if(!strcmp(conn->uri, "/queue")) {
		//call queue function
	} else if(!strcmp(conn->uri, "/key")) {
		//mg_printf_data(conn, "Key: %d", g_appkey[0]);
	} else {
	}
}
static int event_handler(struct mg_connection *conn, enum mg_event ev) {
	if(ev == MG_AUTH) {
		return MG_TRUE;
	} else if(ev == MG_REQUEST) {
		send_reply(conn);
		return MG_TRUE;
	} else {
		return MG_FALSE;
	}
}
int main()
{
	struct mg_server *server = mg_create_server(NULL, event_handler);
	mg_set_option(server, "Partyfy", ".");
	mg_set_option(server, "listening_port", "8080");

	for(;;) {
		mg_poll_server(server, 1000);
	}
	mg_destroy_server(&server);
}

/**
 * Check to see if a track is playing
 */
bool isPlaying() {
    return (g_playbackdone == 0);
}

static sp_link* get_search_track_link(sp_search *search, int index) {
    sp_track *track = sp_search_track(search, index);
    return (sp_link_create_from_track(track, 0));
}

/**
 * Prints the Artist - Title for each item in the search result
 */
static void print_search(sp_search *search) {
    int i;
    for (i=0; i<sp_search_num_tracks(search); i++) {
        sp_track *track = sp_search_track(search, i);
        if (track == NULL) {
            fprintf(stderr, "Search track was null.");
        }
        else {
            printf("%s - \"%s\"\n", sp_track_artist(track), sp_track_name(track));
        }
    }
}

}









