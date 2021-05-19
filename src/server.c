#include "server.h"
#include "helpers.h"

int main(int argc, char **argv) {
    // You got this!
    
    if (argc < 3){
        printf(USAGE_STATEMENT);
        exit(0);
    }

    int opt, num_jobthreads = 2, tick_time = -1;
    while ((opt = getopt(argc-2, argv, "hj:t:")) != -1){
        switch(opt) {
        case 'h':
            printf(USAGE_STATEMENT);
            exit(EXIT_SUCCESS);
        case 'j':
            num_jobthreads = atoi(optarg);
            break;
        case 't':
            tick_time = atoi(optarg);
            break;
        default:
            printf(USAGE_STATEMENT);
            exit(EXIT_FAILURE);
        }
    }

    // TODO: parse the 'AUCTION FILE'

    int *connfdp, listenfd, port;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    port = atoi(argv[argc-2]);
    listenfd = open_listenfd(port);

    while (1) {
        clientlen = sizeof(clientaddr);
        connfdp = malloc(sizeof(int));
        *connfdp = accept(listenfd, (SA *)&clientaddr, &clientlen);
        printf("accepted connection\n");
        
    }

    return 0;
}
