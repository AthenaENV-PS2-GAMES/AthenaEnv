; Opaque texture MODULATE with perspective-correct ST/Q. Interior DIFFUSE
; shades in VU1; clipped vertices carry interpolated UV and pre-lit colors.
.syntax new
.name VU1Draw3DTexture
.vu
.init_vf_all
.init_vi_all
.include "src/modules/render3d/vu1/include/mem_layout.i"
.include "src/modules/render3d/vu1/include/athena_consts.i"
.include "src/modules/render3d/vu1/include/athena_macros.i"
.include "src/modules/render3d/vu1/include/vcl_sml.i"
.include "src/modules/render3d/vu1/include/fog.i"
.include "src/modules/render3d/vu1/include/point_lights.i"

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
    ; (1/guard, 1/guard, 1, 1), guard from SCREEN_SCALE.w.
    addw.xy guardScale, vf00, scale[w]
    maxw.zw guardScale, vf00, vf00[w]
    lq.w bfc_multiplier, CLIPFAN_OFFSET(vi00)
    ilw.y pretransformed, CLIPFAN_OFFSET(vi00)
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
    ; Rejection against the guard band: x and y scaled by 1/guard, so
    ; triangles crossing only the screen edges are drawn and trimmed by the
    ; GS scissor. One full-width mul: OpenVCL paired a mulw.xy and a
    ; move.zw writing the same register in one cycle, where VU1 keeps only
    ; the upper result (z and w then came from the previous colour).
    mul guarded, vertex, guardScale
    clipw.xyz guarded, guarded[w]
    fcand vi01, 0x3ffff
    iaddiu iADC, vi01, 0x7fff
    b project_vertex
clipped_vertex:
    iaddiu iADC, vi00, 0x7fff
project_vertex:
    lq inColorPacked, COLOR_OFFSET(iBase)
    DecompressColor8 color, inColorPacked
    ibeq lightingEnabled, vi00, shading_done
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
    lq objectPosition, POSITION_OFFSET(iBase)
    DecompressPositionW objectPosition
    PointLights light, normal, objectPosition
    mul.xyz color, color, light
    miniw.xyz color, color, vf00
shading_done:
    lq stq, 146(iBase)
    addw.z stq, vf00, vf00
    div q, vf00[w], vertex[w]
    mul.xyz stq, stq, q
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
    ; GS MODULATE uses 128 as neutral RGB (texture * color / 128).
    loi 128.0
    mul.xyz color, color, i
    loi 128.0
    mul.w color, color, i
    ColorFPtoGsRGBAQ intColor, color
    sq.xyz stq, STQ(destAddress)
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
