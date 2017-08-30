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

#include "readline.c"

#define PORT 4070
#define SECRET "cs407rembash"

int main(int argc, char *argv[]) {

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

    /* initialize variables */
    char secret[strlen(SECRET) + 4];
    sprintf(secret, "<%s>\n", SECRET);
    
    /* perform protocol */
    char *line;

    // read <rembash>\n
    if ((line = readline(sockfd)) == NULL) {
        fprintf(stderr, "rembash: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (strcmp(line, "<rembash>") != 0) {
        fprintf(stderr, "rembash: Invalid protocol from server\n");
        exit(EXIT_FAILURE);
    }

    // write <SECRET>\n
    if (write(sockfd, secret, strlen(secret)) == -1) {
        fprintf(stderr, "rembash: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // read <ok>\n or <error>\n
    if ((line = readline(sockfd)) == NULL) {
        fprintf(stderr, "rembash: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (strcmp(line, "<error>") == 0) {
        fprintf(stderr, "rembash: Invalid secret\n");
        exit(EXIT_FAILURE);
    }

    if (strcmp(line, "<ok>") != 0) {
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
            
        case 0:
            while(1) {
                fgets(input, 512, stdin);

                if (write(sockfd, input, strlen(input)) == -1) {
                    fprintf(stderr, "rembash: %s", strerror(errno));
                    kill(getppid(), 15); // SIGTERM to parent
                    exit(EXIT_FAILURE);
                }

            }
    }

    /* infinitely loop, reading lines from socket and writing
     * until EOF */
    char buff[4096];
    int nread;
    while ((nread = read(sockfd, buff, 4096)) != 0) {
        buff[nread] = '\0';
        printf("%s", buff);
        fflush(stdout);
    }
    
    // kill and collect the subprocess
    kill(pid, 15); // SIGTERM to subprocess
    wait(NULL); 

    exit(EXIT_SUCCESS);

} // end main()

