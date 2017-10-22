#include <stdio.h>
#include "tpool.h"

void process_task(int task);

int main(int argc, char *argv[]) {
    tpool_init(process_task);

    while (1) {
        for (int i = 0; i < 50; i++) {
            tpool_add_task(i);
        }
    }
    return 0;
}

void process_task(int task) {
    printf("Task: %d\n", task);
    return;
}
