#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>
#include <semaphore.h>

#define BUFFER_SIZE 1024

pthread_mutex_t init = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t IO_stream = PTHREAD_MUTEX_INITIALIZER;

// struct to hold file data within cache
typedef struct cached_resp{

	char* name;
	char* contents;
	int content_len;
	time_t cache_time;


} cached_resp;

// remove prior caches, only store most recent 5

// linked list to hold 5 most recently cached files
typedef struct linked {

	cached_resp* data;
	struct linked* next_cache;

} cache_list;

cache_list* cache_head = NULL;

void *handler(void*);

int main()
{
	//Sockets represent potential connections
	//We make an internet socket
	int sfd = socket(PF_INET, SOCK_STREAM, 0);
	if(-1 == sfd)
	{
		perror("Cannot create socket\n");
		exit(EXIT_FAILURE);
	}

	//We will configure it to use this machine's IP, or
	//for us, localhost (127.0.0.1)
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));

	addr.sin_family = AF_INET;
	//Web servers always listen on port 80
	addr.sin_port = htons(80);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	//So we bind our socket to port 80
	if(-1 == bind(sfd, (struct sockaddr *)&addr, sizeof(addr)))
	{
		perror("Bind failed");
		exit(EXIT_FAILURE);
	}

	//And set it up as a listening socket with a backlog of 10 pending connections
	if(-1 == listen(sfd, 10))
	{
		perror("Listen failed");
		exit(EXIT_FAILURE);
	}

	//A server's gotta serve...
	for(;;)
	{
		//accept() blocks until a client connects. When it returns,
		//we have a client that we can do client stuff with.
		int *connfd = malloc(sizeof(int));
        *connfd = accept(sfd, NULL, NULL);

		if(connfd < 0)
		{
			perror("Accept failed");
			exit(EXIT_FAILURE);
		}

		// connection has been made

	// creates pthread
	pthread_t thread_id;
        pthread_create(&thread_id, NULL, handler, (void *) connfd);
        pthread_detach(thread_id);

	}
	close(sfd);
	return 0;
}

void send_header(int connfd, int content_len){

	int size;
	char *response = (char *)malloc(BUFFER_SIZE * sizeof(char));

	strcpy(response, "HTTP/1.1 200 OK\n");
	send(connfd, response, strlen(response), 0);

	time_t now;
	time(&now);
	//How convenient that the HTTP Date header field is exactly
	//in the format of the asctime() library function.
	//
	//asctime adds a newline for some dumb reason.
	sprintf(response, "Date: %s", asctime(gmtime(&now)));
	send(connfd, response, strlen(response), 0);

	sprintf(response, "Content-Length: %d\n", content_len);
	send(connfd, response, strlen(response), 0);

	//Tell the client we won't reuse this connection for other files
	strcpy(response, "Connection: close\n");
	send(connfd, response, strlen(response), 0);

	//Send our MIME type and a blank line
	strcpy(response, "Content-Type: text/html\n\n");
	send(connfd, response, strlen(response), 0);

	//fprintf(stderr, "File: %s\n", filename);

	free(response);

}

// takes in open fd and sends response from the cache
void send_body(int connfd, cached_resp* temp){

	char *response = (char *)malloc(BUFFER_SIZE * sizeof(char));

	int bytes_read = 0;

	// reads through cached response for sending
	for(bytes_read = 0; bytes_read <= temp->content_len; bytes_read += BUFFER_SIZE){

		int send_len = BUFFER_SIZE;

		// if this is the last run of the loop, copy and set send_len
		if( bytes_read + BUFFER_SIZE > temp->content_len ){
			memcpy(response, &(temp->contents[bytes_read]), temp->content_len - bytes_read);
			send_len = temp->content_len - bytes_read;
		}else{
			// otherwise, just copy
			memcpy(response, &(temp->contents[bytes_read]), BUFFER_SIZE);
		}

		int sent = send(connfd, response, send_len, 0);

		if( sent == send_len ){
			printf("what was sent had a different content length");
		}

	}

	free(response);

}

// returns a cached version of the requested webpage
// creates cache if the request does not yet exist in the cache
// returns cache if it does exist already
cached_resp* get_my_resp(char* filename){

	if( cache_head != NULL ){

		// iterates through caches
		cache_list* temp = cache_head;
		for(; temp != NULL; temp = temp->next_cache){

			// we have a filename match
			if( !strcmp(temp->data->name, filename) ){

				return temp->data;

			}
		}
	}

	FILE* f = fopen(filename, "rb");
	if(f == NULL){

		return NULL;

	}

	// creates new cache for the requested webpage with all necessary information

	cached_resp* my_resp = (cached_resp *)malloc(sizeof(cached_resp));
	my_resp->name = (char *)malloc(sizeof(char)*(strlen(filename)+1));
	memcpy(my_resp->name, filename, sizeof(char)*(strlen(filename)+1));

	struct stat file_stats;
	fstat(fileno(f), &file_stats);
	my_resp->contents = (char *)malloc(sizeof(char)*file_stats.st_size);
	my_resp->content_len = fread(my_resp->contents, 1, file_stats.st_size, f);

	time_t now;
	my_resp->cache_time = time(&now);

	cache_list* new_cache_head = (cache_list *)malloc(sizeof(cache_list));
	new_cache_head->data = my_resp;
	new_cache_head->next_cache = cache_head;

	// don't want different threads setting the new cache head at the same time / overlapping
	// so lock and unlock afterwards
	pthread_mutex_lock(&init);

	cache_head = new_cache_head;

	pthread_mutex_unlock(&init);

	int counta = 0;
	cache_list* prev = NULL;

	// iterates over cache and removes oldest cache if total exceeds 5
	for(cache_list* temp = cache_head; temp != NULL; temp = temp->next_cache){
		counta++;
		if(counta > 5){
			if( prev != NULL ){
				// modifying cache LL so lock and unlock
				pthread_mutex_lock(&init);
				prev->next_cache = NULL;
				pthread_mutex_unlock(&init);
			}
			free(temp->data->name);
			free(temp->data->contents);
			free(temp->data);
			free(temp);
			break;
		}
		prev = temp;
	}

	fclose(f);
	return my_resp;

}

// the function used by each thread to initialize and begin serving
void *handler(void *arg){

	struct timespec start_time;
	int start = clock_gettime(CLOCK_MONOTONIC, &start_time);

	// printf("Handler started");

    	int connfd = *((int *)arg);

	//At this point a client has connected. The remainder of the
	//loop is handling the client's GET request and producing
	//our response.

	char *buffer = (char *)malloc(BUFFER_SIZE * sizeof(char));
	char *filename = (char *)malloc(BUFFER_SIZE * sizeof(char));
	FILE *f;

	memset(buffer, BUFFER_SIZE, 0);
	memset(filename, BUFFER_SIZE, 0);

	//In HTTP, the client speaks first. So we recv their message
	//into our buffer.
	int amt = recv(connfd, buffer, BUFFER_SIZE, 0);
	fprintf(stderr, "\"%s\"\n", buffer);

	//We only can handle HTTP GET requests for files served
	//from the current working directory, which becomes the website root
	if(sscanf(buffer, "GET /%s", filename)<1) {
		fprintf(stderr, "Bad HTTP request\n");
		close(connfd);
		return NULL;
	}

	cached_resp* my_resp = get_my_resp(filename);
	if( my_resp == NULL ){

		strcpy(buffer, "HTTP/1.1 404 Not Found\n\n");
		send(connfd, buffer, strlen(buffer), 0);

	}else{

		send_header(connfd, my_resp->content_len);
		send_body(connfd, my_resp);

	}

	if(amt == sizeof(buffer))
	{
		//if recv returns as much as we asked for, there may be more data
		while(recv(connfd, buffer, sizeof(buffer), 0) == sizeof(buffer))
			/* discard */;
	}

	// taking in the elapsed CPU time
	struct timespec end_time;
	int end = clock_gettime(CLOCK_MONOTONIC, &end_time);
	float time_secs = end_time.tv_sec - start_time.tv_sec;
	time_secs += (end_time.tv_nsec - start_time.tv_nsec) / 1000000000.0;

	// don't want different threads to write to the file at the same time, so lock
	pthread_mutex_lock(&IO_stream);
	FILE * stats_thread = fopen("stats_thread.txt", "a");
	fprintf(stats_thread, "%s, %d, %.4f\n", filename, my_resp->content_len, time_secs);
	fclose(stats_thread);
	pthread_mutex_unlock(&IO_stream);

	shutdown(connfd, SHUT_RDWR);
	close(connfd);
	free(buffer);
	free(filename);
	free(arg);
	return NULL;
}

