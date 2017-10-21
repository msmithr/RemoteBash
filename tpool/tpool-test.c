#include <stdio.h>
#include "tpool.h"

void process_task(int task);

int main(int argc, char *argv[]) {
    tpool_init(process_task);
    print_queue();

    return 0;
}

void process_task(int task) {
//    printf("Task: %d", task);
    return;
}
