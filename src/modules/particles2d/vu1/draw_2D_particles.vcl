; AthenaEnv Renderer
;
; Particles2D batches. Source of draw_2D_particles.vsm, which the build
; generates with OpenVCL (see the %.vsm rule of the Makefile); edit this
; file, not the .vsm.
;
; Each particle arrives as its centre, its rotated half extent and its
; colour; VU1 builds the four corners, moves them through the 2D view and
; writes a triangle strip (two XYZ3 then two XYZ2 per particle, as
; draw_2D_tile_list_rotated.vcl), so rotation costs the EE nothing more
; than a sine and a cosine per particle.
;
; Static data, set by athena_emitter2d_draw() before the first batch:
;   0: (Xx, Xy)          first column of the view
;   1: (Yx, Yy)          second column
;   2: (Kx, Ky)          GS position of the world origin
;   3: (u1, v1, u2, v2)  texture rectangle in texels, shared by the batch

VIEW_COL_X           .assign  0
VIEW_COL_Y           .assign  1
VIEW_ORIGIN          .assign  2
TEX_RECT             .assign  3

; Input buffer layout, two quadwords per particle
;   x, y, hc, hs     centre and the half extent along the rotated X axis:
;                    (half * cos(angle), half * sin(angle))
;   r, g, b, a       integer colour, as the GS RGBAQ register takes it
;
; Corners: U = (hc, hs) and V = (-hs, hc) through the view; top-left is
; centre - U - V, top-right centre + U - V, bottom-left centre - U + V and
; bottom-right centre + U + V.
;
; Output buffer layout (GIF PACKED: RGBAQ, then UV and XYZ3/XYZ2 per corner)
;   r, g, b, a
;   u1, v1 | top-left     (XYZ3)
;   u1, v2 | bottom-left  (XYZ3)
;   u2, v1 | top-right    (XYZ2)
;   u2, v2 | bottom-right (XYZ2)

INBUF_SIZE          .assign 89          ; Max particles (44 * 2) + prim tag
OUTBUF_SIZE         .assign 397         ; Max particles (44 * 9) + prim tag

GIFTAG_OFFSET      .assign 0

.syntax new
.name VU1Draw2D_Particles
.vu
.init_vf_all
.init_vi_all

--enter
--endenter
    lq  colX,          VIEW_COL_X(vi00)
    lq  colY,          VIEW_COL_Y(vi00)
    lq  viewOrigin,    VIEW_ORIGIN(vi00)
    lq  texRect,       TEX_RECT(vi00)

    ; The four corners share the batch's texture rectangle.
    ftoi4   uvTL, texRect          ; u1, v1, u2, v2
    mr32    uvRot, uvTL            ; v1, u2, v2, u1
    mr32    uvBR, uvRot            ; u2, v2, u1, v1
    move    uvBL, uvTL             ; u1 ...
    move.y  uvBL, uvBR             ; u1, v2
    move    uvTR, uvBR             ; u2 ...
    move.y  uvTR, uvTL             ; u2, v1

    loi            4095.75
    addi           maxvec,  vf00,       i

    fcset   0x000000

init:
    xtop    iBase
    xitop   particleCount

    lq      primTag,        GIFTAG_OFFSET(iBase)
    iaddiu  iBase,          iBase,          1

    iaddiu    kickAddress,    iBase,  INBUF_SIZE
    iaddiu    destAddress,    kickAddress,  1

    sq primTag,    0(kickAddress)

    ; Set the GifTag EOP bit to 1 and NLOOP to the number of particles
    iaddiu               Mask, particleCount, 0x7fff
    iaddiu               Mask, Mask, 0x01
    isw.x                Mask, 0(kickAddress)

    iadd particleCounter, vi00, particleCount
    particleLoop:
        lq inCenter,   0(iBase)        ; x, y, hc, hs
        lq inColor,    1(iBase)

        ; Centre through the view.
        mulax.xy   ACC,  colX, inCenter
        madday.xy  ACC,  colY, inCenter
        maddw.xy   centre, viewOrigin, vf00

        ; U = X * hc + Y * hs and V = Y * hc - X * hs, in screen units.
        mulaz.xy   ACC,  colX, inCenter
        maddw.xy   edgeU, colY, inCenter
        mulaz.xy   ACC,  colY, inCenter
        msubw.xy   edgeV, colX, inCenter

        sub.zw     pTL, vf00, vf00
        sub.zw     pBL, vf00, vf00
        sub.zw     pTR, vf00, vf00
        sub.zw     pBR, vf00, vf00
        sub.xy     leftCentre,  centre, edgeU
        add.xy     rightCentre, centre, edgeU
        sub.xy     pTL, leftCentre,  edgeV
        add.xy     pBL, leftCentre,  edgeV
        sub.xy     pTR, rightCentre, edgeV
        add.xy     pBR, rightCentre, edgeV

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

        sq inColor,  0(destAddress)
        sq uvTL,     1(destAddress)
        sq pTL,      2(destAddress)
        sq uvBL,     3(destAddress)
        sq pBL,      4(destAddress)
        sq uvTR,     5(destAddress)
        sq pTR,      6(destAddress)
        sq uvBR,     7(destAddress)
        sq pBR,      8(destAddress)

        iaddiu          iBase,        iBase,          2
        iaddiu          destAddress,  destAddress,    9

        iaddi   particleCounter,  particleCounter,  -1
        ibne    particleCounter,  vi00,   particleLoop

    xgkick kickAddress
--barrier
--cont

    b init

--exit
--endexit
        .end
