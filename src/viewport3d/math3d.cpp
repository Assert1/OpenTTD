#include "../stdafx.h"

#include "math3d.h"

float angDiff(float a, float b, float min, float max)
{
	a = angClamp(a, min, max);
	b = angClamp(b, min, max);
	float period = (float)(max - min);
	if (fabsf(a - b) >(float)(period / 2))
	{
		if (a > (float)(min + period / 2))
		{
			return (a - period) - b;
		}
		else
		{
			return a - (b - period);
		}
	}
	else
	{
		return (a - b);
	}
}

float angLerp(float a, float b, float min, float max, float t)
{
	a = angClamp(a, min, max);
	b = angClamp(b, min, max);
	if (a == b) return a;
	if (t == 0.0f) return a;
	if (t == 1.0f) return b;

	float period = (float)(max - min);
	if (fabsf(a - b) > (float)(period / 2))
	{
		if (a > (float)(min + period / 2))
		{
			return angClamp(lerpCkeck(a - period, b, t), min, max);
		}
		else
		{
			return angClamp(lerpCkeck(a, b - period, t), min, max);
		}
	}
	else
	{
		return lerpCkeck(a, b, t);
	}
}

void vectRotateX(float *v, float angle)
{
	float s, c;
	SinCos(angle, s, c);

	float m[16];
	matrSetRotateX(m, angle);

	float tmp[3];
	vectCopy3(tmp, v);
	matrApply33(v, m, tmp);
}

void vectRotateY(float *v, float angle)
{
	float m[16];
	matrSetRotateY(m, angle);

	float tmp[3];
	vectCopy3(tmp, v);
	matrApply33(v, m, tmp);
}

void vectRotateZ(float *v, float angle)
{
	float m[16];
	matrSetRotateZ(m, angle);

	float tmp[3];
	vectCopy3(tmp, v);
	matrApply33(v, m, tmp);
}

void matrInverse43(float *dst, const float *m)
{
	float d = 1.0f / matrDet3(m);
	mval(dst, 0, 0) =  matrDet2(pval(1, 1), pval(2, 1), pval(1, 2), pval(2, 2)) * d;
	mval(dst, 0, 1) = -matrDet2(pval(0, 1), pval(2, 1), pval(0, 2), pval(2, 2)) * d;
	mval(dst, 0, 2) =  matrDet2(pval(0, 1), pval(1, 1), pval(0, 2), pval(1, 2)) * d;
	mval(dst, 1, 0) = -matrDet2(pval(1, 0), pval(2, 0), pval(1, 2), pval(2, 2)) * d;
	mval(dst, 1, 1) =  matrDet2(pval(0, 0), pval(2, 0), pval(0, 2), pval(2, 2)) * d;
	mval(dst, 1, 2) = -matrDet2(pval(0, 0), pval(1, 0), pval(0, 2), pval(1, 2)) * d;
	mval(dst, 2, 0) =  matrDet2(pval(1, 0), pval(2, 0), pval(1, 1), pval(2, 1)) * d;
	mval(dst, 2, 1) = -matrDet2(pval(0, 0), pval(2, 0), pval(0, 1), pval(2, 1)) * d;
	mval(dst, 2, 2) =  matrDet2(pval(0, 0), pval(1, 0), pval(0, 1), pval(1, 1)) * d;
	mval(dst, 3, 0) = -matrDet3(pval(1, 0), pval(2, 0), pval(3, 0), pval(1, 1), pval(2, 1), pval(3, 1), pval(1, 2), pval(2, 2), pval(3, 2)) * d;
	mval(dst, 3, 1) =  matrDet3(pval(0, 0), pval(2, 0), pval(3, 0), pval(0, 1), pval(2, 1), pval(3, 1), pval(0, 2), pval(2, 2), pval(3, 2)) * d;
	mval(dst, 3, 2) = -matrDet3(pval(0, 0), pval(1, 0), pval(3, 0), pval(0, 1), pval(1, 1), pval(3, 1), pval(0, 2), pval(1, 2), pval(3, 2)) * d;
	mval(dst, 0, 3) =  0.0f;
	mval(dst, 1, 3) =  0.0f;
	mval(dst, 2, 3) =  0.0f;
	mval(dst, 3, 3) =  1.0f;
}

void matrInverse44(float *dst, const float *m)
{
	float d = 1.0f / matrDet4(m);
	mval(dst, 0, 0) =  matrDet3(pval(1, 1), pval(2, 1), pval(3, 1), pval(1, 2), pval(2, 2), pval(3, 2), pval(1, 3), pval(2, 3), pval(3, 3)) * d;
	mval(dst, 0, 1) = -matrDet3(pval(0, 1), pval(2, 1), pval(3, 1), pval(0, 2), pval(2, 2), pval(3, 2), pval(0, 3), pval(2, 3), pval(3, 3)) * d;
	mval(dst, 0, 2) =  matrDet3(pval(0, 1), pval(1, 1), pval(3, 1), pval(0, 2), pval(1, 2), pval(3, 2), pval(0, 3), pval(1, 3), pval(3, 3)) * d;
	mval(dst, 0, 3) = -matrDet3(pval(0, 1), pval(1, 1), pval(2, 1), pval(0, 2), pval(1, 2), pval(2, 2), pval(0, 3), pval(1, 3), pval(2, 3)) * d;
	mval(dst, 1, 0) = -matrDet3(pval(1, 0), pval(2, 0), pval(3, 0), pval(1, 2), pval(2, 2), pval(3, 2), pval(1, 3), pval(2, 3), pval(3, 3)) * d;
	mval(dst, 1, 1) =  matrDet3(pval(0, 0), pval(2, 0), pval(3, 0), pval(0, 2), pval(2, 2), pval(3, 2), pval(0, 3), pval(2, 3), pval(3, 3)) * d;
	mval(dst, 1, 2) = -matrDet3(pval(0, 0), pval(1, 0), pval(3, 0), pval(0, 2), pval(1, 2), pval(3, 2), pval(0, 3), pval(1, 3), pval(3, 3)) * d;
	mval(dst, 1, 3) =  matrDet3(pval(0, 0), pval(1, 0), pval(2, 0), pval(0, 2), pval(1, 2), pval(2, 2), pval(0, 3), pval(1, 3), pval(2, 3)) * d;
	mval(dst, 2, 0) =  matrDet3(pval(1, 0), pval(2, 0), pval(3, 0), pval(1, 1), pval(2, 1), pval(3, 1), pval(1, 3), pval(2, 3), pval(3, 3)) * d;
	mval(dst, 2, 1) = -matrDet3(pval(0, 0), pval(2, 0), pval(3, 0), pval(0, 1), pval(2, 1), pval(3, 1), pval(0, 3), pval(2, 3), pval(3, 3)) * d;
	mval(dst, 2, 2) =  matrDet3(pval(0, 0), pval(1, 0), pval(3, 0), pval(0, 1), pval(1, 1), pval(3, 1), pval(0, 3), pval(1, 3), pval(3, 3)) * d;
	mval(dst, 2, 3) = -matrDet3(pval(0, 0), pval(1, 0), pval(2, 0), pval(0, 1), pval(1, 1), pval(2, 1), pval(0, 3), pval(1, 3), pval(2, 3)) * d;
	mval(dst, 3, 0) = -matrDet3(pval(1, 0), pval(2, 0), pval(3, 0), pval(1, 1), pval(2, 1), pval(3, 1), pval(1, 2), pval(2, 2), pval(3, 2)) * d;
	mval(dst, 3, 1) =  matrDet3(pval(0, 0), pval(2, 0), pval(3, 0), pval(0, 1), pval(2, 1), pval(3, 1), pval(0, 2), pval(2, 2), pval(3, 2)) * d;
	mval(dst, 3, 2) = -matrDet3(pval(0, 0), pval(1, 0), pval(3, 0), pval(0, 1), pval(1, 1), pval(3, 1), pval(0, 2), pval(1, 2), pval(3, 2)) * d;
	mval(dst, 3, 3) =  matrDet3(pval(0, 0), pval(1, 0), pval(2, 0), pval(0, 1), pval(1, 1), pval(2, 1), pval(0, 2), pval(1, 2), pval(2, 2)) * d;
};


void matrSetRotateX(float *m, float angle)
{
	matrSetIdentity(m);

	float s, c;
	SinCos(angle, s, c);

	pval(1, 1) = +c;
	pval(2, 1) = +s;
	pval(1, 2) = -s;
	pval(2, 2) = +c;
}

void matrSetRotateY(float *m, float angle)
{
	matrSetIdentity(m);

	float s, c;
	SinCos(angle, s, c);

	pval(0, 0) = +c;
	pval(2, 0) = +s;
	pval(0, 2) = -s;
	pval(2, 2) = +c;
}

void matrSetRotateZ(float *m, float angle)
{
	matrSetIdentity(m);

	float s, c;
	SinCos(angle, s, c);

	pval(0, 0) = +c;
	pval(1, 0) = +s;
	pval(0, 1) = -s;
	pval(1, 1) = +c;
}

void matrSetRotateYXZ(float *m, float euler_x, float euler_y, float euler_z)
{
	matrSetIdentity(m);

	float sin_x, sin_y, sin_z;
	float cos_x, cos_y, cos_z;
	SinCos(euler_x, sin_x, cos_x);
	SinCos(euler_y, sin_y, cos_y);
	SinCos(euler_z, sin_z, cos_z);

	pval(0, 0) = +cos_y * cos_z + sin_x * sin_y * sin_z;
	pval(1, 0) = +cos_y * sin_z - cos_z * sin_x * sin_y;
	pval(2, 0) = +cos_x * sin_y;
	pval(0, 1) = -cos_x * sin_z;
	pval(1, 1) = +cos_x * cos_z;
	pval(2, 1) = +sin_x;
	pval(0, 2) = +cos_y * sin_x * sin_z - cos_z * sin_y;
	pval(1, 2) = -sin_y * sin_z - cos_y * cos_z * sin_x;
	pval(2, 2) = +cos_x * cos_y;
}

void matrSetQuat(float *m, const float *q)
{
	matrSetIdentity(m);

	float xx = 2.0f * q[0] * q[0];
	float yy = 2.0f * q[1] * q[1];
	float zz = 2.0f * q[2] * q[2];

	float xy = q[0] * q[1];
	float zw = q[2] * q[3];
	float xz = q[0] * q[2];
	float yw = q[1] * q[3];
	float yz = q[1] * q[2];
	float xw = q[0] * q[3];

	pval(0, 0) = 1.0f - yy - zz;
	pval(1, 1) = 1.0f - xx - zz;
	pval(2, 2) = 1.0f - xx - yy;
	pval(0, 1) = 2.0f * (xy - zw); pval(0, 2) = 2.0f * (xz + yw);
	pval(1, 0) = 2.0f * (xy + zw); pval(1, 2) = 2.0f * (yz - xw);
	pval(2, 0) = 2.0f * (xz - yw); pval(2, 1) = 2.0f * (yz + xw);
}

void matrSetOrtho(float *m, float left, float right, float bottom, float top, float near, float far)
{
	matrSetIdentity(m);

	pval(0, 0) =  2.0f / (right - left);
	pval(1, 1) =  2.0f / (top - bottom);
	pval(2, 2) = -2.0f / (far - near);
	pval(3, 0) = -(right + left) / (right - left);
	pval(3, 1) = -(top + bottom) / (top - bottom);
	pval(3, 2) = -(far + near) / (far - near);
}

void matrSetFrustum(float *m, float left, float right, float bottom, float top, float near, float far)
{
	matrSetIdentity(m);

	pval(0, 0) =  (2.0f * near) / (right - left);
	pval(2, 0) =  (right + left) / (right - left);
	pval(1, 1) =  (2.0f * near) / (top - bottom);
	pval(2, 1) =  (top + bottom) / (top - bottom);
	pval(2, 2) = -(far + near) / (far - near);
	pval(3, 2) = -(2.0f * far * near) / (far - near);
	pval(2, 3) = -1.0f;
	pval(3, 3) =  0.0f;
}

void matrClipPlane(float *dst, const float *m, const float *a, const float *b, const float *c)
{
	float sa[4];
	float sb[4];
	float sc[4];
	matrApply44(sa, m, a);
	matrApply44(sb, m, b);
	matrApply44(sc, m, c);
	vectScale3(sa, sa, 1.0f / sa[3]);
	vectScale3(sb, sb, 1.0f / sb[3]);
	vectScale3(sc, sc, 1.0f / sc[3]);

	vectSub3(sb, sb, sa);
	vectSub3(sc, sc, sa);
	vectCross3(dst, sb, sc);
	vectNormalize3(dst);
	dst[3] = -vectDot3(dst, sa);
}

void matrTransformAABB(float *dmin, float *dmax, const float *smin, const float *smax, const float *m)
{
	float ps[8][3] =
	{
		{ smin[0], smin[1], smin[2] },
		{ smin[0], smin[1], smax[2] },
		{ smin[0], smax[1], smin[2] },
		{ smax[0], smin[1], smin[2] },
		{ smin[0], smax[1], smax[2] },
		{ smax[0], smax[1], smin[2] },
		{ smax[0], smin[1], smax[2] },
		{ smax[0], smax[1], smax[2] },
	};

	float pd[8][3];
	for (int i = 0; i < 8; i++) matrApply33(pd[i], m, ps[i]);

	vectCopy3(dmin, pd[0]);
	vectCopy3(dmax, pd[0]);
	for (int i = 1; i < 8; i++)
	{
		vectMin3(dmin, dmin, pd[i]);
		vectMax3(dmax, dmax, pd[i]);
	}
}

int frustumTestAABB(const float frustum[6][4], const float *bbmin, const float *bbmax)
{
	int ret = 1;
	for (int i = 0; i < 6; i++)
	{
		float pt1[3] =
		{
			frustum[i][0] > 0.0f ? bbmax[0] : bbmin[0],
			frustum[i][1] > 0.0f ? bbmax[1] : bbmin[1],
			frustum[i][2] > 0.0f ? bbmax[2] : bbmin[2],
		};
		float pt2[3] =
		{
			frustum[i][0] < 0.0f ? bbmax[0] : bbmin[0],
			frustum[i][1] < 0.0f ? bbmax[1] : bbmin[1],
			frustum[i][2] < 0.0f ? bbmax[2] : bbmin[2],
		};
		if (vectDot43(frustum[i], pt1) < 0.0f) return 0;
		if (vectDot43(frustum[i], pt2) < 0.0f) ret = -1;
	}
	return ret;
}

int frustumTestPoint(const float frustum[6][4], const float *point)
{
	for (int i = 0; i < 6; i++)
	{
		if (vectDot43(frustum[i], point) >= 0.0f) continue;
		return 0;
	}
	return 1;
}

int rayTestAABB(const float *point, const float *dir, const float *bbmin, const float *bbmax)
{
	float idir[3];
	vectInv3(idir, dir);

	float dmin[3];
	float dmax[3];
	vectSub3(dmin, bbmin, point);
	vectSub3(dmax, bbmax, point);
	vectMul3(dmin, dmin, idir);
	vectMul3(dmax, dmax, idir);

	float tmin = max(max(min(dmin[0], dmax[0]), min(dmin[1], dmax[1])), min(dmin[2], dmax[2]));
	float tmax = min(min(max(dmin[0], dmax[0]), max(dmin[1], dmax[1])), max(dmin[2], dmax[2]));
	if ((tmax < 0.0f) || (tmin > tmax)) return 0;
	return 1;
}

uint32 murmur3_32(const void *key, size_t len, uint32 seed)
{
	uint32 h = seed;
	uint8 *p = (uint8*)(key);
	if (len > 3)
	{
		size_t i = len >> 2;
		do
		{
			uint32 k = *((uint32*)(p));
			p += sizeof(uint32);
			k *= 0xcc9e2d51;
			k = (k << 15) | (k >> 17);
			k *= 0x1b873593;
			h ^= k;
			h = (h << 13) | (h >> 19);
			h = h * 5 + 0xe6546b64;

		} while (--i);
	}
	if (len & 3)
	{
		size_t i = len & 3;
		uint32 k = 0;
		do
		{
			k <<= 8;
			k |= p[i - 1];

		} while (--i);
		k *= 0xcc9e2d51;
		k = (k << 15) | (k >> 17);
		k *= 0x1b873593;
		h ^= k;
	}
	h ^= len;
	h ^= h >> 16;
	h *= 0x85ebca6b;
	h ^= h >> 13;
	h *= 0xc2b2ae35;
	h ^= h >> 16;
	return h;
}