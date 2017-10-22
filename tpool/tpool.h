#include <pthread.h>

typedef struct tpool { 
    pthread_mutex_t mutex;
    pthread_cond_t cv; 
    int queue_front;
    int queue_back;
    int queue_size;
    int *queue;
    void (*process_task)(int);
} tpool_t;

int tpool_init(void (*process_task)(int));
int tpool_add_task(int newtask);
