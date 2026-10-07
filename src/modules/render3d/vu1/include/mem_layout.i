SCREEN_SCALE .assign 0
SCREEN_MATRIX .assign 1
OBJECT_MATRIX .assign 5
CLIPFAN_OFFSET .assign 26
; State carried from one vertex to the next. OpenVCL does not keep values
; live across the loop's back edge (it reused the previous vertex's register
; for the cross product, which broke backface culling on meshes), so the
; loop keeps them in VU memory, set up when each batch starts.
CULL_PREVIOUS_VERTEX .assign 31
CULL_PREVIOUS_EDGE .assign 32
KICK_CYCLE .assign 33
INBUF_SIZE .assign 194
POSITION_OFFSET .assign 2
COLOR_OFFSET .assign 98
