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

#include "dtrace.h"

#define PORT 4070
#define SECRET "<cs407rembash>\n"

int main(int argc, char *argv[]) {

    dtrace("Client started: PID: %d, PPID: %d\n", getpid(), getppid());

    /* handle arguments */
    if (argc != 2) {
        fprintf(stderr, "usage: ./rembash <ip address>\n");
        exit(EXIT_FAILURE);
    }

    char *ip = argv[1];

    /* connect to the server */
    int sockfd;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        fprintf(stderr, "rembash: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(PORT);
    if (inet_aton(ip, &servaddr.sin_addr) == 0) {
        fprintf(stderr, "rembash: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (connect(sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) == -1) {
        exit(EXIT_FAILURE);
        fprintf(stderr, "rembash: %s", strerror(errno));
    }

    dtrace("Connected to server; fd: %d\n", sockfd);

    /* perform protocol */
    char buff[4096];

    int nread;

    // read <rembash>\n
    if ((nread = read(sockfd, buff, 4096)) == -1) {
        fprintf(stderr, "rembash: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    buff[nread] = '\0';

    dtrace("Read %.*s\\n from server\n", (int) strlen(buff)-1, buff);

    if (strcmp(buff, "<rembash>\n") != 0) {
        fprintf(stderr, "rembash: Invalid protocol from server\n");
        exit(EXIT_FAILURE);
    }

    // write <SECRET>\n
    if (write(sockfd, SECRET, strlen(SECRET)) == -1) {
        fprintf(stderr, "rembash: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    dtrace("Wrote %.*s\\n to server\n", (int) strlen(SECRET)-1, SECRET);

    // read <ok>\n or <error>\n
    if ((nread = read(sockfd, buff, 4096)) == -1) {
        fprintf(stderr, "rembash: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    buff[nread] = '\0';

    dtrace("Read %.*s\\n from server\n", (int) strlen(buff)-1, buff);

    if (strcmp(buff, "<error>\n") == 0) {
        fprintf(stderr, "rembash: Invalid secret\n");
        exit(EXIT_FAILURE);
    }

    if (strcmp(buff, "<ok>\n") != 0) {
        fprintf(stderr, "rembash: Invalid protocol from server\n");
        exit(EXIT_FAILURE);
    }

    /* fork off a subprocess to infinitely loop,
     * read lines from stdin and write them to the socket */
    char input[512];
    pid_t pid;

    switch (pid = fork()) {
        case -1:
            fprintf(stderr, "rembash: %s", strerror(errno));
            exit(EXIT_FAILURE);
            
        case 0: // in child process
            dtrace("Subprocess started: PID: %d, PPID: %d\n", getpid(), getppid());
            while(1) {
                fgets(input, 512, stdin);

                if (write(sockfd, input, strlen(input)) == -1) {
                    fprintf(stderr, "rembash: %s", strerror(errno));
                    kill(getppid(), 15); // SIGTERM to parent
                    exit(EXIT_FAILURE);
                } // end if

            } // end while

    } // end switch/case

    /* infinitely loop, reading lines from socket and writing
     * until EOF */
    while ((nread = read(sockfd, buff, 4096)) != 0) {
        buff[nread] = '\0';
        printf("%s", buff);
        fflush(stdout);
    } // end while
    
    // kill and collect the subprocess
    kill(pid, 15); // SIGTERM to subprocess
    wait(NULL); 

    exit(EXIT_SUCCESS);

} // end main()

