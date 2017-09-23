// CS 407 Lab 02
//
// Client/server application allowing user to run bash
// commands on a remote machine, similar to SSH or TELNET
//
// Server usage: ./rembashd
//
// author: Michael Smith

#define _XOPEN_SOURCE 600
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
#include "DTRACE.h"
#define PORT 4070
#define SECRET "cs407rembash"

// function prototypes
void handle_client(int connect_fd);
int setup_server_socket(int port);
int protocol(int connect_fd);
int setuppty(pid_t *pid, int connect_fd);
void write_loop(int fromfd, int tofd);
void sigchld_handler(int signum);

// pid's must be stored globally so they can be killed by
// the signal handler
pid_t spid, wpid;

int main(int argc, char *argv[]) {

    DTRACE("%d: Server staring: PID=%d, PPID=%d, PGID=%d, SID=%d\n", getpid(), getpid(), getppid(), getpgrp(), getsid(0));

    int connect_fd, sockfd;

    if ((sockfd = setup_server_socket(PORT)) == -1) {
        fprintf(stderr, "rembashd: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    DTRACE("%d: Listening socket fd=%d created\n", getpid(), sockfd);

    // children auto collected
    signal(SIGCHLD, SIG_IGN);

    /* infinite loop accepting connections */
    while(1) {
        if ((connect_fd = accept(sockfd, (struct sockaddr *) NULL, NULL)) == -1) {
            continue; // if accept failed, continue to next iteration
        }
        
        DTRACE("%d: Connection fd=%d accepted\n", getpid(), connect_fd);

        switch (fork()) {
        case -1: // error; close fd and move on
            fprintf(stderr, "remcpd: %s\n", strerror(errno));
            close(connect_fd);
            break;

        case 0: // in child
            DTRACE("%d: Subprocess for client created: PID=%d, PPID=%d, PGID=%d, SID=%d\n", getpid(), getpid(), getppid(), getpgrp(), getsid(0));
            close(sockfd); // close listening socket in child
            DTRACE("%d: Listening socket fd=%d closed\n", getpid(), sockfd);
            handle_client(connect_fd);
            exit(EXIT_FAILURE);
        
        default: // in parent
            close(connect_fd); // close connection in parent
            DTRACE("%d: Connection fd=%d closed\n", getpid(), connect_fd);
            break;

        } // end switch/case

    } // end while

    // should never get here
    exit(EXIT_FAILURE);

} // end main()


// function to be called be the child processes
// handles each client given the file descriptor 
// performs protocol, redirects stdin, stdout, and stderr, and exec's into bash
void handle_client(int connect_fd) {

    int mfd;
    struct sigaction act;
    
    // register signal handler
    act.sa_handler = sigchld_handler;
    sigaction(SIGCHLD, &act, NULL);

    // perform rembash protocl
    if (protocol(connect_fd) == -1) {
        // errors in the protocol are DTRACE'd out within the function 
        exit(EXIT_FAILURE);
    }

    DTRACE("%d: Protocol successful\n", getpid());

    // create the pty
    if ((mfd = setuppty(&spid, connect_fd)) == -1) {
        DTRACE("%d: %s\n", getpid(), strerror(errno));
    }

    DTRACE("%d: PTY Created: mfd=%d, spid=%d\n", getpid(), mfd, spid);

    switch (wpid = fork()) {
    case -1:
        exit(EXIT_FAILURE);
    case 0:
        DTRACE("%d: Subprocess created: PID=%d, PPID=%d, PGID=%d, SID=%d\n", getpid(), getpid(), getppid(), getpgrp(), getsid(0));
        DTRACE("%d: Data transfer: connect_fd=%d => mfd=%d\n", getpid(), connect_fd, mfd);
        write_loop(connect_fd, mfd);
        DTRACE("%d: Transfer terminated, exiting\n", getpid());
        exit(EXIT_FAILURE); // sends sigchld to parent
    default:
        DTRACE("%d: Data transfer: mfd=%d => connect_fd=%d\n", getpid(), connect_fd, mfd);
        write_loop(mfd, connect_fd);
        DTRACE("%d Tranfer terminated, killing %d and exiting\n", getpid(), wpid);
        kill(wpid, SIGTERM); // kill child
        wait(NULL);
        exit(EXIT_FAILURE);
    }

    exit(EXIT_SUCCESS);
} // end handle_client()

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

    // write <rembash>\n
    if (write(connect_fd, rembash, strlen(rembash)) == -1) {
        DTRACE("%d: %s\n", getpid(), strerror(errno));
        return -1;
    }

    // read <SECRET>\n 
    if ((nread = read(connect_fd, buff, 4096)) == -1) {
        DTRACE("%d: %s\n", getpid(), strerror(errno));
        return -1;
    }
    buff[nread] = '\0';

    if (strcmp(buff, secret) != 0) {
        // write <error>\n
        DTRACE("%d: remcpd: invalid secret from client\n", getpid()); 
        if (write(connect_fd, error, strlen(error)) == -1) {
            DTRACE("%d: remcpd: %s\n", getpid(), strerror(errno));
        }
        return -1;
    }

    // write <ok>\n
    if (write(connect_fd, ok, strlen(ok)) == -1) {
        DTRACE("%d: %s\n", getpid(), strerror(errno));
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
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i));

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
int setuppty(pid_t *pid, int connect_fd) {
    int mfd;
    char *slavepointer;
    pid_t slavepid;

    if ((mfd = posix_openpt(O_RDWR | O_NOCTTY)) == -1) {
        return -1;
    }

    if (unlockpt(mfd) == -1) {
        close(mfd);
        return -1;
    }

    if ((slavepointer = ptsname(mfd)) == NULL) {
        close(mfd);
        return -1;
    }

    char sname[strlen(slavepointer)];
    strcpy(sname, slavepointer);

    if ((slavepid = fork()) == -1) {
        return -1;
    }

    // parent
    if (slavepid != 0) {
        *pid = slavepid;
        return mfd;
    }
   
    // child will never return from this function
    int sfd;
    close(mfd); // child has no need for this
    close(connect_fd);

    if (setsid() == -1) {
        exit(EXIT_FAILURE);
    }

    if ((sfd = open(sname, O_RDWR)) == -1) {
        exit(EXIT_FAILURE);
    }

    // redirect stdin/stdout/stderr to the pty slave 
    for (int i = 0; i < 3; i++) {
        if (dup2(sfd, i) == -1) {
            DTRACE("%d: %s\n", getpid(), strerror(errno));
            exit(EXIT_FAILURE);
        }
    } 

    close(sfd); // no need for this anymore

    // exec into bash
    execlp("bash", "bash", "--noediting", "-i", NULL);

    // should never reach here, just for gcc
    exit(EXIT_FAILURE); 
}

// continuously read from fromfd and write to tofd
void write_loop(int fromfd, int tofd) {
    char buff[4096];
    int nread;

    while ((nread = read(fromfd, buff, 4096)) > 0) {
        buff[nread] = '\0';
        if (write(tofd, buff, strlen(buff)) == -1) {
            return;
        }
    }
    return;
}

void sigchld_handler(int signum) {
    DTRACE("%d: SIGCHLD handler fired\n", getpid());
    kill(spid, SIGTERM);
    kill(wpid, SIGTERM);

    // both children are now dead, so collect them
    while (waitpid(-1, NULL, WNOHANG) > 0);

    DTRACE("%d: Processes %d and %d have been terminated and collected\n", getpid(), spid, wpid);
    DTRACE("%d: Terminating self\n", getpid());
    exit(EXIT_SUCCESS);
}
