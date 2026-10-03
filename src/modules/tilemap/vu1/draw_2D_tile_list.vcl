; 2025 - Daniel Santos
; AthenaEnv Renderer
;
; TileMap sprite batches. Source of draw_2D_tile_list.vsm, which the build
; generates with OpenVCL (see the %.vsm rule of the Makefile); edit this
; file, not the .vsm.
;
; Static data, set by athena_tilemap_render() before the first batch: the 2D
; view without rotation (athena/graphics/view.h) is screen = S * world + T,
; so every position and size is scaled by S and positions are moved by K,
; the screen position of the draw origin plus the GS drawing offset.

VIEW_SCALE           .assign  0 ; (Sx, Sy, Sx, Sy)
VIEW_ORIGIN          .assign  1 ; (Kx, Ky)

; Dynamic Data
;
; Input buffer layout
; x, y, w, h
; u1, v1, u2, v2
; r, g, b, a
; _, _, zindex, _
;
; Output buffer layout (GIF PACKED: RGBAQ, UV, XYZ2, UV, XYZ2)
;   r,   g,      b, a
;  u1,  v1,      0, 0
;   x,   y, zindex, 0
;  u2,  v2,      0, 0
; x+w, y+h, zindex, 0

INBUF_SIZE          .assign 201         ; Max NbrVerts (50 * 4) + prim tag
OUTBUF_SIZE         .assign 251         ; Max NbrVerts (50 * 5) + prim tag

GIFTAG_OFFSET      .assign 0

RGBA .assign 0
UV1  .assign 1
POS  .assign 2
UV2  .assign 3
SIZE .assign 4

POS_SIZE_OFFSET .assign 0
UVS_OFFSET      .assign 1
COLOR_OFFSET    .assign 2
ZINDEX_OFFSET   .assign 3

.syntax new
.name VU1Draw2D_TileList
.vu
.init_vf_all
.init_vi_all

--enter
--endenter
    lq  viewScale,     VIEW_SCALE(vi00)
    lq  viewOrigin,    VIEW_ORIGIN(vi00)

    fcset   0x000000

init:
    xtop    iBase
    xitop   vertCount

    lq      primTag,        GIFTAG_OFFSET(iBase) ; GIF tag - tell GS how many data we will send
    iaddiu  iBase,          iBase,          1

    iaddiu    kickAddress,    iBase,  INBUF_SIZE       ; pointer for XGKICK
    iaddiu    destAddress,    kickAddress,  1       ; helper pointer for data inserting

    sq primTag,    0(kickAddress) ; prim + tell gs how many data will be

    ; Set the GifTag EOP bit to 1 and NLOOP to the number of vertices
    iaddiu               Mask, vertCount, 0x7fff
    iaddiu               Mask, Mask, 0x01
    isw.x                Mask, 0(kickAddress)

    loi            4095.75
    addi           maxvec,  vf00,       i

    iadd vertexCounter, vi00, vertCount ; loop vertCount times
    vertexLoop:
        lq inPosSize,  POS_SIZE_OFFSET(iBase)
        lq inUVs,      UVS_OFFSET(iBase)
        lq inColor,    COLOR_OFFSET(iBase)
        lq zindex,     ZINDEX_OFFSET(iBase)

        ftoi4       inUVs, inUVs

        mr32 inUV2, inUVs ; yzwx
        mr32 inUV2, inUV2 ; zwxy

        ; (x, y, w, h) through the view scale at once
        mul scaled, inPosSize, viewScale

        sub inPos, vf00, vf00
        add.xy inPos, scaled, viewOrigin

        mr32 inSize, scaled ; yzwx
        mr32 inSize, inSize ; zwxy

        ; The far corner comes from the unclamped near one.
        sub farPos, vf00, vf00
        add.xy farPos, inPos, inSize

        maxx.xy      inPos, inPos,    vf00
        minix.xy     inPos, inPos, maxvec
        maxx.xy      farPos, farPos,  vf00
        minix.xy     farPos, farPos, maxvec

        ftoi4       inPos, inPos
        add.z  inPos, inPos, zindex
        ftoi4       farPos, farPos
        add.z  farPos, farPos, zindex

        sq inColor,      RGBA(destAddress)
        sq inUVs,        UV1(destAddress)
        sq inPos,        POS(destAddress)
        sq inUV2,        UV2(destAddress)
        sq farPos,       SIZE(destAddress)

        iaddiu          iBase,        iBase,          4
        iaddiu          destAddress,  destAddress,    5

        iaddi   vertexCounter,  vertexCounter,  -1	; decrement the loop counter
        ibne    vertexCounter,  vi00,   vertexLoop	; and repeat if needed

    xgkick kickAddress ; dispatch to the GS rasterizer.
--barrier
--cont

    b init

--exit
--endexit
        .end
