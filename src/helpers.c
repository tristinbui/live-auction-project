#include "helpers.h"


void *client_thread(void *client_fdp) {
    int connfd = *(int *)client_fdp;
    free(client_fdp); // on heap, free it

    user *client_user = find_user(connfd);

    petr_header h;
    int flag = 1; // flag for exiting loop
    // TODO: when rd_msgheader returns -1, logoout user
    while (rd_msgheader(connfd, &h) == 0 && flag) {
        sbuf_job *job;
        printf("got: %x\n", h.msg_type);
        // errno = 0;
        switch (h.msg_type) {
        case ANCREATE:
        case ANWATCH:
        case ANLEAVE:;
            // read in message body in the client thread
            char *createbuf = malloc(sizeof(char) * h.msg_len);
            read(connfd, createbuf, h.msg_len);
            createbuf[h.msg_len-1] = 0; // null terminate

            job = job_helper(connfd, &h);
            job->msg_buf = createbuf;
            sbuf_insert(&sbuf, job);
            break;

        case LOGOUT:
            job = job_helper(connfd, &h);
            logout_h(job);
            flag = 0;
            free(job);
            break;
        
        default:;
            job = malloc(sizeof(sbuf_job));
            job->connfd = connfd;
            job->msg_type = h.msg_type;
            job->msg_len = h.msg_len;
            sbuf_insert(&sbuf, job);
            break;
        }
    }
    close(connfd);
    printf("closed client thread\n");
    return NULL;
}



void *job_thread() {
    while (1){
        sbuf_job *job = sbuf_remove(&sbuf);
        // Question: Are the job and client threads supposed to be redundant?

        switch(job->msg_type) {
        case ANCREATE:
            // create an auction
            ancreate_h(job);
            break;

        case ANLIST:
            anlist_h(job);
            break;

        case ANWATCH:
            printf("got anwatch %s\n", job->msg_buf);
            anwatch_h(job);
            break;

        case ANLEAVE:
            anleave_h(job);
            break;

        case USRLIST:
        // list of logged in users (minus requestor)
            printf("got userlist\n");
            userlist_h(job->connfd);
            break;
            
        case USRWINS:
        // list of auctions that user has won
            break;

        case USRSALES:
        // list of auctions created/owned by user that have ended 
            break;

        case USRBLNC:
        // requesting users' balance
            break;

        default:
            break;
        }

        free(job);
    }
    return NULL;
}

void *tick_thread(void *tt) {
    int tick_time = *((int*)tt);
    free(tt);
    printf("%d\n", tick_time);
    while(1){
        if (tick_time == -1)
            getchar();
        else
            sleep(tick_time);
        
        printf("tick\n");
        // TODO: ticking
    
    }
    
}



void ancreate_h(sbuf_job *job) {
    // format: <itemname>\r\n<duration>\r\n<bin_price>
    char *token, *saveptr = NULL, *item_name;
    char *items[3];
    int duration, i = 0;
    long bin_price;
    // printf("%s\n", job->msg_buf);
    token = strtok_r(job->msg_buf, "\r\n", &saveptr);
    for(token = strtok_r(job->msg_buf, "\r\n", &saveptr);
        token != NULL;
        token = strtok_r(NULL, "\r\n", &saveptr)) {
        printf("tok: %s\n", token);
        items[i] = token;
        if(i < 2) saveptr++; // skip the \n
        i++;
    }

    if (i == 3) {
        char *endptr;
        item_name = items[0];
        duration  = strtol(items[1], &endptr, 10);
        if (*endptr != 0) goto invalid_ancreate;
        bin_price = strtol(items[2], &endptr, 10);
        if (*endptr != 0) goto invalid_ancreate;
    } else {
        duration = -1;
    }

    petr_header h;

    if(*item_name == '\0' || duration < 1 || bin_price < 0){
        invalid_ancreate:
        h.msg_len = 0;
        h.msg_type = EINVALIDARG;
        wr_msg(job->connfd, &h, NULL);
    }
    else{
        auction_t *new_auction = malloc(sizeof(auction_t));
        char *h_buf = calloc(1, sizeof(char) * 30);
        // Assigning fields
        user *current_user = find_user(job->connfd);
        new_auction->creator = current_user;
        char* new_item_name = malloc(sizeof(char) * (strlen(item_name)+1));
        strcpy(new_item_name, item_name);
        new_auction->item_name = new_item_name;
        new_auction->duration = duration;
        new_auction->bin_price = bin_price;
        new_auction->highest_bid = 0;
        new_auction->num_watchers = 0;
        // create Watch list
        new_auction->watching_u = CreateList(compare_auction);

        // Lock AuctionID
        P(&AuctionID_mutex);
        new_auction->auction_id = AuctionID;
        sprintf(h_buf, "%d", AuctionID);
        AuctionID++;
        V(&AuctionID_mutex);

        // Add new auction to linked list
        // writer lock & unlock
        P(&auctions.a_sem);
        insertRear(auctions.auction_list, new_auction);
        V(&auctions.a_sem);

        h.msg_len = strlen(h_buf);
        h.msg_type = ANCREATE;
        wr_msg(job->connfd, &h, h_buf);
        free(h_buf);
    }
    
    free(job->msg_buf);
    // auction_t *a = removeFront(auctions.auction_list);
    // printf("%s %d %d %d\n", a->item_name, a->duration, a->auction_id, a->bin_price);

    return;
}

void anlist_h(sbuf_job *job) {
    int connfd = job->connfd;
    char *anlist_buf = calloc(1, sizeof(char));
    size_t size = 0;

    // lock for reader
    reader_lock(&auctions.a_mutex, &auctions.a_sem, &auctions.read_count);
    for(node_t *head = auctions.auction_list->head; head != NULL; head = head->next){
        auction_t *auction = head->value;
        char* item_name = auction->item_name;
        char buffer[100 + strlen(item_name)];
        int id = auction->auction_id;
        long bin_price = auction->bin_price;
        int num_watchers = auction->num_watchers;
        long highest_bid = auction->highest_bid;
        int duration = auction->duration;

        sprintf(buffer, "%d;%s;%ld;%d;%ld;%d\n", id, item_name, bin_price, num_watchers, highest_bid, duration);
        anlist_buf = realloc(anlist_buf, strlen(buffer) + size + 1);

        strcat(anlist_buf, buffer);
        size = strlen(anlist_buf);
    }

    reader_unlock(&auctions.a_mutex, &auctions.a_sem, &auctions.read_count);

    // question: why changing this to malloc causes valgrind error?
    petr_header *h = calloc(1, sizeof(petr_header));
    h->msg_type = ANLIST;
    h->msg_len = size;
    anlist_buf = size == 0 ? NULL : anlist_buf;
    printf("sending: %s", anlist_buf);
    wr_msg(connfd, h, anlist_buf);

    free(h);
    free(anlist_buf);
}

void userlist_h(int connfd) {
    char *uname_buf = calloc(1, sizeof(char));
    
    // lock for reader
    reader_lock(&users.user_mutex, &users.user_sem, &users.read_count);

    
    for(int i=0; i < users.num_users; ++i){
        // TODO: print only users who are logged in?
        if(users.user_list[i]->connfd != connfd){
            int size = strlen(uname_buf)+strlen(users.user_list[i]->uname)+2;
            char *temp = realloc(uname_buf, size);
            if(!temp) exit(1);
            uname_buf = temp;
            strcat(uname_buf, users.user_list[i]->uname);
            strcat(uname_buf, "\n\0");
        }
    }

    // unlock
    reader_unlock(&users.user_mutex, &users.user_sem, &users.read_count);

    // Question: why is this giving valgrind error?
    // petr_header *h = malloc(sizeof(petr_header));
    petr_header *h = calloc(1, sizeof(petr_header));
    h->msg_type = USRLIST;
    h->msg_len = strlen(uname_buf) == 0 ? 0 : strlen(uname_buf)+1;
    // printf("user: %s %d\n", uname_buf, (int) strlen(uname_buf));
    // printf("header: %lx\n", *((long *)h));

    wr_msg(connfd, h, uname_buf);
    
    free(h);
    free(uname_buf);
}

/* Helper function for watching a currently running auction.
 * If successful watch, send ANWATCH <item_name>
 * sends EANNNOTFOUND if no auction exists.
 * 
 */
void anwatch_h(sbuf_job *job) {
    char *endptr;
    int id = strtol(job->msg_buf, &endptr, 10);
    auction_t *to_watch = NULL;
    // TODO: lock/unlock
    // lock for auctions writer
    P(&auctions.a_sem);
    if (*endptr == 0 && id > 0){
        to_watch = find_auction(auctions.auction_list, id);
    }
    
    petr_header *h = calloc(1, sizeof(petr_header));
    if (to_watch == NULL){
        h->msg_type = EANNOTFOUND;
        h->msg_len = 0;
        wr_msg(job->connfd, h, NULL);
    }else{
        
        user *watch_user = find_user(job->connfd);
        insertFront(to_watch->watching_u, watch_user); // add user to watching list
        insertFront(watch_user->watching_a, to_watch); // add auction to user's watchlist
        to_watch->num_watchers++;
        
        h->msg_type = ANWATCH;
        h->msg_len = strlen(to_watch->item_name)+1;
        wr_msg(job->connfd, h, to_watch->item_name);
    }
    // unlock for auctions writer
    V(&auctions.a_sem);
    free(h);
}

/* Stop watching an auction, job->msg_buf contains id of the auction
 * Responds with EANNOTFOUND if auction id does not exist, but OK if the auction 
 * exists but user is not watching it.
 */
void anleave_h(sbuf_job *job) {
    char *endptr;
    int id = strtol(job->msg_buf, &endptr, 10);
    // writer lock
    P(&auctions.a_sem);
    P(&users.user_sem);
    auction_t *a;
    int i = 0;
    user *user = find_user(job->connfd);
    petr_header h;
    
    if(find_auction(auctions.auction_list, id) != NULL){
        // remove auction from user's watchlist
        for(node_t *curr_auc = (user->watching_a)->head; curr_auc != NULL; curr_auc = curr_auc->next){
            auction_t *auction = (curr_auc->value);
            if(auction->auction_id == id){
                removeByIndex(user->watching_a, i);
                a = auction;
                a->num_watchers--;
                break;
            }
            i++;
        }
        
        i = 0;
        // remove user from the auction watching list
        for(node_t *curr_user = (a->watching_u)->head; curr_user != NULL; curr_user = curr_user->next){
            if(user == curr_user->value){
                removeByIndex(a->watching_u, i);
                break;
            }
            i++;
        }
        h.msg_type = OK;
    }
    else { 
        h.msg_type = EANNOTFOUND;
    }
    // unlock
    V(&auctions.a_sem);
    V(&users.user_sem);

    h.msg_len = 0;
    wr_msg(job->connfd, &h, NULL);

    printf("in anleave\n");
    return;
}

/* helper function for finding an auction, returns NULL if
 * auction was not found in the passed auction list, otherise
 * returns pointer to the desired auction_t.
 */
auction_t* find_auction(List_t *auction_list, int auction_id) {
    printf("head: %p\n", auction_list->head);
    for(node_t *head = auction_list->head; head != NULL; head = head->next){
        auction_t *a = head->value;
        if (a->auction_id == auction_id) return a;
    }
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
    exit(EXIT_FAILURE);
}

/* Checks if the user exists, and whether the password is valid
 * @return 0: if the user doesn't exist. 1: user exists & valid password.
 * -1: if the password is invalid.
 */
int user_exists(char* loginbuf, size_t uname_size, user** user_l){
    char *passwd = loginbuf + uname_size + 2;
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

/* Logs out user, removes their watched auctions & removes them from
 * the auctions' watch lists. 
 *
 */
void logout_h(sbuf_job *job) {
    // writer lock for auction & user
    P(&users.user_sem);
    P(&auctions.a_sem);
    // printf("");
    user *user = find_user(job->connfd);
    user->is_loggedin = 0;
    int u_watchindex = 0;
    for(node_t *curr_auc = (user->watching_a)->head; curr_auc != NULL;){
        node_t* curr_user = ((auction_t*)(curr_auc->value))->watching_u->head;
        int a_watchindex = 0;
        while(curr_user != NULL){
            if(user == curr_user->value){
                // removes the user from the auction's watchlist
                removeByIndex(((auction_t*)(curr_auc->value))->watching_u, a_watchindex);
                ((auction_t*)(curr_auc->value))->num_watchers--;
                break;
            }
            a_watchindex++;
        }
        // remove auction from the user's watchlist
        curr_auc = curr_auc->next;
        removeByIndex(user->watching_a, u_watchindex);
        u_watchindex++;
    }

    // // writer unlock for auction & user
    V(&users.user_sem);
    V(&auctions.a_sem);
    
    petr_header *h = calloc(1, sizeof(petr_header));
    h->msg_len = 0;
    h->msg_type = 0;
    wr_msg(job->connfd, h, OK);
    free(h);
}

/* Logs in user, checks if user and password are valid. 
 * If valid, logs the user in, sends OK. If the user is 
 * already logged in, send EUSRLGDIN. If wrong password, 
 * send EWRNGPWD. closes connection if user didn't get logged in.
 * @param connfd fd for the client
 * @param h petr_header with the login message header
 * @return 0 if the user was successfully logged in, and -1 if 
 * the user did not get logged in
 */
int do_login(int connfd, petr_header h){
    char *loginbuf = malloc(sizeof(char) * h.msg_len);
    read(connfd, loginbuf, h.msg_len);
    loginbuf[h.msg_len-1] = 0; // null terminate

    char *passwd = strstr(loginbuf, "\r\n") + 2;
    if (passwd != NULL) {
        int num_users = users.num_users;
        
        size_t uname_size = strlen(loginbuf)-strlen(passwd)-2;

        // result of user_exists
        int exists_res = user_exists(loginbuf, uname_size, users.user_list);
        if(exists_res == 0){
            // creates new user (on heap)
            user* new_user = malloc(sizeof(user));
            new_user->password = calloc(1, sizeof(char) * strlen(passwd) + 1);
            new_user->uname = calloc(1, sizeof(char) * uname_size + 1);

            strcpy(new_user->password, passwd);
            strncpy(new_user->uname, loginbuf, uname_size);
            new_user->is_loggedin = 1;
            new_user->connfd = connfd;
            new_user->watching_a = CreateList(compare_auction);
            // printf("%s\n", new_user->uname);
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
                        // user is already logged in
                        h.msg_type = EUSRLGDIN;
                        h.msg_len = 0;
                        wr_msg(connfd, &h, NULL);
                        close(connfd);
                        exists_res = -1;
                    }
                    else {
                        // user exists and is not logged in, login
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

/* Comparator function: returns 1 if l_auction is greater, 0 if equal,
 * and -1 if the l_auction is lesser
 * @param l_auction pointer to a bgentry_t
 * @param r_auction pointer to a bgentry_t
 */
int compare_auction(void *l_auction, void *r_auction){
    auction_t *ra = (auction_t*) r_auction;
    auction_t *la = (auction_t*) l_auction;
    if (la->auction_id == ra->auction_id) return 0;
    return (la->auction_id < ra->auction_id) ? -1 : 1;
}

int compare_user(void *l_auction, void *r_auction){
    return 0;
}

/* Helper function for allocating an sbuf_job on heap, and assigning
 * basic values to it.
 *
 * @return pointer to an sbuf_job on the heap, with msg_type, msg_len, and connfd set.
 */
sbuf_job *job_helper(int connfd, petr_header *h) {
    sbuf_job *job = malloc(sizeof(sbuf_job));
    job->connfd = connfd;
    job->msg_type = h->msg_type;
    job->msg_len = h->msg_len;
    return job;
}

void reader_lock(sem_t *mutex, sem_t *sem, int *readcnt){
    P(mutex);
    *readcnt++;
    if(*readcnt == 1) P(sem);
    V(mutex);
    return;
}

void reader_unlock(sem_t *mutex, sem_t *sem, int *readcnt){
    P(mutex);
    *readcnt--;
    if(*readcnt == 0) V(sem);
    V(mutex);
    return;
}