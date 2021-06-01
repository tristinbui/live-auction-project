#include "server.h"
#include "helpers.h"
#include "protocol.h"
#include <pthread.h>

// globals
users_db users;
auctions_db auctions;
sbuf_t sbuf;
// the next auction id
int AuctionID;
sem_t AuctionID_mutex;

int main(int argc, char **argv) {
    // You got this!
    if (argc < 3){
        printf(USAGE_STATEMENT);
        exit(0);
    }

    int opt, num_jobthreads = 2, tick_time = -1;
    char *endptr;
    while ((opt = getopt(argc-2, argv, "hj:t:")) != -1){
        endptr = NULL;
        switch(opt) {
        case 'h':
            printf(USAGE_STATEMENT);
            exit(EXIT_SUCCESS);
        case 'j':
            num_jobthreads = strtol(optarg, &endptr, 10);
            if (*endptr != 0 || num_jobthreads <= 0) invalid_usage();
            break;
        case 't':
            tick_time = strtol(optarg, &endptr, 10);
            if (*endptr != 0) invalid_usage();
            break;
        default:
            invalid_usage();
        }
    }
    
    int *connfdp, listenfd, port;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    // open listen socket on port
    port = atoi(argv[argc-2]);
    listenfd = open_listenfd(port);

    // TODO: parse the 'AUCTION FILE'


    // TODO: create tick thread


    // TODO: create signal handler for ctrl-c

    // create job threads
    for (int i = 0; i < num_jobthreads; i++){
        pthread_t tid;  // what is tid for?
        pthread_create(&tid, NULL, job_thread, NULL);
    }

    sbuf_init(&sbuf, 1000);


    // TODO: create auction list
    // next auction ID
    AuctionID = 1;
    sem_init(&(AuctionID_mutex), 0, 1);
    // semaphore/mutex for reader/writer problem
    sem_init(&(auctions.a_sem), 0, 1);
    sem_init(&(auctions.a_mutex), 0, 1);
    auctions.auction_list = CreateList(compare_auction);
    

    // create users list
    users.num_users = 0;
    users.user_list = malloc(sizeof(user*)); // array on heap for users
    // semaphore/mutex for reader/writer problem
    sem_init(&(users.user_sem), 0, 1);
    sem_init(&(users.user_mutex), 0, 1);
    users.read_count = 0;

    // listen for new users
    while (1) {
        // accept new connections
        clientlen = sizeof(clientaddr);
        connfdp = malloc(sizeof(int));
        *connfdp = accept(listenfd, (SA *)&clientaddr, &clientlen);

        petr_header h;
        if (rd_msgheader(*connfdp, &h) || h.msg_type != LOGIN){
            // rd_msgheader returned error or the message is wrong
            free(connfdp);
            close(*connfdp);
            continue;
        }
        
        // lock for write
        P(&users.user_sem);
        
        int login_code = do_login(*connfdp, h);
        // unlock
        V(&users.user_sem);

        printf("login result %d\n", login_code);

        if (login_code == 0) {
            // login successful, create client thread
            pthread_t tid;  // what is tid for?
            // pass connfd
            pthread_create(&tid, NULL, client_thread, connfdp);
        } else {
            // free connfdp if the user does not get logged in
            free(connfdp);
        }
    }

    close(listenfd);
    free(users.user_list);
    deleteList(auctions.auction_list);
    free(auctions.auction_list);

    return 0;
}
