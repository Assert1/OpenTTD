#ifndef LINK3D_H
#define LINK3D_H

#include "../stdafx.h"
#include "../gfx_type.h"

#include <vector>
#include <string>

#define INVALID_LINK	(uint32)(-1)

struct LandTexLink
{
	uint32 image;	// link to texture image
	float coord[4][2];
};

struct ObjectLink
{
	uint32 model;	// link to geometry data
	uint32 image;	// link to texture image
	uint32 shade;	// shading mode (object or land object)
	uint32 slope;	// auto slope mode
	float matr[16];	// geometry transform matrix
	float bbmin[3]; // AABB min (already transformed by the matr)
	float bbmax[3]; // AABB max (already transformed by the matr)
};

struct SpriteLink
{
	LandTexLink land;
	std::vector<ObjectLink> objects;
	float bbmin[3]; // AABB min
	float bbmax[3]; // AABB max
	float length;	// offset of land Z computation
};

extern uint32 GetSpriteLink(uint32 grfid, uint32 ident, SpriteID sprite, PaletteID pal);
extern const SpriteLink *GetSpriteLink(uint32 link);
extern void FreeSpriteLinks();

#endif /* LINK3D_H */