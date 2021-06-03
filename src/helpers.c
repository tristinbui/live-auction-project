#include "helpers.h"


void *client_thread(void *client_fdp) {
    int connfd = *(int *)client_fdp;
    free(client_fdp); // on heap, free it
    // pthread_detach thread
    pthread_detach(pthread_self());
    user *client_user = find_user(connfd);

    petr_header h;
    int rd_result, flag = 1; // flag for exiting loop
    sbuf_job *job;
    // when rd_msgheader returns -1, logout user
    while ((rd_result = rd_msgheader(connfd, &h)) == 0 && flag ) {
        printf("got: %x\n", h.msg_type);
        
        switch (h.msg_type) {
        case ANCREATE:
        case ANWATCH:
        case ANLEAVE:
        case ANBID:;
            // messages that have a message body, read in the client thread
            char *createbuf = calloc(h.msg_len, sizeof(char));
            read(connfd, createbuf, h.msg_len);
            printf("%s\n", createbuf);
            createbuf[h.msg_len-1] = 0; // null terminate

            job = job_helper(connfd, &h);
            job->msg_buf = createbuf;
            sbuf_insert(&sbuf, job);
            break;

        case LOGOUT:
            job = job_helper(connfd, &h);
            logout_h(job);
            flag = 0; // flag indicates that the user logged out with /logout
            free(job);
            break;
        
        default:;
        // other messages 
            job = calloc(1,sizeof(sbuf_job));
            job->connfd = connfd;
            job->msg_type = h.msg_type;
            job->msg_len = h.msg_len;
            printf("inserting\n");
            sbuf_insert(&sbuf, job);
            printf("inserted\n");
            break;
        }
    }

    if(rd_result == -1 && flag != 0){
        // if client suddenly disconnects, user will be logged out if they are not logged out already
        job = job_helper(connfd, &h);
        logout_h(job);
        flag = 0;
        free(job);
    }
    close(connfd);
    printf("closed client thread\n");
    return NULL;
}



void *job_thread() {
    pthread_detach(pthread_self());
    while (1){
        printf("job thread\n");
        sbuf_job *job = sbuf_remove(&sbuf);
        printf("got job: %x\n", job->msg_type);
        switch(job->msg_type) {
        case ANCREATE:
            // create an auction
            ancreate_h(job);
            break;

        case ANLIST:
        // question: why does it print weird in the client first time?
            printf("got anlist\n");
            anlist_h(job);
            break;
        
        case ANCLOSED:
            anclosed_h(job);
            break;

        case ANWATCH:
            printf("got anwatch %s\n", job->msg_buf);
            anwatch_h(job);
            break;

        case ANLEAVE:
            anleave_h(job);
            break;
        
        case ANBID:
            anbid_h(job);
            break;
        
        case USRLIST:
        // list of logged in users (minus requestor)
            printf("got userlist\n");
            userlist_h(job->connfd);
            break;
            
        case USRWINS:
        // list of auctions that user has won
            usrwins_h(job);
            break;

        case USRSALES:
        // list of auctions created/owned by user that have ended 
            usrsales_h(job);
            break;

        case USRBLNC:
        // requesting users' balance
            usrblnc_h(job);
            break;

        default:
            break;
        }

        free(job);
        //TODO: free msg_buf if it exists (remember to change to calloc)
    }
    return NULL;
}

void *tick_thread(void *tt) {
    int tick_time = *((int*)tt);
    pthread_detach(pthread_self());
    free(tt);
    while(1){
        if (tick_time == -1)
            getchar(); // question: is using getchar ok?
        else
            sleep(tick_time);
        
        printf("Ticked\n");
        // TODO: ticking
        // writer lock for auctions
        P(&auctions.a_sem);

        for(node_t *head = auctions.auction_list->head; head != NULL; head = head->next){
            auction_t *a = head->value;
            printf("%s\n", a->item_name);
            a->duration--;
            if(a->duration == 0){
                // auction ended, send ANCLOSED
                petr_header h;
                h.msg_len = 0;
                h.msg_type = ANCLOSED;
                sbuf_job *j = job_helper(a->auction_id, &h);
                sbuf_insert(&sbuf, j);

            }
        }
        // unlock auctions
        V(&auctions.a_sem);
    
    }
    
}

/* Auction is finished
 * <auction_id>\r\n<win_name>\r\n<win_price>
 * if no winner: <auction_id>\r\n<\r\n
 */
void anclosed_h(sbuf_job *job){
    // connfd will hold auction id
    int a_id = job->connfd;
    
    // P(auctionlock)
    // P(users)
    /* TODO: lock rules
     * lock auctions before users
     * if getting the reader lock on one resource, don't get writer lock on another
     * Failing tests: ancreate/anlist timing out
    */ 

    // P(auction)

    // lock users and auctions
    P(&auctions.a_sem);
    P(&users.user_sem);
    int auc_index;
    auction_t* auction = find_auction_index(auctions.auction_list, a_id, &auc_index);
    petr_header *h = calloc(1, sizeof(petr_header));
    char *msg;
    if(auction->highest_bidder == NULL){
        // if auction doesn't have a highest bidder
        msg = calloc(10, sizeof(char));
        sprintf(msg, "%d\r\n\r\n", auction->auction_id);
    } 
    else{
        // if auction has a winner
        msg = calloc(strlen(auction->highest_bidder->uname) + 20, sizeof(char));
        sprintf(msg, "%d\r\n%s\r\n%ld", auction->auction_id, auction->highest_bidder->uname, auction->highest_bid);
        // change balances
        auction->highest_bidder->balance -= auction->highest_bid;
        if (auction->creator != NULL){
            auction->creator->balance += auction->highest_bid;
        }
    }

    for(node_t *head = auction->watching_u->head; head != NULL; head = head->next){
        // send a message to all the watchers
        user *notified_user = head->value;
        h->msg_type = ANCLOSED;
        h->msg_len = strlen(msg)+1;
        wr_msg(notified_user->connfd, h, msg);
        // remove the auction from the user's watchlist
        int auc_index;
        find_auction_index(notified_user->watching_a, a_id, &auc_index);
        removeByIndex(notified_user->watching_a, auc_index);
    }

    
    // remove auction
    auction = removeByIndex(auctions.auction_list, auc_index);
    
    // add into the list of closed auctions
    // TODO: test this
    insertInOrder(closed_auctions.auction_list, auction);
    deleteList(auction->watching_u);
    
    // assign winner

    V(&auctions.a_sem);
    V(&users.user_sem);
    
    free(h);
    free(msg);
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
        token != NULL && i < 3;
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

    petr_header *h = calloc(1, sizeof(petr_header));

    if(duration < 1 || *item_name == '\0' || bin_price < 0){
        invalid_ancreate:
        h->msg_len = 0;
        h->msg_type = EINVALIDARG;
        wr_msg(job->connfd, h, NULL);
    }
    else{
        auction_t *new_auction = calloc(1,sizeof(auction_t));
        char *h_buf = calloc(1, sizeof(char) * 30);
        // Assigning fields
        // reader lock for users
        reader_lock(&users.user_mutex, &users.user_sem, &users.read_count);
        user *current_user = find_user(job->connfd);
        new_auction->creator = current_user;
        reader_unlock(&users.user_mutex, &users.user_sem, &users.read_count);

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

        h->msg_len = strlen(h_buf) + 1;
        h->msg_type = ANCREATE;
        wr_msg(job->connfd, h, h_buf);
        free(h_buf);
    }
    
    free(job->msg_buf);
    free(h);
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

    petr_header *h = calloc(1, sizeof(petr_header));
    h->msg_type = ANLIST;
    h->msg_len = size == 0 ? 0 : size+1;
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
        // print users who are logged in, and not the requesting user
        if(users.user_list[i]->connfd != connfd && users.user_list[i]->is_loggedin){
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
    // printf("%d, %d, %d\n", (int) sizeof(petr_header), (int) sizeof(uint32_t), (int) sizeof(uint8_t));
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

/* Return user's current balance, msg body is <balance>
 *
 */
void usrwins_h(sbuf_job *job){
    reader_lock(&closed_auctions.a_mutex, &closed_auctions.a_sem, &closed_auctions.read_count);
    reader_lock(&users.user_mutex, &users.user_sem, &users.read_count);
    user *curr_user = find_user(job->connfd);
    auction_t *a;
    char *usrwins_buf = calloc(1, sizeof(char));
    size_t size = 0;
    
    for(node_t *head = closed_auctions.auction_list->head; head != NULL; head = head->next){
        a = head->value;

        if(a->highest_bidder == curr_user){
            char buffer[20 + strlen(a->item_name)];
            
            sprintf(buffer, "%d;%s;%ld\n", a->auction_id, a->item_name, a->highest_bid);
            usrwins_buf = realloc(usrwins_buf, strlen(buffer) + size + 1);
        
            strcat(usrwins_buf, buffer);
            size = strlen(usrwins_buf);
        }
    }

    reader_unlock(&closed_auctions.a_mutex, &closed_auctions.a_sem, &closed_auctions.read_count);
    reader_unlock(&users.user_mutex, &users.user_sem, &users.read_count);

    petr_header *h = calloc(1, sizeof(petr_header));
    h->msg_type = USRWINS;
    h->msg_len = size == 0 ? 0 : size+1;
    usrwins_buf = size == 0 ? NULL : usrwins_buf;
    printf("sending: %s", usrwins_buf);
    wr_msg(job->connfd, h, usrwins_buf);

    free(h);
    free(usrwins_buf);
}


/* Sends a list of auctions that the user has created/owned and have
 * concluded.
 *
 */
void usrsales_h(sbuf_job *job){
    reader_lock(&closed_auctions.a_mutex, &closed_auctions.a_sem, &closed_auctions.read_count);
    reader_lock(&users.user_mutex, &users.user_sem, &users.read_count);
    user *curr_user = find_user(job->connfd);
    auction_t *a;
    char *usrsales_buf = calloc(1, sizeof(char));
    size_t size = 0;
    
    for(node_t *head = closed_auctions.auction_list->head; head != NULL; head = head->next){
        a = head->value;
        if(a->creator == curr_user){
            char buffer[20 + strlen(a->item_name) + strlen(a->highest_bidder->uname)];

            sprintf(buffer, "%d;%s;%s;%ld\n", a->auction_id, a->item_name, a->highest_bidder->uname, a->highest_bid);
            usrsales_buf = realloc(usrsales_buf, strlen(buffer) + size + 1);
        
            strcat(usrsales_buf, buffer);
            size = strlen(usrsales_buf);
        }
    }

    reader_unlock(&closed_auctions.a_mutex, &closed_auctions.a_sem, &closed_auctions.read_count);
    reader_unlock(&users.user_mutex, &users.user_sem, &users.read_count);

    petr_header *h = calloc(1, sizeof(petr_header));
    h->msg_type = USRSALES;
    h->msg_len = size == 0 ? 0 : size+1;
    usrsales_buf = size == 0 ? NULL : usrsales_buf;
    printf("sending: %s", usrsales_buf);
    wr_msg(job->connfd, h, usrsales_buf);

    free(h);
    free(usrsales_buf);
}

/* Return user's current balance, msg body is <balance>
 */
void usrblnc_h(sbuf_job *job) {
    // lock users for reading
    reader_lock(&users.user_mutex, &users.user_sem, &users.read_count);

    user *u = find_user(job->connfd);
    char msgbuf[20];
    sprintf(msgbuf, "%ld", u->balance);

    petr_header *h = calloc(1, sizeof(petr_header));
    h->msg_len = strlen(msgbuf)+1;
    h->msg_type = USRBLNC;
    wr_msg(job->connfd, h, msgbuf);

    // unlocks users for reading
    reader_unlock(&users.user_mutex, &users.user_sem, &users.read_count);
    free(h);
}

/* Helper function for watching a currently running auction.
 * If successful watch, send ANWATCH <item_name>\r\n<bin_price>
 * sends EANNNOTFOUND if no auction exists.
 * 
 */
void anwatch_h(sbuf_job *job) {
    char *endptr;
    int id = strtol(job->msg_buf, &endptr, 10);
    auction_t *to_watch = NULL;
    // lock for auctions & users writer
    P(&auctions.a_sem);
    P(&users.user_sem);
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
        
        char* msg = calloc(1, strlen(to_watch->item_name) + 20);
        sprintf(msg, "%s\r\n%ld", to_watch->item_name, to_watch->bin_price);
        
        h->msg_type = ANWATCH;
        h->msg_len = strlen(msg)+1;
        wr_msg(job->connfd, h, msg);
    }
    // unlock for auctions writer
    V(&auctions.a_sem);
    V(&users.user_sem);
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
    petr_header *h = calloc(1, sizeof(petr_header));
    
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
        h->msg_type = OK;
    }
    else { 
        h->msg_type = EANNOTFOUND;
    }
    // unlock
    V(&auctions.a_sem);
    V(&users.user_sem);

    h->msg_len = 0;
    wr_msg(job->connfd, h, NULL);
    free(h);
    printf("in anleave\n");
    return;
}

/* Runs when user bids on an auction, msg body format is 
 * <auction_id>\r\n<bid>.
 * If a user tries to bid on an auction they are not 
 * watching or created, send ANDENIED. If the bid is less than
 * or equal to the current highest bid, send EBIDLOW. If auction
 * was not found, send EANNOTFOUND. If successful, send OK.
 * Afterwards, send ANUPDATE to all watchers, format
 * <auction_id>\r\n<item_name\r\n<from_username>\r\n<bid>
 */
void anbid_h(sbuf_job *job) {
    petr_header *h = calloc(1, sizeof(petr_header));
    // parse msg body
    //11\r\n200\0
    //11\0\n200\0
    char *auc_str = job->msg_buf;
    char *bid_str = strstr(job->msg_buf, "\r\n") + 2;
    *(bid_str-2) = '\0';

    int bid = atoi(bid_str);
    int auc_id = atoi(auc_str);

    // lock auctions and users
    P(&auctions.a_sem);
    P(&users.user_sem);
    user *curr_user = find_user(job->connfd);
    auction_t *a;
    if((a = find_auction(auctions.auction_list, auc_id)) != NULL){
        if (find_watching_user(a->watching_u, curr_user) == 0 && a->creator != curr_user){
            // user is watching the auction
            if(bid <= a->highest_bid){
                // bid too low
                h->msg_type = EBIDLOW;
            } else {
                // successful bid, update highest bidder
                a->highest_bidder = curr_user;
                a->highest_bid = bid; 
                h->msg_type = OK;
            }
        } else {
            // user is not watching the auction or is the creator of the auction
            h->msg_type = EANDENIED;
        }
    }
    else{
        h->msg_type = EANNOTFOUND;
    }
    // send the first message
    h->msg_len = 0;
    wr_msg(job->connfd, h, NULL);

    if (h->msg_type == OK){
        // send anupdate
        // format <auction_id>\r\n<item_name\r\n<from_username>\r\n<bid>
        size_t msgbuf_len = strlen(a->item_name) + strlen(a->highest_bidder->uname) + 20;
        char *update_msgbuf = calloc(msgbuf_len , sizeof(char));
        sprintf(update_msgbuf, "%d\r\n%s\r\n%s\r\n%ld", a->auction_id, a->item_name, a->highest_bidder->uname, a->highest_bid);

        for(node_t *head = a->watching_u->head; head != NULL; head = head->next){
            user *u = head->value;
            h->msg_type = ANUPDATE;
            h->msg_len = strlen(update_msgbuf)+1;

            wr_msg(u->connfd, h, update_msgbuf);
        }
        free(update_msgbuf);
    }

    V(&auctions.a_sem);
    V(&users.user_sem);
    free(h);
}


/* helper function for finding an auction, returns NULL if
 * auction was not found in the passed auction list, otherise
 * returns pointer to the desired auction_t.
 */
auction_t* find_auction(List_t *auction_list, int auction_id) {
    for(node_t *head = auction_list->head; head != NULL; head = head->next){
        auction_t *a = head->value;
        if (a->auction_id == auction_id) return a;
    }
    return NULL;
}
auction_t* find_auction_index(List_t *auction_list, int auction_id, int *index) {
    int i = 0;
    for(node_t *head = auction_list->head; head != NULL; head = head->next){
        auction_t *a = head->value;
        if (a->auction_id == auction_id){
            *index = i;
            return a;
        }
        i++;
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
    P(&auctions.a_sem);
    P(&users.user_sem);
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
    V(&auctions.a_sem);
    V(&users.user_sem);
    
    petr_header *h = calloc(1, sizeof(petr_header));
    h->msg_len = 0;
    h->msg_type = 0;
    wr_msg(job->connfd, h, OK);
    free(h);
}

// question: does the signal handler need to clean up memory?
void sigint_handler(int sig) {
    close(listenfd);
    exit(0);
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
int do_login(int connfd, petr_header *h){
    char *loginbuf = malloc(sizeof(char) * h->msg_len);
    read(connfd, loginbuf, h->msg_len);
    loginbuf[h->msg_len-1] = 0; // null terminate

    char *passwd = strstr(loginbuf, "\r\n") + 2;
    if (passwd != NULL) {
        int num_users = users.num_users;
        
        size_t uname_size = strlen(loginbuf)-strlen(passwd)-2;

        // result of user_exists
        int exists_res = user_exists(loginbuf, uname_size, users.user_list);
        if(exists_res == 0){
            // creates new user (on heap)
            user* new_user = calloc(1, sizeof(user));
            new_user->password = calloc(1, sizeof(char) * strlen(passwd) + 1);
            new_user->uname = calloc(1, sizeof(char) * uname_size + 1);

            strcpy(new_user->password, passwd);
            strncpy(new_user->uname, loginbuf, uname_size);
            new_user->is_loggedin = 1;
            new_user->connfd = connfd;
            new_user->watching_a = CreateList(compare_auction);
            new_user->balance = 0;
            // new_user->auctions_won = CreateList(compare_auction);
            // printf("%s\n", new_user->uname);
            // printf("%s\n", new_user->password);

            users.num_users++;
            users.user_list[num_users] = new_user;
            users.user_list = realloc(users.user_list, (users.num_users + 1) * sizeof(user*));
        }
        else if (exists_res == 1) {
            for(int i=0; i < num_users; ++i){
                if(strncmp(users.user_list[i]->uname, loginbuf, uname_size) == 0){
                    if(users.user_list[i]->is_loggedin == 1){
                        // user is already logged in
                        h->msg_type = EUSRLGDIN;
                        h->msg_len = 0;
                        wr_msg(connfd, h, NULL);
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
            h->msg_type = EWRNGPWD;
            h->msg_len = 0;
            wr_msg(connfd, h, NULL);
            close(connfd);
        }
        
        if (exists_res >= 0){
            // successful login
            h->msg_type = OK;
            h->msg_len = 0;
            wr_msg(connfd, h, NULL);
            free(loginbuf);
            return 0;
        }
    }
    free(loginbuf);
    return -1;
}


void parse_aucfile(FILE *a_file) {
    char *line = NULL;
    size_t len = 0;
    int duration;
    long bin_price;
    char *name;
    do{
        int i = getline(&line, &len, a_file);
        if (i == -1 || *line == '\n')
            break;
        name = calloc(i, sizeof(char));
        strncpy(name, line, strlen(line)-2);
        getline(&line, &len, a_file);
        duration = atoi(line);
        getline(&line, &len, a_file);
        bin_price = atoi(line);
        
        printf("%s\n", name);
        auction_t *a = calloc(1, sizeof(auction_t));
        a->item_name = name;
        a->duration = duration;
        a->bin_price = bin_price;
        a->auction_id = AuctionID;
        a->watching_u = CreateList(compare_auction);

        AuctionID++;
        // add the new auction to the rear of the auction list
        insertRear(auctions.auction_list, a);
    } while (getline(&line, &len, a_file) != -1);
    
    free(line);
    fclose(a_file);
    return;
}

user* find_user(int connfd){
    for(int i=0; i < users.num_users; ++i){
        if(connfd == users.user_list[i]->connfd){
            return users.user_list[i];
        }
    }
    return NULL;
}

/* find if a user exists in an auction's watchlist.
 * @param watchlist an auction's watchlist, consisting of user pointers
 * @return 0 if it's found, and -1 if not.
 */
int find_watching_user(List_t *watchlist, user* u){
    for(node_t *head = watchlist->head; head != NULL; head = head->next){
        user* curr_user = head->value;
        if(curr_user == u){
            return 0;
        }
    }
    return -1;
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
    sbuf_job *job = calloc(1,sizeof(sbuf_job));
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