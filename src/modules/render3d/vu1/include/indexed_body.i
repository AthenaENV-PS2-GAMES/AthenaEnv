; Indexed batch: transform/shade each local vertex once, then emit corners.
; The two TOP buffers never overlap: max 24 vertices / 48 corners uses 411
; qwords, less than OFFSET=420. Morph uses 16 vertices / 24 corners (331 qw).
--enter
--endenter
    MatrixLoad ScreenMatrix, SCREEN_MATRIX, vi00
    MatrixLoad ObjectMatrix, OBJECT_MATRIX, vi00
    MatrixMultiply ObjectToScreen, ObjectMatrix, ScreenMatrix
    MatrixLoad NormalMatrix, 27, vi00
    ilw.w dirLightCount, 15(vi00)
    ilw.z lightingEnabled, CLIPFAN_OFFSET(vi00)
    ilw.y textured, CLIPFAN_OFFSET(vi00)
    lq scale, SCREEN_SCALE(vi00)
    AddScreenOffset scale
    lq.w bfc_multiplier, CLIPFAN_OFFSET(vi00)
    ftoi0.w bfc_sign_mask, bfc_multiplier
    mtir z_sign_mask, bfc_sign_mask[w]
    ibeq z_sign_mask, vi00, ignore_face_culling
    iaddiu z_sign_mask, vi00, 0x20
ignore_face_culling:
    xtop batchBase
    iadd iBase, batchBase, vi00
    ilw.x vertexCounter, 1(batchBase)
    iaddiu cacheAddress, batchBase, INDEXED_CACHE
uniqueLoop:
    lq vertex, 2(iBase)
    DecompressPositionW vertex
    lq inNormal, 26(iBase)
    IndexedDeform vertex, inNormal, iBase
    move worldPosition, vertex
    MatrixMultiplyVertex vertex, ObjectToScreen, vertex
    lq inColorPacked, 50(iBase)
    DecompressColor8 color, inColorPacked
    ibeq lightingEnabled, vi00, shading_done
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
    move objectPosition, worldPosition
    PointLights light, normal, objectPosition
    mul.xyz color, color, light
    miniw.xyz color, color, vf00
shading_done:
    div q, vf00[w], vertex[w]
    waitq
    move stq, vf00
    ibeq textured, vi00, uv_done
    lq inUV, 74(iBase)
    mul.xy stq, inUV, q
    addq.z stq, vf00, q
uv_done:
    mul.xyz vertex, vertex, q
    mul.xyz vertex, vertex, scale
    add.xyz vertex, vertex, offset
    lq.x rgbScale, CLIPFAN_OFFSET(vi00)
    mulx.xyz color, color, rgbScale
    loi 128.0
    mul.w color, color, i
    ColorFPtoGsRGBAQ intColor, color
    sq vertex, 0(cacheAddress)
    sq intColor, 1(cacheAddress)
    sq stq, 2(cacheAddress)
    iaddiu cacheAddress, cacheAddress, 3
    iaddiu iBase, iBase, 1
    iaddi vertexCounter, vertexCounter, -1
    ibne vertexCounter, vi00, uniqueLoop
    ; Vertex clip rejection is unnecessary here: the EE bounds check proved
    ; the entire batch inside the GS guard band and near/far planes.
    sq vf00, CULL_PREVIOUS_VERTEX(vi00)
    sq vf00, CULL_PREVIOUS_EDGE(vi00)
    iaddiu kickPattern, vi00, 0x4000
    iadd kickPattern, kickPattern, kickPattern
    isw.x kickPattern, KICK_CYCLE(vi00)
    isw.y kickPattern, KICK_CYCLE(vi00)
    isw.z vi00, KICK_CYCLE(vi00)
    xitop cornerCounter
    lq primTag, 0(batchBase)
    iaddiu kickAddress, batchBase, INDEXED_OUTPUT
    iaddiu destAddress, kickAddress, 1
    sq primTag, 0(kickAddress)
    iaddiu Mask, cornerCounter, 0x7fff
    iaddiu Mask, Mask, 1
    isw.x Mask, 0(kickAddress)
    iaddiu indexAddress, batchBase, 98
    fcset 0
cornerLoop:
    ilw.x index, 0(indexAddress)
    iadd cacheOffset, index, index
    iadd cacheOffset, cacheOffset, index
    iadd cacheAddress, batchBase, cacheOffset
    lq vertex, INDEXED_CACHE(cacheAddress)
    lq intColor, INDEXED_CACHE+1(cacheAddress)
    lq stq, INDEXED_CACHE+2(cacheAddress)
    iaddiu iADC, vi00, 0x7fff
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
    sq.xyz stq, STQ(destAddress)
    sq intColor, RGBA(destAddress)
    FogStore vertex, iADC, destAddress
    iaddiu indexAddress, indexAddress, 1
    iaddiu destAddress, destAddress, 3
    iaddi cornerCounter, cornerCounter, -1
    ibne cornerCounter, vi00, cornerLoop
    xgkick kickAddress
--barrier
--exit
--endexit
.end
