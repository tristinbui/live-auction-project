#include <getopt.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include "protocol.h"
#include <pthread.h>
#include "sbuf.h"



typedef struct s_user {
    char *uname;
    char *password;
    int connfd;
    int is_loggedin;
} user;

typedef struct s_users{
    user **user_list; // an array of pointers, 
    int num_users;
    sem_t user_sem;
    sem_t user_mutex;
    int read_count;
    
} users_db;

typedef struct s_sbuf_job {
    int msg_type;
    int msg_len;
    int connfd;
} sbuf_job;

extern users_db users;
extern sbuf_t sbuf; 

int open_listenfd(int port);
void invalid_usage();
int user_exists(char* loginbuf, size_t uname_size, user** user_l);
void userlist_h(int connfd);

void *client_thread();
void *job_thread();

int do_login(int connfd, petr_header h);
user* find_user(int connfd);

#define BUFFER_SIZE 1024
#define SA struct sockaddr

#define USAGE_STATEMENT \
"./bin/petr_server [-h] [-j N] [-t M] PORT_NUMBER AUCTION_FILENAME\n\n\
-h                 Displays this help menu, and returns EXIT_SUCCESS.\n\
-j N               Number of job threads. If option not specified, default to 2.\n\
-t M               M seconds between time ticks. If option not specified, default is to wait on input from stdin to indicate a tick.\n\
PORT_NUMBER        Port number to listen on.\n\
AUCTION_FILENAME   File to read auction item information from at the start of the server.\n"
