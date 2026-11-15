/* kosz.c — saját lebegőpontos matematika (nincs libc) */
#include "kernel.h"

static float normalize(float x) {
    while (x > 3.14159265f) x -= 6.28318531f;
    while (x < -3.14159265f) x += 6.28318531f;
    return x;
}

float kosz_sinf(float x) {
    x = normalize(x);
    /* Taylor-sor 7 tagban */
    float x2 = x * x;
    return x * (1.0f - x2 / 6.0f * (1.0f - x2 / 20.0f * (1.0f - x2 / 42.0f * (1.0f - x2 / 72.0f))));
}

float kosz_cosf(float x) {
    x = normalize(x);
    float x2 = x * x;
    return 1.0f - x2 / 2.0f * (1.0f - x2 / 12.0f * (1.0f - x2 / 30.0f * (1.0f - x2 / 56.0f)));
}
