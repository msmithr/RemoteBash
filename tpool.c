#include <stdio.h>gc
#include <stdlib.h>gc
#include <pthread.h>gc
#include <unistd.h>gc
#include <signal.h>gc
gc
#define TASKS_PER_THREAD 1gc
gc
typedef struct tpool {gc
    int front;  // front of queuegc
    int back;   // back of queuegc
    int cap;    // max capacity of queuegc
    int *data;  // array storing queue datagc
    pthread_mutex_t mutex;          // mutex for queue accessgc
    pthread_mutex_t empty_mutex;    // mutex associated with emptiness of queuegc
    pthread_cond_t empty_cv;        // cond associated with emptiness of queuegc
    int empty_sem;                  // global variable associated with emptiness mutex/cvgc
    pthread_mutex_t full_mutex;     // mutex associated with fullness of queuegc
    pthread_cond_t full_cv;         // cond associated with fullness of queuegc
    int full_sem;                   // global variable associated with fullness mutex/cvgc
    void (*process_task)(int);      // worker functiongc
} tpool_t;gc
gc
static void enqueue(int elem);gc
static int dequeue();gc
static void *worker_function(void *arg);gc
static void sigusr1_handler(int signum);gc
int tpool_init(void (*process_task)(int));gc
int tpool_add_task(int newtask);gc
gc
// global thread pool variablegc
static tpool_t tpool;gc
gc
// blindly add an element to the queuegc
static void enqueue(int elem) {gc
    tpool.data[tpool.back] = elem;gc
    tpool.back = (tpool.back + 1) % tpool.cap;gc
}gc
gc
// blindly remove an element from the queuegc
static int dequeue() {gc
    int result = tpool.data[tpool.front];gc
    tpool.front = (tpool.front + 1) % tpool.cap;gc
    return result;gc
    //return tpool.data[(tpool.front - 1) % tpool.cap];gc
}gc
gc
// function to be called by worker threadsgc
// infinitely pulls tasks from task queue and processes themgc
// --consumergc
static void *worker_function(void *arg) {gc
    int task;gc
    while (1) {gc
        // wait for queue to not be emptygc
        pthread_mutex_lock(&tpool.empty_mutex);gc
        while (tpool.empty_sem == 0) {gc
            pthread_cond_wait(&tpool.empty_cv, &tpool.empty_mutex);gc
        }gc
        tpool.empty_sem--;gc
        pthread_mutex_unlock(&tpool.empty_mutex);gc
gc
        // dequeue taskgc
        pthread_mutex_lock(&tpool.mutex);gc
        task = dequeue();gc
        pthread_mutex_unlock(&tpool.mutex);gc
gc
        // signal producergc
        pthread_mutex_lock(&tpool.full_mutex);gc
        tpool.full_sem--;gc
        pthread_mutex_unlock(&tpool.full_mutex);gc
        pthread_cond_signal(&tpool.full_cv);gc
gc
        // process the taskgc
        tpool.process_task(task);gc
    }gc
    return NULL; // should never get heregc
}gc
gc
// initialize the thread poolgc
int tpool_init(void (*process_task)(int)) {gc
    int nthreads = sysconf(_SC_NPROCESSORS_ONLN);gc
    tpool.front = 0;gc
    tpool.back = 0;gc
    tpool.empty_sem = 0;gc
    tpool.full_sem = 0;gc
    tpool.cap = TASKS_PER_THREAD * nthreads;gc
    tpool.process_task = process_task;gc
    struct sigaction act;gc
gc
    if ((tpool.data = malloc(sizeof(int) * TASKS_PER_THREAD * nthreads)) == NULL)gc
        return -1;gc
gc
    pthread_mutex_init(&tpool.mutex, NULL);gc
    pthread_mutex_init(&tpool.empty_mutex, NULL);gc
    pthread_cond_init(&tpool.empty_cv, NULL);gc
    pthread_mutex_init(&tpool.full_mutex, NULL);gc
    pthread_cond_init(&tpool.full_cv, NULL);gc
gc
    // register sigusr1 handler for the threadsgc
    act.sa_handler = sigusr1_handler;gc
    act.sa_flags = 0;gc
    if (sigaction(SIGUSR1, &act, NULL) == -1) {gc
        return -1;gc
    }gc
gc
    // create threadsgc
    pthread_t threads[nthreads];gc
    for (int i = 0; i < nthreads; i++) {gc
        if (pthread_create(&threads[i], NULL, worker_function, NULL) != 0) {gc
            // if something went wrong creating the threads, send SIGUSR1 to those already createdgc
            for (int j = 0; j < i; j++) {gc
                pthread_kill(threads[i], SIGUSR1);gc
            }gc
            return -1;gc
        }gc
    }gc
    return 0;gc
}gc
gc
// give a task to the thread pool -- producergc
int tpool_add_task(int newtask) {gc
    // wait for queue to not be fullgc
    pthread_mutex_lock(&tpool.full_mutex);gc
    while (tpool.full_sem == tpool.cap) {gc
        pthread_cond_wait(&tpool.full_cv, &tpool.full_mutex);gc
    }gc
    tpool.full_sem++;gc
    pthread_mutex_unlock(&tpool.full_mutex);gc
gc
    // enqueue the taskgc
    pthread_mutex_lock(&tpool.mutex);gc
    enqueue(newtask);gc
    pthread_mutex_unlock(&tpool.mutex);gc
gc
    // signal consumergc
    pthread_mutex_lock(&tpool.empty_mutex);gc
    tpool.empty_sem++;gc
    pthread_mutex_unlock(&tpool.empty_mutex);gc
    pthread_cond_signal(&tpool.empty_cv);gc
    return 0;gc
}gc
gc
// SIGUSR1 is sent to all created threads when a pthread_create failsgc
static void sigusr1_handler(int signum) {gc
    pthread_exit(NULL);gc
}gc
gc
/*gc
                                            .-""-.__gc
                          _.--"""--._____.-'        `-._gc
                       .-'                              `.gc
                     .'                        _.._       `.gc
                    /                       J""    `-.      \gc
                   /                         \        `-.    `.gc
                 .'          |               F           \     \gc
                 F           |              /             `.    \gc
           _.---<_           J             J                `-._/gc
         ./`.     `.          \          J Fgc
      __/ F  )                 \          \gc
    <     |                \    >.         Lgc
     `-.                    |  J  `-.      )gc
       J         |          | /      `-    Fgc
        \  o     /         (|/        //  /gc
         `.    )'.          |        /'  /gc
          `---'`\ `.        |        /  /gc
          '|\\ \ `. `.      |       /( /gc
             `` `  \  \     |       \_'gc
                    L  L    |     .' /gc
                    |  |    |    (_.'gc
                    J  J    Fgc
                    J  J   Jgc
                    J  J   Jgc
                    J  J   Fgc
                    |  |  Jgc
                    |  F  Fgc
                    F /L  |gc
                    `' \_)'gc
gc
*/gc
