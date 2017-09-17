// CS407 Lab 01
// 
// Client/server application allowing user to run bash
// commands on a remote machine, similar to SSH or Telnet
//
// Client usage: ./rembash <ip address>
//
// author: Michael Smith

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "DTRACE.h"

#define PORT 4070
#define SECRET "<cs407rembash>\n" // shared secret of the form <SECRET>\n

// continuously read from fromfd and write to tofd
void write_loop(int fromfd, int tofd);

// function to create a connection to a tcp server
// returns the socket file descriptor,
// -1 on most failures,
// and -2 if the given ip address is invalid (because inet_aton doesn't set errno)
int connect_server(char *ip, int port);

// performs client end of the rembash protocol
// returns 0, or -1 on failure
int protocol(int sockfd);

int main(int argc, char *argv[]) {

    char *ip = argv[1];
    int sockfd;
    pid_t pid;
    
    DTRACE("DEBUG: Client staring: PID=%d, PPID=%d, PGID=%d, SID=%d\n", getpid(), getppid(), getpgrp(), getsid(0));

    /* handle arguments */
    if (argc != 2) {
        fprintf(stderr, "usage: ./rembash <ip address>\n");
        exit(EXIT_FAILURE);
    }
    
    // connect to server
    if ((sockfd = connect_server(ip, PORT)) == -1) {
        fprintf(stderr, "rembash: error occured while attempting to connect to server\n");
        exit(EXIT_FAILURE);
    }

    // perform protocol
    if (protocol(sockfd) == -1) {
        fprintf(stderr, "rembash: error during rembash protocol\n");
        exit(EXIT_FAILURE);
    }

    switch (pid = fork()) {
    case -1: // error
        fprintf(stderr, "rembash: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    case 0: // in child process
        write_loop(0, sockfd);
        exit(EXIT_FAILURE);
    } // end switch/case

    write_loop(sockfd, 1); 

    // kill and collect the subprocess
    kill(pid, SIGTERM); // SIGTERM to subprocess
    wait(NULL); 

    exit(EXIT_SUCCESS);

} // end main()

// performs client end of the rembash protocol
// returns 0, or -1 on failure
int protocol(int sockfd) {

    /* perform protocol */
    char buff[4096];
    int nread;

    // read <rembash>\n
    if ((nread = read(sockfd, buff, 4096)) == -1) {
        DTRACE("DEBUG: %s\n", strerror(errno));
        return -1;
    }
    buff[nread] = '\0';

    if (strcmp(buff, "<rembash>\n") != 0) {
        DTRACE("DEBUG: invalid protocol from server\n");
        return -1;
    }

    // write <SECRET>\n
    if (write(sockfd, SECRET, strlen(SECRET)) == -1) {
        DTRACE("DEBUG: %s\n", strerror(errno));
        return -1;
    }

    // read <ok>\n or <error>\n
    if ((nread = read(sockfd, buff, 4096)) == -1) {
        DTRACE("DEBUG: %s\n", strerror(errno));
        return -1;
    }
    buff[nread] = '\0';

    if (strcmp(buff, "<error>\n") == 0) {
        DTRACE("DEBUG: Invalid secret\n");
        return -1;
    }

    if (strcmp(buff, "<ok>\n") != 0) {
        DTRACE("DEBUG: Invalid protocol from server\n");
        return -1;
    }

    return 0;
}

// continuously read from fromfd and write to tofd
void write_loop(int fromfd, int tofd) {
    int nread;
    char buff[4096];
    while ((nread = read(fromfd, buff, 4096)) > 0) {
        buff[nread] = '\0';
        if (write(tofd, buff, strlen(buff)) == -1) {
            return;
        }
    }   
    return;
}

// function to create a connection to a tcp server
// returns the socket file descriptor,
// -1 on most failures,
// and -2 if the given ip address is invalid (because inet_aton doesn't set errno)
int connect_server(char *ip, int port) {
    int sockfd;

    // create socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        DTRACE("DEBUG: %s\n", strerror(errno));
        return -1;
    }
    
    // set up sockaddr_in struct
    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    if (inet_aton(ip, &servaddr.sin_addr) == 0) {
        DTRACE("DEBUG: invalid ip address: %s\n", ip);
        return -1;
    }
    
    // connect
    if (connect(sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) == -1) {
        DTRACE("DEBUG: %s\n", strerror(errno));
        return -1;    
    }

    return sockfd;
} // end connect_server()
