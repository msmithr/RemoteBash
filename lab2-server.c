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
#include "DTRACE.h"
#define PORT 4070
#define SECRET "<cs407rembash>\n"

// function to be called be the child processes
// handles each client given the file descriptor 
// performs protocol, redirects stdin, stdout, and stderr, and exec's into bash
void handle_client(int connect_fd);

// function to create and set up a tcp server socket
// returns the socket file descriptor,
// or -1 on failure
int setup_server_socket(int port);

// performs the server end of the rembash protocol
// returns 0, or -1 on failure
int protocol(int connect_fd);

// creates and sets up the psuedoterminal master/slave
// returns master fd, and stores subprocess pid in pointer
// must be given the socket fd to close it in subprocess
// returns -1 on failure
int setuppty(pid_t *pid, int connect_fd);

// continuously read from fromfd and write to tofd
void write_loop(int fromfd, int tofd);

int main(int argc, char *argv[]) {

    DTRACE("DEBUG: Server staring: PID=%d, PPID=%d, PGID=%d, SID=%d\n", getpid(), getppid(), getpgrp(), getsid(0));

    int connect_fd, sockfd;

    if ((sockfd = setup_server_socket(PORT)) == -1) {
        fprintf(stderr, "rembashd: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // children auto collected
    signal(SIGCHLD, SIG_IGN);

    /* infinite loop accepting connections */
    while(1) {
        connect_fd = accept(sockfd, (struct sockaddr *) NULL, NULL);

        if (connect_fd == -1) {
            continue; // if accept failed, continue to next iteration
        }

        switch (fork()) {
        case -1: // error
            fprintf(stderr, "remcpd: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
            break;

        case 0: // in child
            DTRACE("DEBUG: Subprocess for client created: PID=%d, PPID=%d, PGID=%d, SID=%d\n", getpid(), getppid(), getpgrp(), getsid(0));
            close(sockfd); // close server socket in child
            handle_client(connect_fd);
            exit(EXIT_FAILURE);
        
        default: // in parent
            close(connect_fd); // close connection in parent
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

    pid_t spid, wpid;
    int mfd;

    if (protocol(connect_fd) == -1) {
        exit(EXIT_FAILURE);
    }

    mfd = setuppty(&spid, connect_fd); // creates child process, 

    switch (wpid = fork()) {
    case -1:
        exit(EXIT_FAILURE);
    case 0:
        write_loop(connect_fd, mfd);
        close(connect_fd);
        close(mfd);
        kill(getppid(), SIGTERM);
        exit(EXIT_FAILURE);
    default:
        write_loop(mfd, connect_fd);
        close(connect_fd);
        close(mfd);
        kill(wpid, SIGTERM);
        exit(EXIT_FAILURE);
    }

    // connect_fd => mfd
    // mfd => connect_fd
    
    exit(EXIT_SUCCESS);
} // end handle_client()

// performs the server end of the rembash protocol
// returns 0, or -1 on failure
int protocol(int connect_fd) {
    /* initialize variables */
    const char *rembash = "<rembash>\n";
    const char *error = "<error>\n";
    const char *ok = "<ok>\n";

    char buff[4096];
    int nread;

    // write <rembash>\n
    if (write(connect_fd, rembash, strlen(rembash)) == -1) {
        DTRACE("DEBUG: %s\n", strerror(errno));
        return -1;
    }

    // read <SECRET>\n 
    if ((nread = read(connect_fd, buff, 4096)) == -1) {
        DTRACE("DEBUG: %s\n", strerror(errno));
        return -1;
    }
    buff[nread] = '\0';

    if (strcmp(buff, SECRET) != 0) {
        // write <error>\n
        DTRACE("DEBUG: remcpd: invalid secret from client\n"); 
        if (write(connect_fd, error, strlen(error)) == -1) {
            DTRACE("DEBUG: remcpd: %s\n", strerror(errno));
        }
        return -1;
    }

    // write <ok>\n
    if (write(connect_fd, ok, strlen(ok)) == -1) {
        DTRACE("DEBUG: %s\n", strerror(errno));
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
   
    // child
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
            DTRACE("DEBUG: %s\n", strerror(errno));
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
