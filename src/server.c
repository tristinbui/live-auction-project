#include "server.h"
#include "helpers.h"
#include "protocol.h"
#include <pthread.h>

// globals
users_db users;
sbuf_t sbuf;

int main(int argc, char **argv) {
    // You got this!
    users.num_users = 0;
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
            if (*endptr != 0 || num_jobthreads < 0) invalid_usage();
            break;
        case 't':
            tick_time = strtol(optarg, &endptr, 10);
            if (*endptr != 0) invalid_usage();
            break;
        default:
            invalid_usage();
        }
    }

    // TODO: parse the 'AUCTION FILE'


    // TODO: create job threads

    for (int i = 0; i < num_jobthreads; i++){
        pthread_t tid;  // what is tid for?
        pthread_create(&tid, NULL, job_thread, NULL);
    }

    sbuf_init(&sbuf, 1000);

    int *connfdp, listenfd, port;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    port = atoi(argv[argc-2]);
    listenfd = open_listenfd(port);

    users.user_list = malloc(sizeof(user*)); // array on heap for users
    sem_init(&(users.user_sem), 0, 1);
    sem_init(&(users.user_mutex), 0, 1);
    users.read_count = 0;
    // listen for new users
    while (1) {
        // accept new connections
        clientlen = sizeof(clientaddr);
        connfdp = malloc(sizeof(int));
        *connfdp = accept(listenfd, (SA *)&clientaddr, &clientlen);
        printf("accepted connection\n");

        petr_header h;
        if (rd_msgheader(*connfdp, &h) || h.msg_type != LOGIN){
            // rd_msgheader returned error or the message is wrong
            free(connfdp);
            close(*connfdp);
            continue;
        }
        
        // printf("logged in, msglen: %d\n", h.msg_len);
        // lock
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
        }
        // free(connfdp); don't free because thread needs the file descriptor (on heap)
    }

    close(listenfd);
    free(users.user_list);
    return 0;
}
