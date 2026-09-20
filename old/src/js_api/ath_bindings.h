#ifndef ATH_BINDINGS_H
#define ATH_BINDINGS_H

#include "../quickjs/quickjs-libc.h"
#include <render.h>

#ifdef ATHENA_ODE
#include <ode/ode.h>
#endif

#define ATHENA_PROP_INT32(item) JS_PROP_INT32_DEF(stringify(item), item, JS_PROP_CONFIGURABLE)

#ifdef ATHENA_AUDIO
#include <athena/sound.h>

typedef struct {
    AthenaSound *sound;
} JSSoundStream;

typedef struct {
    AthenaSfx *sfx;
} JSSoundEffect;
#endif

#ifdef ATHENA_GRAPHICS
#include <shadows.h>
#include <fntsys.h>
#include <athena/shadows_facade.h>
#include <athena/render_facade.h>

typedef struct {
    AthenaRenderObject *native;
} JSRenderObject;

typedef struct {
    AthenaShadowProjector *proj;
} JSShadowProjector;

JSClassID get_img_class_id(void);
JSClassID get_imglist_class_id(void);
extern JSClassID js_render_object_class_id;
#endif

#ifdef ATHENA_ODE
#include <athena/ode_facade.h>

typedef struct {
    AthenaOdeWorld *native;
} JSWorld;

typedef struct {
    AthenaOdeSpace *native;
} JSSpace;

typedef struct {
    dGeomID geom;
    dSpaceID parent_space;
} JSGeom;

typedef struct {
    AthenaOdeBody *native;
} JSBody;

typedef struct {
    dJointID joint;
    dWorldID parent_world;
} JSJoint;

typedef struct {
    dJointGroupID group;
} JSJointGroup;

extern JSClassID js_geom_class_id;
extern JSClassID js_body_class_id;
extern JSClassID js_space_class_id;
#endif

JSClassID get_matrix4_class_id(void);
JSClassID get_vector2_class_id(void);
JSClassID get_vector3_class_id(void);
JSClassID get_vector4_class_id(void);

#define countof(x) (sizeof(x) / sizeof((x)[0]))

#endif /* ATH_BINDINGS_H */
