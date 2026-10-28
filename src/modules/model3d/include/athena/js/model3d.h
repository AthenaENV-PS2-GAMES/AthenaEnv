#ifndef ATHENA_MODEL3D_JS_H
#define ATHENA_MODEL3D_JS_H
#include <ath_env.h>
#include <athena/model3d.h>
/* Export boundary (Phase 5C.5D). A script Texture may be opted in to canonical compaction
 * (ATHENA_TEXTURE3D_COMPACT_CANONICAL=1): after its first bind its canonical RGBA is released and the legacy, infallible
 * athena_texture3d_view() could not rebuild it under memory pressure. Hence two families of the same bridge:
 *
 *   athena_texture3d_from_value / athena_mesh3d_from_value / athena_instance3d_from_value / athena_model3d_js_material
 *     The compatible, SAFE forms (the historical names). On success the canonical RGBA of the exported texture (for a
 *     mesh or instance: its material texture) is present and pinned until that texture's final release, so view() is
 *     valid, 128-byte aligned and address-stable exactly as before. On allocation failure they return NULL / 0 with a
 *     pending JS out-of-memory exception, export nothing, release what they retained and may be retried. Calling them
 *     on a compacted texture gives back its canonical buffer for good; use *_borrow when pixels are not needed.
 *   athena_texture3d_from_value_borrow / athena_mesh3d_from_value_borrow / athena_instance3d_from_value_borrow /
 *   athena_model3d_js_material_borrow
 *     COMPACTION-AWARE forms for code that only retains/releases, binds, uploads, reads athena_texture3d_info() or mesh
 *     geometry. They never allocate pixels or touch the GS. A texture obtained through them (or through a mesh, instance,
 *     node or scene reached from them) may have no canonical RGBA: read pixels only through athena_texture3d_try_view()
 *     or after a successful athena_texture3d_pin_canonical(). The legacy view() is not safe on such a texture.
 *
 * Scene3D (Phase 5C.5E): athena_node3d_from_value hands out a node and reaches no pixels; the single way from a node to a mesh
 * (and so to material.texture) is athena_node3d_mesh, now the SAFE form (pins that one mesh's texture, NULL on ENOMEM), with
 * athena_node3d_mesh_pin (status) and athena_node3d_mesh_borrow (compaction-aware) beside it: see athena/scene3d.h. Scene3D attaches
 * meshes through the borrow forms above, so building, updating and drawing a scene never materializes pixels. */
AthenaInstance3D *athena_instance3d_from_value(JSContext *ctx,JSValueConst value);
AthenaInstance3D *athena_instance3d_from_value_borrow(JSContext *ctx,JSValueConst value);
AthenaTexture3D *athena_texture3d_from_value(JSContext *ctx,JSValueConst value);
AthenaTexture3D *athena_texture3d_from_value_borrow(JSContext *ctx,JSValueConst value);
AthenaMesh3D *athena_mesh3d_from_value(JSContext *ctx,JSValueConst value);
AthenaMesh3D *athena_mesh3d_from_value_borrow(JSContext *ctx,JSValueConst value);
/* A Model3D.Mesh handle for a native mesh, e.g. one built by another module.
 * Takes over the caller's reference (released by dispose() or the finalizer,
 * and on failure, which returns JS_EXCEPTION). Requires Model3D initialized
 * in ctx. */
JSValue athena_mesh3d_js_wrap(JSContext *ctx,AthenaMesh3D *mesh);
/* The material argument of Model3D.load(): undefined gives the default. On
 * success the caller owns a reference to out->texture (may be NULL) and
 * releases it with athena_texture3d_release() after loading. */
int athena_model3d_js_material(JSContext *ctx,JSValueConst value,AthenaMaterial3D *out);
int athena_model3d_js_material_borrow(JSContext *ctx,JSValueConst value,AthenaMaterial3D *out);
/* A Model3D.Texture handle for a native texture: takes over the caller's reference (released by
 * dispose(), the finalizer, and on failure, which returns JS_EXCEPTION). */
JSValue athena_texture3d_js_wrap(JSContext *ctx,AthenaTexture3D *texture);
#endif
