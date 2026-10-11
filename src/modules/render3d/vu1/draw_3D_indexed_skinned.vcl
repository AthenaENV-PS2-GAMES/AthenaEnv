; Bounded indexed VU1 pipeline; shared body holds projection, lighting,
; perspective ST/Q, fog and triangle-list backface culling.
.syntax new
.name VU1Draw3DIndexedSkinned
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
    ; Joint indices become palette addresses (four quadwords per joint).
    lq joints, 146(\base)
    lq weightsPacked, 170(\base)
    itof0 weights, weightsPacked
    loi 0.00392156862745
    muli weights, weights, i
    mtir jointA, joints[x]
    mtir jointB, joints[y]
    mtir jointC, joints[z]
    mtir jointD, joints[w]
    iadd jointA, jointA, jointA
    iadd jointA, jointA, jointA
    iadd jointB, jointB, jointB
    iadd jointB, jointB, jointB
    iadd jointC, jointC, jointC
    iadd jointC, jointC, jointC
    iadd jointD, jointD, jointD
    iadd jointD, jointD, jointD
    ; Blended columns: sum of weight * column over the four joints.
    lq columnA, 36+0(jointA)
    lq columnB, 36+0(jointB)
    lq columnC, 36+0(jointC)
    lq columnD, 36+0(jointD)
    mulax ACC, columnA, weights
    madday ACC, columnB, weights
    maddaz ACC, columnC, weights
    maddw skin0, columnD, weights
    lq columnA, 36+1(jointA)
    lq columnB, 36+1(jointB)
    lq columnC, 36+1(jointC)
    lq columnD, 36+1(jointD)
    mulax ACC, columnA, weights
    madday ACC, columnB, weights
    maddaz ACC, columnC, weights
    maddw skin1, columnD, weights
    lq columnA, 36+2(jointA)
    lq columnB, 36+2(jointB)
    lq columnC, 36+2(jointC)
    lq columnD, 36+2(jointD)
    mulax ACC, columnA, weights
    madday ACC, columnB, weights
    maddaz ACC, columnC, weights
    maddw skin2, columnD, weights
    lq columnA, 36+3(jointA)
    lq columnB, 36+3(jointB)
    lq columnC, 36+3(jointC)
    lq columnD, 36+3(jointD)
    mulax ACC, columnA, weights
    madday ACC, columnB, weights
    maddaz ACC, columnC, weights
    maddw skin3, columnD, weights
    ; Deformed position (w = 1) and normal (no translation).
    mulax ACC, skin0, \vertex
    madday ACC, skin1, \vertex
    maddaz ACC, skin2, \vertex
    maddw skinned, skin3, \vertex
    move \vertex, skinned
    mulax.xyz ACC, skin0, \normal
    madday.xyz ACC, skin1, \normal
    maddz.xyz \normal, skin2, \normal
.endm
.include "src/modules/render3d/vu1/include/indexed_body.i"
