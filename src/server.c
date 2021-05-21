#include "server.h"
#include "helpers.h"
#include "protocol.h"


int main(int argc, char **argv) {
    // You got this!
    users_db users;
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
        
        printf("logged in, msglen: %d\n", h.msg_len);
        
        char *loginbuf = malloc(sizeof(char) * h.msg_len);

        read(*connfdp, loginbuf, h.msg_len);
        loginbuf[h.msg_len-1] = 0; // null terminate
        char *passwd = strstr(loginbuf, "\n") + 1;
        if (passwd != NULL) {
            printf("%ld\n", strlen(passwd));
            int num_users = users.num_users;
            
            // TODO: check if user already exists & password is correct
            size_t uname_size = strlen(loginbuf)-strlen(passwd)-1;

            // result of user_exists
            int exists_res = user_exists(loginbuf, uname_size, users_l);

            if(exists_res == 0){
                users_l[num_users].password = malloc(sizeof(char) * strlen(passwd));
                users_l[num_users].uname = malloc(sizeof(char) * uname_size);

                strcpy(users_l[num_users].password, passwd);
                strncpy(users_l[num_users].uname, loginbuf, uname_size);

                printf("%s\n", users_l[num_users].uname);

                users.num_users++;
                users.user_list = realloc(users.user_list, users.num_users);
            }
            else if (exists_res == 1) {
                // user exists, login
                h.msg_type = OK;
                h.msg_len = 0;
                wr_msg(*connfdp, &h, NULL);
            }    
            else if (exists_res == -1) {
                h.msg_type = EWRNGPWD;
                h.msg_len = 0;
                wr_msg(*connfdp, &h, NULL);
            }
            
        }

        free(loginbuf);
        free(connfdp);
    }

    free(users_l);
    return 0;
}
