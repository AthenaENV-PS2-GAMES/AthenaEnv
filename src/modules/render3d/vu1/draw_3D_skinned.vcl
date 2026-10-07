; Skinned Gouraud pass for meshes fully inside the frustum (others are
; skinned on the EE and clipped in C). Each vertex blends the columns of up to
; four joint matrices of the palette by its weights, then goes through the
; diffuse pass unchanged: the object matrix is identity (skins are in world
; space) and unlit meshes come with a white ambient and no lights.
;
; Static data beyond mem_layout.i: the palette, four columns per joint
; (world(joint) * inverse_bind), from SKIN_PALETTE.
; Batch input (room for 32 vertices; the EE sends 30, whole triangles):
; positions +2, normals +34, colors +66, joints +98 and weights +130 (V4_8,
; weights in 0..255); output from +163.
.syntax new
.name VU1Draw3DSkinned
.vu
.init_vf_all
.init_vi_all
.include "src/modules/render3d/vu1/include/mem_layout.i"
.include "src/modules/render3d/vu1/include/athena_consts.i"
.include "src/modules/render3d/vu1/include/athena_macros.i"
.include "src/modules/render3d/vu1/include/vcl_sml.i"
.include "src/modules/render3d/vu1/include/fog.i"
.include "src/modules/render3d/vu1/include/point_lights.i"
SKIN_NORMAL_OFFSET .assign 34
SKIN_COLOR_OFFSET .assign 66
SKIN_JOINT_OFFSET .assign 98
SKIN_WEIGHT_OFFSET .assign 130
SKIN_INBUF_SIZE .assign 162
SKIN_PALETTE .assign 36

--enter
--endenter
    MatrixLoad ScreenMatrix, SCREEN_MATRIX, vi00
    MatrixLoad ObjectMatrix, OBJECT_MATRIX, vi00
    MatrixMultiply ObjectToScreen, ObjectMatrix, ScreenMatrix
    MatrixLoad NormalMatrix, 27, vi00
    ilw.w dirLightCount, 15(vi00)
    lq scale, SCREEN_SCALE(vi00)
    AddScreenOffset scale
    ; (1/guard, 1/guard, 1, 1), guard from SCREEN_SCALE.w.
    addw.xy guardScale, vf00, scale[w]
    maxw.zw guardScale, vf00, vf00[w]
    lq.w bfc_multiplier, CLIPFAN_OFFSET(vi00)
    ftoi0.w bfc_sign_mask, bfc_multiplier
    mtir z_sign_mask, bfc_sign_mask[w]
    ibeq z_sign_mask, vi00, ignore_face_culling
    iaddiu z_sign_mask, vi00, 0x20
ignore_face_culling:
    sq vf00, CULL_PREVIOUS_VERTEX(vi00)
    sq vf00, CULL_PREVIOUS_EDGE(vi00)
    ; Only the third vertex of each triangle may kick: the first two always
    ; carry ADC. Otherwise, after a culled triangle (no kick on its third
    ; vertex), the next vertex would kick with the culled one's last two
    ; vertices still queued, drawing a stray triangle and misaligning the
    ; rest of the list. Batches hold whole triangles, so the cycle restarts
    ; with each program call.
    iaddiu kickPattern, vi00, 0x4000
    iadd kickPattern, kickPattern, kickPattern
    isw.x kickPattern, KICK_CYCLE(vi00)
    isw.y kickPattern, KICK_CYCLE(vi00)
    isw.z vi00, KICK_CYCLE(vi00)
    fcset 0
    xtop iBase
    xitop vertCount
    lq primTag, 0(iBase)
    iaddiu kickAddress, iBase, SKIN_INBUF_SIZE
    iaddiu destAddress, kickAddress, 1
    sq primTag, 0(kickAddress)
    iaddiu Mask, vertCount, 0x7fff
    iaddiu Mask, Mask, 1
    isw.x Mask, 0(kickAddress)
    iadd vertexCounter, vi00, vertCount
vertexLoop:
    lq vertex, POSITION_OFFSET(iBase)
    DecompressPositionW vertex
    ; Joint indices become palette addresses (four quadwords per joint).
    lq joints, SKIN_JOINT_OFFSET(iBase)
    lq weightsPacked, SKIN_WEIGHT_OFFSET(iBase)
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
    lq columnA, SKIN_PALETTE+0(jointA)
    lq columnB, SKIN_PALETTE+0(jointB)
    lq columnC, SKIN_PALETTE+0(jointC)
    lq columnD, SKIN_PALETTE+0(jointD)
    mulax ACC, columnA, weights
    madday ACC, columnB, weights
    maddaz ACC, columnC, weights
    maddw skin0, columnD, weights
    lq columnA, SKIN_PALETTE+1(jointA)
    lq columnB, SKIN_PALETTE+1(jointB)
    lq columnC, SKIN_PALETTE+1(jointC)
    lq columnD, SKIN_PALETTE+1(jointD)
    mulax ACC, columnA, weights
    madday ACC, columnB, weights
    maddaz ACC, columnC, weights
    maddw skin1, columnD, weights
    lq columnA, SKIN_PALETTE+2(jointA)
    lq columnB, SKIN_PALETTE+2(jointB)
    lq columnC, SKIN_PALETTE+2(jointC)
    lq columnD, SKIN_PALETTE+2(jointD)
    mulax ACC, columnA, weights
    madday ACC, columnB, weights
    maddaz ACC, columnC, weights
    maddw skin2, columnD, weights
    lq columnA, SKIN_PALETTE+3(jointA)
    lq columnB, SKIN_PALETTE+3(jointB)
    lq columnC, SKIN_PALETTE+3(jointC)
    lq columnD, SKIN_PALETTE+3(jointD)
    mulax ACC, columnA, weights
    madday ACC, columnB, weights
    maddaz ACC, columnC, weights
    maddw skin3, columnD, weights
    ; Deformed position (w = 1) and normal (no translation).
    mulax ACC, skin0, vertex
    madday ACC, skin1, vertex
    maddaz ACC, skin2, vertex
    maddw skinned, skin3, vertex
    MatrixMultiplyVertex vertex, ObjectToScreen, skinned
    ; Rejection against the guard band: x and y scaled by 1/guard, so
    ; triangles crossing only the screen edges are drawn and trimmed by the
    ; GS scissor. One full-width mul: OpenVCL paired a mulw.xy and a
    ; move.zw writing the same register in one cycle, where VU1 keeps only
    ; the upper result (z and w then came from the previous colour).
    mul guarded, vertex, guardScale
    clipw.xyz guarded, guarded[w]
    fcand vi01, 0x3ffff
    iaddiu iADC, vi01, 0x7fff
    lq inColorPacked, SKIN_COLOR_OFFSET(iBase)
    DecompressColor8 color, inColorPacked
    lq inNormal, SKIN_NORMAL_OFFSET(iBase)
    mulax.xyz ACC, skin0, inNormal
    madday.xyz ACC, skin1, inNormal
    maddz.xyz skinnedNormal, skin2, inNormal
    MatrixMultiplyVector normal, NormalMatrix, skinnedNormal
    VectorNormalize normal, normal
    lq light, 14(vi00)
    iadd currentLight, vi00, vi00
    ibeq dirLightCount, vi00, lights_done
lightLoop:
    lq direction, 10(currentLight)
    VectorDotProduct intensity, normal, direction
    maxx.x intensity, intensity, vf00
    lq diffuse, 18(currentLight)
    mulx.xyz diffuse, diffuse, intensity
    add.xyz light, light, diffuse
    iaddiu currentLight, currentLight, 1
    ibne currentLight, dirLightCount, lightLoop
lights_done:
    ; The skinned position is already in world space (identity object matrix).
    PointLights light, normal, skinned
    mul.xyz color, color, light
    miniw.xyz color, color, vf00
    div q, vf00[w], vertex[w]
    mul.xyz vertex, vertex, q
    mul.xyz vertex, vertex, scale
    add.xyz vertex, vertex, offset
    lq.xyz previousVertex, CULL_PREVIOUS_VERTEX(vi00)
    lq.xyz oldvector, CULL_PREVIOUS_EDGE(vi00)
    sub.xyz vector, vertex, previousVertex
    sq.xyz vertex, CULL_PREVIOUS_VERTEX(vi00)
    sq.xyz vector, CULL_PREVIOUS_EDGE(vi00)
    opmula.xyz acc, vector, oldvector
    opmsub.xyz crossproduct, oldvector, vector
    mulw.z crossproduct, crossproduct, bfc_multiplier
    fmand z_sign, z_sign_mask
    iaddiu z_sign, z_sign, 0x7fe0
    ior iADC, iADC, z_sign
    ilw.x kickFirst, KICK_CYCLE(vi00)
    ilw.y kickSecond, KICK_CYCLE(vi00)
    ilw.z kickThird, KICK_CYCLE(vi00)
    ior iADC, iADC, kickFirst
    isw.x kickSecond, KICK_CYCLE(vi00)
    isw.y kickThird, KICK_CYCLE(vi00)
    isw.z kickFirst, KICK_CYCLE(vi00)
    ; Untextured RGB uses the full 8-bit range; GS alpha uses 0..128.
    loi 255.0
    mul.xyz color, color, i
    loi 128.0
    mul.w color, color, i
    ColorFPtoGsRGBAQ intColor, color
    ; Untextured PRIM: deterministic ST/Q, independent of old VU memory.
    sq vf00, STQ(destAddress)
    sq intColor, RGBA(destAddress)
    FogStore vertex, iADC, destAddress
    iaddiu iBase, iBase, 1
    iaddiu destAddress, destAddress, 3
    iaddi vertexCounter, vertexCounter, -1
    ibne vertexCounter, vi00, vertexLoop
    xgkick kickAddress
--barrier
--exit
--endexit
.end
