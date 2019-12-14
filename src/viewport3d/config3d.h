#ifndef CONFIG3D_H
#define CONFIG3D_H

#include "../stdafx.h"
#include "../gfx_type.h"

#include <vector>
#include <string>

struct LandTexEntry
{
	std::string image;
	float coord[4][2];
};

struct ObjectEntry
{
	std::string model;
	std::string image;
	uint32 shade;
	uint32 slope;
	float matr[16];
};

struct SpriteEntry
{
	LandTexEntry land;
	std::vector<ObjectEntry> objects;
	float length;
};

extern void LoadConfigFiles();
extern const SpriteEntry *GetSpriteEntry(uint32 grfid, uint32 ident, SpriteID sprite, PaletteID pal);
extern void FreeSprites();

#endif /* CONFIG3D_H */