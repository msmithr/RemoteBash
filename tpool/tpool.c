#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#define TASKS_PER_THREAD 5 // initial queue size (?)

typedef struct tpool {
    int front;  // front of queue
    int back;   // back of queue
    int cap;    // current max capacity of queue
    int size;   // current size of queue --modified by producer/consumers, not en/dequeue
    int *data;  // array storing queue data
    pthread_mutex_t mutex;          // mutex for queue access
    pthread_mutex_t empty_mutex;    // mutex associated with emptiness of queue
    pthread_cond_t empty_cv;        // cond associated with emptiness of queue
    void (*process_task)(int);      // worker function 
} tpool_t;

static void enqueue(int elem);
static int dequeue();
static void queue_expand();
static void print_queue();
static void *worker_function(void *arg);
int tpool_init(void (*process_task)(int));
int tpool_add_task(int newtask);

tpool_t tpool;

static void enqueue(int elem) {
    if (tpool.size == tpool.cap) {
        queue_expand();
    }

    tpool.data[tpool.back] = elem;
    tpool.back = (tpool.back + 1) % tpool.cap;
}

static int dequeue() {
    tpool.front = (tpool.front + 1) % tpool.cap;
    return tpool.data[(tpool.front - 1) % tpool.cap];
}

static void queue_expand() {
    int *new_array = malloc(sizeof(int) * tpool.size * 2);
    int i = 0;
    for (int j = 0; j < tpool.size; j++) {
        new_array[i] = tpool.data[(j + tpool.front) % tpool.cap];
        i++;
    }

    tpool.front = 0;
    tpool.back = tpool.size;

    tpool.cap *= 2;
    free(tpool.data);
    tpool.data = new_array;
}

static void print_queue() {
    printf("Data: ");
    for (int i = 0; i < tpool.cap; i++) {
        printf("%d ", tpool.data[i]);
    }
    printf("\n");
    printf("Queue: ");
    
    for (int i = 0; i < tpool.size; i++) {
        printf("%d ", tpool.data[(i + tpool.front) % tpool.cap]);
    }
    printf("\n");
}

// function to be called by worker threads
// infinitely pulls tasks from task queue and processes them
// --consumer
static void *worker_function(void *arg) {
    int task;
    while (1) {
        pthread_mutex_lock(&tpool.empty_mutex);
        while (tpool.size == 0) {
            pthread_cond_wait(&tpool.empty_cv, &tpool.empty_mutex);
        }
        tpool.size--;
        pthread_mutex_unlock(&tpool.empty_mutex);

        pthread_mutex_lock(&tpool.mutex);
        task = dequeue();
        pthread_mutex_unlock(&tpool.mutex);

        tpool.process_task(task);
    }
    return NULL;
}

// initialize the thread pool
int tpool_init(void (*process_task)(int)) {
    tpool.front = 0;
    tpool.back = 0;
    tpool.cap = TASKS_PER_THREAD;
    tpool.size = 0;
    tpool.process_task = process_task;

    if ((tpool.data = malloc(sizeof(int) * TASKS_PER_THREAD)) == NULL)
        return -1;

    pthread_mutex_init(&tpool.mutex, NULL);
    pthread_mutex_init(&tpool.empty_mutex, NULL);
    pthread_cond_init(&tpool.empty_cv, NULL);

    // create threads
    pthread_t tid;
    for (int i = 0; i < sysconf(_SC_NPROCESSORS_ONLN); i++) {
        pthread_create(&tid, NULL, worker_function, NULL);
    }
    return 0;
}

// give a task to the thread pool -- producer
int tpool_add_task(int newtask) {
    pthread_mutex_lock(&tpool.mutex);

    // add task to queue
    if (tpool.size == tpool.cap) {
        queue_expand();
    }
    enqueue(newtask);

    pthread_mutex_unlock(&tpool.mutex);

    // signal consumer that condition var has changed
    pthread_mutex_lock(&tpool.empty_mutex);
    tpool.size++;
    pthread_mutex_unlock(&tpool.empty_mutex);
    pthread_cond_signal(&tpool.empty_cv);
    return 0;
}
