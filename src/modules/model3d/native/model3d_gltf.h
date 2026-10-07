#ifndef ATHENA_MODEL3D_GLTF_H
#define ATHENA_MODEL3D_GLTF_H
/* Private: glTF primitive conversion shared with the gltf3d module, which
 * includes cgltf.h from this folder (the implementation is in model3d_load.c). */
#include <athena/model3d.h>
#include "cgltf/cgltf.h"
int athena_model3d_gltf_primitive(const char *path,cgltf_primitive *prim,
    const AthenaMaterial3D *material_override,AthenaMesh3D **out);
#endif
