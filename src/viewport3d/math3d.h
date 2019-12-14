#ifndef MATH3D_H
#define MATH3D_H

#include "viewport3d.h"
#include "../core/math_func.hpp"
#include "../core/mem_func.hpp"
#include <cfloat>
#include <cmath>

#define RAD(x)		(((x) / 180.0f) * (float)(M_PI))
#define GRAD(x)		(((x) / (float)(M_PI)) * 180.0f)

#define FP(x)		((float*)(&(x)))

inline float lerpCkeck(float a, float b, float t)
{
	if (a == b) return a;
	if (t == 0.0f) return a;
	if (t == 1.0f) return b;
	return a * (1.0f - t) + b * t;
}

inline float angClamp(float v, float min, float max)
{
	float period = (max - min);
	if (v <  min)v += (float)(((int)((min - v) / period) + 1) * period);
	if (v >= max)v -= (float)(((int)((v - max) / period) + 1) * period);
	return v;
}

extern float angDiff(float a, float b, float min, float max);
extern float angLerp(float a, float b, float min, float max, float t);

inline void vectSet3(float *dst, float src)
{
	for (int i = 0; i < 3; i++) dst[i] = src;
}

inline void vectMin3(float *dst, const float *a, const float *b)
{
	for (int i = 0; i < 3; i++) dst[i] = min(a[i], b[i]);
}

inline void vectMax3(float *dst, const float *a, const float *b)
{
	for (int i = 0; i < 3; i++) dst[i] = max(a[i], b[i]);
}

inline void vectCopy2(float *dst, const float *src)
{
	for (int i = 0; i < 2; i++) dst[i] = src[i];
}

inline void vectCopy3(float *dst, const float *src)
{
	for (int i = 0; i < 3; i++) dst[i] = src[i];
}

inline void vectCopy4(float *dst, const float *src)
{
	for (int i = 0; i < 4; i++) dst[i] = src[i];
}

inline void vectAdd3(float *dst, const float *a, const float *b)
{
	for (int i = 0; i < 3; i++) dst[i] = a[i] + b[i];
}

inline void vectNeg3(float *dst, const float *src)
{
	for (int i = 0; i < 3; i++) dst[i] = -src[i];
}

inline void vectSub3(float *dst, const float *a, const float *b)
{
	for (int i = 0; i < 3; i++) dst[i] = a[i] - b[i];
}

inline void vectMul3(float *dst, const float *a, const float *b)
{
	for (int i = 0; i < 3; i++) dst[i] = a[i] * b[i];
}

inline void vectScale3(float *dst, const float *a, float s)
{
	for (int i = 0; i < 3; i++) dst[i] = a[i] * s;
}

inline void vectDiv3(float *dst, const float *a, const float *b)
{
	for (int i = 0; i < 3; i++) dst[i] = a[i] / b[i];
}

inline float vectDot3(const float *a, const float *b)
{
	float dst = 0.0f;
	for (int i = 0; i < 3; i++) dst += a[i] * b[i];
	return dst;
}

inline float vectDot43(const float *a, const float *b)
{
	float dst = 0.0f;
	for (int i = 0; i < 3; i++) dst += a[i] * b[i];
	dst += a[3];
	return dst;
}

inline float vectLength3(const float *v)
{
	return sqrt(vectDot3(v, v));
}

inline void vectInv3(float *dst, const float *src)
{
	for (int i = 0; i < 3; i++) dst[i] = 1.0f / src[i];
}

inline void vectNormalize3(float *v)
{
	float s = 1.0f / vectLength3(v);
	for (int i = 0; i < 3; i++) v[i] *= s;
}

inline void vectCross3(float *dst, const float *a, const float *b)
{
	dst[0] = a[1] * b[2] - b[1] * a[2];
	dst[1] = a[2] * b[0] - b[2] * a[0];
	dst[2] = a[0] * b[1] - b[0] * a[1];
}

extern void vectRotateX(float *v, float angle);
extern void vectRotateY(float *v, float angle);
extern void vectRotateZ(float *v, float angle);

inline void vectTriNormal(float *dst, const float *a, const float *b, const float *c)
{
	float sb[3];
	float sc[3];
	vectSub3(sb, b, a);
	vectSub3(sc, c, a);
	vectCross3(dst, sb, sc);
	vectNormalize3(dst);
}

inline void SinCos(float angle, float &s, float &c)
{
	s = sin(angle);
	c = cos(angle);
}

inline void quatSetRotate(float *dst, float x, float y, float z, float angle)
{
	float axis[3] = { x, y, z };
	vectNormalize3(axis);

	float ang2_sin, ang2_cos;
	SinCos(angle/2, ang2_sin, ang2_cos);
	dst[0] = axis[0] * ang2_sin;
	dst[1] = axis[1] * ang2_sin;
	dst[2] = axis[2] * ang2_sin;
	dst[3] = ang2_cos;
}

#define mval(m, x, y)	m[(y) * 4 + (x)]
#define pval(x, y)		m[(y) * 4 + (x)]

inline void matrSetIdentity(float *m)
{
	for (int i = 0; i < 16; i++) m[i] = 0.0f;
	pval(0, 0) = 1.0f;
	pval(1, 1) = 1.0f;
	pval(2, 2) = 1.0f;
	pval(3, 3) = 1.0f;
}

inline void matrCopy(float *dst, const float *src)
{
	for (int i = 0; i < 16; i++) dst[i] = src[i];
}

inline void matrMul(float *m, const float *a, const float *b)
{
	for (int x = 0; x < 4; x++)
	{
		for (int y = 0; y < 4; y++)
		{
			pval(x, y) = 0.0f;
			for (int i = 0; i < 4; i++) pval(x, y) += mval(a, i, y) * mval(b, x, i);
		}
	}
}

inline void matrMul(float *dst, const float *src)
{
	float tmp[16];
	matrMul(tmp, dst, src);
	matrCopy(dst, tmp);
}

inline void matrPreMul(float *dst, const float *src)
{
	float tmp[16];
	matrMul(tmp, src, dst);
	matrCopy(dst, tmp);
}

inline void matrApply22(float *dst, const float *m, const float *v)
{
	for (int y = 0; y < 2; y++)
	{
		dst[y] = 0.0f;
		for (int x = 0; x < 2; x++) dst[y] += pval(x, y) * v[x];
		dst[y] += pval(2, y);
		dst[y] += pval(3, y);
	}
}

inline void matrApply33(float *dst, const float *m, const float *v)
{
	for (int y = 0; y < 3; y++)
	{
		dst[y] = 0.0f;
		for (int x = 0; x < 3; x++) dst[y] += pval(x, y) * v[x];
		dst[y] += pval(3, y);
	}
}

inline void matrApplyNoTrans33(float *dst, const float *m, const float *v)
{
	for (int y = 0; y < 3; y++)
	{
		dst[y] = 0.0f;
		for (int x = 0; x < 3; x++) dst[y] += pval(x, y) * v[x];
	}
}

inline void matrApply43(float *dst, const float *m, const float *v)
{
	for (int y = 0; y < 4; y++)
	{
		dst[y] = 0.0f;
		for (int x = 0; x < 3; x++) dst[y] += pval(x, y) * v[x];
		dst[y] += pval(3, y);
	}
}

inline void matrApply44(float *dst, const float *m, const float *v)
{
	for (int y = 0; y < 4; y++)
	{
		dst[y] = 0.0f;
		for (int x = 0; x < 4; x++) dst[y] += pval(x, y) * v[x];
	}
}

inline float matrDet2(float a1, float b1, float a2, float b2) { return (a1 * b2 - a2 * b1); };
inline float matrDet3(float a1, float b1, float c1, float a2, float b2, float c2, float a3, float b3, float c3) { return (a1 * b2 * c3 + a2 * b3 * c1 + a3 * b1 * c2 - c1 * b2 * a3 - c2 * b3 * a1 - a2 * b1 * c3); };
inline float matrDet4(float a1, float b1, float c1, float d1, float a2, float b2, float c2, float d2, float a3, float b3, float c3, float d3, float a4, float b4, float c4, float d4) { return (d1 * c2 * b3 * a4 - c1 * d2 * b3 * a4 - d1 * b2 * c3 * a4 + b1 * d2 * c3 * a4 + c1 * b2 * d3 * a4 - b1 * c2 * d3 * a4 - d1 * c2 * a3 * b4 + c1 * d2 * a3 * b4 + d1 * a2 * c3 * b4 - a1 * d2 * c3 * b4 - c1 * a2 * d3 * b4 + a1 * c2 * d3 * b4 + d1 * b2 * a3 * c4 - b1 * d2 * a3 * c4 - d1 * a2 * b3 * c4 + a1 * d2 * b3 * c4 + b1 * a2 * d3 * c4 - a1 * b2 * d3 * c4 - c1 * b2 * a3 * d4 + b1 * c2 * a3 * d4 + c1 * a2 * b3 * d4 - a1 * c2 * b3 * d4 - b1 * a2 * c3 * d4 + a1 * b2 * c3 * d4); };
inline float matrDet3(const float *m) { return matrDet3(pval(0, 0), pval(1, 0), pval(2, 0), pval(0, 1), pval(1, 1), pval(2, 1), pval(0, 2), pval(1, 2), pval(2, 2)); };
inline float matrDet4(const float *m) { return matrDet4(pval(0, 0), pval(1, 0), pval(2, 0), pval(3, 0), pval(0, 1), pval(1, 1), pval(2, 1), pval(3, 1), pval(0, 2), pval(1, 2), pval(2, 2), pval(3, 2), pval(0, 3), pval(1, 3), pval(2, 3), pval(3, 3)); };

extern void matrInverse43(float *dst, const float *m);
extern void matrInverse44(float *dst, const float *m);
extern void matrSetRotateX(float *m, float angle);
extern void matrSetRotateY(float *m, float angle);
extern void matrSetRotateZ(float *m, float angle);
extern void matrSetRotateYXZ(float *m, float euler_x, float euler_y, float euler_z);
extern void matrSetQuat(float *m, const float *q);

inline void matrRotateX(float *m, float angle)
{
	float tmp[16];
	matrSetRotateX(tmp, angle);
	matrMul(m, tmp);
}

inline void matrRotateY(float *m, float angle)
{
	float tmp[16];
	matrSetRotateY(tmp, angle);
	matrMul(m, tmp);
}

inline void matrRotateZ(float *m, float angle)
{
	float tmp[16];
	matrSetRotateZ(tmp, angle);
	matrMul(m, tmp);
}

inline void matrScale(float *m, float x, float y, float z)
{
	for (int i = 0; i < 4; i++)
	{
		pval(0, i) *= x;
		pval(1, i) *= y;
		pval(2, i) *= z;
	}
}

inline void matrPreScale(float *m, float x, float y, float z)
{
	for (int i = 0; i < 4; i++)
	{
		pval(i, 0) *= x;
		pval(i, 1) *= y;
		pval(i, 2) *= z;
	}
}

inline void matrTranslate(float *m, float x, float y, float z)
{
	for (int i = 0; i < 4; i++)
	{
		pval(3, i) += pval(0, i) * x;
		pval(3, i) += pval(1, i) * y;
		pval(3, i) += pval(2, i) * z;
	}
//	for (int i = 0; i < 4; i++) pval(3, i) += pval(0, i) * x + pval(1, i) * y + pval(2, i) * z;
}

inline void matrPreTranslate(float *m, float x, float y, float z)
{
	for (int i = 0; i < 4; i++)
	{
		pval(i, 0) += pval(i, 3) * x;
		pval(i, 1) += pval(i, 3) * y;
		pval(i, 2) += pval(i, 3) * z;
	}
/*
	for (int i = 0; i < 4; i++)
	{
		pval(i, 0) += pval(3, i) * x;
		pval(i, 1) += pval(3, i) * y;
		pval(i, 2) += pval(3, i) * z;
	}
/**/
}

extern void matrSetOrtho(float *m, float left, float right, float bottom, float top, float near, float far);
extern void matrSetFrustum(float *m, float left, float right, float bottom, float top, float nearZ, float farZ);

extern void matrClipPlane(float *dst, const float *m, const float *a, const  float *b, const float *c);
extern void matrTransformAABB(float *dmin, float *dmax, const float *smin, const float *smax, const float *m);

extern int frustumTestAABB(const float frustum[6][4], const float *bbmin, const float *bbmax);
extern int frustumTestPoint(const float frustum[6][4], const float *point);

extern int rayTestAABB(const float *point, const float *dir, const float *bbmin, const float *bbmax);

extern uint32 murmur3_32(const void *key, size_t len, uint32 seed);

#endif /* MATH3D_H */
