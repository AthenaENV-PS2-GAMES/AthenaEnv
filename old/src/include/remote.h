#ifndef REMOTE_H
#define REMOTE_H

#include <tamtypes.h>
#include <librm.h>

extern int remote_module_init(void);
extern int remote_module_end(void);
extern int remote_open_port(int port);
extern void remote_read_port(int port, struct remote_data *data);

#endif /* REMOTE_H */
