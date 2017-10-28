#include <stdio.h>
#include <unistd.h>
#include <syscall.h>
#include "tpool.h"

void process_task(int task);
int factorial(int n);

int main(int argc, char *argv[]) {
    tpool_init(process_task);
    while (1) {
        for (int i = 0; i < 10; i++) {
            tpool_add_task(i);
        }

        for (int i = 0; i < 10; i++) {
            tpool_add_task(i);
        }
    }

    printf("%d\n", factorial(4));
    sleep(10000);
    return 0;
}

// brute force algorithm for calculating factorial
void process_task(int task) {
    sleep(2);
    for (int i = 0;;i++) {
        if (i == factorial(i)) {
            printf("%ld:  %d! = %d\n", syscall(__NR_gettid), task, factorial(task));
            break;
        }
    }
}

int factorial(int n) {
    if (n == 0 || n == 1) {
        return 1; 
    } else {
        return (n * factorial(n-1));
    }
}
