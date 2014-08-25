#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include "partyfy.h"
#include "mongoose.h"
#include "sp_key.h"
#include "audio.h"
#include "libspotify/api.h"

void upvoteHelper(const char* s);
//Output queue for audio
static audio_fifo_t g_audiofifo;
//Synchronization mutex for main
static pthread_mutex_t g_notify_mutex;
//Synchronization condition for main
static pthread_cond_t g_notify_cond;
//synchronization variable telling main to continue
static int g_notify_do;
//Non-zero when a track has ended and another has not started
static int g_playback_done;
//Global session pointer
static sp_session *g_sess;
//Name of the playlist
const char *g_listname;
//remove tracks flag
static int g_remove_tracks = 0;
//handle to current track
static sp_track *g_currenttrack;
//Index to next track
static int g_track_index;

static pthread_mutex_t g_search_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_search_cond = PTHREAD_COND_INITIALIZER;

songInQueue* firstSong;
songInQueue* lastSong;

#define TRUE 1
#define FALSE 0

static void try_playback_start(void) {
	sp_track *t;
	if(amtSongs() == 0) {
		fprintf(stderr, "Playlist: No tracks in playlist. Waiting\n");
		return;
	}
	t = sp_link_as_track(firstSong->song);
	if(g_currenttrack && t != g_currenttrack) {
		audio_fifo_flush(&g_audiofifo);
		sp_session_player_unload(g_sess);
		g_currenttrack = NULL;
	}
	
	if(!t) {
		printf("Null Song\n");
		fflush(stdout);
		return;
	}
	int next_timeout = 0;
	sp_session_process_events(g_sess, &next_timeout);
	while(sp_track_error(t) != SP_ERROR_OK) {
		sp_session_process_events(g_sess, &next_timeout);
	   	printf("Loading Track...\n");
        usleep(100000);
	}
	if(sp_track_error(t) != SP_ERROR_OK) {
		printf("SP_ERRoR\n");
		printf("%i\n", sp_track_error(t));
		fflush(stdout);
		return;
	}
	if(g_currenttrack == t) {
		printf("Coninuting same track\n");
		fflush(stdout);
		return;
	}
	g_currenttrack = t;
	printf("Partyfy: Now playing \"%s\"...\n", sp_track_name(t));
	fflush(stdout);
	sp_session_player_load(g_sess, t);
	sp_session_player_play(g_sess, 1);
}

/*---- Session Callback ----*/
static void logged_in(sp_session *sess, sp_error error) {
	if(SP_ERROR_OK != error) {
		fprintf(stderr, "Partyfy: Login Failed: %s\n",
				sp_error_message(error));
		exit(2);
	}
}
static void notify_main_thread(sp_session *sess) {
	pthread_mutex_lock(&g_notify_mutex);
	g_notify_do = 1;
	pthread_cond_signal(&g_notify_cond);
	pthread_mutex_unlock(&g_notify_mutex);
}
static int music_delivery(sp_session *sess, const sp_audioformat *format,
		const void *frames, int num_frames) {
	audio_fifo_t *af = &g_audiofifo;
	audio_fifo_data_t *afd;
	size_t s;
	if(num_frames == 0)
		return 0;
	pthread_mutex_lock(&af->mutex);
	if(af->qlen > format->sample_rate) {
		pthread_mutex_unlock(&af->mutex);
		return 0;
	}
	s = num_frames * sizeof(int16_t) * format->channels;
	afd = malloc(sizeof(*afd) + s);
	memcpy(afd->samples, frames, s);
	afd->nsamples = num_frames;
	afd->rate = format->sample_rate;
	afd->channels = format->channels;
	//TAILQ_INSERT_TAIL(&af->q, afd, link);
	af->qlen += num_frames;
	pthread_cond_signal(&af->cond);
	pthread_mutex_unlock(&af->mutex);
	return num_frames;
}
static void end_of_track(sp_session *sess) {
	pthread_mutex_lock(&g_notify_mutex);
	g_playback_done = 1;
	g_notify_do = 1;
	pthread_cond_signal(&g_notify_cond);
	pthread_mutex_unlock(&g_notify_mutex);
}
static sp_session_callbacks session_callbacks = {
	.logged_in = &logged_in,
	.notify_main_thread = &notify_main_thread,
	.music_delivery = &music_delivery,
	.log_message = NULL,
	.end_of_track = &end_of_track,
};
static sp_session_config spconfig = {
	.api_version = SPOTIFY_API_VERSION,
	.cache_location = "tmp",
	.settings_location = "tmp",
   	.application_key = g_appkey,
   	.application_key_size = 0, // Set in main()
    .user_agent = "Partyfy",
    .callbacks = &session_callbacks,
	NULL,
};
/*---- Session Callbacks end ----*/
static void track_ended(void) {
	int tracks = 0;
	if(g_currenttrack) {
		g_currenttrack = NULL;
		sp_session_player_unload(g_sess);
		pop_queue();
		try_playback_start();
	}
}

static void SP_CALLCONV search_complete(sp_search *search, void *userdata) {
    printf("search complete!\n");
    fflush(stdout);
//    pthread_mutex_lock(&g_search_mutex);
    if (sp_search_error(search) == SP_ERROR_OK) {
//        pthread_cond_signal(&g_search_cond);
        *((int*) userdata) = 1;
    }
    else {
        printf("Failed to search: %s\n",
                sp_error_message(sp_search_error(search)));
        fflush(stdout);
    }

    char* rv = search_to_json(search);
    puts(rv);
//    pthread_mutex_unlock(&g_search_mutex);
}

static void print_search_error(sp_search *search) {
    switch (sp_search_error(search)) {
        case SP_ERROR_OK:
            printf("ERROR_OK\n");
            break;
        case SP_ERROR_IS_LOADING:
            printf("ERROR_IS_LOADING\n");
            break;
        case SP_ERROR_OTHER_PERMANENT:
            printf("ERROR_OTHER_PERMANENT\n");
            break;
        case SP_ERROR_OTHER_TRANSIENT:
            printf("ERROR_OTHER_TRANSIENT\n");
            break;
        default:
            printf("I dunno what the problem is with search.\n");
    }
}

static void send_reply(struct mg_connection *conn) {
  if(!strcmp(conn->uri, "/search")) {
        printf("Received \"search\" request.\n");
        fflush(stdout);

		//mg_printf_data(conn, "Search %s", conn->query_string);
		//Call search function
		
        int isLoaded = 0;
        sp_search* search = sp_search_create(g_sess, conn->query_string, 0, 100, 0, 100, 0, 100, 0, 100, SP_SEARCH_STANDARD, &search_complete, &isLoaded);
		int timeout = 0;
		sp_session_process_events(g_sess, &timeout);
		while(!isLoaded) {
			usleep(100000);
			printf("Waiting...\n");
			sp_session_process_events(g_sess, &timeout);
		}
//    pthread_mutex_lock(&g_search_mutex);

//    while (!isLoaded) {
//    while(!sp_search_is_loaded(search)) {
//        printf("Waiting...\n");
//        fflush(stdout);
//        pthread_cond_wait(&g_search_cond, &g_search_mutex);
//        printConnectionState();
//        print_search_error(search);
//        usleep(1000000);
//    }
//    printf("Received search_complete signal.");
//    fflush(stdout);
    char* rv = search_to_json(search);
//    pthread_mutex_unlock(&g_search_mutex);

        sp_search_release(search);

        mg_printf_data(conn, rv);

	} else if(!strcmp(conn->uri, "/upvote")) {
		mg_printf_data(conn, "Upvote %s", conn->query_string);
		//call upvote function
		mg_printf_data(conn,"Upvoted");
		upvoteHelper(conn->query_string);
		try_playback_start();
	} else if(!strcmp(conn->uri, "/queue")) {
		//call queue function
		// TODO: print this to user not console printf(print_queue());
		mg_printf_data(conn,print_queue());
	} else if(!strcmp(conn->uri, "/key")) {
		//mg_printf_data(conn, "Key: %d", g_appkey[0]);
		// this is not part of our project. it will not be done.
	} else if(!strcmp(conn->uri, "/ping")) {
		mg_printf_data(conn,"Hello. Partyfy is running.");
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

	const char *username = "cs283project";
    //size_t size;
	char *password = getpass("Enter the password for the account\n");

	sp_session *sp;
	spconfig.application_key_size = g_appkey_size;
	sp_error err = sp_session_create(&spconfig, &sp);
	if(SP_ERROR_OK != err) {
		fprintf(stderr, "Unable to create session: %s\n",
				sp_error_message(err));
		exit(1);
	}
	g_sess = sp;
	pthread_mutex_init(&g_notify_mutex, NULL);
	pthread_cond_init(&g_notify_cond, NULL);

	sp_session_login(g_sess, username, password, 0, NULL);
    
    // See if the user logged in successfully
    printConnectionState();
    int timeout = 0;
    sp_connectionstate state = sp_session_connectionstate(g_sess);
    while (state != SP_CONNECTION_STATE_LOGGED_IN) {
		sp_session_process_events(g_sess, &timeout);
	   	printf("Logging in...\n");
        usleep(100000);
        state = sp_session_connectionstate(g_sess);
    }
	//audio_init(&g_audiofifo);
	printf("Logged in\n");
    for(;;) {
		mg_poll_server(server, 1000);
		sp_session_process_events(g_sess, &timeout);
		//while(!g_notify_do)
		//	pthread_cond_wait(&g_notify_cond, &g_notify_mutex);
		//g_notify_do = 0;
		//pthread_mutex_unlock(&g_notify_mutex);
		//if(g_playback_done) {
		//	g_playback_done = 0;
		//}
		//pthread_mutex_lock(&g_notify_mutex);
	}
	mg_destroy_server(&server);
}

void printConnectionState() {
    sp_connectionstate state = sp_session_connectionstate(g_sess);
    switch(state) {
        case SP_CONNECTION_STATE_LOGGED_OUT:
            printf(".User is logged out.\n");
            break;
        case SP_CONNECTION_STATE_LOGGED_IN:
            printf(".User is logged in.\n");
            break;
        case SP_CONNECTION_STATE_DISCONNECTED:
            printf(".Someone was logged in.. but now they're not.\n");
            break;
        case SP_CONNECTION_STATE_UNDEFINED:
            printf(".Connection state is undefined.\n");
            break;
        case SP_CONNECTION_STATE_OFFLINE:
            printf(".User in offline mode.\n");
            break;
        default:
            printf(".Not sure what the state is...");
            break;
        }
}

/**
 * Check to see if a track is playing
 */
bool isPlaying() {
    return (g_playback_done == 0);
}

/**
 * Return JSON for query results of search
 * Remember to free memory of return value when done with it.
 *
 * Returns NULL on error.
 */
char* search_to_json(sp_search *search) {
    int i;
    int nTracks = sp_search_num_tracks(search);

    // pointer passed to strcat_resize, so this can't be a char array
    int json_size = 1024;
    char* json = malloc(json_size * sizeof(char));
    memset(json, '\0', json_size * sizeof(char));

    if (json == NULL)
    {
        fprintf(stderr, "Failed to allocate memory for JSON string.\n");
        return NULL;
    }
    strcat(json, "{\"track_results\":");

    strcat(json, "[");
    for (i=0; i < nTracks; i++)
    {
        sp_track *track = sp_search_track(search,i);
        if (!track_to_json(track, &json, &json_size, 0))
        {
          fprintf(stderr, "Failed to set track info in JSON buffer.\n");
          if (json)
            free(json);
          return NULL;
        }
        if (i < nTracks - 1)
          strcat_resize(&json, &json_size, ",");
        // Release the track
//        sp_track_release(track);
		// TODO: delete this
		  //sp_link* l;
		  //char url[256];
		  //l = sp_link_create_from_track(track,0);
		  //sp_link_as_string(l,url,sizeof(url));
		  //printf("\t\t%s\n", url);
	 }
    strcat_resize(&json, &json_size, "]}");
    return json;
}

/**
 * Append the JSON representing the track to param json.
 * 
 * Returns TRUE if success - FALSE otherwise
 */
int track_to_json(sp_track* track, char** json, int* json_size, int count)
{
  int track_info_size = 256;
  char* append = malloc(track_info_size * sizeof(char));
  memset(append, '\0', track_info_size * sizeof(char));

  if (append == NULL)
  {
    fprintf(stderr, "Failed to allocate memory for track info.\n");
    return FALSE;
  }
        
  // Print track here (watch for quotes!)
  strcat(append, "{\"track_name\":\"");
  append_string_cleanse(&append, &track_info_size, sp_track_name(track));

  int j;
  int nArtists = sp_track_num_artists(track);
  // Print artists here (watch for quotes!)
  strcat_resize(&append, &track_info_size, "\",\"artists\":[\"");
  for (j=0; j<nArtists; j++)
  {
    sp_artist *artist = sp_track_artist(track, j);
    if (artist == NULL)
    {
      fprintf(stderr, "track artist retrieved was null.\n");
      if (append)
        free(append);
      return FALSE;
    }
    append_string_cleanse(&append, &track_info_size, sp_artist_name(artist));
    if (j < nArtists - 1)
      strcat_resize(&append, &track_info_size, "\",\"");
//    sp_artist_release(artist);
  }

  // Print album here (watch for quotes!)
  strcat_resize(&append, &track_info_size, "\"],\"album\":\"");
  sp_album *album = sp_track_album(track);
  append_string_cleanse(&append, &track_info_size, sp_album_name(album));
//  sp_album_release(album);

  // Print track url here (probably safe to assume there are no quotes here...)
  strcat_resize(&append, &track_info_size, "\",\"track_url\":\"");
  sp_link *l;
  char url[256];
  l = sp_link_create_from_track(track, 0);
  sp_link_as_string(l, url, sizeof(url));
  strcat_resize(&append, &track_info_size, url);
//  sp_link_release(l);
  char votes[5];
  sprintf(votes, "%d", count);
  strcat_resize(&append, &track_info_size, "\",\"votes\":\"");
  strcat_resize(&append, &track_info_size, votes);  


  strcat_resize(&append, &track_info_size, "\"}"); // close track_url quotes
  strcat_resize(json, json_size, append);

  if (append)
    free(append);
  return TRUE;
}

/**
 * Appends the source to dest, escaping double quotes and resizing dest as necessary.
 *
 * NOTE* does not cleanse dest. It only cleanses the appended string.
 *
 * Assumes no other characters besides double quotes and apostrophies 
 * exist in source that require escaping.
 */
void append_string_cleanse(char** dest, int* dest_size, const char* source)
{
    if (source == NULL) {
        fprintf(stderr, "[append_string_cleanse] source was NULL\n");
        return;
    }
    int sourceLen = strlen(source);
    char* append = malloc (2 * sourceLen + 1 * sizeof(char)); // guarantees enough space

    if (append == NULL) {
        fprintf(stderr, "Failed to allocate memory for cleansed string.\n");
        return;
    }

    int i=0;
    int pos=0;
    for (i=0; i<sourceLen; i++) {
        if (source[i] == '\"')
            append[pos++] = '\\';
        append[pos++] = source[i];
    }
    append[pos++] = '\0';
    
    strcat_resize(dest, dest_size, append);
    free(append);
}

/**
 * Allows string concatenation without worrying about buffer overflows.
 * Refactors dest size by powers of 2 as necessary.
 *
 * In case it fails to allocate memory for new size, it tries
 * to allocate the bare minimum. If that fails, it prints an error
 * and returns.
 *
 */
void strcat_resize(char** dest, int* dest_size, const char* source)
{
    if (source == NULL) {
        fprintf(stderr, "[strcat_resize] source was NULL\n");
        return;
    }
    int overage = strlen(source) + strlen(*dest) + 1 - (*dest_size);
    if (overage > 0)
    {
        // resize the dest string
        int refactorSize = 2;
        while (refactorSize * (*dest_size) < overage + (*dest_size))
            refactorSize *= 2;

        // malloc new memory for the string to be stored in
        // DO NOT use a char array, or it will free the memory when
        // it goes out of scope and return a dangling pointer.
        char* newString = malloc((*dest_size) * refactorSize * sizeof(char));
        if (newString == NULL)
        {
            newString = malloc((*dest_size) + overage);
            if (newString == NULL) {
                fprintf(stderr, "Failed to allocate memory for concatenated string.\n");
                return;
            }
        }
        strncpy(newString, *dest, strlen(*dest)+1); // adds 1 for null terminator
        if (*dest)
            free(*dest);
        *dest = newString;
        *dest_size = refactorSize * (*dest_size);
    }
    strcat(*dest, source);
}

/**
 * Prints the Title - Artists for each item in the search result
 */
static void print_search(sp_search *search) {
    int i;
    for (i=0; i<sp_search_num_tracks(search); i++) {
        sp_track *track = sp_search_track(search, i);
        if (track == NULL) {
            fprintf(stderr, "Search track was null.\n");
            return;
        }
        else {
            int artistBufferSize = 16;
            char* artistBuffer = malloc (artistBufferSize * sizeof(char));
            memset(artistBuffer, '\0', artistBufferSize * sizeof(char));
            int nArtists = sp_track_num_artists(track);
            int j;
            for (j=0; j<nArtists; j++) {
                sp_artist *artist = sp_track_artist(track, j);
                strcat_resize(&artistBuffer, &artistBufferSize, sp_artist_name(artist));
                if (j < nArtists - 1)
                    strcat_resize(&artistBuffer, &artistBufferSize, ",");
//                sp_artist_release(artist);
            }
            printf("\"%s\" - %s\n", sp_track_name(track), artistBuffer);
            
            free (artistBuffer);
        }
//        sp_track_release(track);
    }
}

int amtSongs()
{
	int count = 0;
	songInQueue* temp = firstSong;
	while (temp != NULL)
	{
		count++;
		temp = temp->next;
	}
	return count;
}

void enqueue(songInQueue* song)
{
	if (firstSong == NULL || firstSong->song == NULL)
	{
		firstSong = song;
		lastSong = song;
	}
	else
	{
		songInQueue* temp = firstSong;
		if (song->nVotes > temp->nVotes || temp->prev == NULL)
		{
			firstSong->prev=song;
			song->next = firstSong;
			firstSong = song;
		}
		else
		{
			while (song->nVotes < temp->nVotes)
			{
				if (temp->next == NULL)
					break;
				temp = temp->next;
			}
			if (temp->next == NULL && (song->nVotes < temp->nVotes))
			{
				lastSong->next = song;
				song->prev = lastSong;
				lastSong = song;
			}
			else
			{
				(temp->prev)->next = song;
				song->prev = temp->prev;
				song->next = temp;
				temp->prev = song;
			}
		}
	}
}

void upvoteHelper(const char* s)
{
	// turns s into sp_link
	sp_link* temp = sp_link_create_from_string(s);
	upvote(temp);
	// calls upvote
}

void upvote(sp_link* link)
{
	// This will be set to true if the song already exists in the queue
	int found = FALSE;

	// This will be updated as we cycle through the list
	songInQueue* temp = firstSong;
	// This will be the one we create IF it doesn't exist yet
	songInQueue* temp1 = malloc(sizeof(songInQueue));
	char s1[256];
	char s2[265];

	while (temp != NULL && temp -> song != NULL)
	{
		// Get links as strings for comparison
		sp_link_as_string(temp->song,s1,sizeof(s1));
		sp_link_as_string(link,s2,sizeof(s2));
		// If found, add a vote
		if (strcmp(s1,s2) == 0)
		{
			(temp->nVotes)++;
			// Set found to true so keep searching
			found = TRUE;
		}
		temp = temp->next;
	}
	if (found == FALSE)
	{
		// make the temp1 object
		temp1->song = link;
		temp1->next = NULL;
		temp1->prev = NULL;
		temp1->nVotes = 1;
		// add it with a nVotes starting at 1
		enqueue(temp1);
	}
}

char* print_queue()
{
    int json_size = 1024;
    char* json = malloc(json_size * sizeof(char));
    memset(json, '\0', json_size * sizeof(char));
    strcpy(json, "{\"queue\":[");
    songInQueue* temp = firstSong;
    while (temp != NULL && temp->song != NULL)
    { 
        if (temp->song == NULL) {
            printf("Warning: song was null for enqueued track.\n");
            fflush(stdout);
        }
        sp_track *track = sp_link_as_track(temp->song);
        if (track == NULL) {
            fprintf(stderr, "link was not for a track.\n");
            return NULL;
        }
        if (!track_to_json(track, &json, &json_size, temp->nVotes)) {
            fprintf(stderr, "Failed to set track info in JSON buffer\n");
            return NULL;
        }
        // if it's not the last element in the queue, print a comma
        if (temp->next != NULL) {
            strcat_resize(&json, &json_size, ",");
		}

		temp = temp->next;
		//sp_track_release(track);
    }
    strcat_resize(&json, &json_size, "]}");

    return json;
}

sp_link* pop_queue()
{
	songInQueue* ret;
	if (lastSong == NULL)
	{
		// No last song means no songs
		return NULL;
	}
	else if (lastSong->prev == NULL)
	{
		// No song before the last song means there is only 1 song
		
		songInQueue* temp;
		songInQueue* tt = lastSong;
		temp = tt;

		// Reset first and last song
		firstSong = NULL;
		lastSong = NULL;

		// Set the return value
		ret = temp;
	}
	else
	{
		// This one will be changed in the while loop
		songInQueue* temp = firstSong;
		// This is the one with most votes
		songInQueue* winner = firstSong;
		while (temp != NULL)
		{
			// This one has the lasrgest votes atm
			if (temp->nVotes > winner->nVotes)
			{
				winner = temp;
			}
			temp = temp->next;
		}

		// Update links in both directions
		if (winner->next != NULL)
		{
			(winner->next)->prev = winner->prev;
		}
		if (winner->prev != NULL)
		{
			(winner->prev)->next = winner->next;
		}
		winner->next = NULL;
		winner->prev = NULL;

		// Set return value
		ret = winner;
	}

	// Update last song
	songInQueue* temp = firstSong;
	while (temp->next != NULL)
	{
		temp = temp->next;
	}
	lastSong = temp;

	// Return the one you popped
	// TODO: return sp_track instead of sp_link
	return ret->song;
}















































