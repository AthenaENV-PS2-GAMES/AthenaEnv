/* Host stub of src/modules/mutex/include/athena/mutex.h (same signatures). */
#pragma once
typedef struct AthenaMutex AthenaMutex;
AthenaMutex *athena_mutex_core_create(void);
int athena_mutex_core_lock(AthenaMutex *mutex);
int athena_mutex_core_unlock(AthenaMutex *mutex);
int athena_mutex_core_destroy(AthenaMutex *mutex);
