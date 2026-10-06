; Opaque Gouraud diffuse pass. Native clipping uses pre-lit colors in the
; unchanged color program. World normals use a true inverse-transpose matrix.
.syntax new
.name VU1Draw3DDiffuse
.vu
.init_vf_all
.init_vi_all
.include "src/modules/render3d/vu1/include/mem_layout.i"
.include "src/modules/render3d/vu1/include/athena_consts.i"
.include "src/modules/render3d/vu1/include/athena_macros.i"
.include "src/modules/render3d/vu1/include/vcl_sml.i"

--enter
--endenter
    MatrixLoad ScreenMatrix, SCREEN_MATRIX, vi00
    MatrixLoad ObjectMatrix, OBJECT_MATRIX, vi00
    MatrixMultiply ObjectToScreen, ObjectMatrix, ScreenMatrix
    MatrixLoad NormalMatrix, 27, vi00
    ilw.w dirLightCount, 15(vi00)
    lq scale, SCREEN_SCALE(vi00)
    AddScreenOffset scale
    lq.w bfc_multiplier, CLIPFAN_OFFSET(vi00)
    ilw.y pretransformed, CLIPFAN_OFFSET(vi00)
    ftoi0.w bfc_sign_mask, bfc_multiplier
    mtir z_sign_mask, bfc_sign_mask[w]
    ibeq z_sign_mask, vi00, ignore_face_culling
    iaddiu z_sign_mask, vi00, 0x20
ignore_face_culling:
    move vector, vf00
    move oldvector, vf00
    move vertex2, vf00
    move vertex3, vf00
    fcset 0
    xtop iBase
    xitop vertCount
    lq primTag, 0(iBase)
    iaddiu kickAddress, iBase, INBUF_SIZE
    iaddiu destAddress, kickAddress, 1
    sq primTag, 0(kickAddress)
    iaddiu Mask, vertCount, 0x7fff
    iaddiu Mask, Mask, 1
    isw.x Mask, 0(kickAddress)
    iadd vertexCounter, vi00, vertCount
vertexLoop:
    lq vertex, POSITION_OFFSET(iBase)
    ibne pretransformed, vi00, clipped_vertex
    DecompressPositionW vertex
    MatrixMultiplyVertex vertex, ObjectToScreen, vertex
    clipw.xyz vertex, vertex[w]
    fcand vi01, 0x3ffff
    iaddiu iADC, vi01, 0x7fff
    b project_vertex
clipped_vertex:
    iaddiu iADC, vi00, 0x7fff
project_vertex:
    lq inColorPacked, COLOR_OFFSET(iBase)
    DecompressColor8 color, inColorPacked
    lq inNormal, 50(iBase)
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
    mul.xyz color, color, light
    miniw.xyz color, color, vf00
    div q, vf00[w], vertex[w]
    mul.xyz vertex, vertex, q
    mul.xyz vertex, vertex, scale
    add.xyz vertex, vertex, offset
    move vertex2, vertex3
    move vertex3, vertex
    move.xyz oldvector, vector
    sub.xyz vector, vertex3, vertex2
    opmula.xyz acc, vector, oldvector
    opmsub.xyz crossproduct, oldvector, vector
    mulw.z crossproduct, crossproduct, bfc_multiplier
    fmand z_sign, z_sign_mask
    iaddiu z_sign, z_sign, 0x7fe0
    ior iADC, iADC, z_sign
    mfir.w vertex, iADC
    ftoi4.xy vertex, vertex
    ftoi0.z vertex, vertex
    ; Untextured RGB uses the full 8-bit range; GS alpha uses 0..128.
    loi 255.0
    mul.xyz color, color, i
    loi 128.0
    mul.w color, color, i
    ColorFPtoGsRGBAQ intColor, color
    ; Untextured PRIM: deterministic ST/Q, independent of old VU memory.
    sq vf00, STQ(destAddress)
    sq intColor, RGBA(destAddress)
    sq vertex, XYZ2(destAddress)
    iaddiu iBase, iBase, 1
    iaddiu destAddress, destAddress, 3
    iaddi vertexCounter, vertexCounter, -1
    ibne vertexCounter, vi00, vertexLoop
    xgkick kickAddress
--barrier
--exit
--endexit
.end
