// CS 407 Lab 02 - Server
//
// Client/server application allowing user to run bash
// commands on a remote machine, similar to SSH or TELNET
//
// Server usage: ./rembashd
//
// author: Michael Smith

#define _XOPEN_SOURCE 600
#define _POSIX_C_SOURCE 199309L
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
#include "DTRACE.h"
#define PORT 4070
#define SECRET "cs407rembash"
#define MAX_NUM_CLIENTS 10000

// function prototypes
int setup();
void *epoll_loop(void *args);
int setup_server_socket(int port);
int protocol(int connect_fd);
int setuppty(pid_t *pid);
void write_loop(int fromfd, int tofd);
void sigchld_handler(int signum);
void *handle_client(void *args);
void sigalrm_handler(int signum);

// pid's must be stored globally so they can be killed by
// the signal handler
pid_t spid, wpid;
int epfd;
int fdmap[(MAX_NUM_CLIENTS * 2) + 5];

int main(int argc, char *argv[]) {

    DTRACE("%d: Server staring: PID=%d, PPID=%d, PGID=%d, SID=%d\n", getpid(), getpid(), getppid(), getpgrp(), getsid(0));

    int connect_fd, sockfd, *connect_fd_ptr;
    pthread_t clienttid;

    // generic setup
    if ((sockfd = setup()) == -1) {
        fprintf(stderr, "rembashd: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    /* infinite loop accepting connections */
    while(1) {
        if ((connect_fd = accept(sockfd, (struct sockaddr *) NULL, NULL)) == -1) {
            continue; // if accept failed, continue to next iteration
        }

        DTRACE("%d: Connection fd=%d accepted\n", getpid(), connect_fd);
        
        // store connect_fd in malloc'd memory to pass to thread
        if ((connect_fd_ptr = malloc(sizeof(int))) == NULL) {
            close(connect_fd);
            continue;
        }
        *connect_fd_ptr = connect_fd;

        if (pthread_create(&clienttid, NULL, handle_client, connect_fd_ptr) != 0) {
            close(connect_fd);
            continue;
        }

    } // end while

    // should never get here
    exit(EXIT_FAILURE);

} // end main()

// generic setup for the server
// returns listening socket fd, or -1 on failure
int setup() {
    int sockfd;
    pthread_t iothread;

    // set up the server socket
    if ((sockfd = setup_server_socket(PORT)) == -1) {
        return -1;
    }

    // set close on exec on listening server
    fcntl(sockfd, F_SETFD, FD_CLOEXEC);

    DTRACE("%d: Listening socket fd=%d created\n", getpid(), sockfd);

    // ignore SIGCHLD 
    if (signal(SIGCHLD, SIG_IGN) == SIG_ERR) {
        return -1; 
    }

    // ignore SIGPIPE
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        return -1; 
    }

    // create epoll
    if ((epfd = epoll_create1(EPOLL_CLOEXEC)) == -1) {
        return -1; 
    }

    // create the I/O thread
    if (pthread_create(&iothread, NULL, epoll_loop, NULL) != 0) {
        return -1;
    }


    return sockfd;
} // end setup()

// function to be called be the thread 
// handles I/O using epoll
void *epoll_loop(void *args) {
    int ready_fd, nread;
    struct epoll_event evlist[MAX_NUM_CLIENTS];
    char buff[4096];

    while (1) {
        ready_fd = epoll_wait(epfd, evlist, MAX_NUM_CLIENTS, -1);
        for (int i = 0; i < ready_fd; i++) {
            nread = read(evlist[i].data.fd, buff, 4096);
            buff[nread] = '\0';
            write(fdmap[evlist[i].data.fd], buff, strlen(buff));
        }
    }

    return NULL;
}

// performs the server end of the rembash protocol
// returns 0, or -1 on failure
int protocol(int connect_fd) {
    /* initialize variables */
    const char * const rembash = "<rembash>\n";
    const char * const error = "<error>\n";
    const char * const ok = "<ok>\n";
    const char * const secret = "<" SECRET ">\n";

    char buff[4096];
    int nread;
    timer_t timerid;
    struct itimerspec ts;
    struct sigaction act;

    // register sigalrm handler
    act.sa_handler = sigalrm_handler;
    act.sa_flags = 0;
    if (sigaction(SIGALRM, &act, NULL) == -1) {
        DTRACE("%d: %s\n", getpid(), strerror(errno));
        return -1;
    }

    // set up timer
    ts.it_value.tv_sec = 5;
    if (timer_create(CLOCK_REALTIME, NULL, &timerid) == -1) {
        DTRACE("%d: %s\n", getpid(), strerror(errno));
        return -1;
    }
    if (timer_settime(timerid, 0, &ts, NULL) == -1) {
        DTRACE("%d: %s\n", getpid(), strerror(errno));
        return -1;
    }

    // write <rembash>\n
    if (write(connect_fd, rembash, strlen(rembash)) == -1) {
        DTRACE("%d: %s\n", getpid(), strerror(errno));
        return -1;
    }
    DTRACE("%d: Wrote %s", getpid(), rembash);

    // read <SECRET>\n 
    if ((nread = read(connect_fd, buff, 4096)) == -1) {
        DTRACE("%d: %s\n", getpid(), strerror(errno));
        return -1;
    }
    buff[nread] = '\0';
    DTRACE("%d: Read %s", getpid(), buff);

    if (strcmp(buff, secret) != 0) {
        // write <error>\n
        DTRACE("%d: remcpd: invalid secret from client\n", getpid()); 
        if (write(connect_fd, error, strlen(error)) == -1) {
            DTRACE("%d: remcpd: %s\n", getpid(), strerror(errno));
        }
        DTRACE("%d: Wrote %s", getpid(), error);
        return -1;
    }

    // write <ok>\n
    if (write(connect_fd, ok, strlen(ok)) == -1) {
        DTRACE("%d: %s\n", getpid(), strerror(errno));
        return -1;
    }
    DTRACE("%d: Wrote %s", getpid(), ok);
   
    // disarm the timer
    if (timer_delete(timerid) == -1) {
        return -1;
    }
    
    return 0;
} // end protocol()

// function to create and set up a server socket
// returns the socket file descriptor,
// or -1 on failure
int setup_server_socket(int port) {
    int sockfd;

    // create the socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        return -1;
    }

    // immediate reuse of port for testing
    int i = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i)) == -1) {
        DTRACE("%d: setsockopt failed\n", getpid());
    }

    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);

    // bind the socket
    if ((bind(sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr))) == -1) {
        return -1;
    }

    // set the socket to listen
    if ((listen(sockfd, 10)) == -1) {
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
        DTRACE("%d: remcpd: %s\n", getpid(), strerror(errno));
        return -1;
    }

    fcntl(mfd, F_SETFD, FD_CLOEXEC);

    if (unlockpt(mfd) == -1) {
        DTRACE("%d: remcpd: %s\n", getpid(), strerror(errno));
        close(mfd);
        return -1;
    }

    if ((slavepointer = ptsname(mfd)) == NULL) {
        DTRACE("%d: remcpd: %s\n", getpid(), strerror(errno));
        close(mfd);
        return -1;
    }

    char sname[strlen(slavepointer)];
    strcpy(sname, slavepointer);

    if ((slavepid = fork()) == -1) {
        DTRACE("%d: remcpd: %s\n", getpid(), strerror(errno));
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
        DTRACE("%d: remcpd: %s\n", getpid(), strerror(errno));
        exit(EXIT_FAILURE);
    }

    if ((sfd = open(sname, O_RDWR)) == -1) {
        DTRACE("%d: remcpd: %s\n", getpid(), strerror(errno));
        exit(EXIT_FAILURE);
    }

    DTRACE("%d: Redirecting stdin/out/err to sfd=%d\n", getpid(), sfd);
    DTRACE("%d: Closing sfd=%d\n", getpid(), sfd);
    DTRACE("%d: Execing into bash\n", getpid());

    // redirect stdin/stdout/stderr to the pty slave 
    for (int i = 0; i < 3; i++) {
        if (dup2(sfd, i) == -1) {
            DTRACE("%d: %s\n", getpid(), strerror(errno));
            exit(EXIT_FAILURE);
        }
    } 

    close(sfd); // no need for this anymore

    // exec into bash
    execlp("bash", "bash", NULL);

    // should only reach here if execlp failed 
    exit(EXIT_FAILURE); 
} // end setuppty

// signal handler for sigchld
void sigchld_handler(int signum) {
    DTRACE("%d: SIGCHLD handler fired\n", getpid());
    kill(spid, SIGTERM);
    kill(wpid, SIGTERM);

    // both children are now dead, so collect them
    while (waitpid(-1, NULL, WNOHANG) > 0);

    DTRACE("%d: Processes %d and %d have been terminated and collected\n", getpid(), spid, wpid);
    DTRACE("%d: Terminating self\n", getpid());
    exit(EXIT_SUCCESS);
} // end sigchld_handler

void sigalrm_handler(int signum) {
    DTRACE("sigalrm!\n");
}
                 
// function to be called by thread to handle each client
void *handle_client(void *args) {
    struct epoll_event event;   
    int mfd, connect_fd;
    pid_t ptyfd;

    connect_fd = *(int*)args;
    free(args);

    // set close on exec on the connection
    fcntl(connect_fd, F_SETFD, FD_CLOEXEC);

    if (protocol(connect_fd) == -1) {
        close(connect_fd);
        return NULL;
    }

    // add connection to epoll
    event.data.fd = connect_fd;
    event.events = EPOLLIN;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, connect_fd, &event) == -1) {
        DTRACE("%d: Failed to add fd=%d to epoll, closing %d\n", getpid(), connect_fd, connect_fd);
        close(connect_fd);
        return NULL;
    }

    DTRACE("%d: fd=%d added to epoll\n", getpid(), connect_fd);

    // create pty
    if ((mfd = setuppty(&ptyfd)) == -1) {
        close(connect_fd);
        return NULL;
    }

    // add mfd to epoll
    event.data.fd = mfd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, mfd, &event) == -1) {
        DTRACE("%d: Failed to add fd=%d to epoll, closing %d\n", getpid(), mfd, mfd);
        close(connect_fd);
        close(mfd);
        return NULL;
    }
    DTRACE("%d: fd=%d added to epoll\n", getpid(), mfd);

    fdmap[connect_fd] = mfd;
    fdmap[mfd] = connect_fd;

    return NULL;
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
