#ifndef ATHENA_MUTEX_H
#define ATHENA_MUTEX_H

typedef struct AthenaMutex {
    int id;
} AthenaMutex;

AthenaMutex *athena_mutex_create(void);
void athena_mutex_destroy(AthenaMutex *mutex);
void athena_mutex_lock(AthenaMutex *mutex);
void athena_mutex_unlock(AthenaMutex *mutex);

#endif /* ATHENA_MUTEX_H */
