#include <pthread.h>

int tpool_init(void (*process_task)(int));
int tpool_add_task(int newtask);
