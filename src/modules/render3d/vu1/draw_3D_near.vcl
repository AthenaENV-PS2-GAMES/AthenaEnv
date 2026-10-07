; Near plane clipping on VU1, for objects whose part in front of the camera
; lies inside the GS guard band and before the far plane (the EE tests that
; from the box). Per triangle: transform, light and classify the three
; vertices against the near plane (z <= w), clip the polygon (Sutherland-
; Hodgman, one plane: 0, 3 or 4 vertices, attributes interpolated in clip
; space), and emit its fan (1 or 2 triangles) with backface culling. Only
; whole triangles are written, so no ADC kick pattern; the GIF tag's NLOOP is
; the number of vertices emitted.
;
; One program for every pipeline: CLIPFAN.x is the colour scale (255 for
; untextured RGB, 128 for MODULATE), CLIPFAN.z enables Gouraud diffuse,
; STQ is written always (ignored without TME).
;
; Input (at most 24 vertices: the output may double): the static layout of
; mem_layout.i. Per-triangle state lives in VU memory (OpenVCL does not keep
; registers live across loop back edges):
;   36..39 clip positions of v0, v1, v2 and v0 again
;   40..43 colours, 44..47 UVs (same order)
;   48..51 polygon positions, 52..55 colours, 56..59 UVs
.syntax new
.name VU1Draw3DNear
.vu
.init_vf_all
.init_vi_all
.include "src/modules/render3d/vu1/include/mem_layout.i"
.include "src/modules/render3d/vu1/include/athena_consts.i"
.include "src/modules/render3d/vu1/include/athena_macros.i"
.include "src/modules/render3d/vu1/include/vcl_sml.i"
.include "src/modules/render3d/vu1/include/fog.i"
.include "src/modules/render3d/vu1/include/point_lights.i"
NEAR_POS .assign 36
NEAR_COL .assign 40
NEAR_UV .assign 44
POLY_POS .assign 48
POLY_COL .assign 52
POLY_UV .assign 56

--enter
--endenter
    MatrixLoad ScreenMatrix, SCREEN_MATRIX, vi00
    MatrixLoad ObjectMatrix, OBJECT_MATRIX, vi00
    MatrixMultiply ObjectToScreen, ObjectMatrix, ScreenMatrix
    MatrixLoad NormalMatrix, 27, vi00
    ilw.w dirLightCount, 15(vi00)
    ilw.z lightingEnabled, CLIPFAN_OFFSET(vi00)
    lq scale, SCREEN_SCALE(vi00)
    AddScreenOffset scale
    lq.w bfc_multiplier, CLIPFAN_OFFSET(vi00)
    ftoi0.w bfc_sign_mask, bfc_multiplier
    mtir z_sign_mask, bfc_sign_mask[w]
    ibeq z_sign_mask, vi00, ignore_face_culling
    iaddiu z_sign_mask, vi00, 0x20
ignore_face_culling:
    xtop iBase
    xitop vertCount
    lq primTag, 0(iBase)
    iaddiu kickAddress, iBase, INBUF_SIZE
    iaddiu destAddress, kickAddress, 1
    sq primTag, 0(kickAddress)
    iadd emitted, vi00, vi00
    iadd vertexCounter, vi00, vertCount
triangleLoop:
    ; Transform, light and store the three vertices.
    iadd k, vi00, vi00
prepareLoop:
    iadd source, iBase, k
    lq vertex, POSITION_OFFSET(source)
    DecompressPositionW vertex
    MatrixMultiplyVertex clip, ObjectToScreen, vertex
    sq clip, NEAR_POS(k)
    lq inColorPacked, COLOR_OFFSET(source)
    DecompressColor8 color, inColorPacked
    ibeq lightingEnabled, vi00, shading_done
    lq inNormal, 50(source)
    MatrixMultiplyVector normal, NormalMatrix, inNormal
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
    PointLights light, normal, vertex
    mul.xyz color, color, light
    miniw.xyz color, color, vf00
shading_done:
    sq color, NEAR_COL(k)
    lq uv, 146(source)
    sq uv, NEAR_UV(k)
    iaddiu k, k, 1
    iaddiu three, vi00, 3
    ibne k, three, prepareLoop
    ; v0 again after v2: edge e runs from slot e to slot e + 1.
    lq first, NEAR_POS(vi00)
    sq first, NEAR_POS+3(vi00)
    lq firstColor, NEAR_COL(vi00)
    sq firstColor, NEAR_COL+3(vi00)
    lq firstUV, NEAR_UV(vi00)
    sq firstUV, NEAR_UV+3(vi00)
    ; Clip against the near plane: inside when w - z >= 0.
    iadd edge, vi00, vi00
    iadd polyCount, vi00, vi00
edgeLoop:
    ; MAC flag bit 4: sign of the w lane.
    iaddiu signW, vi00, 0x10
    lq pa, NEAR_POS(edge)
    lq pb, NEAR_POS+1(edge)
    subz.w da, pa, pa[z]
    fmand behindA, signW
    subz.w db, pb, pb[z]
    fmand behindB, signW
    ibne behindA, vi00, a_behind
    lq keepPos, NEAR_POS(edge)
    sq keepPos, POLY_POS(polyCount)
    lq keepColor, NEAR_COL(edge)
    sq keepColor, POLY_COL(polyCount)
    lq keepUV, NEAR_UV(edge)
    sq keepUV, POLY_UV(polyCount)
    iaddiu polyCount, polyCount, 1
a_behind:
    ibeq behindA, behindB, no_crossing
    ; t = da / (da - db), in [0, 1] since the signs differ.
    sub.w gap, da, db
    div q, da[w], gap[w]
    lq ca, NEAR_POS(edge)
    lq cb, NEAR_POS+1(edge)
    sub cdelta, cb, ca
    mulq cdelta, cdelta, q
    add cross, ca, cdelta
    ; Exactly on the plane: z = w.
    addw.z cross, vf00, cross[w]
    sq cross, POLY_POS(polyCount)
    lq ka, NEAR_COL(edge)
    lq kb, NEAR_COL+1(edge)
    sub kdelta, kb, ka
    mulq kdelta, kdelta, q
    add kcross, ka, kdelta
    sq kcross, POLY_COL(polyCount)
    lq ua, NEAR_UV(edge)
    lq ub, NEAR_UV+1(edge)
    sub udelta, ub, ua
    mulq udelta, udelta, q
    add ucross, ua, udelta
    sq ucross, POLY_UV(polyCount)
    iaddiu polyCount, polyCount, 1
no_crossing:
    iaddiu edge, edge, 1
    iaddiu edges, vi00, 3
    ibne edge, edges, edgeLoop
    ; Fan (0, f, f + 1) for f = 1 .. polyCount - 2.
    ibeq polyCount, vi00, triangle_done
    iaddiu fan, vi00, 1
fanLoop:
    ; Project the three corners into VU memory 60..68 (positions,
    ; colours, STQ), then cull by the screen-space winding.
    iadd corner, vi00, vi00
    iadd at, vi00, vi00
projectLoop:
    lq.xyz colorScale, CLIPFAN_OFFSET(vi00)
    lq pos, POLY_POS(at)
    lq col, POLY_COL(at)
    lq tex, POLY_UV(at)
    div q, vf00[w], pos[w]
    mul.xyz pos, pos, q
    mul.xyz pos, pos, scale
    add.xyz pos, pos, offset
    addw.z tex, vf00, vf00[w]
    mul.xyz tex, tex, q
    mulx.xyz col, col, colorScale[x]
    loi 128.0
    muli.w col, col, i
    sq pos, 60(corner)
    sq col, 63(corner)
    sq.xyz tex, 66(corner)
    iaddiu corner, corner, 1
    ; Corners 0, fan, fan + 1 of the polygon.
    iadd at, fan, corner
    iaddi at, at, -1
    iaddiu threeCorners, vi00, 3
    ibne corner, threeCorners, projectLoop
    lq.xyz s0, 60(vi00)
    lq.xyz s1, 61(vi00)
    lq.xyz s2, 62(vi00)
    sub.xyz oldvector, s1, s0
    sub.xyz vector, s2, s1
    opmula.xyz acc, vector, oldvector
    opmsub.xyz crossproduct, oldvector, vector
    lq.w bfcScale, CLIPFAN_OFFSET(vi00)
    mulw.z crossproduct, crossproduct, bfcScale
    fmand culled, z_sign_mask
    ibne culled, vi00, fan_next
    iadd corner, vi00, vi00
emitLoop:
    lq out, 60(corner)
    lq outColor, 63(corner)
    lq.xyz outTex, 66(corner)
    ColorFPtoGsRGBAQ outInt, outColor
    sq.xyz outTex, STQ(destAddress)
    sq outInt, RGBA(destAddress)
    ; Whole triangles: no kick bit, only the fog factor (the w lane still
    ; holds the clip w).
    iadd noKick, vi00, vi00
    FogStore out, noKick, destAddress
    iaddiu destAddress, destAddress, 3
    iaddiu emitted, emitted, 1
    iaddiu corner, corner, 1
    iaddiu threeOut, vi00, 3
    ibne corner, threeOut, emitLoop
fan_next:
    iaddiu fan, fan, 1
    iaddi last, polyCount, -1
    ibne fan, last, fanLoop
triangle_done:
    iaddiu iBase, iBase, 3
    iaddi vertexCounter, vertexCounter, -3
    ibne vertexCounter, vi00, triangleLoop
    ; NLOOP = vertices emitted, with EOP.
    iaddiu nloop, emitted, 0x7fff
    iaddiu nloop, nloop, 1
    isw.x nloop, 0(kickAddress)
    xgkick kickAddress
--barrier
--exit
--endexit
.end
