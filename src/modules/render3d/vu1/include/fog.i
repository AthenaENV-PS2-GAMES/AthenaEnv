;//--------------------------------------------------------------------
;// FogStore - converts a projected vertex and stores it at XYZ2(dest), for
;// XYZ2, or for XYZF2 with GS fog. "vertex" holds screen x, y, depth and
;// the clip w; "adc" has the kick bit in bit 15 (other bits are ignored).
;// Fog parameters at FOG_PARAMS: x = enabled (integer), y = fog end,
;// z = 255 / (end - start). F = clamp((end - w) * z, 0, 255), w being the
;// view depth; the GS blends towards FOGCOL as F goes to 0. XYZF2 packed:
;// z in 24 bits shifted by 4, F shifted by 4 in w, next to ADC (bit 15).
;// The w lane is stored from an integer register (isw.w): OpenVCL paired
;// mfir.w with ftoi4.xy on one register in one cycle, where VU1 keeps only
;// the upper instruction's write.
;//--------------------------------------------------------------------
FOG_PARAMS .assign 9
   .macro   FogStore vertex, adc, dest
    iaddiu kickMask, vi00, 0x7fff
    iaddiu kickMask, kickMask, 1
    iand kickBit, \adc, kickMask
    ilw.x fogOn, FOG_PARAMS(vi00)
    ibeq fogOn, vi00, fog_off
    lq.yz fogParams, FOG_PARAMS(vi00)
    subw.y fogValue, fogParams, \vertex[w]
    mulz.y fogValue, fogValue, fogParams[z]
    loi 255.0
    minii.y fogValue, fogValue, i
    maxx.y fogValue, fogValue, vf00[x]
    ftoi4.y fogValue, fogValue
    mtir fogBits, fogValue[y]
    ior kickBit, kickBit, fogBits
    ftoi4.xyz \vertex, \vertex
    b fog_done
fog_off:
    ftoi4.xy \vertex, \vertex
    ftoi0.z \vertex, \vertex
fog_done:
    sq.xyz \vertex, XYZ2(\dest)
    isw.w kickBit, XYZ2(\dest)
   .endm
