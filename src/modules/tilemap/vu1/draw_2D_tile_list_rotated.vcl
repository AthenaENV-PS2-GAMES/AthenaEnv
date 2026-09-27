; AthenaEnv Renderer
;
; TileMap sprite batches under a rotating 2D view. Source of
; draw_2D_tile_list_rotated.vsm, which the build generates with OpenVCL (see
; the %.vsm rule of the Makefile); edit this file, not the .vsm.
;
; The GS draws sprites axis-aligned, so each sprite becomes a triangle strip
; of four corners. The first two go to XYZ3 (queued, no drawing) and the last
; two to XYZ2, so every sprite restarts the strip without a PRIM write:
; triangles (TL, BL, TR) and (BL, TR, BR).
;
; Static data, set by athena_tilemap_render() before the first batch: the
; view is screen = X * x + Y * y + K.
;   0: (Xx, Xy)   first column of the view (what one world unit of x moves)
;   1: (Yx, Yy)   second column
;   2: (Kx, Ky)   screen position of the draw origin plus the GS offset

VIEW_COL_X           .assign  0
VIEW_COL_Y           .assign  1
VIEW_ORIGIN          .assign  2

; Input buffer layout (as draw_2D_tile_list.vcl)
; x, y, w, h
; u1, v1, u2, v2
; r, g, b, a
; _, _, zindex, _
;
; Output buffer layout (GIF PACKED: RGBAQ, then UV and XYZ3/XYZ2 per corner)
;   r, g, b, a
;   u1, v1 | x0 y0 (XYZ3)     top-left
;   u1, v2 | x1 y1 (XYZ3)     bottom-left
;   u2, v1 | x2 y2 (XYZ2)     top-right: draws TL, BL, TR
;   u2, v2 | x3 y3 (XYZ2)     bottom-right: draws BL, TR, BR

INBUF_SIZE          .assign 145         ; Max sprites (36 * 4) + prim tag
OUTBUF_SIZE         .assign 325         ; Max sprites (36 * 9) + prim tag

GIFTAG_OFFSET      .assign 0

.syntax new
.name VU1Draw2D_TileListRotated
.vu
.init_vf_all
.init_vi_all

--enter
--endenter
    lq  colX,          VIEW_COL_X(vi00)
    lq  colY,          VIEW_COL_Y(vi00)
    lq  viewOrigin,    VIEW_ORIGIN(vi00)

    fcset   0x000000

init:
    xtop    iBase
    xitop   vertCount

    lq      primTag,        GIFTAG_OFFSET(iBase)
    iaddiu  iBase,          iBase,          1

    iaddiu    kickAddress,    iBase,  INBUF_SIZE
    iaddiu    destAddress,    kickAddress,  1

    sq primTag,    0(kickAddress)

    ; Set the GifTag EOP bit to 1 and NLOOP to the number of sprites
    iaddiu               Mask, vertCount, 0x7fff
    iaddiu               Mask, Mask, 0x01
    isw.x                Mask, 0(kickAddress)

    loi            4095.75
    addi           maxvec,  vf00,       i

    iadd vertexCounter, vi00, vertCount
    vertexLoop:
        lq inPosSize,  0(iBase)
        lq inUVs,      1(iBase)
        lq inColor,    2(iBase)
        lq zindex,     3(iBase)

        ; UVs of the four corners: (u1,v1) (u1,v2) (u2,v1) (u2,v2)
        ftoi4   uvTL, inUVs          ; u1, v1, u2, v2
        mr32    uvRot, uvTL          ; v1, u2, v2, u1
        mr32    uvBR, uvRot          ; u2, v2, u1, v1
        move    uvBL, uvTL           ; u1 ...
        move.y  uvBL, uvBR           ; u1, v2
        move    uvTR, uvBR           ; u2 ...
        move.y  uvTR, uvTL           ; u2, v1

        ; Top-left corner through the view, then the edges w and h
        mulax.xy   ACC,  colX, inPosSize
        madday.xy  ACC,  colY, inPosSize
        maddw.xy   pTL,  viewOrigin, vf00
        mulz.xy    edgeW, colX, inPosSize
        mulw.xy    edgeH, colY, inPosSize

        sub.zw     pTL, vf00, vf00
        sub.zw     pBL, vf00, vf00
        sub.zw     pTR, vf00, vf00
        sub.zw     pBR, vf00, vf00
        add.xy     pBL, pTL, edgeH
        add.xy     pTR, pTL, edgeW
        add.xy     pBR, pTR, edgeH

        maxx.xy    pTL, pTL, vf00
        minix.xy   pTL, pTL, maxvec
        maxx.xy    pBL, pBL, vf00
        minix.xy   pBL, pBL, maxvec
        maxx.xy    pTR, pTR, vf00
        minix.xy   pTR, pTR, maxvec
        maxx.xy    pBR, pBR, vf00
        minix.xy   pBR, pBR, maxvec

        ftoi4   pTL, pTL
        ftoi4   pBL, pBL
        ftoi4   pTR, pTR
        ftoi4   pBR, pBR
        add.z   pTL, pTL, zindex
        add.z   pBL, pBL, zindex
        add.z   pTR, pTR, zindex
        add.z   pBR, pBR, zindex

        sq inColor,  0(destAddress)
        sq uvTL,     1(destAddress)
        sq pTL,      2(destAddress)
        sq uvBL,     3(destAddress)
        sq pBL,      4(destAddress)
        sq uvTR,     5(destAddress)
        sq pTR,      6(destAddress)
        sq uvBR,     7(destAddress)
        sq pBR,      8(destAddress)

        iaddiu          iBase,        iBase,          4
        iaddiu          destAddress,  destAddress,    9

        iaddi   vertexCounter,  vertexCounter,  -1
        ibne    vertexCounter,  vi00,   vertexLoop

    xgkick kickAddress
--barrier
--cont

    b init

--exit
--endexit
        .end
