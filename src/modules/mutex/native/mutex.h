#ifndef ATH_NATIVE_MUTEX_H
#define ATH_NATIVE_MUTEX_H

#include <stdbool.h>

typedef struct AthenaMutex AthenaMutex;

AthenaMutex *athena_mutex_core_create(void);
int athena_mutex_core_lock(AthenaMutex *mutex);
int athena_mutex_core_unlock(AthenaMutex *mutex);
void athena_mutex_core_destroy(AthenaMutex *mutex);

#endif /* ATH_NATIVE_MUTEX_H */
