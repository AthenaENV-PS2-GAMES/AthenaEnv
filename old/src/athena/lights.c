#include <athena/lights.h>

int athena_lights_new(void)
{
    return NewLight();
}

void athena_lights_set(int id, int attr, float x, float y, float z)
{
    SetLightAttribute(id, x, y, z, attr);
}
