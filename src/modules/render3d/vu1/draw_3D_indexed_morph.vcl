; Bounded indexed VU1 pipeline; shared body holds projection, lighting,
; perspective ST/Q, fog and triangle-list backface culling.
.syntax new
.name VU1Draw3DIndexedMorph
.vu
.init_vf_all
.init_vi_all
.include "src/modules/render3d/vu1/include/mem_layout.i"
.include "src/modules/render3d/vu1/include/athena_consts.i"
.include "src/modules/render3d/vu1/include/athena_macros.i"
.include "src/modules/render3d/vu1/include/vcl_sml.i"
.include "src/modules/render3d/vu1/include/fog.i"
.include "src/modules/render3d/vu1/include/point_lights.i"
INDEXED_CACHE .assign 210
INDEXED_OUTPUT .assign 258
.macro IndexedDeform vertex, normal, base
    ilw.x targets, 35(vi00)
    iaddiu deltaAddress, \base, 146
    iadd target, vi00, vi00
    ibeq targets, vi00, indexed_deltas_done
indexed_delta_loop:
    lq delta, 0(deltaAddress)
    lq weight, 36(target)
    mul.xyz delta, delta, weight
    add.xyz \vertex, \vertex, delta
    iaddiu deltaAddress, deltaAddress, 16
    iaddiu target, target, 1
    ibne target, targets, indexed_delta_loop
indexed_deltas_done:
.endm
.include "src/modules/render3d/vu1/include/indexed_body.i"
