#include <athena/task.h>
#include <taskman.h>

int athena_thread_create(const char *title, void *func, int stack_size, int priority)
{
    return create_task(title, func, stack_size, priority);
}

void athena_thread_start(int id, void *args)
{
    init_task(id, args);
}

void athena_thread_kill(int id)
{
    kill_task(id);
}

void athena_thread_free(int id)
{
    free_task(id);
}

void athena_thread_exit(void)
{
    exit_task();
}
