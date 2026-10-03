#ifndef ATHENA_ANIM3D_H
#define ATHENA_ANIM3D_H

#include <stdint.h>

#include <render.h>

typedef struct AthenaAnim3dCollection {
    athena_animation_collection collection;
} AthenaAnim3dCollection;

AthenaAnim3dCollection *athena_anim3d_load(const char *path);
void athena_anim3d_free(AthenaAnim3dCollection *col);
uint32_t athena_anim3d_count(const AthenaAnim3dCollection *col);
athena_animation *athena_anim3d_get(AthenaAnim3dCollection *col, uint32_t index);
athena_animation *athena_anim3d_find(AthenaAnim3dCollection *col, const char *name);

#endif /* ATHENA_ANIM3D_H */
