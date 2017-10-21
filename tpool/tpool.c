#include <stdio.h>
#include <string.h>

#define QUEUE_SIZE 4

typedef struct queue {
    int front;
    int back;
    int data[QUEUE_SIZE];
} queue;

queue jobqueue;

void print_queue();
static int isFull();
static int isEmpty();
static int enqueue(int element);
static int dequeue();

// initialize the job queue
void queue_init() {
    jobqueue.front = 0;
    jobqueue.back = 0;
}

void print_queue() {
    printf("Real Data: ");
    for (int i = 0; i < QUEUE_SIZE; i++) {
        printf("%d ", jobqueue.data[i]);
    }
    printf("\n");

    printf("Queue: ");
    if (isEmpty()) {
        printf("Empty!\n");
    } else {
        for (int i = jobqueue.front; i != jobqueue.back; i=(i+1) % QUEUE_SIZE) {
            printf("%d ", jobqueue.data[i]);
        }
        printf("\n");
    }
    printf("Front: %d\n", jobqueue.front);
    printf("Back: %d\n", jobqueue.back);

}

// enqueue an element into the job queue
// returns 0 on success, -1 on failure
static int enqueue(int elem) {
    if (!isFull()) {
        jobqueue.data[jobqueue.back] = elem;
        jobqueue.back = (jobqueue.back + 1) % QUEUE_SIZE;
        return 0;
    }
    return -1;
}

// dequeue the first element in the queue
// returns the dequeue'd element on success,
// -1 on failure
static int dequeue() {
    if (!isEmpty()) {
        int result = jobqueue.data[jobqueue.front];
        jobqueue.front = (jobqueue.front + 1) % QUEUE_SIZE;
        return result;
    }
    return -1;
}

static int isFull() {
    return jobqueue.front == (jobqueue.back + 1) % QUEUE_SIZE;
}

static int isEmpty() {
    return jobqueue.front == jobqueue.back;
}

int tpool_init(void (*process_task)(int)) {
    queue_init();
    for (int i = 0; i < 1000; i++) {
        enqueue(i);
    }
    print_queue();
    return 0;
}

int tpool_add_task(int newtask) {
    return 0;
}
