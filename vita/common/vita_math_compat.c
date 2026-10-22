/*
 * C23 math functions missing from the VitaSDK newlib, used by simde
 * (simde_math_roundevenf for vrfin / _mm_round_ps nearest).
 *
 * roundeven*: round to integral value, ties to even, independent of the
 * current rounding mode; NaN/Inf returned unchanged; sign of zero preserved.
 */
#include <math.h>

float roundevenf(float x) {
  if (!isfinite(x))
    return x;
  if (fabsf(x) >= 8388608.0f) /* 2^23: already integral */
    return x;
  const float f = floorf(x);
  const float diff = x - f; /* exact: 0 <= diff < 1 */
  float r;
  if (diff > 0.5f)
    r = f + 1.0f;
  else if (diff < 0.5f)
    r = f;
  else
    r = (fmodf(f, 2.0f) == 0.0f) ? f : f + 1.0f;
  return r == 0.0f ? copysignf(0.0f, x) : r;
}

double roundeven(double x) {
  if (!isfinite(x))
    return x;
  if (fabs(x) >= 4503599627370496.0) /* 2^52 */
    return x;
  const double f = floor(x);
  const double diff = x - f;
  double r;
  if (diff > 0.5)
    r = f + 1.0;
  else if (diff < 0.5)
    r = f;
  else
    r = (fmod(f, 2.0) == 0.0) ? f : f + 1.0;
  return r == 0.0 ? copysign(0.0, x) : r;
}
