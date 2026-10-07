#ifndef ATHENA_EASE_CURVES_H
#define ATHENA_EASE_CURVES_H
/* The curves of the JavaScript Ease module in C, same names and formulas
 * (Penner / easings.net): f(0) = 0, f(1) = 1, t clamped to [0, 1]; back and
 * elastic overshoot in between. */
typedef enum {
    ATHENA_EASE_LINEAR=0,
    /* in, out, inOut for each family, in this order. */
    ATHENA_EASE_IN_QUAD, ATHENA_EASE_OUT_QUAD, ATHENA_EASE_IN_OUT_QUAD,
    ATHENA_EASE_IN_CUBIC, ATHENA_EASE_OUT_CUBIC, ATHENA_EASE_IN_OUT_CUBIC,
    ATHENA_EASE_IN_QUART, ATHENA_EASE_OUT_QUART, ATHENA_EASE_IN_OUT_QUART,
    ATHENA_EASE_IN_QUINT, ATHENA_EASE_OUT_QUINT, ATHENA_EASE_IN_OUT_QUINT,
    ATHENA_EASE_IN_SINE, ATHENA_EASE_OUT_SINE, ATHENA_EASE_IN_OUT_SINE,
    ATHENA_EASE_IN_EXPO, ATHENA_EASE_OUT_EXPO, ATHENA_EASE_IN_OUT_EXPO,
    ATHENA_EASE_IN_CIRC, ATHENA_EASE_OUT_CIRC, ATHENA_EASE_IN_OUT_CIRC,
    ATHENA_EASE_IN_BACK, ATHENA_EASE_OUT_BACK, ATHENA_EASE_IN_OUT_BACK,
    ATHENA_EASE_IN_ELASTIC, ATHENA_EASE_OUT_ELASTIC, ATHENA_EASE_IN_OUT_ELASTIC,
    ATHENA_EASE_IN_BOUNCE, ATHENA_EASE_OUT_BOUNCE, ATHENA_EASE_IN_OUT_BOUNCE,
    ATHENA_EASE_COUNT
} AthenaEaseCurve;
float athena_ease(AthenaEaseCurve curve,float t);
/* Short ("outBack") or long ("easeOutBack") name; -1 when unknown. */
int athena_ease_find(const char *name);
const char *athena_ease_name(AthenaEaseCurve curve);
#endif
