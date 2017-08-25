// rembashd

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <signal.h>

#define PORT 7656
#define SECRET "testing123"

// creates a server socket
// returns socket file descriptor or -1 on failure
int setup_socket();

void handle_client(int connect_fd);

int main(int argc, char *argv[]) {
    int sockfd = setup_socket();
    int connect_fd;

    if (sockfd == -1) {
        fprintf(stderr, "rembashd: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // children auto collected
    signal(SIGCHLD, SIG_IGN);

    while(1) {
        connect_fd = accept(sockfd, (struct sockaddr *) NULL, NULL);
        
        if (connect_fd == -1) {
            fprintf(stderr, "rembashd: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }

        switch (fork()) {
            case -1:
                fprintf(stderr, "rembashd: %s\n", strerror(errno));
                exit(EXIT_FAILURE);

            case 0: // in child
                handle_client(connect_fd);
                exit(EXIT_SUCCESS);

        } // end switch/case

        // in parent, close connection
        close(connect_fd);

    } // end while

} // end main()


void handle_client(int connect_fd) {
    /* initialize variables */
    const char *rembash = "<rembash>\n";
    const char *error = "<error>\n";
    const char *ok = "<ok>\n";
    char secret[512];
    char buff[512];
    int nread;
    
    sprintf(secret, "<%s>\n", SECRET);
    
    /* perform protocol */

    // write <rembash>\n
    if (write(connect_fd, rembash, strlen(rembash)) == -1) {
        fprintf(stderr, "remcpd: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // read <SECRET>\n 
    if ((nread = read(connect_fd, buff, 512)) == -1) {
        fprintf(stderr, "remcpd: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    buff[nread] = '\0';
   
    if (strcmp(buff, secret) != 0) {
        // write <error>\n
        if (write(connect_fd, error, strlen(error)) == -1) {
            fprintf(stderr, "remcpd: %s", strerror(errno));
        }
        exit(EXIT_FAILURE);
    }

    // write <ok>\n
    if (write(connect_fd, ok, strlen(ok)) == -1) {
        fprintf(stderr, "remcpd: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // redirections
    dup2(connect_fd, 0);
    dup2(connect_fd, 1);
    dup2(connect_fd, 2);

    // exec bash
    execlp("bash", "bash", "--noediting", "-i", NULL);
}

int setup_socket() {

    int sockfd;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        return -1;
    }
   
    // immediate reuse of port for testing
    int i = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i));

    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(PORT);
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if ((bind(sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr))) == -1) {
        return -1;
    }
    
    if ((listen(sockfd, 5)) == -1) {
        return -1;
    }
    
    return sockfd;
}
