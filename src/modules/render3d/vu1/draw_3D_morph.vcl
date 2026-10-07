; Morph targets on VU1, for meshes whose morphed bounds lie inside the guard
; band (the EE checks the conservative morph box): per triangle, each vertex
; is base + sum(weight * delta) over up to 4 active targets; with generated
; flat normals the face normal comes from the morphed triangle, otherwise
; the base normals stay. Then the usual path: diffuse and point lights
; (CLIPFAN.z), guard band rejection, backface culling, fog, texture STQ.
; A skipped triangle carries ADC on its three vertices, so nothing kicks.
;
; Static: weights at MORPH_WEIGHTS (one qword per target, (w, w, w, 0)),
; MORPH_INFO (35): x = active targets, y = flat normals. CLIPFAN.x = colour scale (255
; untextured, 128 for MODULATE). Scratch 40..57.
; Batch input (at most 33 vertices, whole triangles): positions +2, colours
; +35, normals +68, UVs +101, deltas +134 + 33 t; output from +267.
.syntax new
.name VU1Draw3DMorph
.vu
.init_vf_all
.init_vi_all
.include "src/modules/render3d/vu1/include/mem_layout.i"
.include "src/modules/render3d/vu1/include/athena_consts.i"
.include "src/modules/render3d/vu1/include/athena_macros.i"
.include "src/modules/render3d/vu1/include/vcl_sml.i"
.include "src/modules/render3d/vu1/include/point_lights.i"
.include "src/modules/render3d/vu1/include/fog.i"
MORPH_WEIGHTS .assign 36
MORPH_INFO .assign 35
MORPH_COLOR .assign 35
MORPH_NORMAL .assign 68
MORPH_UV .assign 101
MORPH_DELTA .assign 134
MORPH_STRIDE .assign 33
MORPH_INBUF .assign 266
SCR_POS .assign 40
SCR_NRM .assign 43
SCR_SCREEN .assign 46
SCR_COL .assign 49
SCR_STQ .assign 52

--enter
--endenter
    ; Matrices and screen constants are read inside the loops (OpenVCL does
    ; not keep registers live across loop back edges).
    lq.w bfc_multiplier, CLIPFAN_OFFSET(vi00)
    ftoi0.w bfc_sign_mask, bfc_multiplier
    mtir z_sign_mask, bfc_sign_mask[w]
    ibeq z_sign_mask, vi00, ignore_face_culling
    iaddiu z_sign_mask, vi00, 0x20
ignore_face_culling:
    fcset 0
    xtop iBase
    xitop vertCount
    lq primTag, 0(iBase)
    iaddiu kickAddress, iBase, MORPH_INBUF
    iaddiu destAddress, kickAddress, 1
    sq primTag, 0(kickAddress)
    iaddiu Mask, vertCount, 0x7fff
    iaddiu Mask, Mask, 1
    isw.x Mask, 0(kickAddress)
    iadd vertexCounter, vi00, vertCount
triangleLoop:
    ; Morph the three positions (object space) and fetch their normals.
    iadd k, vi00, vi00
morphLoop:
    iadd source, iBase, k
    lq position, POSITION_OFFSET(source)
    DecompressPositionW position
    ilw.x targets, MORPH_INFO(vi00)
    iaddiu deltaAddress, source, MORPH_DELTA
    iadd target, vi00, vi00
    ibeq targets, vi00, deltas_done
deltaLoop:
    lq delta, 0(deltaAddress)
    lq weight, MORPH_WEIGHTS(target)
    mul.xyz delta, delta, weight
    add.xyz position, position, delta
    iaddiu deltaAddress, deltaAddress, MORPH_STRIDE
    iaddiu target, target, 1
    ibne target, targets, deltaLoop
deltas_done:
    sq position, SCR_POS(k)
    lq baseNormal, MORPH_NORMAL(source)
    sq baseNormal, SCR_NRM(k)
    iaddiu k, k, 1
    iaddiu three, vi00, 3
    ibne k, three, morphLoop
    ; Generated flat normals: the morphed face's.
    ilw.y flat, MORPH_INFO(vi00)
    ibeq flat, vi00, normals_done
    lq.xyz p0, SCR_POS(vi00)
    lq.xyz p1, SCR_POS+1(vi00)
    lq.xyz p2, SCR_POS+2(vi00)
    sub.xyz e1, p1, p0
    sub.xyz e2, p2, p0
    opmula.xyz acc, e1, e2
    opmsub.xyz face, e2, e1
    sq.xyz face, SCR_NRM(vi00)
    sq.xyz face, SCR_NRM+1(vi00)
    sq.xyz face, SCR_NRM+2(vi00)
normals_done:
    ; Light, transform and project each corner into scratch.
    iadd k, vi00, vi00
shadeLoop:
    iadd source, iBase, k
    lq objectPosition, SCR_POS(k)
    lq inColorPacked, MORPH_COLOR(source)
    DecompressColor8 color, inColorPacked
    ilw.z lightingEnabled, CLIPFAN_OFFSET(vi00)
    ibeq lightingEnabled, vi00, shading_done
    MatrixLoad NormalMatrix, 27, vi00
    lq inNormal, SCR_NRM(k)
    MatrixMultiplyVector normal, NormalMatrix, inNormal
    VectorNormalize normal, normal
    lq light, 14(vi00)
    ilw.w dirLightCount, 15(vi00)
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
    PointLights light, normal, objectPosition
    mul.xyz color, color, light
    miniw.xyz color, color, vf00
shading_done:
    MatrixLoad Model, OBJECT_MATRIX, vi00
    MatrixLoad Screen, SCREEN_MATRIX, vi00
    MatrixMultiplyVertex world, Model, objectPosition
    MatrixMultiplyVertex clip, Screen, world
    ; Guard band test: x and y scaled by 1/guard (SCREEN_SCALE.w).
    lq.w guardInverse, SCREEN_SCALE(vi00)
    mulw.xy guarded, clip, guardInverse[w]
    mulw.zw guarded, clip, vf00[w]
    clipw.xyz guarded, guarded[w]
    lq.xyz screenScale, SCREEN_SCALE(vi00)
    loi 2048.0
    addi.xy screenOffset, vf00, i
    add.zw screenOffset, vf00, vf00
    add.xyz screenOffset, screenScale, screenOffset
    div q, vf00[w], clip[w]
    mul.xyz projected, clip, q
    mul.xyz projected, projected, screenScale
    add.xyz projected, projected, screenOffset
    mulw.w projected, clip, vf00[w]
    sq projected, SCR_SCREEN(k)
    lq.xyz colorScale, CLIPFAN_OFFSET(vi00)
    mulx.xyz color, color, colorScale[x]
    loi 128.0
    muli.w color, color, i
    sq color, SCR_COL(k)
    lq uv, MORPH_UV(source)
    addw.z uv, vf00, vf00[w]
    mulq.xyz uv, uv, q
    sq.xyz uv, SCR_STQ(k)
    iaddiu k, k, 1
    iaddiu threeShaded, vi00, 3
    ibne k, threeShaded, shadeLoop
    ; Skip the triangle when a corner leaves the guard band or it faces
    ; away (the same winding test as the other programs).
    fcand vi01, 0x3ffff
    iadd outside, vi01, vi00
    lq.xyz s0, SCR_SCREEN(vi00)
    lq.xyz s1, SCR_SCREEN+1(vi00)
    lq.xyz s2, SCR_SCREEN+2(vi00)
    sub.xyz oldvector, s1, s0
    sub.xyz vector, s2, s1
    opmula.xyz acc, vector, oldvector
    opmsub.xyz crossproduct, oldvector, vector
    lq.w bfcScale, CLIPFAN_OFFSET(vi00)
    mulw.z crossproduct, crossproduct, bfcScale
    fmand culled, z_sign_mask
    ior skip, outside, culled
    iadd adc, vi00, vi00
    ibeq skip, vi00, emit
    iaddiu adc, vi00, 0x7fff
    iaddiu adc, adc, 1
emit:
    iadd k, vi00, vi00
emitLoop:
    lq out, SCR_SCREEN(k)
    lq outColor, SCR_COL(k)
    lq.xyz outStq, SCR_STQ(k)
    ColorFPtoGsRGBAQ outInt, outColor
    sq.xyz outStq, STQ(destAddress)
    sq outInt, RGBA(destAddress)
    FogStore out, adc, destAddress
    iaddiu destAddress, destAddress, 3
    iaddiu k, k, 1
    iaddiu threeOut, vi00, 3
    ibne k, threeOut, emitLoop
    iaddiu iBase, iBase, 3
    iaddi vertexCounter, vertexCounter, -3
    ibne vertexCounter, vi00, triangleLoop
    xgkick kickAddress
--barrier
--exit
--endexit
.end
