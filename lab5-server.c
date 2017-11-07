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

// type definitions
typedef enum client_state {
    NEW,
    ESTABLISHED,
    UNWRITTEN,
    TERMINATED,
} client_state;

typedef struct client_object {
    client_state state;
    int ptyfd;
    int sockfd;
    char *data;
} client_object;

// function prototypes
int *setup();
int setup_server_socket(int port);
int protocol_init(int conncetfd);
int protocol_receive_secret(int connectfd);
int setuppty(pid_t *pid);
void sigalrm_handler(int signum);
void worker_function(int task);
int epoll_add(int epfd, int fd);
int client_init(int epfd, int connectfd);

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
    if (epoll_add(epfd, sockfd) == -1) {
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

// first half of rembash protocol 
// write protocol id
int protocol_init(int connectfd) {
    const char * const rembash = "<rembash>\n";
    if (write(connectfd, rembash, strlen(rembash)) == -1) {
        DTRACE("%d: Error writing <rembash>: %s\n", getpid(), strerror(errno));
        return -1;
    }
    return 0;
}

// second half of rembash protocol
// read and verify secret
int protocol_receive_secret(int connectfd) {
    const char * const error = "<error>\n";
    const char * const ok = "<ok>\n";
    const char * const secret = "<" SECRET ">\n";
    char buff[4096];
    int nread;

    // read <SECRET>\n
    if ((nread = read(connectfd, buff, 4096)) == -1) {
        DTRACE("%d: Error reading <SECRET>: %s\n", getpid(), strerror(errno));
        return -1;
    }
    buff[nread] = '\0';
    DTRACE("%d: Read %s", getpid(), buff);

    
    if (strcmp(buff, secret) != 0) {
        // write <error>\n
        DTRACE("%d: rembashd: invalid secret from client\n", getpid());
        if (write(connectfd, error, strlen(error)) == -1) {
            DTRACE("%d: rembashd: Error writing <error>: %s\n", getpid(), strerror(errno));
        }
        DTRACE("%d: Wrote %s", getpid(), error);
        return -1;
    }

    // write <ok>\n
    if (write(connectfd, ok, strlen(ok)) == -1) {
        DTRACE("%d: Error writing <ok>: %s\n", getpid(), strerror(errno));
        return -1;
    }
    DTRACE("%d: Wrote %s", getpid(), ok);

    return 0;
}

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
    int tofd, fromfd, nread, connectfd;
    char buff[4096];
    client_object *client;

    // if the event is a new connection
    if (task == sockfd) {
        connectfd = accept4(task, (struct sockaddr *) NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
        //connectfd = accept(task, (struct sockaddr *) NULL, NULL); 
        client_init(epfd, connectfd);
        return;
    }
    
    client = fdmap[task];
    switch (client->state) {
        case NEW:
            if (protocol_receive_secret(task) == -1) {
                DTRACE("Secret retreival failed\n");
                client->state = TERMINATED;
            } else {
                DTRACE("Connection established with %d\n", task);
                client->state = ESTABLISHED;
            }
            break;
        case ESTABLISHED:
            fromfd = task;
            tofd = (fromfd == client->sockfd) ? client->ptyfd : client->sockfd;
            if ((nread = read(fromfd, buff, 4096)) == -1) {
                DTRACE("Failed to read from %d\n", fromfd);
                client->state = TERMINATED;
                break;
            }
            buff[nread] = '\0';
            if (write(tofd, buff, nread) == -1) {
                DTRACE("Failed to write to %d\n", tofd);
                client->state = TERMINATED;
                break;
            }
            break;
        case TERMINATED:
            break;
        default:
            printf("Hello?\n");
            break;
    }
}

// add a given fd to a given epoll
// returns 0, or -1 on failure
int epoll_add(int epfd, int fd) {
    struct epoll_event event;   
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event) == -1) {
        DTRACE("%d: Failed to add fd=%d to epoll\n", getpid(), fd);
        return -1;
    }
    return 0;
}

// initialize a client
int client_init(int epfd, int connectfd) {
    int mfd;
    pid_t ptyfd;
    client_object *client;
    
    // perform first half of the procol with the client
    if (protocol_init(connectfd) == -1) {
        return -1;
    } 

    // create pty
    if ((mfd = setuppty(&ptyfd)) == -1) {
        return -1;
    }
   
    // create client object
    client = malloc(sizeof(client_object));
    client->sockfd = connectfd;
    client->ptyfd = mfd;
    client->state = NEW;

    DTRACE("%d, %d\n", connectfd, mfd);
    DTRACE("Client object created: sockfd=%d, ptyfd=%d\n", client->sockfd, client->ptyfd);
    // set up mapping
    fdmap[connectfd] = client;
    fdmap[mfd] = client;
    
    // add connection to epoll
    if (epoll_add(epfd, connectfd) == -1) {
        return -1;
    }

    // add mfd to epoll
    if (epoll_add(epfd, mfd) == -1) {
        return -1;
    }

    return 0;
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
