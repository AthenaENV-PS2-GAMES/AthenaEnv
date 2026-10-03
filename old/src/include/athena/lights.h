#ifndef ATHENA_LIGHTS_H
#define ATHENA_LIGHTS_H

#include <render.h>

int athena_lights_new(void);
void athena_lights_set(int id, int attr, float x, float y, float z);

#endif /* ATHENA_LIGHTS_H */
