/* Declarations only: the float code the port has (widescreen, ghost depth)
 * is not linked into the Pocket build, or uses the compiler's soft float. */
#ifndef _POCKET_MATH_H
#define _POCKET_MATH_H
#define M_PI 3.14159265358979323846
float floorf(float); float ceilf(float); float roundf(float); float fabsf(float);
float fminf(float, float); float fmaxf(float, float); float sqrtf(float);
float sinf(float); float cosf(float); float atan2f(float, float);
double floor(double); double ceil(double); double fabs(double); double sqrt(double);
#endif
