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
#include "linkedList.h"


typedef struct s_user {
    char *uname;
    char *password;
    int connfd;
    int is_loggedin; // 0 if not logged in, 1 if loggedin
    List_t *watching_a;
    // List_t *auctions_won;
    long balance;
} user;

typedef struct s_users{
    user **user_list; // an array of pointers, 
    int num_users;
    sem_t user_sem;
    sem_t user_mutex;
    int read_count;
} users_db;

typedef struct s_auction {
    int auction_id;
    char* item_name;
    int duration;
    long bin_price;
    long highest_bid;
    int num_watchers;
    user *highest_bidder;
    user *creator;
    List_t* watching_u;
} auction_t;

typedef struct s_auctions {
    List_t *auction_list;
    sem_t a_sem;
    sem_t a_mutex;
    int read_count;
} auctions_db;

typedef struct s_sbuf_job {
    int msg_type;
    int msg_len;
    int connfd;
    char *msg_buf;
} sbuf_job;

extern users_db users;
extern sbuf_t sbuf;
extern auctions_db auctions;
extern auctions_db closed_auctions;
extern int AuctionID;
extern sem_t AuctionID_mutex;
extern int listenfd;

int open_listenfd(int port);
void invalid_usage();

void userlist_h(int connfd);
void ancreate_h(sbuf_job *job);
void anclosed_h(sbuf_job *job, int dont_lock);
void anlist_h(sbuf_job *job);
void anwatch_h(sbuf_job *job);
void anleave_h(sbuf_job *job);
void anbid_h(sbuf_job *job);
void usrblnc_h(sbuf_job *job);
void logout_h(sbuf_job *job);
void usrwins_h(sbuf_job *job);
void usrsales_h(sbuf_job *job);

void *client_thread();
void *job_thread();
void *tick_thread();

int do_login(int connfd, petr_header *h);
user *find_user(int connfd);
int user_exists(char* loginbuf, size_t uname_size, user** user_l);
auction_t *find_auction(List_t *auction_list, int auction_id);
auction_t* find_auction_index(List_t *auction_list, int auction_id, int *index);
int find_watching_user(List_t *watchlist, user* u);

int compare_auction(void *l_auction, void *r_auction);
int compare_user(void *l_auction, void *r_auction);
sbuf_job *job_helper(int connfd, petr_header *h);
void reader_lock(sem_t *mutex, sem_t *sem, int *readcnt);
void reader_unlock(sem_t *mutex, sem_t *sem, int *readcnt);
void parse_aucfile(FILE *a_file);
void sigint_handler(int sig);

#define BUFFER_SIZE 1024
#define SA struct sockaddr

#define USAGE_STATEMENT \
"./bin/petr_server [-h] [-j N] [-t M] PORT_NUMBER AUCTION_FILENAME\n\n\
-h                 Displays this help menu, and returns EXIT_SUCCESS.\n\
-j N               Number of job threads. If option not specified, default to 2.\n\
-t M               M seconds between time ticks. If option not specified, default is to wait on input from stdin to indicate a tick.\n\
PORT_NUMBER        Port number to listen on.\n\
AUCTION_FILENAME   File to read auction item information from at the start of the server.\n"
 