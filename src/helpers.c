#include "helpers.h"


void *client_thread(void *client_fdp) {
    int connfd = *(int *)client_fdp;
    free(client_fdp); // on heap, free it

    user *client_user = find_user(connfd);

    petr_header h;
    int flag = 1;
    while (rd_msgheader(connfd, &h) == 0 && flag) {
        switch (h.msg_type) {
        case ANCREATE:
            

            break;
        // case USRLIST:
        //     sbuf_job *job = malloc(sizeof(job));
        //     job->connfd = connfd;
        //     job->msg_type = h.msg_type;
        //     job->msg_len = h.msg_len;
        //     sbuf_insert(&sbuf, job);
        //     // connfd, message type
        //     // sbuf_job

        //     break;

        case LOGOUT:
            printf("got logout\n");
            client_user->is_loggedin = 0;
            h.msg_len = 0;
            h.msg_type = 0;
            
            wr_msg(connfd, &h, OK);
            flag = 0;
            break;
        
        default:;
            sbuf_job *job = malloc(sizeof(sbuf_job));
            job->connfd = connfd;
            job->msg_type = h.msg_type;
            job->msg_len = h.msg_len;
            sbuf_insert(&sbuf, job);
            break;
        }
    }
    close(connfd);
    return NULL;
}

void *job_thread() {
    while (1){
        sbuf_job *job = sbuf_remove(&sbuf);
        // Question: Are the job and client threads supposed to be redundant?

        switch(job->msg_type) {
            case USRLIST:
                printf("got userlist\n");
                userlist_h(job->connfd);
                break;
                
            case USRWINS:
                

            default:
                break;
        }

        free(job);
    }
    return NULL;
}

void userlist_h(int connfd) {
    char *uname_buf = calloc(1, sizeof(char));
    printf("connfd: %d\n", connfd);
    // TODO: mutex
    P(&users.user_mutex);
    users.read_count++;
    if(users.read_count == 1) P(&users.user_sem);
    V(&users.user_mutex);

    
    for(int i=0; i < users.num_users; ++i){
        if(users.user_list[i]->connfd != connfd){
            int size = strlen(uname_buf)+strlen(users.user_list[i]->uname)+2;
            char *temp = realloc(uname_buf, size);
            if(!temp) exit(1);
            uname_buf = temp;
            strcat(uname_buf, users.user_list[i]->uname);
            strcat(uname_buf, "\n\0");
        }
    }

    P(&users.user_mutex);
    users.read_count--;
    if(users.read_count == 0) V(&users.user_sem);
    V(&users.user_mutex);
    

    petr_header h;
    h.msg_type = USRLIST;
    h.msg_len = strlen(uname_buf) == 0 ? 0 : strlen(uname_buf)+1;
    // Question: why is this giving valgrind error?
    printf("user: %s %d\n", uname_buf, (int) strlen(uname_buf));

    wr_msg(connfd, &h, uname_buf);
    
    free(uname_buf);
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
int user_exists(char* loginbuf, size_t uname_size, user** user_l){
    char *passwd = loginbuf + uname_size + 1;
    printf("in user_exists: %d\n", users.num_users);
    for(int i=0; i < users.num_users; ++i) {
        if(strncmp(user_l[i]->uname, loginbuf, uname_size) == 0){
            // check password
            if(strcmp(user_l[i]->password, passwd) == 0){
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
    // user *users_l = users.user_list;
    read(connfd, loginbuf, h.msg_len);
    loginbuf[h.msg_len-1] = 0; // null terminate
    char *passwd = strstr(loginbuf, "\r\n") + 2;
    if (passwd != NULL) {
        int num_users = users.num_users;
        
        // TODO: check if user already exists & password is correct
        size_t uname_size = strlen(loginbuf)-strlen(passwd)-2;

        // result of user_exists
        int exists_res = user_exists(loginbuf, uname_size, users.user_list);
        if(exists_res == 0){
            // new user
            user* new_user = malloc(sizeof(user));
            new_user->password = calloc(1, sizeof(char) * strlen(passwd) + 1);
            new_user->uname = calloc(1, sizeof(char) * uname_size + 1);

            strcpy(new_user->password, passwd);
            strncpy(new_user->uname, loginbuf, uname_size);
            new_user->is_loggedin = 1;
            new_user->connfd = connfd;
            printf("%s\n", new_user->uname);
            // printf("%s\n", new_user->password);

            users.num_users++;
            users.user_list[num_users] = new_user;
            users.user_list = realloc(users.user_list, (users.num_users + 1) * sizeof(user*));
        }
        else if (exists_res == 1) {
            // TODO: check if the account is in use
            for(int i=0; i < num_users; ++i){
                if(strncmp(users.user_list[i]->uname, loginbuf, uname_size) == 0){
                    if(users.user_list[i]->is_loggedin == 1){
                        h.msg_type = EUSRLGDIN;
                        h.msg_len = 0;
                        wr_msg(connfd, &h, NULL);
                        close(connfd);
                        exists_res = -1;
                    }
                    else {
                        // user exists, login
                        users.user_list[i]->is_loggedin = 1;
                        users.user_list[i]->connfd = connfd;
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

user* find_user(int connfd){
    for(int i=0; i < users.num_users; ++i){
        if(connfd == users.user_list[i]->connfd){
            return users.user_list[i];
        }
    }

    return NULL;
}