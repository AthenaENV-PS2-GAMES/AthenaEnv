; Bounded indexed VU1 pipeline; shared body holds projection, lighting,
; perspective ST/Q, fog and triangle-list backface culling.
.syntax new
.name VU1Draw3DIndexed
.vu
.init_vf_all
.init_vi_all
.include "src/modules/render3d/vu1/include/mem_layout.i"
.include "src/modules/render3d/vu1/include/athena_consts.i"
.include "src/modules/render3d/vu1/include/athena_macros.i"
.include "src/modules/render3d/vu1/include/vcl_sml.i"
.include "src/modules/render3d/vu1/include/fog.i"
.include "src/modules/render3d/vu1/include/point_lights.i"
INDEXED_CACHE .assign 194
INDEXED_OUTPUT .assign 266
.macro IndexedDeform vertex, normal, base
.endm
.include "src/modules/render3d/vu1/include/indexed_body.i"
