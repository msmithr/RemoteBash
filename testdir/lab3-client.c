// CS407 Lab 03
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
#include <termios.h>
#include "DTRACE.h"

#define PORT 4070
#define SECRET "cs407rembash"

// must be global so signal handler can reach it
struct termios saved_termset;

// function prototypes
void write_loop(int fromfd, int tofd);
int connect_server(char *ip, int port);
int protocol(int sockfd);
void sigchld_handler(int signum);
int setup(char *ip, int port);

int main(int argc, char *argv[]) {

    char *ip = argv[1];
    int sockfd;
    pid_t pid;
   
    
    DTRACE("%d: Client starting: PID=%d, PPID=%d, PGID=%d, SID=%d\n", getpid(), getpid(), getppid(), getpgrp(), getsid(0));

    /* handle arguments */
    if (argc != 2) {
        fprintf(stderr, "rembash: usage: ./rembash <ip address>\n");
        exit(EXIT_FAILURE);
    }

    if ((sockfd = setup(ip, PORT)) == -1) {
        fprintf(stderr, "rembash: %s\n", strerror(errno));
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

    tcsetattr(0, TCSAFLUSH, &saved_termset);
    exit(EXIT_SUCCESS);

} // end main()

// generic setup for client
// returns the connection fd, or -1 on failure
int setup(char *ip, int port) {
    struct sigaction act;
    struct termios termset;
    int sockfd;

    // ignore sigpipe
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        return -1;
    }   

    // connect to server
    if ((sockfd = connect_server(ip, PORT)) == -1) {
        return -1;
    }    

    // perform protocol
    if (protocol(sockfd) == -1) {
        return -1;
    }

    // set pty to noncanonical mode
    if (tcgetattr(0, &termset) == -1) {
        return -1;
    }

    saved_termset = termset; // save tty settings
    termset.c_lflag &= ~ICANON;
    termset.c_lflag &= ~ECHO;
    termset.c_cc[VMIN] = 1;
    termset.c_cc[VTIME] = 0;
    if (tcsetattr(0, TCSAFLUSH, &termset) == -1) {
        return -1;
    }

    // register signal handler
    act.sa_handler = sigchld_handler;
    if (sigaction(SIGCHLD, &act, NULL) == -1) {
        return -1;
    }

    return sockfd;
} // end setup()

// performs client end of the rembash protocol
// returns 0, or -1 on failure
int protocol(int sockfd) {

    /* perform protocol */
    char buff[4096];
    int nread;

    const char * const rembash = "<rembash>\n";
    const char * const ok = "<ok>\n";
    const char * const error = "<error>\n";
    const char * const secret = "<" SECRET ">\n";

    // read <rembash>\n
    if ((nread = read(sockfd, buff, 4096)) == -1) {
        DTRACE("%d: %s\n", getpid(), strerror(errno));
        return -1;
    }
    buff[nread] = '\0';
    DTRACE("%d: Read %s", getpid(), buff);

    if (strcmp(buff, rembash) != 0) {
        DTRACE("%d: invalid protocol from server\n", getpid());
        return -1;
    }

    // write <SECRET>\n
    if (write(sockfd, secret, strlen(secret)) == -1) {
        DTRACE("%d: %s\n", getpid(), strerror(errno));
        return -1;
    }
    DTRACE("%d: Wrote %s", getpid(), secret);

    // read <ok>\n or <error>\n
    if ((nread = read(sockfd, buff, 4096)) == -1) {
        DTRACE("%d: %s\n", getpid(), strerror(errno));
        return -1;
    }
    buff[nread] = '\0';
    DTRACE("%d: Read %s", getpid(), buff);

    if (strcmp(buff, error) == 0) {
        DTRACE("%d: Invalid secret\n", getpid());
        return -1;
    }

    if (strcmp(buff, ok) != 0) {
        DTRACE("%d: Invalid protocol from server\n", getpid());
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
        DTRACE("%d: %s\n", getpid(), strerror(errno));
        return -1;
    }
    
    // set up sockaddr_in struct
    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    if (inet_aton(ip, &servaddr.sin_addr) == 0) {
        DTRACE("%d: invalid ip address: %s\n", getpid(), ip);
        return -1;
    }
    
    // connect
    if (connect(sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) == -1) {
        DTRACE("%d: %s\n", getpid(), strerror(errno));
        return -1;    
    }

    return sockfd;
} // end connect_server()

// sigchld handler, fires whenever child process dies
void sigchld_handler(int signum) {
    int status;

    DTRACE("%d: SIGCHLD handler fired, child process has terminated\n", getpid());

    wait(&status); // wait for child

    // restore tty attributes
    if (tcsetattr(0, TCSAFLUSH, &saved_termset) == -1) {
        fprintf(stderr, "%s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    // if the child process failed, exit failure
    if (!WIFEXITED(status) || WEXITSTATUS(status) != EXIT_SUCCESS) {
        _exit(EXIT_FAILURE);
    }

    // terminate
    _exit(EXIT_SUCCESS);
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
//              (      )  _(^)^^))  )  )\^^^^^))^*+/    /       /
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
