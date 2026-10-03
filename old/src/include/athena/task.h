#ifndef ATHENA_TASK_H
#define ATHENA_TASK_H

int athena_thread_create(const char *title, void *func, int stack_size, int priority);
void athena_thread_start(int id, void *args);
void athena_thread_kill(int id);
void athena_thread_free(int id);
void athena_thread_exit(void);

#endif /* ATHENA_TASK_H */
