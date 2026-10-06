# Static model loaders

`fast_obj/` and `cgltf/` were copied from `old/src/` with their MIT licenses.
They are unchanged in this migration; fast_obj already includes the legacy
Athena strip-related fields. The new wrapper rejects strips and supports OBJ
triangle faces with diffuse colors, and a single static glTF/GLB primitive
with POSITION, optional NORMAL/COLOR_0/TEXCOORD_0, indices and base color factor. OBJ vn
normals are imported when complete; missing diffuse normals are generated per
face. The default is unlit; an explicit copied material override enables
Gouraud diffuse lighting. glTF metallic/roughness values do not affect shading.
OBJ vt is imported when complete (V flipped to the top-left origin). glTF
base-color textures support external RGB/RGBA images, power-of-two sizes up
to 512, and explicit clamp samplers with matching nearest/linear filters.
Embedded/data images, repeat and mipmaps require a caller-supplied texture
override or are rejected. Meshes retain native textures independently of Image.
OBJ material maps remain unsupported; supply a texture override on UV geometry.
Skins, animation, sparse accessors, node transforms and
advanced PBR shading are explicitly unsupported. Parser inputs are not assumed
to be adversarially hardened; expanded output is bounded and validated, but
the vendored parsers may allocate input-sized temporary data before that.
