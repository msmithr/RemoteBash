#include <stdlib.h>
#include <stdio.h>

#define INITIAL_SIZE 5

typedef struct queue {
    int front;
    int back;
    int *data;
    int size;
} queue;

static void queue_init();
static void print_queue();
static void enqueue();
static int dequeue();
static int is_empty();
static int is_full();
static void expand_queue();

queue job_queue;

static void queue_init() {
    job_queue.front = 0;
    job_queue.back = INITIAL_SIZE-1;
    job_queue.data = malloc(sizeof(int) * INITIAL_SIZE);
    job_queue.size = INITIAL_SIZE;
}

static void print_queue() {
    printf("Data: ");
    for (int i = 0; i < job_queue.size; i++) {
        printf("%d ", job_queue.data[i]);
    }
    printf("\n");


    printf("Queue: ");
    if (is_empty()) {
        printf("Empty!\n");
    } else {
        for (int i = job_queue.front; i != (job_queue.back + 1) % job_queue.size; i = (i + 1) % job_queue.size) {
            printf("%d ", job_queue.data[i]);
        }
        if (is_full()) {
            printf("Full!");
        }
        printf("\n");
    }

    printf("Front: %d\n", job_queue.front);
    printf("Back: %d\n", job_queue.back);
}

static void enqueue(int elem) {
    if (is_full()) {
        expand_queue();
    }
    job_queue.back = (job_queue.back + 1) % job_queue.size;
    job_queue.data[job_queue.back] = elem;
}

static int dequeue() {
    int result = job_queue.data[job_queue.front];
    job_queue.front = (job_queue.front + 1) % job_queue.size;
    return result;
}

static int is_empty() {
    return (job_queue.front == (job_queue.back + 1) % job_queue.size);
}

static int is_full() {
    return (job_queue.front == (job_queue.back + 2) % job_queue.size);
}

static void expand_queue() {
    int *new_array = malloc(sizeof(int) * job_queue.size * 2);
    int i = 0;
    for (int j = job_queue.front; j != (job_queue.back + 1) % job_queue.size; j = (j + 1) % job_queue.size) {
        new_array[i] = job_queue.data[j];
        i++;
    }
    job_queue.size *= 2;
    free(job_queue.data);
    job_queue.data = new_array;
}


int tpool_init() {
    queue_init();
    return 0;
}

int tpool_add_task(int newtask) {
    return 0;
}
