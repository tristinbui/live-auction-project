#include "server.h"
#include "helpers.h"
#include "protocol.h"
#include <pthread.h>

users_db users;

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
            if (*endptr != 0) invalid_usage();
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

    int *connfdp, listenfd, port;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    port = atoi(argv[argc-2]);
    listenfd = open_listenfd(port);

    users.user_list = malloc(sizeof(user)); // array on heap for users
    user *users_l = users.user_list;
    // listen for new users
    while (1) {
        clientlen = sizeof(clientaddr);
        connfdp = malloc(sizeof(int));
        *connfdp = accept(listenfd, (SA *)&clientaddr, &clientlen);
        printf("accepted connection\n");
        petr_header h;
        if (rd_msgheader(*connfdp, &h) || h.msg_type != LOGIN){
            free(connfdp);
            close(*connfdp);
            continue;
        }
        
        // printf("logged in, msglen: %d\n", h.msg_len);

        int login_code = do_login(*connfdp, h);
        printf("login result: %d\n", login_code);

        if (login_code == 0) {
            // login successful, create client thread
            pthread_t tid;
            // pthread_create(&tid, NULL, client_thread(), connfdp);

        }

        free(connfdp);
    }

    free(users_l);
    return 0;
}
