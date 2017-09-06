// CS 407 Lab 01
//
// Client/server application allowing user to run bash
// commands on a remote machine, similar to SSH or TELNET
//
// Server usage: ./rembashd
//
// author: Michael Smith


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <signal.h>

#include "dtrace.h"

#define PORT 4070
#define SECRET "<cs407rembash>\n"

void handle_client(int connect_fd);

int main(int argc, char *argv[]) {

    DTRACE("Server started: PID: %d, PPID: %d\n", getpid(), getppid());    

    int sockfd;
    int connect_fd;

    /* set up the socket */
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        fprintf(stderr, "rembashd: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
   
    // immediate reuse of port for testing
    int i = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i));

    // set up sockaddr_in struct
    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(PORT);
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    // bind the socket
    if ((bind(sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr))) == -1) {
        fprintf(stderr, "rembashd: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
   
    // listen
    if ((listen(sockfd, 5)) == -1) {
        fprintf(stderr, "rembashd: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    // children auto collected
    signal(SIGCHLD, SIG_IGN);

    /* infinite loop accepting connections */
    while(1) {
        connect_fd = accept(sockfd, (struct sockaddr *) NULL, NULL);
        
        if (connect_fd == -1) {
            DTRACE(stderr, "rembashd: %s\n", strerror(errno));
            continue;
        }

        switch (fork()) {
            case -1: // error
                DTRACE(stderr, "rembashd: %s\n", strerror(errno));
                break;

            case 0: // in child
                close(sockfd); // close server socket in child
                handle_client(connect_fd);
                exit(EXIT_SUCCESS);
            
            default: // in parent
                close(connect_fd);
                break;

        } // end switch/case

    } // end while

} // end main()


void handle_client(int connect_fd) {

    /* initialize variables */
    const char *rembash = "<rembash>\n";
    const char *error = "<error>\n";
    const char *ok = "<ok>\n";
    
    /* perform protocol */
    char buff[4096];
    int nread;

    // write <rembash>\n
    if (write(connect_fd, rembash, strlen(rembash)) == -1) {
        fprintf(stderr, "remcpd: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // read <SECRET>\n 
    if ((nread = read(connect_fd, buff, 4096)) == -1) {
        fprintf(stderr, "remcpd: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    buff[nread] = '\0';

    if (strcmp(buff, SECRET) != 0) {
        // write <error>\n
        if (write(connect_fd, error, strlen(error)) == -1) {
            fprintf(stderr, "remcpd: %s\n", strerror(errno));
        }
        exit(EXIT_FAILURE);
    }

    // write <ok>\n
    if (write(connect_fd, ok, strlen(ok)) == -1) {
        fprintf(stderr, "remcpd: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
   
    // need a new session for concurrency
    setsid();

    // redirections
    dup2(connect_fd, 0);
    dup2(connect_fd, 1);
    dup2(connect_fd, 2);
    
    close(connect_fd); // no need to keep this fd after dup

    // exec bash
    execlp("bash", "bash", "--noediting", "-i", NULL);
}

