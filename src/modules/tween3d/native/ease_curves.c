#include <math.h>
#include <string.h>
#include <athena/ease_curves.h>
#define PI_F 3.14159265358979323846f
#define BACK 1.70158f
#define BACK_IN_OUT (1.70158f*1.525f)
static const char *const families[]={"Quad","Cubic","Quart","Quint","Sine","Expo","Circ","Back","Elastic","Bounce"};
static float bounce_out(float t) {
    const float n=7.5625f,d=2.75f;
    if(t<1/d) return n*t*t;
    if(t<2/d) { t-=1.5f/d; return n*t*t+.75f; }
    if(t<2.5f/d) { t-=2.25f/d; return n*t*t+.9375f; }
    t-=2.625f/d; return n*t*t+.984375f;
}
/* Unclamped in-curve of a family; back uses `overshoot`. */
static float curve_in(int family,float t,float overshoot) {
    switch(family) {
        case 0: return t*t;
        case 1: return t*t*t;
        case 2: return t*t*t*t;
        case 3: return t*t*t*t*t;
        case 4: return 1-cosf(t*PI_F/2);
        case 5: return t==0?0:powf(2,10*t-10);
        case 6: return 1-sqrtf(1-t*t);
        case 7: return t*t*((overshoot+1)*t-overshoot);
        case 8: {
            /* Amplitude 1, period 0.3: s = period / 4. */
            if(t==0||t==1) return t;
            const float period=.3f,s=period/4;
            return -(powf(2,10*(t-1))*sinf((t-1-s)*2*PI_F/period));
        }
        default: return 1-bounce_out(1-t);
    }
}
float athena_ease(AthenaEaseCurve curve,float t) {
    t=t<0?0:t>1?1:t;
    if(curve<=ATHENA_EASE_LINEAR||curve>=ATHENA_EASE_COUNT) return t;
    int family=(curve-1)/3,form=(curve-1)%3;
    if(form==0) return curve_in(family,t,BACK);
    if(form==1) return 1-curve_in(family,1-t,BACK);
    float overshoot=family==7?BACK_IN_OUT:BACK;
    return t<.5f?curve_in(family,2*t,overshoot)/2:1-curve_in(family,2-2*t,overshoot)/2;
}
int athena_ease_find(const char *name) {
    if(!name) return -1;
    if(!strcmp(name,"linear")) return ATHENA_EASE_LINEAR;
    /* easeInOutBack -> inOutBack */
    char short_name[32];
    if(!strncmp(name,"ease",4)&&name[4]>='A'&&name[4]<='Z'&&strlen(name)<sizeof(short_name)) {
        strcpy(short_name,name+4); short_name[0]+='a'-'A'; name=short_name;
    }
    static const char *const forms[]={"in","out","inOut"};
    for(int f=0;f<10;f++) for(int k=0;k<3;k++) {
        size_t n=strlen(forms[k]);
        if(!strncmp(name,forms[k],n)&&!strcmp(name+n,families[f])) return 1+f*3+k;
    }
    return -1;
}
const char *athena_ease_name(AthenaEaseCurve curve) {
    static char names[ATHENA_EASE_COUNT][16];
    if(curve<=ATHENA_EASE_LINEAR||curve>=ATHENA_EASE_COUNT) return "linear";
    if(!names[curve][0]) {
        static const char *const forms[]={"in","out","inOut"};
        strcpy(names[curve],forms[(curve-1)%3]); strcat(names[curve],families[(curve-1)/3]);
    }
    return names[curve];
}
