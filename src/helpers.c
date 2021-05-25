#include "helpers.h"


void *client_thread(void *vargp) {


    return NULL;
}


/* Binds to a port and opens an fd for listening
 *
 * @param port port to listen on
 * @return fd that is listening for new connections
 */
int open_listenfd(int port) {
    // copied from lab 8
    int sockfd;
    struct sockaddr_in servaddr;

    // socket create and verification
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        printf("socket creation failed...\n");
        exit(EXIT_FAILURE);
    }
    // else
    // printf("Socket successfully created\n");

    bzero(&servaddr, sizeof(servaddr));

    // assign IP, PORT
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);

    int opt = 1;
    if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, (char *)&opt, sizeof(opt))<0) {
        perror("setsockopt");exit(EXIT_FAILURE);
    }

    // Binding newly created socket to given IP and verification
    if ((bind(sockfd, (SA*)&servaddr, sizeof(servaddr))) != 0) {
        printf("socket bind failed\n");
        exit(EXIT_FAILURE);
    }
    // else
        // printf("Socket successfully binded\n");

    // Now server is ready to listen and verification
    if ((listen(sockfd, 1)) != 0) {
        printf("Listen failed\n");
        exit(EXIT_FAILURE);
    }
    else
        printf("Currently listening on port: %d.\n", port);

    return sockfd;

    return 0;
}

// exits when user attempts to use program wrong
void invalid_usage() {
    printf(USAGE_STATEMENT);
    exit(EXIT_SUCCESS);
}

/* Checks if the user exists, and whether the password is valid
 * @return 0: if the user doesn't exist. 1: user exists & valid password.
 * -1: if the password is invalid.
 */
int user_exists(char* loginbuf, size_t uname_size, user* user_l){
    char *passwd = loginbuf + uname_size + 1;
    printf("in user_exists: %d\n", users.num_users);
    for(int i=0; i < users.num_users; ++i) {
        if(strncmp(user_l[i].uname, loginbuf, uname_size) == 0){
            // check password
            if(strcmp(user_l[i].password, passwd) == 0){
                // valid password
                return 1;
            }
            // invalid password
            else return -1;
        }
    }
    // user doesn't exist
    return 0;
}

/* Logs in user, checks if user and password are valid. 
 * If valid, logs the user in, sends OK. If the user is 
 * already logged in, send EUSRLGDIN. If wrong password, 
 * send EWRNGPWD.
 * @param connfd fd for the client
 * @param h petr_header with the login message header
 * @return 0 if the user was successfully logged in, and -1 if 
 * the user did not get logged in
 */
int do_login(int connfd, petr_header h){
    char *loginbuf = malloc(sizeof(char) * h.msg_len);
    user *users_l = users.user_list;
    read(connfd, loginbuf, h.msg_len);
    loginbuf[h.msg_len-1] = 0; // null terminate
    char *passwd = strstr(loginbuf, "\r\n") + 1;
    if (passwd != NULL) {
        int num_users = users.num_users;
        
        // TODO: check if user already exists & password is correct
        size_t uname_size = strlen(loginbuf)-strlen(passwd)-1;

        // result of user_exists
        int exists_res = user_exists(loginbuf, uname_size, users_l);
        if(exists_res == 0){
            // new user
            users_l[num_users].password = calloc(1, sizeof(char) * strlen(passwd) + 1);
            users_l[num_users].uname = calloc(1, sizeof(char) * uname_size + 1);

            strcpy(users_l[num_users].password, passwd);
            strncpy(users_l[num_users].uname, loginbuf, uname_size);
            users_l[num_users].is_loggedin = 1;
            printf("%s\n", users_l[num_users].uname);
            printf("%s\n", users_l[num_users].password);

            users.num_users++;
            users.user_list = realloc(users.user_list, (users.num_users + 1) * sizeof(user));
            users_l = users.user_list;
        }
        else if (exists_res == 1) {
            // TODO: check if the account is in use
            for(int i=0; i < num_users; ++i){
                if(strncmp(users_l[i].uname, loginbuf, uname_size) == 0){
                    if(users_l[i].is_loggedin == 1){
                        h.msg_type = EUSRLGDIN;
                        h.msg_len = 0;
                        wr_msg(connfd, &h, NULL);
                        close(connfd);
                        exists_res = -1;
                    }
                    else {
                        // user exists, login
                        users_l[i].is_loggedin = 1;
                    }
                }
            }
        }    
        else if (exists_res == -1) {
            // invalid password
            h.msg_type = EWRNGPWD;
            h.msg_len = 0;
            wr_msg(connfd, &h, NULL);
            close(connfd);
        }
        
        if (exists_res >= 0){
            // successful login
            h.msg_type = OK;
            h.msg_len = 0;
            wr_msg(connfd, &h, NULL);
            free(loginbuf);
            return 0;
        }
    }
    free(loginbuf);
    return -1;
}