#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include "tpool.h"

#define INITIAL_SIZE 5

static void print_queue();
static void enqueue();
static int dequeue();
static int is_empty();
static int is_full();
static void expand_queue();

// global thread pool variable
tpool_t tpool;

static void print_queue() {
    printf("Data: ");
    for (int i = 0; i < tpool.queue_size; i++) {
        printf("%d ", tpool.queue[i]);
    }
    printf("\n");


    printf("Queue: ");
    if (is_empty()) {
        printf("Empty!\n");
    } else {
        for (int i = tpool.queue_front; i != (tpool.queue_back + 1) % tpool.queue_size; i = (i + 1) % tpool.queue_size) {
            printf("%d ", tpool.queue[i]);
        }
        if (is_full()) {
            printf("Full!");
        }
        printf("\n");
    }

    printf("Front: %d\n", tpool.queue_front);
    printf("Back: %d\n", tpool.queue_back);
}

static void enqueue(int elem) {
    if (is_full()) {
        expand_queue();
    }
    tpool.queue_back = (tpool.queue_back + 1) % tpool.queue_size;
    tpool.queue[tpool.queue_back] = elem;
}

static int dequeue() {
    int result = tpool.queue[tpool.queue_front];
    tpool.queue_front = (tpool.queue_front + 1) % tpool.queue_size;
    return result;
}

static int is_empty() {
    return (tpool.queue_front == (tpool.queue_back + 1) % tpool.queue_size);
}

static int is_full() {
    return (tpool.queue_front == (tpool.queue_back + 2) % tpool.queue_size);
}

static void expand_queue() {
    int *new_array = malloc(sizeof(int) * tpool.queue_size * 2);
    int i = 0;
    for (int j = tpool.queue_front; j != (tpool.queue_back + 1) % tpool.queue_size; j = (j + 1) % tpool.queue_size) {
        new_array[i] = tpool.queue[j];
        i++;
    }
    tpool.queue_size *= 2;
    free(tpool.queue);
    tpool.queue = new_array;
}


int tpool_init(void (*process_task)(int)) {
    pthread_mutex_init(&tpool.mutex, NULL);
    pthread_cond_init(&tpool.cv, NULL);
    tpool.queue_front = 0;
    tpool.queue_back = INITIAL_SIZE-1;
    tpool.queue_size = INITIAL_SIZE;
    tpool.queue = malloc(sizeof(int) * INITIAL_SIZE);
    tpool.process_task = process_task;
    return 0;
}

int tpool_add_task(int newtask) {
    pthread_mutex_lock(&tpool.mutex);
    enqueue(newtask);
    pthread_mutex_unlock(&tpool.mutex);
    pthread_cond_signal(&tpool.cv);
    print_queue();
    return 0;
}
