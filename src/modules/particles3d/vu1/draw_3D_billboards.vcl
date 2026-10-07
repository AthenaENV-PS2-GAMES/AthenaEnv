; AthenaEnv Renderer
;
; Particles3D batches: camera-facing quads. Source of draw_3D_billboards.vsm,
; which the build generates with OpenVCL (see the %.vsm rule of the
; Makefile); edit this file, not the .vsm.
;
; Each particle arrives as its world centre with its half size and its
; colour. VU1 builds the four corners along the camera's right and up axes,
; projects them and writes a triangle strip (two XYZ3, then two XYZ2), as
; draw_2D_particles.vcl. The EE has already dropped particles outside the
; near/far range (all four corners share the view depth); a particle with a
; corner beyond the guard band (four screens) sets ADC on its two XYZ2
; corners, so the GS draws none of it.
;
; Static data, set by athena_emitter3d_draw() before the first batch:
;   0..3: view-projection, Y flipped for the GS (as Render3D)
;   4:    (W/2, H/2, maxZ/2, 0) screen scale
;   5:    camera right axis, unit
;   6:    camera up axis, unit
;   7:    (u1, v1, u2, v2) texture rectangle in texels
;   8:    (0.25, 0.25, 0, 0) guard band scale for the clip test

VIEW_PROJECTION     .assign  0
SCREEN_SCALE        .assign  4
CAMERA_RIGHT        .assign  5
CAMERA_UP           .assign  6
TEX_RECT            .assign  7
GUARD_SCALE         .assign  8

; Input buffer layout, two quadwords per particle
;   x, y, z, half    world centre and half size
;   r, g, b, a       integer colour, as the GS RGBAQ register takes it
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
.name VU1Draw3D_Billboards
.vu
.init_vf_all
.init_vi_all

--enter
--endenter
    lq  vp0,          VIEW_PROJECTION+0(vi00)
    lq  vp1,          VIEW_PROJECTION+1(vi00)
    lq  vp2,          VIEW_PROJECTION+2(vi00)
    lq  vp3,          VIEW_PROJECTION+3(vi00)
    lq  scale,        SCREEN_SCALE(vi00)
    lq  camRight,     CAMERA_RIGHT(vi00)
    lq  camUp,        CAMERA_UP(vi00)
    lq  texRect,      TEX_RECT(vi00)
    lq  guard,        GUARD_SCALE(vi00)

    ; Screen offset: scale plus the GS origin, as Render3D's AddScreenOffset.
    loi            2048.0
    addi.xy        offset, vf00, i
    add.zw         offset, vf00, vf00
    add.xyz        offset, scale, offset

    ; The four corners share the batch's texture rectangle.
    ftoi4   uvTL, texRect          ; u1, v1, u2, v2
    mr32    uvRot, uvTL            ; v1, u2, v2, u1
    mr32    uvBR, uvRot            ; u2, v2, u1, v1
    move    uvBL, uvTL             ; u1 ...
    move.y  uvBL, uvBR             ; u1, v2
    move    uvTR, uvBR             ; u2 ...
    move.y  uvTR, uvTL             ; u2, v1

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
        lq inCenter,   0(iBase)        ; x, y, z, half
        lq inColor,    1(iBase)

        ; Corners in world space: centre -+ right * half -+ up * half.
        mulw.xyz   edgeR, camRight, inCenter
        mulw.xyz   edgeU, camUp,    inCenter
        move.w     worldTL, vf00
        move.w     worldBL, vf00
        move.w     worldTR, vf00
        move.w     worldBR, vf00
        sub.xyz    leftEdge,  inCenter, edgeR
        add.xyz    rightEdge, inCenter, edgeR
        add.xyz    worldTL, leftEdge,  edgeU
        sub.xyz    worldBL, leftEdge,  edgeU
        add.xyz    worldTR, rightEdge, edgeU
        sub.xyz    worldBR, rightEdge, edgeU

        ; Clip space.
        mulax      ACC,  vp0, worldTL
        madday     ACC,  vp1, worldTL
        maddaz     ACC,  vp2, worldTL
        maddw      clipTL, vp3, worldTL
        mulax      ACC,  vp0, worldBL
        madday     ACC,  vp1, worldBL
        maddaz     ACC,  vp2, worldBL
        maddw      clipBL, vp3, worldBL
        mulax      ACC,  vp0, worldTR
        madday     ACC,  vp1, worldTR
        maddaz     ACC,  vp2, worldTR
        maddw      clipTR, vp3, worldTR
        mulax      ACC,  vp0, worldBR
        madday     ACC,  vp1, worldBR
        maddaz     ACC,  vp2, worldBR
        maddw      clipBR, vp3, worldBR

        ; Guard band: |x|, |y| within four times |w| at every corner.
        mul.xyz    guardTL, clipTL, guard
        mul.xyz    guardBL, clipBL, guard
        mul.xyz    guardTR, clipTR, guard
        mul.xyz    guardBR, clipBR, guard
        clipw.xyz  guardTL, clipTL[w]
        clipw.xyz  guardBL, clipBL[w]
        clipw.xyz  guardTR, clipTR[w]
        clipw.xyz  guardBR, clipBR[w]
        fcand      vi01,    0xffffff
        iaddiu     iADC,    vi01,   0x7fff

        ; Perspective divide and screen mapping.
        div        q, vf00[w], clipTL[w]
        mul.xyz    pTL, clipTL, q
        div        q, vf00[w], clipBL[w]
        mul.xyz    pBL, clipBL, q
        div        q, vf00[w], clipTR[w]
        mul.xyz    pTR, clipTR, q
        div        q, vf00[w], clipBR[w]
        mul.xyz    pBR, clipBR, q
        mul.xyz    pTL, pTL, scale
        mul.xyz    pBL, pBL, scale
        mul.xyz    pTR, pTR, scale
        mul.xyz    pBR, pBR, scale
        add.xyz    pTL, pTL, offset
        add.xyz    pBL, pBL, offset
        add.xyz    pTR, pTR, offset
        add.xyz    pBR, pBR, offset

        ; XYZ3 corners never draw; ADC on the XYZ2 ones drops the particle.
        mfir.w     pTL, vi00
        mfir.w     pBL, vi00
        mfir.w     pTR, iADC
        mfir.w     pBR, iADC
        ftoi4.xy   pTL, pTL
        ftoi4.xy   pBL, pBL
        ftoi4.xy   pTR, pTR
        ftoi4.xy   pBR, pBR
        ftoi0.z    pTL, pTL
        ftoi0.z    pBL, pBL
        ftoi0.z    pTR, pTR
        ftoi0.z    pBR, pBR

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
