#include <stdlib.h>

#include <athena/mutex.h>
#include <lockman.h>

AthenaMutex *athena_mutex_create(void)
{
    AthenaMutex *mutex = malloc(sizeof(*mutex));
    if (!mutex)
        return NULL;

    mutex->id = create_mutex();
    return mutex;
}

void athena_mutex_destroy(AthenaMutex *mutex)
{
    if (!mutex)
        return;

    if (mutex->id != -1)
        delete_mutex(mutex->id);

    free(mutex);
}

void athena_mutex_lock(AthenaMutex *mutex)
{
    lock_mutex(mutex->id);
}

void athena_mutex_unlock(AthenaMutex *mutex)
{
    unlock_mutex(mutex->id);
}
