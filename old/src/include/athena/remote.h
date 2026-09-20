#ifndef ATHENA_REMOTE_H
#define ATHENA_REMOTE_H

#ifdef ATHENA_REMOTE

#include <stdbool.h>
#include <stdint.h>

#include <librm.h>

typedef struct AthenaRemote {
    int port;
    uint32_t status;
    uint32_t button;
    uint32_t old_button;
} AthenaRemote;

int athena_remote_open(void);
int athena_remote_close(void);

AthenaRemote *athena_remote_get(int port);
void athena_remote_free(AthenaRemote *remote);
void athena_remote_update(AthenaRemote *remote);
bool athena_remote_pressed(const AthenaRemote *remote, uint32_t button);
bool athena_remote_just_pressed(const AthenaRemote *remote, uint32_t button);
bool athena_remote_released(const AthenaRemote *remote);
bool athena_remote_is_active(int port);

#endif /* ATHENA_REMOTE */

#endif /* ATHENA_REMOTE_H */
