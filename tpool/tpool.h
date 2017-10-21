// TODO: struct definition
// TODO: remove print_queue
//
int tpool_init(void (*process_task)(int));
int tpool_add_task(int newtask);


void print_queue();
