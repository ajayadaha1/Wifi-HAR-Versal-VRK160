#pragma once
// ---------------------------------------------------------------------------
// fmath.h - deterministic float helpers for the PL kernels.
//
// Only IEEE-754 basic operations (+ - * / and sqrtf, all correctly rounded in
// both HLS and glibc) are used, plus constant tables, so HLS C-sim, the RTL and
// the host-side g++ golden model (hls/tb/golden_chain) are bit-identical.
// atan2 is a 9th-order minimax polynomial (max error ~1e-5 rad); complex
// exponentials come from the 4096-point COS4K table (phase step 1.5e-3 rad).
// ---------------------------------------------------------------------------
#include "wifi_tables.h"

static const float F_PI = 3.14159265358979f;
static const float F_2PI = 6.28318530717959f;

static inline float f_atan01(float z) {
    const float z2 = z * z;
    return z * (0.9998660f + z2 * (-0.3302995f + z2 * (0.1801410f + z2 * (-0.0851330f + z2 * 0.0208351f))));
}

static inline float f_atan2(float y, float x) {
    const float ax = x < 0.0f ? -x : x;
    const float ay = y < 0.0f ? -y : y;
    if (ax == 0.0f && ay == 0.0f) return 0.0f;
    float a;
    if (ax >= ay) a = f_atan01(ay / ax);
    else a = 0.5f * F_PI - f_atan01(ax / ay);
    if (x < 0.0f) a = F_PI - a;
    if (y < 0.0f) a = -a;
    return a;
}

// wrap to [-pi, pi) (bounded: a - 2*pi*floor((a+pi)/(2*pi)))
static inline float f_wrap(float a) {
    const float t = (a + F_PI) / F_2PI;
    int n = (int)t;
    if ((float)n > t) n--;
    return a - (float)n * F_2PI;
}

// radians -> 32-bit phase (2^32 == 2*pi), round half away from zero, saturated
static inline int f_rad2ph(float r) {
    float v = r * 683565275.576431632f;
    if (v > 2147483520.0f) v = 2147483520.0f;
    if (v < -2147483520.0f) v = -2147483520.0f;
    return (int)(v >= 0.0f ? v + 0.5f : v - 0.5f);
}

// exp(j*2*pi*ph/2^32) from the table
static inline void f_cis(unsigned int ph, float& c, float& s) {
    const unsigned int idx = ph >> 20;
    c = COS4K[idx];
    s = COS4K[(idx - 1024u) & 4095u];
}
