# Static model loaders

`fast_obj/` and `cgltf/` were copied from `old/src/` with their MIT licenses.
They are unchanged in this migration; fast_obj already includes the legacy
Athena strip-related fields. The new wrapper rejects strips and supports OBJ
triangle faces with diffuse colors, and a single static glTF/GLB primitive
with POSITION, optional COLOR_0, indices and base color factor. This stage is
an unlit color pipeline: glTF metallic/roughness values do not affect shading.
Skins, animation, sparse accessors, node transforms, texture materials and
advanced shading are explicitly unsupported. Parser inputs are not assumed
to be adversarially hardened; expanded output is bounded and validated, but
the vendored parsers may allocate input-sized temporary data before that.
