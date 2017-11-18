// CS 407 Lab 05 - Server
// Client/server application allowing user to run bash
// commands on a remote machine, similar to SSH or TELNET
//
// Server usage: ./rembashd
//
// author: Michael Smith

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include "DTRACE.h"
#include "tpool.h"

#define PORT 4070
#define SECRET "cs407rembash"
#define MAX_NUM_CLIENTS 100000
#define TIMEOUT 5

// type definitions

// state enum for the client objects
typedef enum client_state {
    STATE_NEW,
    STATE_SECRET,
    STATE_OK,
    STATE_ESTABLISHED,
    STATE_UNWRITTEN,
    STATE_TERMINATED,
} client_state;

// options for the epoll_add function
typedef enum epoll_add_options {
    ADD_EPOLLIN,
    ADD_EPOLLOUT,
    RESET_EPOLLIN,
    RESET_EPOLLOUT,
} epoll_add_options;

// client object
typedef struct client_object {
    client_state state;
    int ptyfd;
    int sockfd;
    int index; // position in data buffer
    struct client_object *next; // pointer to next, for the linked list
    int free; // flag to determine whether this object is free or allocated
    char data[4096]; // data to be written to sockfd
} client_object;

// function prototypes
int setup();
int setup_server_socket(int port);
int setup_timer(int sockfd);
void protocol_init(int connectfd);
void protocol_receive_secret(int connectfd);
void protocol_send_ok(int connectfd);
void protocol_send_error(int connectfd);
int pty_init(client_object *client);
int setuppty(pid_t *pid);
void worker_function(int task);
void worker_newconnection(int task);
void worker_timer(int task);
void worker_established(int task);
void worker_unwritten(int task);
void worker_new(int task);
int epoll_add(int epfd, int fd, epoll_add_options mode);
int client_init(int epfd, int connectfd);
void cleanup_client(client_object *client);
client_object *allocate_client();
void free_client(client_object *client);

// global variables
client_object *fdmap[(MAX_NUM_CLIENTS * 2) + 7];
int timer_map[(MAX_NUM_CLIENTS * 2) + 6];
int sockfd; // listening socket
int epfd; // epoll fd
int timer_epfd; // timer epoll fd
client_object slab[MAX_NUM_CLIENTS]; // slab allocation
client_object *list_head = &slab[0];

int main(int argc, char *argv[]) {
    DTRACE("--INITIALIZATION--\n");
    DTRACE("Server staring: PID=%d, PPID=%d, PGID=%d, SID=%d\n", getpid(), getppid(), getpgrp(), getsid(0));
    int readyfd;
    struct epoll_event evlist[MAX_NUM_CLIENTS*2];

    // generic setup
    if (setup() == -1) {
        fprintf(stderr, "rembashd: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    DTRACE("Entering epoll_wait loop\n");
    DTRACE("--END INITIALIZATION--\n");

    // epoll_wait loop
    while (1) {
        if ((readyfd = epoll_wait(epfd, evlist, MAX_NUM_CLIENTS*2, -1)) == -1) {
            DTRACE("Error on epoll_wait: %s\n", strerror(errno));
            break;
        }
        for (int i = 0; i < readyfd; i++) {
            // if event is an error, kill client
            if (evlist[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                DTRACE("ERR/HUP/RDHUP on a client\n");
                cleanup_client(fdmap[evlist[i].data.fd]);
            } else {
                // hand task to the thread pool
                tpool_add_task(evlist[i].data.fd);
            } // end if/else
        } // end for
    } // end while

    // should never get here
    exit(EXIT_FAILURE);
} // end main()

// generic setup for the server
// returns 0, or -1 on failure
int setup() {
    // set up the server socket
    if ((sockfd = setup_server_socket(PORT)) == -1) {
        return -1;
    }

    // ignore SIGCHLD
    if (signal(SIGCHLD, SIG_IGN) == SIG_ERR) {
        DTRACE("%d: Error setting SIGCHLD to ignore: %s\n", getpid(), strerror(errno));
        return -1;
    }

    DTRACE("SIGCHLD ignored\n");

    // ignore SIGPIPE
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        DTRACE("%d: Error setting SIGPIPE to ignore: %s\n", getpid(), strerror(errno));
        return -1;
    }

    DTRACE("SIGPIPE ignored\n");

    // create epoll
    if ((epfd = epoll_create1(EPOLL_CLOEXEC)) == -1) {
        DTRACE("%d: Error creating epoll: %s\n", getpid(), strerror(errno));
        return -1;
    }

    DTRACE("Epoll fd=%d created\n", epfd);

    // create timer epoll
    if ((timer_epfd = epoll_create1(EPOLL_CLOEXEC)) == -1) {
        DTRACE("%d: Error creating timer epoll: %s\n", getpid(), strerror(errno));
        return -1;
    }

    DTRACE("Timer epoll fd=%d created\n", timer_epfd);

    // add timer epoll to the epoll
    if (epoll_add(epfd, timer_epfd, ADD_EPOLLIN) == -1) {
        return -1;
    }

    DTRACE("Timer epoll fd=%d added to epoll fd=%d\n", timer_epfd, epfd);

    // add listening socket to the epoll
    if (epoll_add(epfd, sockfd, ADD_EPOLLIN) == -1) {
        return -1;
    }

    DTRACE("Listening socket fd=%d added to epoll fd=%d\n", sockfd, epfd);

    // create thread pool
    if (tpool_init(worker_function) == -1) {
        DTRACE("%d: Error initializing thread tpool: %s\n", getpid(), strerror(errno));
        return -1;
    }

    DTRACE("Thread pool initialized\n");

    // initialize the linked list
    for (int i = 0; i < MAX_NUM_CLIENTS-1; i++) {
        slab[i].next = &slab[i+1];
        slab[i].free = 1;
    }

    DTRACE("Linked list of client memory initialized\n");

    return 0;
} // end setup()

// create a timerfd, add it to the timer epoll, and arm it
// needs sockfd of associated client 
int setup_timer(int sockfd) {
    struct itimerspec tspec;
    int timerfd;

    if ((timerfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC)) == -1) {
        DTRACE("Error creating timerfd: %s\n", strerror(errno));
        return -1;
    }

    DTRACE("Created timerfd=%d\n", timerfd);

    // add the timer to the timer epoll
    if (epoll_add(timer_epfd, timerfd, ADD_EPOLLIN) == -1) {
        close(timerfd);
        return -1;
    }
    DTRACE("Timerfd=%d added to timer epoll fd=%d\n", timerfd, timer_epfd);

    // store timer/sockfd association
    timer_map[timerfd] = sockfd;
    timer_map[sockfd] = timerfd;

    // arm the timer
    tspec.it_value.tv_sec = 1;
    tspec.it_value.tv_nsec = 0;
    tspec.it_interval.tv_sec = 0;
    tspec.it_interval.tv_nsec = 0;
    if (timerfd_settime(timerfd, 0, &tspec, NULL) == -1) {
        DTRACE("Error setting timerfd: %s\n", strerror(errno));
        close(timerfd);
        return -1;
    }
    DTRACE("Armed timerfd=%d\n", timerfd);
    
    return 0;
} // end setup_timer()

// STATE_NEW
void protocol_init(int connectfd) {
    const char * const rembash = "<rembash>\n";
    client_object *client = fdmap[connectfd];

    // write <rembash>
    if (write(connectfd, rembash, strlen(rembash)) == -1) {
        if (errno == EAGAIN) {
            errno = 0;
            if (epoll_add(epfd, connectfd, RESET_EPOLLOUT) == -1)
                cleanup_client(client);
            return;
        }
        DTRACE("%d: Error writing <rembash>: %s\n", getpid(), strerror(errno));
        cleanup_client(client);
        return;
    }
    DTRACE("Wrote <rembash> to fd=%d\n", connectfd);

    // set up the associated timer
    if (setup_timer(client->sockfd) == -1) {
        cleanup_client(client);
        return;
    }

    // set client to STATE_SECRET
    DTRACE("Client fd=%d state=STATE_SECRET\n", connectfd);
    client->state = STATE_SECRET;

    if (epoll_add(epfd, connectfd, RESET_EPOLLIN) == -1)
        cleanup_client(client);

    return;
} // end protocol_init()

// STATE_SECRET
void protocol_receive_secret(int connectfd) {
    const char * const secret = "<" SECRET ">\n";
    const char * const error = "<error>\n";
    char buff[4096];
    int nread, timerfd;
    client_object *client = fdmap[connectfd];

    // read <SECRET>\n
    if ((nread = read(connectfd, buff, 4096)) == -1) {
        if (errno == EAGAIN) {
            errno = 0;
            if (epoll_add(epfd, connectfd, RESET_EPOLLIN) == -1)
                cleanup_client(client);
            return;
        }
        DTRACE("Error reading from fd=%d: %s\n", connectfd, strerror(errno));
        cleanup_client(client);
        return;
    }
    buff[nread] = '\0';
    DTRACE("Read secret from fd=%d: %s", connectfd, buff);

    // disarm the timer
    timerfd = timer_map[connectfd];
    close(timerfd);
    DTRACE("Timer fd=%d disarmed and closed\n", timerfd);

    if (strcmp(buff, secret) != 0) {
        if (write(connectfd, error, strlen(error)) == -1) {
            errno = 0; // reset errno in case of EAGAIN
            DTRACE("Error writing error message to client %d\n", connectfd);
        }
        DTRACE("Wrote <error> to client %d\n", connectfd);
        cleanup_client(client);
        return;
    } else {
        // state = STATE_OK;
        client->state = STATE_OK;
        DTRACE("Client fd=%d state=STATE_OK\n", connectfd);
    }

    if (epoll_add(epfd, connectfd, RESET_EPOLLOUT) == -1) {
        cleanup_client(client);
    }
    return;
} // end protocol_receive_secret()

// STATE_OK
void protocol_send_ok(int connectfd) {
    const char * const ok = "<ok>\n";

    client_object *client = fdmap[connectfd];
    if (write(connectfd, ok, strlen(ok)) == -1) {
        if (errno == EAGAIN) {
            if (epoll_add(epfd, connectfd, RESET_EPOLLOUT) == -1)
                cleanup_client(client);
            errno = 0;
            return;
        }
        cleanup_client(client);
        return;
    }

    DTRACE("Wrote <ok> to fd=%d\n", connectfd);

    DTRACE("Client fd=%d state=STATE_ESTABLISHED\n", connectfd);
    DTRACE("Setting up PTY for client fd=%d\n", connectfd);

    if (pty_init(client) == -1) {
        cleanup_client(client);
        return;
    }

    client->state = STATE_ESTABLISHED;

    epoll_add(epfd, connectfd, RESET_EPOLLIN);
    return;
} // end protocol_send_ok()

// function to create and set up a server socket
// returns the socket file descriptor,
// or -1 on failure
int setup_server_socket(int port) {
    int sockfd;

    // create the listening socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0)) == -1) {
        DTRACE("%d: Error creating socket: %s\n", getpid(), strerror(errno));
        return -1;
    }

    DTRACE("Created listening socket fd=%d\n", sockfd);

    // immediate reuse of port for testing
    int i = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i)) == -1) {
        DTRACE("%d: Error setting socket options (port may not be reusable immediately)\n", getpid());
    }

    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);

    // bind the socket
    if ((bind(sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr))) == -1) {
        DTRACE("%d: Error binding the socket: %s\n", getpid(), strerror(errno));
        return -1;
    }

    DTRACE("Bound listening socket fd=%d to port %d\n", sockfd, port);

    // set the socket to listen
    if ((listen(sockfd, 10)) == -1) {
        DTRACE("%d: Error setting socket to listen: %s\n", getpid(), strerror(errno));
        return -1;
    }

    DTRACE("Set listening socket fd=%d to listen\n", sockfd);

    return sockfd;

} // end setup_server_socket()

// creates and sets up the psuedoterminal master/slave
// returns master fd, and stores subprocess pid in pointer
// returns -1 on failure
int setuppty(pid_t *pid) {
    int mfd;
    char *slavepointer;
    pid_t slavepid;

    if ((mfd = posix_openpt(O_RDWR | O_NOCTTY)) == -1) {
        DTRACE("Error creating PTY: %s\n", strerror(errno));
        return -1;
    }

    // close on exec
    if (fcntl(mfd, F_SETFD, FD_CLOEXEC) == -1) {
        DTRACE("Error setting mfd to close on exec: %s\n", strerror(errno));
        return -1;
    }

    // nonblocking
    if (fcntl(mfd, F_SETFL, O_NONBLOCK) == -1) {
        DTRACE("Error setting mfd to nonblocking: %s\n", strerror(errno));
        return -1;
    }

    if (unlockpt(mfd) == -1) {
        DTRACE("Error unlocking pty: %s\n", strerror(errno));
        close(mfd);
        return -1;
    }

    if ((slavepointer = ptsname(mfd)) == NULL) {
        DTRACE("Error on ptsname: %s\n", strerror(errno));
        close(mfd);
        return -1;
    }

    char sname[strlen(slavepointer)];
    strcpy(sname, slavepointer);

    if ((slavepid = fork()) == -1) {
        DTRACE("Error on fork: %s\n", strerror(errno));
        return -1;
    }

    // parent
    if (slavepid != 0) {
        *pid = slavepid;
        return mfd;
    }

    DTRACE("Slave subprocess created: PID=%d, PPID=%d, PGID=%d, SID=%d\n", getpid(), getppid(), getpgrp(), getsid(0));

    // child will never return from this function
    int sfd;

    if (setsid() == -1) {
        DTRACE("Error setting sid: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if ((sfd = open(sname, O_RDWR)) == -1) {
        DTRACE("Error opening slave pty: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    DTRACE("Redirecting stdin/out/err to sfd=%d\n", sfd);
    // redirect stdin/stdout/stderr to the pty slave
    for (int i = 0; i < 3; i++) {
        if (dup2(sfd, i) == -1) {
            exit(EXIT_FAILURE);
        }
    }

    close(sfd); // no need for this anymore

    // exec into bash
    execlp("bash", "bash", NULL);

    // should only reach here if execlp failed
    exit(EXIT_FAILURE);
} // end setuppty()

void worker_function(int task) {
    client_object *client;

    // if the event is a new connection
    if (task == sockfd) {
        worker_newconnection(task);
        return;
    }

    // if the event is a timer
    if (task == timer_epfd) {
        worker_timer(task);
        return;
    }

    client = fdmap[task];
    if (client == NULL) return;
    switch (client->state) {
        case STATE_NEW:
            protocol_init(task);
            break;

        case STATE_SECRET:
            protocol_receive_secret(task);
            break;

        case STATE_OK:
            protocol_send_ok(task);
            break;

        case STATE_ESTABLISHED:
            worker_established(task);
            break;

        case STATE_UNWRITTEN:
            worker_unwritten(task);
            break;

        case STATE_TERMINATED:
            // client is terminated -> do nothing
            break;

        default:
            DTRACE("Error: Invalid client state\n");
            break;
    }

} // end worker_function()

// new connection
void worker_newconnection(int task) {
    int connectfd;

    if (task == sockfd) {
        if ((connectfd = accept4(task, (struct sockaddr *) NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK)) == -1) {
            if (errno == EAGAIN) {
                errno = 0;
                epoll_add(epfd, task, RESET_EPOLLIN);
                return;
            }
            DTRACE("Error accepting a client: %s\n", strerror(errno));
        }

        DTRACE("Client accepted: fd=%d\n", connectfd);
        if (client_init(epfd, connectfd) == -1) {
            close(connectfd);
            epoll_add(epfd, task, RESET_EPOLLIN);
            return; 
        }

        epoll_add(epfd, task, RESET_EPOLLIN);
        return;
    }
} // end worker_newconnection()

// task is an expired timer
void worker_timer(int task) {
    int readyfd;
    struct epoll_event evlist[MAX_NUM_CLIENTS];
    client_object *client;

    if ((readyfd = epoll_wait(task, evlist, MAX_NUM_CLIENTS, -1)) == -1) {
        DTRACE("Error on epoll_wait: %s\n", strerror(errno));
        return;
    }
    for (int i = 0; i < readyfd; i++) {
        client = fdmap[timer_map[evlist[i].data.fd]];
        DTRACE("Timer expired: %d\n", client->sockfd);
        cleanup_client(client);
        close(evlist[i].data.fd);
    }
    epoll_add(epfd, task, RESET_EPOLLIN);
    return;
} // end worker_timer()

void worker_established(int task) {
    int fromfd, tofd, nread, nwrote;
    char buff[4096];

    client_object *client = fdmap[task];
    fromfd = task;
    tofd = (fromfd == client->sockfd) ? client->ptyfd : client->sockfd;
    if ((nread = read(fromfd, buff, 4096)) == -1) {
        if (errno == EAGAIN) {
            epoll_add(epfd, task, RESET_EPOLLIN);
            errno = 0; // reset errno
            return;
        } else {
            DTRACE("Failed to read from %d\n", fromfd);
            cleanup_client(client);
        }
        return;
    }
    buff[nread] = '\0';

    if ((nwrote = write(tofd, buff, nread)) == -1) {
        if (errno == EAGAIN) {
            errno = 0; // reset errno
            epoll_add(epfd, task, RESET_EPOLLIN);
            return;
        } else {
            DTRACE("Failed to write to %d\n", tofd);
            cleanup_client(client);
            return;
        }
    }

    // partial write
    if (nwrote < nread) {
        client->state = STATE_UNWRITTEN;
        client->index = 0;

        // store info in the array
        int j = 0;
        for (int i = nwrote; i < nread; i++) {
            client->data[j++] = buff[i];
        }
        client->data[j] = '\0';

        // add sockfd to epoll listening for EPOLLOUT
        epoll_add(epfd, client->sockfd, RESET_EPOLLOUT);
        return;
    }
    epoll_add(epfd, task, RESET_EPOLLIN);
    return;

} // end worker_established

void worker_unwritten(int task) {
    int nwrote;
    client_object *client = fdmap[task];
    char *to_write = &client->data[client->index];

    if ((nwrote = write(client->sockfd, to_write, strlen(to_write))) == -1) {
        if (errno == EAGAIN) {
            errno = 0;
        } else {
            DTRACE("Unable to write to %d\n", client->sockfd);
            cleanup_client(client);
        }
    }

    if (nwrote < strlen(to_write)) {
        client->index += nwrote;
        epoll_add(epfd, client->sockfd, RESET_EPOLLOUT);
    } else {
        client->state = STATE_ESTABLISHED;
        epoll_add(epfd, client->sockfd, RESET_EPOLLIN);
        epoll_add(epfd, client->ptyfd, RESET_EPOLLIN);
    }

} // end worker_unwritten()

// add a given fd to a given epoll
int epoll_add(int epfd, int fd, epoll_add_options mode) {
    int op, events;

    switch (mode) {
        case ADD_EPOLLIN:
            op = EPOLL_CTL_ADD;
            events = EPOLLIN | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
            break;
        case RESET_EPOLLIN:
            op = EPOLL_CTL_MOD;
            events = EPOLLIN | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
            break;
        case RESET_EPOLLOUT:
            op = EPOLL_CTL_MOD;
            events = EPOLLOUT | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
            break;
        case ADD_EPOLLOUT:
            op = EPOLL_CTL_ADD;
            events = EPOLLOUT | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
            break;
    }

    struct epoll_event event;
    event.data.fd = fd;
    event.events = events;
    if (epoll_ctl(epfd, op, fd, &event) == -1) {
        DTRACE("Failed to add fd=%d to epoll fd=%d: %s\n", fd, epfd, strerror(errno));
        return -1;
    }
    return 0;
} // end epoll_add()

// initialize a client
// creates a client object and adds the sockfd to the epoll
int client_init(int epfd, int connectfd) {
    client_object *client;

    // create client object
    if ((client = allocate_client()) == NULL) {
        DTRACE("Too many clients\n");
        return -1;
    }
    client->sockfd = connectfd;
    client->ptyfd = -1;
    client->state = STATE_NEW;
    client->index = 0;

    DTRACE("Client object created: sockfd=%d, ptyfd=%d\n", client->sockfd, client->ptyfd);
    // set up mapping
    fdmap[connectfd] = client;

    // add connection to epoll listening for EPOLLOUT
    if (epoll_add(epfd, connectfd, ADD_EPOLLOUT) == -1) {
        return -1;
    }

    return 0;
} // end client_init()

// give a client a pty
int pty_init(client_object *client) {
    int mfd;
    pid_t ptyfd;

    if ((mfd = setuppty(&ptyfd)) == -1) {
        return -1;
    }

    client->ptyfd = mfd;
    fdmap[mfd] = client;

    if (epoll_add(epfd, mfd, ADD_EPOLLIN) == -1) {
        return -1;
    }

    return 0;
} // end pty_init()

// kill and clean up a client connection
void cleanup_client(client_object *client) {
    DTRACE("Cleaning up client: %d\n", client->sockfd);
    DTRACE("Closing %d and %d\n", client->sockfd, client->ptyfd);
    client->state = STATE_TERMINATED;
    close(client->sockfd);
    close(client->ptyfd);
    epoll_ctl(epfd, EPOLL_CTL_DEL, client->sockfd, NULL);
    epoll_ctl(epfd, EPOLL_CTL_DEL, client->ptyfd, NULL);
    free_client(client);
} // end cleanup_client()

// analogous to malloc()
client_object *allocate_client() {
    client_object *result = list_head;
    if (result == NULL) {
        return NULL;
    }
    list_head = list_head->next;
    result->free = 0;
    return result;
} // end allocate_client()

// analogous to free()
void free_client(client_object *client) {
    if (!client->free) {
        client->next = list_head;
        list_head = client;
        client->free = 1;
    }
} // end free_client()


//   _____
//  < EOF >
//   -----
//    \                                  ,+*^^*+___+++_
//     \                           ,*^^^^              )
//      \                       _+*                     ^**+_
//       \                    +^       _ _++*+_+++_,         )
//                _+^^*+_    (     ,+*^ ^          \+_        )
//               {       )  (    ,(    ,_+--+--,      ^)      ^\.
//              { (@)    } f   ,(  ,+-^ __*_*_  ^^\_   ^\       )
//             {:;-/    (_+*-+^^^^^+*+*<_ _++_)_    )    )      /
//            ( /  (    (        ,___    ^*+_+* )   <    <      \.
//             U _/     )    *--<  ) ^\-----++__)   )    )       )
//              (      )  _(^)^^))  )  )\^^^^^))^*+/    /       /.
//            (      /  (_))_^)) )  )  ))^^^^^))^^^)__/     +^^
//           (     ,/    (^))^))  )  ) ))^^^^^^^))^^)       _)
//            *+__+*       (_))^)  ) ) ))^^^^^^))^^^^^)____*^
//            \             \_)^)_)) ))^^^^^^^^^^))^^^^)
//             (_             ^\__^^^^^^^^^^^^))^^^^^^^)
//               ^\___            ^\__^^^^^^))^^^^^^^^)\\.
//                    ^^^^^\uuu/^^\uuu/^^^^\^\^\^\^\^\^\^\.
//                       ___) >____) >___   ^\_\_\_\_\_\_\)
//                      ^^^//\\_^^//\\_^       ^(\_\_\_\)
//                        ^^^ ^^ ^^^ ^
