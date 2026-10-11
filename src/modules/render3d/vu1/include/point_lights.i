;//--------------------------------------------------------------------
;// PointLights - adds the point lights to "light" (accumulated RGB) for a
;// unit world normal and an object-space position (w = 1):
;//   light += colour * max(n . l, 0) * (1 - d^2 / range^2)^2, d < range
;// Lights: positions at POINT_LIGHTS (w = 1 / range^2), colours at
;// POINT_LIGHTS + 4, count in 15.z. The object matrix is read from VU
;// memory here (not kept in registers across the vertex loop: OpenVCL
;// does not keep values live across loop back edges), and only when
;// there are point lights.
;//--------------------------------------------------------------------
POINT_LIGHTS .assign 132
   .macro   PointLights light, normal, objectPosition
    ilw.z pointCount, 15(vi00)
    ibeq pointCount, vi00, points_done
    MatrixLoad pointModel, OBJECT_MATRIX, vi00
    MatrixMultiplyVertex pointWorld, pointModel, \objectPosition
    iadd currentPoint, vi00, vi00
pointLoop:
    lq pointPosition, POINT_LIGHTS(currentPoint)
    sub.xyz toLight, pointPosition, pointWorld
    mul.xyz squared, toLight, toLight
    add.x squared, squared, squared[y]
    add.x squared, squared, squared[z]
    rsqrt q, vf00[w], squared[x]
    mulq.xyz toLight, toLight, q
    VectorDotProduct facing, \normal, toLight
    maxx.x facing, facing, vf00
    ; fade = max(1 - d^2 / range^2, 0)^2, in w.
    mulw.x ratio, squared, pointPosition[w]
    subx.w fade, vf00, ratio[x]
    maxx.w fade, fade, vf00
    mul.w fade, fade, fade
    mulw.x facing, facing, fade[w]
    lq pointColor, POINT_LIGHTS+4(currentPoint)
    mulx.xyz pointColor, pointColor, facing[x]
    add.xyz \light, \light, pointColor
    iaddiu currentPoint, currentPoint, 1
    ibne currentPoint, pointCount, pointLoop
points_done:
   .endm
