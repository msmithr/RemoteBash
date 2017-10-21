#include <stdio.h>
#include "tpool.h"

void process_task(int task);

int main(int argc, char *argv[]) {
    tpool_init(process_task);

    return 0;
}

void process_task(int task) {
//    printf("Task: %d", task);
    return;
}
