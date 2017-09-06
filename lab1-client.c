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

#define PORT 4070
#define SECRET "<cs407rembash>\n" // shared secret of the form <SECRET>\n

// function to be called by the child process
// continuously pulls text from the local terminal and writes it to the fd
void write_loop(int fd);

// function to create a connection to a tcp server
// returns the socket file descriptor,
// -1 on most failures,
// and -2 if the given ip address is invalid (because inet_aton doesn't set errno)
int connect_server(char *ip, int port);


int main(int argc, char *argv[]) {

    /* handle arguments */
    if (argc != 2) {
        fprintf(stderr, "usage: ./rembash <ip address>\n");
        exit(EXIT_FAILURE);
    }

    char *ip = argv[1];

    /* connect to the server */
    int sockfd;

    switch (sockfd = connect_server(ip, PORT)) {
    case -2:
        fprintf(stderr, "rembash: invalid ip address: %s\n", ip);
        exit(EXIT_FAILURE);
    case -1:
        fprintf(stderr, "rembash: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    } // end switch/case

    /* perform protocol */
    char buff[4096];
    int nread;

    // read <rembash>\n
    if ((nread = read(sockfd, buff, 4096)) == -1) {
        fprintf(stderr, "rembash: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    buff[nread] = '\0';

    if (strcmp(buff, "<rembash>\n") != 0) {
        fprintf(stderr, "rembash: Invalid protocol from server\n");
        exit(EXIT_FAILURE);
    }

    // write <SECRET>\n
    if (write(sockfd, SECRET, strlen(SECRET)) == -1) {
        fprintf(stderr, "rembash: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // read <ok>\n or <error>\n
    if ((nread = read(sockfd, buff, 4096)) == -1) {
        fprintf(stderr, "rembash: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    buff[nread] = '\0';

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
    pid_t pid;

    switch (pid = fork()) {
    case -1:
        fprintf(stderr, "rembash: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
        
    case 0: // in child process
        write_loop(sockfd);
        exit(EXIT_FAILURE);

    } // end switch/case

    // infinitely loop, reading lines from socket and writing
    // until EOF 
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


// function to be called by the child process
// continuously pulls text from the local terminal and writes it to the fd
void write_loop(int fd) {
    char input[512];

    while(1) {
        fgets(input, 512, stdin);

        if (write(fd, input, strlen(input)) == -1) {
            fprintf(stderr, "rembash: %s\n", strerror(errno));
            kill(getppid(), 15); // kill the parent
            exit(EXIT_FAILURE);
        } // end if
    } // end while()    

} // end write_loop()

// function to create a connection to a tcp server
// returns the socket file descriptor,
// -1 on most failures,
// and -2 if the given ip address is invalid (because inet_aton doesn't set errno)
int connect_server(char *ip, int port) {
    int sockfd;

    // create socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        return -1;
    }
    
    // set up sockaddr_in struct
    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    if (inet_aton(ip, &servaddr.sin_addr) == 0) {
        return -2;
    }
    
    // connect
    if (connect(sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) == -1) {
        return -1;    
    }

    return sockfd;
}
