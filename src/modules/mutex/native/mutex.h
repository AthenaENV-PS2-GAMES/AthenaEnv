#ifndef ATH_NATIVE_MUTEX_H
#define ATH_NATIVE_MUTEX_H

#include <stdbool.h>

typedef struct AthenaMutex AthenaMutex;

AthenaMutex *athena_mutex_core_create(void);
int athena_mutex_core_lock(AthenaMutex *mutex);
int athena_mutex_core_unlock(AthenaMutex *mutex);
int athena_mutex_core_destroy(AthenaMutex *mutex);
void athena_mutex_core_request_destroy(AthenaMutex *mutex);
void athena_mutex_core_finalize_deferred(AthenaMutex *mutex);

#endif /* ATH_NATIVE_MUTEX_H */
