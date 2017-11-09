// CS 407 Lab 05 - Server
// Client/server application allowing user to run bash
// commands on a remote machine, similar to SSH or TELNET
//
// Server usage: ./rembashd
//
// author: Michael Smith

#define _XOPEN_SOURCE 600
#define _POSIX_C_SOURCE 199309L
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/epoll.h>
#include <pthread.h>
#include <time.h>
#include <sys/syscall.h>
#include "DTRACE.h"
#include "tpool.h"

#define PORT 4070
#define SECRET "cs407rembash"
#define MAX_NUM_CLIENTS 10000
#define TIMEOUT 5 

// preprocessor definitions for epoll_add
#define ADD_EPOLLIN 0
#define ADD_EPOLLOUT 1
#define RESET_EPOLLIN 2
#define RESET_EPOLLOUT 3

// type definitions
typedef enum client_state {
    STATE_NEW,
    STATE_SECRET,
    STATE_OK,
    STATE_ERROR,
    STATE_ESTABLISHED,
    STATE_UNWRITTEN,
    STATE_TERMINATED,
} client_state;

typedef struct client_object {
    client_state state;
    int ptyfd;
    int sockfd;
    int index;
    char data[4096];
} client_object;

// function prototypes
int *setup();
int setup_server_socket(int port);
void protocol_init(int connectfd);
void protocol_receive_secret(int connectfd);
void protocol_send_ok(int connectfd);
void protocol_send_error(int connectfd);
int pty_init(client_object *client);
int setuppty(pid_t *pid);
void sigalrm_handler(int signum);
void worker_function(int task);
void worker_established(int task);
void worker_unwritten(int task);
void worker_new(int task);
int epoll_add(int epfd, int fd, int mode);
int client_init(int epfd, int connectfd);
void cleanup_client(client_object *client);

// global variables
client_object *fdmap[(MAX_NUM_CLIENTS * 2) + 6]; 
int sockfd;
int epfd;

int main(int argc, char *argv[]) {

    DTRACE("%d: Server staring: PID=%d, PPID=%d, PGID=%d, SID=%d\n", getpid(), getpid(), getppid(), getpgrp(), getsid(0));

    int *setup_result, readyfd;
    struct epoll_event evlist[MAX_NUM_CLIENTS*2];

    // generic setup
    if ((setup_result = setup()) == NULL) {
        fprintf(stderr, "rembashd: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }   
    
    sockfd = setup_result[0];
    epfd = setup_result[1];

    while (1) {
        readyfd = epoll_wait(epfd, evlist, MAX_NUM_CLIENTS*2, -1);
        for (int i = 0; i < readyfd; i++) {
            if (evlist[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                DTRACE("ERR/HUP/RDHUP on a client\n");
                cleanup_client(fdmap[evlist[i].data.fd]);
                continue;
            }
            // hand task to the thread pool
            
            tpool_add_task(evlist[i].data.fd);
        }
    }

    exit(EXIT_SUCCESS);
} // end main()

// generic setup for the server
// returns an array consisting of sockfd and epoll fd
// or NULL on failure
int *setup() {
    int sockfd, epfd;
    struct sigaction act;
    static int result[2];

    // set up the server socket
    if ((sockfd = setup_server_socket(PORT)) == -1) {
        return NULL;
    }

    DTRACE("%d: Listening socket fd=%d created\n", getpid(), sockfd);

    // ignore SIGCHLD
    if (signal(SIGCHLD, SIG_IGN) == SIG_ERR) {
        DTRACE("%d: Error setting SIGCHLD to ignore: %s\n", getpid(), strerror(errno));
        return NULL;
    }

    // ignore SIGPIPE
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        DTRACE("%d: Error setting SIGPIPE to ignore: %s\n", getpid(), strerror(errno));
        return NULL;
    }

    // register sigalrm handler
    act.sa_handler = sigalrm_handler;
    act.sa_flags = 0;
    if (sigaction(SIGALRM, &act, NULL) == -1) {
        DTRACE("%d: Error registering SIGALRM handler: %s\n", getpid(), strerror(errno));
        return NULL;
    }

    // create epoll
    if ((epfd = epoll_create1(EPOLL_CLOEXEC)) == -1) {
        DTRACE("%d: Error creating epoll: %s\n", getpid(), strerror(errno));
        return NULL;
    }

    // add listening socket to the epoll
    if (epoll_add(epfd, sockfd, ADD_EPOLLIN) == -1) {
        return NULL;
    }

    // create thread pool
    if (tpool_init(worker_function) == -1) {
        DTRACE("%d: Error initializing thread tpool: %s\n", getpid(), strerror(errno));
        return NULL;
    }

    result[0] = sockfd;
    result[1] = epfd;
    return result;
} // end setup()

// write protocol id
void protocol_init(int connectfd) {
    const char * const rembash = "<rembash>\n";
    client_object *client = fdmap[connectfd];

    if (write(connectfd, rembash, strlen(rembash)) == -1) {
        if (errno == EAGAIN) {
            errno = 0;
            epoll_add(epfd, connectfd, RESET_EPOLLOUT);
            return;
        }
        DTRACE("%d: Error writing <rembash>: %s\n", getpid(), strerror(errno));
        close(client->sockfd);
        epoll_ctl(epfd, EPOLL_CTL_DEL, client->sockfd, NULL);
        client->state = STATE_TERMINATED;
        free(client);
        return;
    }
    client->state = STATE_SECRET;
    epoll_add(epfd, connectfd, RESET_EPOLLIN);
    return;
} // end protocol_init()

// read and verify secret
void protocol_receive_secret(int connectfd) {
    const char * const secret = "<" SECRET ">\n";
    char buff[4096];
    int nread;
    client_object *client = fdmap[connectfd];

    // read <SECRET>\n
    if ((nread = read(connectfd, buff, 4096)) == -1) {
        if (errno == EAGAIN) {
            errno = 0;
            epoll_add(epfd, connectfd, RESET_EPOLLIN);
            return;
        } 
        client->state = STATE_TERMINATED;
        return;
    }
    buff[nread] = '\0';
    DTRACE("%d: Read %s", getpid(), buff);
    
    // state = STATE_ERROR;
    if (strcmp(buff, secret) != 0) {
        client->state = STATE_ERROR;
        // write <error>\n
        DTRACE("%d: rembashd: invalid secret from client\n", getpid());
        return;
    } else {
        client->state = STATE_OK;
    }

    epoll_add(epfd, connectfd, RESET_EPOLLOUT);
    return;
} // end protocol_receive_secret()

void protocol_send_ok(int connectfd) {
    const char * const ok = "<ok>\n";

    client_object *client = fdmap[connectfd];
    if (write(connectfd, ok, strlen(ok)) == -1) {
        if (errno == EAGAIN) {
            epoll_add(epfd, connectfd, RESET_EPOLLOUT);
            errno = 0;
            return;
        } 
        client->state = STATE_TERMINATED;
        return;
    }
    client->state = STATE_ESTABLISHED;
    epoll_add(epfd, connectfd, RESET_EPOLLIN);
    pty_init(client);
    return;
} // end protocol_send_ok()

void protocol_send_error(int connectfd) {
    const char * const error = "<error>\n";

    client_object *client = fdmap[connectfd];

    if (write(connectfd, error, strlen(error)) == -1) {
        if (errno == EAGAIN) {
            epoll_add(epfd, connectfd, RESET_EPOLLOUT);
            errno = 0;
            return;
        }
        client->state = STATE_TERMINATED;
        return;
    }
    client->state = STATE_TERMINATED;
    return;
} // end protocol_send_error()

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

    // set the socket to listen
    if ((listen(sockfd, 10)) == -1) {
        DTRACE("%d: Error setting socket to listen: %s\n", getpid(), strerror(errno));
        return -1;
    }

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
        DTRACE("%d: Error creating PTY: %s\n", getpid(), strerror(errno));
        return -1;
    }

    // close on exec
    if (fcntl(mfd, F_SETFD, FD_CLOEXEC) == -1) {
        DTRACE("%d: Error setting mfd to close on exec: %s\n", getpid(), strerror(errno));
        return -1;
    }

    // nonblocking
    if (fcntl(mfd, F_SETFL, O_NONBLOCK) == -1) {
        DTRACE("%d: Error setting mfd to nonblocking: %s\n", getpid(), strerror(errno));
        return -1;
    }

    if (unlockpt(mfd) == -1) {
        DTRACE("%d: Error unlocking pty: %s\n", getpid(), strerror(errno));
        close(mfd);
        return -1;
    }

    if ((slavepointer = ptsname(mfd)) == NULL) {
        DTRACE("%d: Error on ptsname: %s\n", getpid(), strerror(errno));
        close(mfd);
        return -1;
    }

    char sname[strlen(slavepointer)];
    strcpy(sname, slavepointer);

    if ((slavepid = fork()) == -1) {
        DTRACE("%d: Error on fork: %s\n", getpid(), strerror(errno));
        return -1;
    }

    // parent
    if (slavepid != 0) {
        *pid = slavepid;
        return mfd;
    }

    DTRACE("%d: Slave subprocess created: PID=%d, PPID=%d, PGID=%d, SID=%d\n", getpid(), getpid(), getppid(), getpgrp(), getsid(0));

    // child will never return from this function
    int sfd;

    if (setsid() == -1) {
        DTRACE("%d: Error setting sid: %s\n", getpid(), strerror(errno));
        exit(EXIT_FAILURE);
    }

    if ((sfd = open(sname, O_RDWR)) == -1) {
        DTRACE("%d: Error opening slave pty: %s\n", getpid(), strerror(errno));
        exit(EXIT_FAILURE);
    }

    DTRACE("%d: Redirecting stdin/out/err to sfd=%d\n", getpid(), sfd);
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

// signal handler for sigalrm
void sigalrm_handler(int signum) {
    DTRACE("%d: SIGALRM handler fired\n", getpid());
} // end sigalrm_handler()

void worker_function(int task) {
    int connectfd;
    client_object *client;

    // if the event is a new connection
    if (task == sockfd) {
        connectfd = accept4(task, (struct sockaddr *) NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
        client_init(epfd, connectfd);
        epoll_add(epfd, task, RESET_EPOLLIN);
        return;
    }
    
    client = fdmap[task];
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

        case STATE_ERROR:
            protocol_send_error(task);
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
}

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
        DTRACE("Partial write!\n");
        DTRACE("Read %d\n Wrote %d\n", nread, nwrote);
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
// mode: 0: normal,
//       1: already in epoll, just needs reset
//       2: reset with EPOLLOUT
//       3: add with EPOLLOUT
// returns 0, or -1 on failure
int epoll_add(int epfd, int fd, int mode) {
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
        DTRACE("%d: Failed to add fd=%d to epoll: %s\n", getpid(), fd, strerror(errno));
        return -1;
    }
    return 0;
} // end epoll_add()

// initialize a client
int client_init(int epfd, int connectfd) {
    client_object *client;
    
    // create client object
    client = malloc(sizeof(client_object));
    client->sockfd = connectfd;
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
}

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
    close(client->sockfd);
    close(client->ptyfd);
    epoll_ctl(epfd, EPOLL_CTL_DEL, client->sockfd, NULL);
    epoll_ctl(epfd, EPOLL_CTL_DEL, client->ptyfd, NULL);
    free(client);
}


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
