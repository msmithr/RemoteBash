#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#define TASKS_PER_THREAD 5

typedef struct tpool {
    int front;  // front of queue
    int back;   // back of queue
    int cap;    // max capacity of queue
    int *data;  // array storing queue data
    pthread_mutex_t mutex;          // mutex for queue access
    pthread_mutex_t empty_mutex;    // mutex associated with emptiness of queue
    pthread_cond_t empty_cv;        // cond associated with emptiness of queue
    int empty_sem;                  // global variable associated with emptiness mutex/cv
    pthread_mutex_t full_mutex;     // mutex associated with fullness of queue
    pthread_cond_t full_cv;         // cond associated with fullness of queue
    int full_sem;                   // global variable associated with fullness mutex/cv
    void (*process_task)(int);      // worker function 
} tpool_t;

static void enqueue(int elem);
static int dequeue();
static void print_queue();
static void *worker_function(void *arg);
int tpool_init(void (*process_task)(int));
int tpool_add_task(int newtask);

// global thread pool variable
static tpool_t tpool;

// blindly add an element to the queue
static void enqueue(int elem) {
    tpool.data[tpool.back] = elem;
    tpool.back = (tpool.back + 1) % tpool.cap;
}

// blindly remove an element from the queue
static int dequeue() {
    tpool.front = (tpool.front + 1) % tpool.cap;
    return tpool.data[(tpool.front - 1) % tpool.cap];
}

static void print_queue() {
    printf("Data: ");
    for (int i = 0; i < tpool.cap; i++) {
        printf("%d ", tpool.data[i]);
    }
    printf("\n");
    printf("Queue: ");
    
    for (int i = 0; i < tpool.empty_sem; i++) {
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
        // wait for queue to not be empty
        pthread_mutex_lock(&tpool.empty_mutex);
        while (tpool.empty_sem == 0) {
            pthread_cond_wait(&tpool.empty_cv, &tpool.empty_mutex);
        }
        tpool.empty_sem--;
        pthread_mutex_unlock(&tpool.empty_mutex);

        // dequeue task
        pthread_mutex_lock(&tpool.mutex);
        task = dequeue();
        pthread_mutex_unlock(&tpool.mutex);

        // signal producer
        pthread_mutex_lock(&tpool.full_mutex);
        tpool.full_sem--;
        pthread_mutex_unlock(&tpool.full_mutex);
        pthread_cond_signal(&tpool.full_cv);

        // process the task
        tpool.process_task(task);
    }
    return NULL; // should never get here
}

// initialize the thread pool
int tpool_init(void (*process_task)(int)) {
    int nthreads = sysconf(_SC_NPROCESSORS_ONLN);
    tpool.front = 0;
    tpool.back = 0;
    tpool.empty_sem = 0;
    tpool.full_sem = 0;
    tpool.cap = TASKS_PER_THREAD * nthreads;
    tpool.process_task = process_task;


    if ((tpool.data = malloc(sizeof(int) * TASKS_PER_THREAD * nthreads)) == NULL)
        return -1;

    pthread_mutex_init(&tpool.mutex, NULL);
    pthread_mutex_init(&tpool.empty_mutex, NULL);
    pthread_cond_init(&tpool.empty_cv, NULL);
    pthread_mutex_init(&tpool.full_mutex, NULL);
    pthread_cond_init(&tpool.full_cv, NULL);

    // create threads
    pthread_t tid;
    for (int i = 0; i < sysconf(_SC_NPROCESSORS_ONLN); i++) {
        pthread_create(&tid, NULL, worker_function, NULL);
    }
    return 0;
}

// give a task to the thread pool -- producer
int tpool_add_task(int newtask) {
    // wait for queue to not be full
    pthread_mutex_lock(&tpool.full_mutex);
    while (tpool.full_sem == tpool.cap) {
        pthread_cond_wait(&tpool.full_cv, &tpool.full_mutex);
    }
    tpool.full_sem++;
    pthread_mutex_unlock(&tpool.full_mutex);

    // enqueue the task
    pthread_mutex_lock(&tpool.mutex);
    enqueue(newtask);
    pthread_mutex_unlock(&tpool.mutex);

    // signal consumer
    pthread_mutex_lock(&tpool.empty_mutex);
    tpool.empty_sem++;
    pthread_mutex_unlock(&tpool.empty_mutex);
    pthread_cond_signal(&tpool.empty_cv);
    return 0;
}
