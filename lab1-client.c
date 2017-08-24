#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define PORT 7656
#define SECRET "testing123"

// connects to a server
// returns socket file descriptor or -1 on failure
int make_connection(char *ip);

int main(int argc, char *argv[]) {

    /* handle arguments */
    if (argc != 2) {
        fprintf(stderr, "usage: ./rembash <ip address>\n");
        return EXIT_FAILURE;
    }

    char *ip = argv[1];

    /* connect to the server */
    int sockfd;
    if ((sockfd = make_connection(ip)) == -1) {
        fprintf(stderr, "rembash: %s", strerror(errno));
        return EXIT_FAILURE;
    }

    /* initialize variables */
    char buff[512];
    char secret[512];
    int nread;
    sprintf(secret, "<%s>\n", SECRET);

    /* perform protocol */

    // read <rembash>\n
    if ((nread = read(sockfd, buff, 512)) == -1) {
        fprintf(stderr, "rembash: %s", strerror(errno));
        return EXIT_FAILURE;
    }
    buff[nread] = '\0';

    if (strcmp(buff, "<rembash>\n") != 0) {
        fprintf(stderr, "rembash: Invalid protocol from server");
        return EXIT_FAILURE;
    }

    // write <SECRET>\n
    if (write(sockfd, secret, strlen(secret)) == -1) {
        fprintf(stderr, "rembash: %s", strerror(errno));
        return EXIT_FAILURE;
    }

    // read <ok>\n  or <error>\n
    if ((nread = read(sockfd, buff, 512)) == -1) {
        fprintf(stderr, "rembash: %s", strerror(errno));
        return EXIT_FAILURE;
    }
    buff[nread] = '\0';

    if (strcmp(buff, "<error>\n") == 0) {
        fprintf(stderr, "rembash: Invalid secret");
        return EXIT_FAILURE;
    }

    if (strcmp(buff, "<ok>\n") != 0) {
        fprintf(stderr, "rembash: Invalid protocol from server");
        return EXIT_FAILURE;
    }

    buff[nread] = '\0';
    
}

int make_connection(char *ip) {
    int sockfd;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        return -1;
    }

    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(PORT);
    if (inet_aton(ip, &servaddr.sin_addr) == 0) {
        return -1;
    }

    if (connect(sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) == -1) {
        return -1;
    }

    return sockfd;
}
