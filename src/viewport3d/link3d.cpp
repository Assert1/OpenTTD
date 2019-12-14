#include "link3d.h"

#include "math3d.h"
#include "config3d.h"
#include "image3d.h"
#include "model3d.h"

std::vector<SpriteLink> _sprite_link;
std::vector<uint32> _sprite_cache;

static void LinkSprite(SpriteLink *sl, const SpriteEntry *se)
{
	sl->land.image = se->land.image.size() ? LoadImageFile(se->land.image.data()) : INVALID_IMAGE;
	for (int i = 0; i < 4; i++)
	{
		sl->land.coord[i][0] = se->land.coord[i][0];
		sl->land.coord[i][1] = se->land.coord[i][1];
	}
	vectSet3(sl->bbmin, +FLT_MAX);
	vectSet3(sl->bbmax, -FLT_MAX);
	sl->objects.resize(se->objects.size());
	for (size_t i = 0; i < se->objects.size(); i++)
	{
		ObjectLink *ol = &sl->objects[i];
		const ObjectEntry *oe = &se->objects[i];
		ol->model = LoadModelFile(oe->model.data());
		ol->image = LoadImageFile(oe->image.data());
		ol->shade = oe->shade;
		ol->slope = oe->slope;
		matrCopy(ol->matr, oe->matr);

		const ModelEntry *m = GetModelEntry(ol->model);
		if (!m) continue;

		matrTransformAABB(ol->bbmin, ol->bbmax, m->bbmin, m->bbmax, ol->matr);
		vectMin3(sl->bbmin, sl->bbmin, ol->bbmin);
		vectMax3(sl->bbmax, sl->bbmax, ol->bbmax);
	}
	sl->length = se->length;
}

uint32 GetSpriteLink(uint32 grfid, uint32 id, SpriteID s, PaletteID p)
{
	if ((_sprite_cache.size() > s) && (_sprite_cache[s] < _sprite_link.size())) return _sprite_cache[s];

	const SpriteEntry *se = GetSpriteEntry(grfid, id, s, p);
	if (!se) return INVALID_LINK;

	if (s >= _sprite_cache.size())
	{
		size_t size = _sprite_cache.size();
		_sprite_cache.resize(s + 1);
		for (size_t i = size; i <= s; i++) _sprite_cache[i] = (uint32)(-1);
	}
	_sprite_cache[s] = (uint32)(_sprite_link.size());

	_sprite_link.emplace_back();
	SpriteLink *sl = &_sprite_link.back();
	LinkSprite(sl, se);

	return _sprite_cache[s];
}

const SpriteLink *GetSpriteLink(uint32 link)
{
	if (_sprite_cache.size() <= link) return nullptr;
	return &_sprite_link[link];
}

void FreeSpriteLinks()
{
	_sprite_link.clear();
	_sprite_cache.clear();
}
