#include "config3d.h"

#include "../ini_type.h"
#include "../fileio_func.h"
#include "../direction_type.h"
#include "math3d.h"

#include <png.h>
#include <setjmp.h>
#include "../blitter/opengl.hpp"
#include "../spritecache.h"

#include "../table/sprites.h"
#include "../table/train_cmd.h"
#include "../slope_type.h"

#include <map>

#define INVALID_PALETTE		(uint32)(-1)
#define	INVALID_SPRITE		(uint32)(-1)
#define INVALID_IDENT		(uint32)(-1)

typedef std::map<uint32, SpriteEntry> PalEntry; // map by palette id

struct ScopeEntry
{
	std::vector<PalEntry> entrys;
	std::vector<uint32> sprites;
	std::vector<uint32> idents;
};

static std::map<uint32, ScopeEntry> _scope; // map by GRFIDs

void SetSpriteRange(ScopeEntry *scope, uint32 from, uint32 count, uint32 step, uint32 entry)
{
	uint32 to = from + count * step - 1;
	if (scope->sprites.size() <= to)
	{
		size_t s = scope->sprites.size();
		scope->sprites.resize(to + 1);
		for (size_t i = s; i < scope->sprites.size(); i++) scope->sprites[i] = INVALID_IDENT;
	}
	for (size_t i = 0; i < count; i++) scope->sprites[from + i * step] = entry;
}

void SetIdentRange(ScopeEntry *scope, uint32 from, uint32 count, uint32 step, uint32 entry)
{
	uint32 to = from + count * step - 1;
	if (scope->idents.size() <= to)
	{
		size_t s = scope->idents.size();
		scope->idents.resize(to + 1);
		for (size_t i = s; i < scope->idents.size(); i++) scope->idents[i] = INVALID_IDENT;
	}
	for (size_t i = 0; i < count; i++) scope->idents[from + i * step] = entry;
}

static bool IsSeparator(char c) { return (c == '\\') || (c == '/'); }

class ConfigScanner: public FileScanner
{
public:
	bool AddFile(const char *filename, size_t basepath_length, const char *tar_filename)
	{
		IniFile ini;
		ini.LoadFromDisk(filename + basepath_length, subdir);
		if (!ini.group) return false;

		const char *pe = filename + strlen(filename) - 1;
		while ((pe > filename) && !IsSeparator(pe[0])) pe--;
		pe++;

		std::string rel_path(filename + basepath_length, pe - filename - basepath_length);
		for (IniGroup *g = ini.group; g; g = g->next)
		{
			uint32 grfid = 0;
			{
				IniItem *i_grfid = g->GetItem("grfid", false);
				if (!i_grfid || !i_grfid->value) continue;

				uint8 b_grfid[4];
				if (sscanf(i_grfid->value, " 0x%08X", (uint32*)(&b_grfid[0])) != 1)
				{
					if (sscanf(i_grfid->value, " %c%c%c%c", &b_grfid[3], &b_grfid[2], &b_grfid[1], &b_grfid[0]) != 4)
					{
						error("Config INI '%s' error: at group '%s', invalid GRFID format, use 0xXXXXXXXX hex, or 4 ASCII chars ident.", filename + basepath_length, g->name);
						continue;
					}
				}
				grfid = *((uint32*)(b_grfid));
			}

			int rebase = 0;
			{
				IniItem *rebase_i = g->GetItem("rebase", false);
				if (rebase_i && rebase_i->value)
				{
					if (sscanf(rebase_i->value, " %i", &rebase) != 1)
					{
						error("Config INI '%s' error: at group '%s', invalid 'rebase' item format, use integer.", filename + basepath_length, g->name);
					}
				}
			}

			ScopeEntry *scope = &_scope[grfid];
			uint32 entry = (uint32)(scope->entrys.size());
			scope->entrys.emplace_back();
			{
				IniItem *sprite = g->GetItem("sprite", false);
				if (sprite && sprite->value)
				{
					const char *s = sprite->value;
					while (s[0])
					{
						uint32 sprite1 = 0;
						uint32 sprite2 = 0;
						uint32 sprite3 = 0;
						switch (sscanf(s, " %i : %i : %i", &sprite1, &sprite2, &sprite3))
						{
						case 1: SetSpriteRange(scope, sprite1 + rebase, 1, 1, entry); break;
						case 2: SetSpriteRange(scope, sprite1 + rebase, sprite2, 1, entry); break;
						case 3: SetSpriteRange(scope, sprite1 + rebase, sprite2, sprite3, entry); break;
						default:
							error("Config INI '%s' error: at group '%s', invalid 'sprite' item format near '%s', use id[[:count]:step].", filename + basepath_length, g->name, s);
							break;
						};
						while (s[0] && (s[0] != ',')) s++;
						if (!s[0]) break;
						s++;
					}
				}
			}
			{
				IniItem *ident = g->GetItem("ident", false);
				if (ident && ident->value)
				{
					const char *s = ident->value;
					while (s[0])
					{
						uint32 ident1 = 0;
						uint32 ident2 = 0;
						uint32 ident3 = 0;
						switch (sscanf(s, " %i : %i : %i", &ident1, &ident2, &ident3))
						{
						case 1: SetIdentRange(scope, ident1 + rebase, 1, 1, entry); break;
						case 2: SetIdentRange(scope, ident1 + rebase, ident2, 1, entry); break;
						case 3: SetIdentRange(scope, ident1 + rebase, ident2, ident3, entry); break;
						default:
							error("Config INI '%s' error: at group '%s', invalid 'ident' item format near '%s', use id[[:count]:step].", filename + basepath_length, g->name, s);
							break;
						};
						while (s[0] && (s[0] != ',')) s++;
						if (!s[0]) break;
						s++;
					}
				}
			}
			uint32 pal = INVALID_PALETTE;
			{
				IniItem *palette = g->GetItem("palette", false);
				if (palette && palette->value)
				{
					if ((sscanf(palette->value, " 0x%08X", &pal) != 1) && (sscanf(palette->value, " %i", &pal) != 1))
					{
						error("Config INI '%s' error: at group '%s', invalid 'palette' item format, use integer.", filename + basepath_length, g->name);
					}
				}
			}

			SpriteEntry *se = &scope->entrys[entry][pal];
			{
				IniItem *land_image = g->GetItem("land_image", false);
				if (land_image && land_image->value) se->land.image = IsSeparator(land_image->value[0]) ? land_image->value : (rel_path + land_image->value);

				se->land.coord[0][0] = 0.0f;
				se->land.coord[0][1] = 0.0f;
				se->land.coord[1][0] = 1.0f;
				se->land.coord[1][1] = 0.0f;
				se->land.coord[2][0] = 1.0f;
				se->land.coord[2][1] = 1.0f;
				se->land.coord[3][0] = 0.0f;
				se->land.coord[3][1] = 1.0f;
				se->length = TILE_SIZE / 4.0f - 1.0f;

				IniItem *land_coord = g->GetItem("land_coord", false);
				if (land_coord && land_coord->value)
				{
					if (sscanf(land_coord->value, " [ %f %f ] [ %f %f ] [ %f %f ] [ %f %f ]",
							   &se->land.coord[0][0], &se->land.coord[0][1],
							   &se->land.coord[1][0], &se->land.coord[1][1],
							   &se->land.coord[2][0], &se->land.coord[2][1],
							   &se->land.coord[3][0], &se->land.coord[3][1]) != 8)
					{
						error("Config INI '%s' error: at group '%s', invalid 'land_coord' item format, use [ xN yN ] [ xW yW ] [ xS yS ] [ xE yE ].", filename + basepath_length, g->name);
					}
				}

				IniItem *land_rotate = g->GetItem("land_rotate", false);
				if (land_rotate && land_rotate->value)
				{
					float angle = 0.0f;
					if (sscanf(land_rotate->value, " %f", &angle) != 1)
					{
						error("Config INI '%s' error: at group '%s', invalid 'land_rotate' item format, use float.", filename + basepath_length, g->name);
					}

					float matr[16];
					matrSetRotateZ(matr, RAD(angle));
					matrPreTranslate(matr, 0.5f, 0.5f, 0.0f);
					matrTranslate(matr, -0.5f, -0.5f, 0.0f);

					for (int i = 0; i < 4; i++)
					{
						float tmp[2];
						matrApply22(tmp, matr, se->land.coord[i]);
						vectCopy2(se->land.coord[i], tmp);
					}
				}

				IniItem *veh_length = g->GetItem("veh_length", false);
				if (veh_length && veh_length->value)
				{
					if (sscanf(veh_length->value, " %f", &se->length) != 1)
					{
						error("Config INI '%s' error: at group '%s', invalid 'veh_length' item format, ise float.", filename + basepath_length, g->name);
					}
				}
			}

			for (IniItem *i = g->item; i; i = i->next)
			{
				if (strcmp(i->name, "object") || !i->value) continue;

				IniGroup *object = ini.GetGroup(i->value, 0, false);
				if (!object)
				{
					error("Config INI '%s' error: at group '%s', object '%s' not found.", filename + basepath_length, g->name, i->value);
					continue;
				}

				IniItem *model = object->GetItem("model", false);
				if (!model || !model->value)
				{
					error("Config INI '%s' error: at group '%s', object '%s', 'model' item missing.", filename + basepath_length, g->name, object->name);
					continue;
				}

				se->objects.emplace_back();
				ObjectEntry *oe = &se->objects.back();
				oe->model = IsSeparator(model->value[0]) ? model->value : (rel_path + model->value);
				oe->shade = 0;
				oe->slope = 0;

				IniItem *image = object->GetItem("image", false);
				if (image && image->value) oe->image = IsSeparator(image->value[0]) ? image->value : (rel_path + image->value);

				IniItem *shade = object->GetItem("shade", false);
				if (shade && shade->value)
				{
					if (!strcmp(shade->value, "land"))
					{
						oe->shade = 1;
					}
					else
					{
						error("Config INI '%s' error: at group '%s', invalid 'shade' item format, use 'land' or none.", filename + basepath_length, g->name);
					}
				}

				IniItem *slope = object->GetItem("slope", false);
				if (slope && slope->value)
				{
					if (!strcmp(slope->value, "auto")) { oe->slope = 1; } else
					{
						error("Config INI '%s' error: at group '%s', invalid 'slope' item format, use 'auto' or none.", filename + basepath_length, g->name);
					}
				}

				matrSetIdentity(oe->matr);
				for (IniItem *oi = object->item; oi; oi = oi->next)
				{
					if (!oi->value) continue;

					if (!strcmp(oi->name, "trans"))
					{
						float x = 0.0f, y = 0.0f, z = 0.0f;
						if (sscanf(oi->value, " %f %f %f", &x, &y, &z) != 3)
						{
							error("Config INI '%s' error: at group '%s', object '%s', invalid 'trans' item format near '%s', use float x y z.", filename + basepath_length, g->name, object->name, oi->value);
							continue;
						}
						matrTranslate(oe->matr, x, y, z);
					}
					if (!strcmp(oi->name, "scale"))
					{
						float x = 0.0f, y = 0.0f, z = 0.0f;
						if (sscanf(oi->value, " %f %f %f", &x, &y, &z) != 3)
						{
							error("Config INI '%s' error: at group '%s', object '%s', invalid 'scale' item format near '%s', use float x y z.", filename + basepath_length, g->name, object->name, oi->value);
							continue;
						}
						matrScale(oe->matr, x, y, z);
					}
					if (!strcmp(oi->name, "rotate"))
					{
						float x = 0.0f, y = 0.0f, z = 0.0f, angle = 0.0f;
						if (sscanf(oi->value, " %f %f %f %f", &x, &y, &z, &angle) != 4)
						{
							error("Config INI '%s' error: at group '%s', object '%s', invalid 'rotate' item format near '%s', use float x y z angle.", filename + basepath_length, g->name, object->name, oi->value);
							continue;
						}

						float quat[4];
						quatSetRotate(quat, x, y, z, RAD(angle));

						float rot[16];
						matrSetQuat(rot, quat);
						matrMul(oe->matr, rot);
					}
				}
			}
		}
		return true;
	};
};

const SpriteEntry *GetSpriteEntry(uint32 grfid, uint32 id, SpriteID s, PaletteID p)
{
	ScopeEntry *se = &_scope[grfid];
	if ((se->idents.size() > id) && (se->idents[id] < se->entrys.size()))
	{
		PalEntry *pe = &se->entrys[se->idents[id]];

		PalEntry::iterator it = pe->find(p);
		if (it != pe->end()) return &it->second;

		it = pe->find(INVALID_PALETTE);
		if (it != pe->end()) return &it->second;
		return nullptr;
	}
	if ((se->sprites.size() > s) && (se->sprites[s] < se->entrys.size()))
	{
		PalEntry *pe = &se->entrys[se->sprites[s]];

		PalEntry::iterator it = pe->find(p);
		if (it != pe->end()) return &it->second;

		it = pe->find(INVALID_PALETTE);
		if (it != pe->end()) return &it->second;
		return nullptr;
	}
	return nullptr;
}

void FreeSprites()
{
	_scope.clear();
}

/// TEST SET AUTOGEN ///

static void SaveImage(const char *path, uint32 size_x, uint32 size_y, uint32 *pixels, const char *title)
{
	FILE *f = fopen(path, "wb");
	if (!f) return;

	png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
	if (png_ptr == nullptr)
	{
		fclose(f);
		return;
	}

	png_infop info_ptr = png_create_info_struct(png_ptr);
	if (info_ptr == nullptr)
	{
		png_destroy_write_struct(&png_ptr, nullptr);
		fclose(f);
		return;
	}

	if (setjmp(png_jmpbuf(png_ptr)))
	{
		png_destroy_write_struct(&png_ptr, &info_ptr);
		fclose(f);
		return;
	}

	png_init_io(png_ptr, f);

	png_set_IHDR(png_ptr, info_ptr, size_x, size_y,
				 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
				 PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

	if (title)
	{
		png_text title_text;
		title_text.compression = PNG_TEXT_COMPRESSION_NONE;
		title_text.key = "Title";
		title_text.text = (png_charp)(title);
		png_set_text(png_ptr, info_ptr, &title_text, 1);
	}

	png_write_info(png_ptr, info_ptr);
	for (uint32 y = 0; y < size_y; y++)
	{
		png_write_row(png_ptr, (png_bytep)(&pixels[y * size_x]));
	}
	png_write_end(png_ptr, info_ptr);

	png_destroy_write_struct(&png_ptr, &info_ptr);
	fclose(f);
}

static void CopyPixels(uint32 *p_dst, const uint32 *p_src, uint32 size_x, uint32 size_y, int sx, int sy, int x0, int y0, int x1, int y1)
{
	uint32 offs_y = max(-y1, 0);
	uint32 offs_x = max(-x1, 0);
	uint32 offs = offs_y * size_x + offs_x;
	int count_x = min(min(x1 + sx, size_x) - x1, size_x - offs_x);

	const uint32 *src = p_src + y0 * (int)(size_x) + x0 + offs;
	uint32 *dst = p_dst + y1 * (int)(size_x) + x1 + offs;
	for (int y = max(y1, 0); y < min(y1 + sy, size_y); y++)
	{
		for (int x = 0; x < count_x; x++)
		{
			uint8 *s = (uint8*)(&src[x]);
			if (!s[3]) continue;

			uint8 *d = (uint8*)(&dst[x]);
			dst[x] = src[x];
//			d[3] = 255;
		}
		src += size_x;
		dst += size_x;
	}
}

static void SaveSprite(SpriteID s)
{
	Blitter_OpenGL *blitter = (Blitter_OpenGL*)(BlitterFactory::GetCurrentBlitter());

	uint32 size_x, size_y;
	if (!blitter->CopySprite(s, size_x, size_y, nullptr)) return;

	std::vector<uint32> src(size_x * size_y);
	if (!blitter->CopySprite(s, size_x, size_y, src.data())) return;

	for (size_t i = 0; i < src.size(); i++)
	{
		uint8 *s = (uint8*)(&src[i]);
		if (!s[3]) continue;
		s[3] = 255;
	}

	char path[MAX_PATH];
	sprintf(path, "newgrf/tmp/%i.png", s);
	SaveImage(path, size_x, size_y, src.data(), nullptr);
}

static void GenSpriteEntry(FILE *ini, const char *name, SpriteID s, uint32 count = 1, int offs_x = 0, int offs_y = 0)
{
	Blitter_OpenGL *blitter = (Blitter_OpenGL*)(BlitterFactory::GetCurrentBlitter());

	uint32 size_x, size_y;
	if (!blitter->CopySprite(s, size_x, size_y, nullptr)) return;
//	if ((size_x != 64) || (size_y != 31)) return;

	size_x = 64;
	size_y = 64;
	std::vector<uint32> src(64 * 64);
	if (!blitter->CopySprite(s, size_x, size_y, src.data())) return;

	std::vector<uint32> tmp(64 * 64);
	CopyPixels(tmp.data(), src.data(), 64, 64, 64, 31, 0, 0, offs_x +  0, offs_y + 16);

	CopyPixels(tmp.data(), src.data(), 64, 64, 64, 31, 0, 0, offs_x - 32, offs_y + 32);
	CopyPixels(tmp.data(), src.data(), 64, 64, 64, 31, 0, 0, offs_x + 32, offs_y + 32);
	CopyPixels(tmp.data(), src.data(), 64, 64, 64, 31, 0, 0, offs_x +  0, offs_y + 48);

	CopyPixels(tmp.data(), src.data(), 64, 64, 64, 31, 0, 0, offs_x - 32, offs_y +  0);
	CopyPixels(tmp.data(), src.data(), 64, 64, 64, 31, 0, 0, offs_x + 32, offs_y +  0);
	CopyPixels(tmp.data(), src.data(), 64, 64, 64, 31, 0, 0, offs_x +  0, offs_y - 16);

	for (int i = 0; i < tmp.size(); i++)
	{
		uint8 *c = (uint8*)(&tmp[i]);
		if (!c[3]) continue;
/*
		float y =  c[0] * 0.29900 + c[1] * 0.58700 + c[2] * 0.11400;
		float u = -c[0] * 0.14713 - c[1] * 0.28886 + c[2] * 0.43600;
		float v =  c[0] * 0.61500 - c[1] * 0.51499 - c[2] * 0.10001;

		y *= 1.5f;

		c[0] = (uint8)(roundf(y * 1.0 + u * 0.00000 + v * 1.39830));
		c[1] = (uint8)(roundf(y * 1.0 - u * 0.39465 - v * 0.58060));
		c[2] = (uint8)(roundf(y * 1.0 + u * 2.03211 + v * 0.00000));
*/
		if (((c[3] >= 0xC6) && (c[3] <= 0xCD)) || (c[3] >= 0xE3)) continue; // animated colors
		c[3] = 255;
	}

	char path[MAX_PATH];
	sprintf(path, "newgrf/base/test/gen/%s.png", name);
	SaveImage(path, 64, 64, tmp.data(), name);

	uint32 grfid = GetOriginGRFID(s);

	fprintf(ini, "[%s]\r\n", name);
	fprintf(ini, "grfid = 0x%08X\r\n", grfid);
	if (s >= SPR_NEWGRFS_BASE)
	{
		fprintf(ini, "ident = ");
		for (uint32 i = 0; i < count; i++)
		{
			uint32 ident = GetOriginID(s + i);
			fprintf(ini, "%s%i", i?", ":"", ident);
		}
		fprintf(ini, "\r\n");
	}
	else
	{
		fprintf(ini, "sprite = %i:%i\r\n", s, count);
	}
	fprintf(ini, "land_image = gen/%s.png\r\n", name);
	fprintf(ini, "land_coord = [ 0.5 0.2421875 ] [ 0.0 0.4921875 ] [ 0.5 0.7421875 ] [ 1.0 0.4921875 ]\r\n");
	fprintf(ini, "\r\n");
}

static void GenTestSet()
{
	{
		FILE *ini = fopen("newgrf/base/test/foundations_gen.ini", "wb");

		int frot[4] ={ 180, -90, 0, 90 };
		uint32 fsides_i[4] ={ 0x01, 0x03, 0x07, 0x05 };
		const char *dname[4] ={ "NE", "NW", "SW", "SE" };
		const char *ename[4] ={ "E0", "E1", "E2", "E3" };

		{
			uint32 fsides = 0x0F;
			fprintf(ini,
					"[FOUNDATION_FLAT]\r\n" \
					"model = foundationF.obj\r\n" \
					"image = foundation.png\r\n\r\n");

			SpriteID sprite = SPR_FOUNDATION_BASE + fsides;
			fprintf(ini,
					"[SPR_FOUNDATION_FLAT]\r\n" \
					"grfid = BASE\r\n" \
					"sprite = %i\r\n" \
					"object = FOUNDATION_FLAT\r\n\r\n", sprite);
		}
		for (int n = 0; n < 3; n++)
		{
			uint32 fsides = fsides_i[n];
			for (int i = 0; i < 4; i++)
			{
				fprintf(ini,
						"[FOUNDATION_FLAT_%i%s]\r\n" \
						"model = foundationF%i.obj\r\n" \
						"image = foundation.png\r\n" \
						"trans = 8 8 0\r\n" \
						"rotate = 0 0 1 %i\r\n" \
						"trans = -8 -8 0\r\n\r\n", n + 1, ename[i], n + 1, frot[i]);

				SpriteID sprite = SPR_FOUNDATION_BASE + fsides;
				fprintf(ini,
						"[SPR_FOUNDATION_FLAT_%i%s]\r\n" \
						"grfid = BASE\r\n" \
						"sprite = %i\r\n" \
						"object = FOUNDATION_FLAT_%i%s\r\n\r\n", n + 1, ename[i], sprite, n + 1, ename[i]);

				fsides = ((fsides << 1) & 0x0F) | (((fsides << 1) & 0xF0) >> 4);
			}
		}
		{
			uint32 n = 3;
			uint32 fsides = fsides_i[n];
			for (int i = 0; i < 2; i++)
			{
				fprintf(ini,
						"[FOUNDATION_FLAT_%i%s]\r\n" \
						"model = foundationF%i.obj\r\n" \
						"image = foundation.png\r\n" \
						"trans = 8 8 0\r\n" \
						"rotate = 0 0 1 %i\r\n" \
						"trans = -8 -8 0\r\n\r\n", n + 1, ename[i], n + 1, frot[i]);

				SpriteID sprite = SPR_FOUNDATION_BASE + fsides;
				fprintf(ini,
						"[SPR_FOUNDATION_FLAT_%i%s]\r\n" \
						"grfid = BASE\r\n" \
						"sprite = %i\r\n" \
						"object = FOUNDATION_FLAT_%i%s\r\n\r\n", n + 1, ename[i], sprite, n + 1, ename[i]);

				fsides = ((fsides << 1) & 0x0F) | (((fsides << 1) & 0xF0) >> 4);
			}
		}

		int drot[4] ={ 90, 0, -90, 180 };
		for (int d = 0; d < 4; d++)
		{
			{
				uint32 fsides = 0x0F;
				fprintf(ini,
						"[FOUNDATION_INC_%s]\r\n" \
						"model = foundationI.obj\r\n" \
						"image = foundation.png\r\n" \
						"trans = 8 8 0\r\n" \
						"rotate = 0 0 1 %i\r\n" \
						"trans = -8 -8 0\r\n\r\n", dname[d], drot[d]);

				SpriteID sprite = SPR_SLOPES_BASE + fsides + 16 * d;
				fprintf(ini,
						"[SPR_FOUNDATION_INC_%s]\r\n" \
						"grfid = 0x01544FFFF\r\n" \
						"sprite = %i\r\n" \
						"object = FOUNDATION_INC_%s\r\n\r\n", dname[d], sprite, dname[d]);
			}
			for (int n = 0; n < 4; n++)
			{
				uint32 fsides = fsides_i[n];
				for (int i = 0; i < 4; i++)
				{
					fprintf(ini,
							"[FOUNDATION_INC_%s_%i%s]\r\n" \
							"model = foundationI%i%s.obj\r\n" \
							"image = foundation.png\r\n" \
							"trans = 8 8 0\r\n" \
							"rotate = 0 0 1 %i\r\n" \
							"trans = -8 -8 0\r\n\r\n", dname[d], n + 1, ename[i], n + 1, ename[i], drot[d]);

					SpriteID sprite = SPR_SLOPES_BASE + 16 * d + fsides;
					fprintf(ini,
							"[SPR_FOUNDATION_INC_%s_%i%s]\r\n" \
							"grfid = 0x01544FFFF\r\n" \
							"sprite = %i\r\n" \
							"object = FOUNDATION_INC_%s_%i%s\r\n\r\n", dname[d], n + 1, ename[i], sprite, dname[d], n + 1, ename[i]);

					fsides = ((fsides << 1) & 0x0F) | (((fsides << 1) & 0xF0) >> 4);
				}
			}
			{
				uint32 n = 3;
				uint32 fsides = fsides_i[n];
				for (int i = 0; i < 2; i++)
				{
					fprintf(ini,
							"[FOUNDATION_INC_%s_%i%s]\r\n" \
							"model = foundationI%i%s.obj\r\n" \
							"image = foundation.png\r\n" \
							"trans = 8 8 0\r\n" \
							"rotate = 0 0 1 %i\r\n" \
							"trans = -8 -8 0\r\n\r\n", dname[d], n + 1, ename[i], n + 1, ename[i], drot[d]);

					SpriteID sprite = SPR_SLOPES_BASE + 16 * d + fsides;
					fprintf(ini,
							"[SPR_FOUNDATION_INC_%s_%i%s]\r\n" \
							"grfid = 0x01544FFFF\r\n" \
							"sprite = %i\r\n" \
							"object = FOUNDATION_INC_%s_%i%s\r\n\r\n", dname[d], n + 1, ename[i], sprite, dname[d], n + 1, ename[i]);

					fsides = ((fsides << 1) & 0x0F) | (((fsides << 1) & 0xF0) >> 4);
				}
			}
		}

		int crot[4] ={ -90, 180, 90, 0 };
		const char *cname[4] ={ "W", "S", "E", "N" };

		for (int c = 0; c < 4; c++)
		{
			{
				uint32 csides = 0x03;
				fprintf(ini,
						"[FOUNDATION_DIAG_%s]\r\n" \
						"model = foundationD.obj\r\n" \
						"image = foundation.png\r\n" \
						"trans = 8 8 0\r\n" \
						"rotate = 0 0 1 %i\r\n" \
						"trans = -8 -8 0\r\n\r\n", cname[c], crot[c]);

				SpriteID sprite = SPR_SLOPES_BASE + 16 * 4 + c * 4 + csides;
				fprintf(ini,
						"[SPR_FOUNDATION_DIAG_%s]\r\n" \
						"grfid = 0x01544FFFF\r\n" \
						"sprite = %i\r\n" \
						"object = FOUNDATION_DIAG_%s\r\n\r\n", cname[c], sprite, cname[c]);
			}
			for (int n = 0; n < 1; n++)
			{
				uint32 csides = fsides_i[n];
				for (int i = 0; i < 2; i++)
				{
					fprintf(ini,
							"[FOUNDATION_DIAG_%s_%i%s]\r\n" \
							"model = foundationD%i%s.obj\r\n" \
							"image = foundation.png\r\n" \
							"trans = 8 8 0\r\n" \
							"rotate = 0 0 1 %i\r\n" \
							"trans = -8 -8 0\r\n\r\n", cname[c], n + 1, ename[i], n + 1, ename[i], crot[c]);

					SpriteID sprite = SPR_SLOPES_BASE + 16 * 4 + c * 4 + csides;
					fprintf(ini,
							"[SPR_FOUNDATION_DIAG_%s_%i%s]\r\n" \
							"grfid = 0x01544FFFF\r\n" \
							"sprite = %i\r\n" \
							"object = FOUNDATION_DIAG_%s_%i%s\r\n\r\n", cname[c], n + 1, ename[i], sprite, cname[c], n + 1, ename[i]);

					csides = ((csides << 1) & 0x03) | (((csides << 1) & 0xFC) >> 4);
				}
			}
			{
				uint32 csides = 0x00;
				fprintf(ini,
						"[FOUNDATION_DIAG_%s_2]\r\n" \
						"model = foundationD2.obj\r\n" \
						"image = foundation.png\r\n" \
						"trans = 8 8 0\r\n" \
						"rotate = 0 0 1 %i\r\n" \
						"trans = -8 -8 0\r\n\r\n", cname[c], crot[c]);

				SpriteID sprite = SPR_SLOPES_BASE + 16 * 4 + c * 4 + csides;
				fprintf(ini,
						"[SPR_FOUNDATION_DIAG_%s_2]\r\n" \
						"grfid = 0x01544FFFF\r\n" \
						"sprite = %i\r\n" \
						"object = FOUNDATION_DIAG_%s_2\r\n\r\n", cname[c], sprite, cname[c]);
			}
		}
		fclose(ini);
	}
	{
		FILE *ini = fopen("newgrf/base/test/land_gen.ini", "wb");
		GenSpriteEntry(ini, "SPR_FLAT_BARE_LAND", SPR_FLAT_BARE_LAND, 19);
		GenSpriteEntry(ini, "SPR_FLAT_1_THIRD_GRASS_TILE", SPR_FLAT_1_THIRD_GRASS_TILE, 19);
		GenSpriteEntry(ini, "SPR_FLAT_2_THIRD_GRASS_TILE", SPR_FLAT_2_THIRD_GRASS_TILE, 19);
		GenSpriteEntry(ini, "SPR_FLAT_GRASS_TILE", SPR_FLAT_GRASS_TILE, 19);
		GenSpriteEntry(ini, "SPR_FLAT_ROUGH_LAND", SPR_FLAT_ROUGH_LAND, 19);
		GenSpriteEntry(ini, "SPR_FLAT_ROUGH_LAND_1", SPR_FLAT_ROUGH_LAND_1, 1);
		GenSpriteEntry(ini, "SPR_FLAT_ROUGH_LAND_2", SPR_FLAT_ROUGH_LAND_2, 1);
		GenSpriteEntry(ini, "SPR_FLAT_ROUGH_LAND_3", SPR_FLAT_ROUGH_LAND_3, 1);
		GenSpriteEntry(ini, "SPR_FLAT_ROUGH_LAND_4", SPR_FLAT_ROUGH_LAND_4, 1);
		GenSpriteEntry(ini, "SPR_FLAT_ROCKY_LAND_1", SPR_FLAT_ROCKY_LAND_1, 19);
		GenSpriteEntry(ini, "SPR_FLAT_ROCKY_LAND_2", SPR_FLAT_ROCKY_LAND_2, 19);
		GenSpriteEntry(ini, "SPR_FLAT_1_QUART_SNOW_DESERT_TILE", SPR_FLAT_1_QUART_SNOW_DESERT_TILE, 19);
		GenSpriteEntry(ini, "SPR_FLAT_2_QUART_SNOW_DESERT_TILE", SPR_FLAT_2_QUART_SNOW_DESERT_TILE, 19);
		GenSpriteEntry(ini, "SPR_FLAT_3_QUART_SNOW_DESERT_TILE", SPR_FLAT_3_QUART_SNOW_DESERT_TILE, 19);
//		GenSpriteEntry(ini, "SPR_FLAT_SNOW_DESERT_TILE", SPR_FLAT_SNOW_DESERT_TILE, 19);
		fclose(ini);
	}
	{
		FILE *ini = fopen("newgrf/base/test/farm_gen.ini", "wb");
		GenSpriteEntry(ini, "SPR_FARMLAND_BARE", SPR_FARMLAND_BARE, 19);
		GenSpriteEntry(ini, "SPR_FARMLAND_STATE_1", SPR_FARMLAND_STATE_1, 19);
		GenSpriteEntry(ini, "SPR_FARMLAND_STATE_2", SPR_FARMLAND_STATE_2, 19);
		GenSpriteEntry(ini, "SPR_FARMLAND_STATE_3", SPR_FARMLAND_STATE_3, 19);
		GenSpriteEntry(ini, "SPR_FARMLAND_STATE_4", SPR_FARMLAND_STATE_4, 19);
		GenSpriteEntry(ini, "SPR_FARMLAND_STATE_5", SPR_FARMLAND_STATE_5, 19);
		GenSpriteEntry(ini, "SPR_FARMLAND_STATE_6", SPR_FARMLAND_STATE_6, 19);
		GenSpriteEntry(ini, "SPR_FARMLAND_STATE_7", SPR_FARMLAND_STATE_7, 19);
		GenSpriteEntry(ini, "SPR_FARMLAND_HAYPACKS", SPR_FARMLAND_HAYPACKS, 19);
		fclose(ini);
	}
	{
		FILE *ini = fopen("newgrf/base/test/builds_gen.ini", "wb");
		GenSpriteEntry(ini, "SPR_CONCRETE_GROUND", SPR_CONCRETE_GROUND, 1);
		GenSpriteEntry(ini, "SPR_GRND_SMLBLCKFLATS_02", SPR_GROUND_SMLBLCKFLATS_02, 1);
		GenSpriteEntry(ini, "SPR_GRND_TEMPCHURCH", SPR_GROUND_TEMPCHURCH, 1);
		GenSpriteEntry(ini, "SPR_GRND_TOWNHOUSE_06_V1", SPR_GRND_TOWNHOUSE_06_V1, 1);
		GenSpriteEntry(ini, "SPR_GRND_STADIUM_N", SPR_GRND_STADIUM_N, 1);
		GenSpriteEntry(ini, "SPR_GRND_STADIUM_E", SPR_GRND_STADIUM_E, 1);
		GenSpriteEntry(ini, "SPR_GRND_STADIUM_W", SPR_GRND_STADIUM_W, 1);
		GenSpriteEntry(ini, "SPR_GRND_STADIUM_S", SPR_GRND_STADIUM_S, 1);
		GenSpriteEntry(ini, "SPR_GRND_TOWNHOUSE_06_V2", SPR_GRND_TOWNHOUSE_06_V2, 1);
		GenSpriteEntry(ini, "SPR_GRND_HOUSE_TOY1", SPR_GRND_HOUSE_TOY1, 1);
		GenSpriteEntry(ini, "SPR_GRND_HOUSE_TOY2", SPR_GRND_HOUSE_TOY2, 1);
		GenSpriteEntry(ini, "SPR_GRND_HOUSE_A", 1511, 1);
		GenSpriteEntry(ini, "SPR_GRND_HOUSE_B", 1517, 1);
		GenSpriteEntry(ini, "SPR_GRND_HOUSE_C", 1522, 1);
		GenSpriteEntry(ini, "SPR_GRND_HOUSE_D", 1574, 1);
		GenSpriteEntry(ini, "SPR_GRND_HOUSE_E", 1499, 1);
		GenSpriteEntry(ini, "SPR_GRND_HOUSE_F", 1495, 1);
		GenSpriteEntry(ini, "SPR_GRND_HOUSE_G", 1487, 1);
		GenSpriteEntry(ini, "SPR_GRND_HOUSE_H", 1572, 1);
		GenSpriteEntry(ini, "SPR_GRND_HOUSE_I", 1493, 1);
		GenSpriteEntry(ini, "SPR_GRND_HOUSE_J", 1495, 1);
		GenSpriteEntry(ini, "SPR_GRND_HOUSE_K", 1487, 1);
		GenSpriteEntry(ini, "SPR_GRND_HOUSE_L", 1503, 1);
		GenSpriteEntry(ini, "SPR_GRND_THEATRE", 1552, 1);
		GenSpriteEntry(ini, "SPR_GRND_LARGEHOUSE_A", 1528, 1);
		GenSpriteEntry(ini, "SPR_GRND_TALLOFFICE_A", 1534, 1);
		GenSpriteEntry(ini, "SPR_GRND_TALLOFFICE_B", 1550, 1);
		GenSpriteEntry(ini, "SPR_GRND_TALLOFFICE_C", 1530, 1);
		GenSpriteEntry(ini, "SPR_GRND_OFFICE_SHOP_A", 1544, 1);
		GenSpriteEntry(ini, "SPR_GRND_OFFICE_C", 1562, 1);
		GenSpriteEntry(ini, "SPR_GRND_OFFICE_D", 1564, 1);
		GenSpriteEntry(ini, "SPR_GRND_CINEMA", 4404, 1);
		GenSpriteEntry(ini, "SPR_GRND_SOCCER_N", 1554, 1);
		GenSpriteEntry(ini, "SPR_GRND_SOCCER_E", 1555, 1);
		GenSpriteEntry(ini, "SPR_GRND_SOCCER_W", 1556, 1);
		GenSpriteEntry(ini, "SPR_GRND_SOCCER_S", 1557, 1);
		fclose(ini);
	}
	{
		FILE *ini = fopen("newgrf/base/test/road_gen.ini", "wb");
		GenSpriteEntry(ini, "SPR_BUS_STOP_NE_GROUND", SPR_BUS_STOP_NE_GROUND, 1);
		GenSpriteEntry(ini, "SPR_BUS_STOP_SE_GROUND", SPR_BUS_STOP_SE_GROUND, 1);
		GenSpriteEntry(ini, "SPR_BUS_STOP_SW_GROUND", SPR_BUS_STOP_SW_GROUND, 1);
		GenSpriteEntry(ini, "SPR_BUS_STOP_NW_GROUND", SPR_BUS_STOP_NW_GROUND, 1);
		GenSpriteEntry(ini, "SPR_TRUCK_STOP_NE_GROUND", SPR_TRUCK_STOP_NE_GROUND, 1);
		GenSpriteEntry(ini, "SPR_TRUCK_STOP_SE_GROUND", SPR_TRUCK_STOP_SE_GROUND, 1);
		GenSpriteEntry(ini, "SPR_TRUCK_STOP_SW_GROUND", SPR_TRUCK_STOP_SW_GROUND, 1);
		GenSpriteEntry(ini, "SPR_TRUCK_STOP_NW_GROUND", SPR_TRUCK_STOP_NW_GROUND, 1);
		GenSpriteEntry(ini, "SPR_ROAD_Y", SPR_ROAD_Y, 1);
		GenSpriteEntry(ini, "SPR_ROAD_X", SPR_ROAD_X, 1);
		GenSpriteEntry(ini, "SPR_ROAD_CROSS_X", 1334, 1);
		GenSpriteEntry(ini, "SPR_ROAD_CROSS_NE", 1335, 1);
		GenSpriteEntry(ini, "SPR_ROAD_CROSS_NW", 1336, 1);
		GenSpriteEntry(ini, "SPR_ROAD_CROSS_SW", 1337, 1);
		GenSpriteEntry(ini, "SPR_ROAD_CROSS_SE", 1338, 1);
		GenSpriteEntry(ini, "SPR_ROAD_W", 1339, 1);
		GenSpriteEntry(ini, "SPR_ROAD_N", 1340, 1);
		GenSpriteEntry(ini, "SPR_ROAD_E", 1341, 1);
		GenSpriteEntry(ini, "SPR_ROAD_S", 1342, 1);
		GenSpriteEntry(ini, "SPR_ROAD_END_SW", 1347, 1);
		GenSpriteEntry(ini, "SPR_ROAD_END_NW", 1348, 1);
		GenSpriteEntry(ini, "SPR_ROAD_END_NE", 1349, 1);
		GenSpriteEntry(ini, "SPR_ROAD_END_SE", 1350, 1);
		GenSpriteEntry(ini, "SPR_ROAD_PAVED_Y", SPR_ROAD_PAVED_STRAIGHT_Y, 1);
		GenSpriteEntry(ini, "SPR_ROAD_PAVED_X", SPR_ROAD_PAVED_STRAIGHT_X, 1);
		GenSpriteEntry(ini, "SPR_ROAD_PAVED_CROSS_X", 1315, 1);
		GenSpriteEntry(ini, "SPR_ROAD_PAVED_CROSS_NE", 1316, 1);
		GenSpriteEntry(ini, "SPR_ROAD_PAVED_CROSS_NW", 1317, 1);
		GenSpriteEntry(ini, "SPR_ROAD_PAVED_CROSS_SW", 1318, 1);
		GenSpriteEntry(ini, "SPR_ROAD_PAVED_CROSS_SE", 1319, 1);
		GenSpriteEntry(ini, "SPR_ROAD_PAVED_W", 1320, 1);
		GenSpriteEntry(ini, "SPR_ROAD_PAVED_N", 1321, 1);
		GenSpriteEntry(ini, "SPR_ROAD_PAVED_E", 1322, 1);
		GenSpriteEntry(ini, "SPR_ROAD_PAVED_S", 1323, 1);
		GenSpriteEntry(ini, "SPR_ROAD_END_SW", 1328, 1);
		GenSpriteEntry(ini, "SPR_ROAD_END_NW", 1329, 1);
		GenSpriteEntry(ini, "SPR_ROAD_END_NE", 1330, 1);
		GenSpriteEntry(ini, "SPR_ROAD_END_SE", 1331, 1);
		fclose(ini);
	}
	{
		FILE *ini = fopen("newgrf/base/test/airport_gen.ini", "wb");
		GenSpriteEntry(ini, "SPR_AIRPORT_APRON", SPR_AIRPORT_APRON, 1);
		GenSpriteEntry(ini, "SPR_AIRPORT_AIRCRAFT_STAND", SPR_AIRPORT_AIRCRAFT_STAND, 1);
		GenSpriteEntry(ini, "SPR_AIRPORT_TAXIWAY_NS_WEST", SPR_AIRPORT_TAXIWAY_NS_WEST, 1);
		GenSpriteEntry(ini, "SPR_AIRPORT_TAXIWAY_EW_SOUTH", SPR_AIRPORT_TAXIWAY_EW_SOUTH, 1);
		GenSpriteEntry(ini, "SPR_AIRPORT_TAXIWAY_XING_SOUTH", SPR_AIRPORT_TAXIWAY_XING_SOUTH, 1);
		GenSpriteEntry(ini, "SPR_AIRPORT_TAXIWAY_XING_WEST", SPR_AIRPORT_TAXIWAY_XING_WEST, 1);
		GenSpriteEntry(ini, "SPR_AIRPORT_TAXIWAY_NS_CTR", SPR_AIRPORT_TAXIWAY_NS_CTR, 1);
		GenSpriteEntry(ini, "SPR_AIRPORT_TAXIWAY_XING_EAST", SPR_AIRPORT_TAXIWAY_XING_EAST, 1);
		GenSpriteEntry(ini, "SPR_AIRPORT_TAXIWAY_NS_EAST", SPR_AIRPORT_TAXIWAY_NS_EAST, 1);
		GenSpriteEntry(ini, "SPR_AIRPORT_TAXIWAY_EW_NORTH", SPR_AIRPORT_TAXIWAY_EW_NORTH, 1);
		GenSpriteEntry(ini, "SPR_AIRPORT_TAXIWAY_EW_CTR", SPR_AIRPORT_TAXIWAY_EW_CTR, 1);
		GenSpriteEntry(ini, "SPR_AIRPORT_RUNWAY_EXIT_A", SPR_AIRPORT_RUNWAY_EXIT_A, 1);
		GenSpriteEntry(ini, "SPR_AIRPORT_RUNWAY_EXIT_B", SPR_AIRPORT_RUNWAY_EXIT_B, 1);
		GenSpriteEntry(ini, "SPR_AIRPORT_RUNWAY_EXIT_C", SPR_AIRPORT_RUNWAY_EXIT_C, 1);
		GenSpriteEntry(ini, "SPR_AIRPORT_RUNWAY_EXIT_D", SPR_AIRPORT_RUNWAY_EXIT_D, 1);
		GenSpriteEntry(ini, "SPR_AIRPORT_RUNWAY_END", SPR_AIRPORT_RUNWAY_END, 1);
		GenSpriteEntry(ini, "SPR_AIRFIELD_APRON_A", SPR_AIRFIELD_APRON_A, 1);
		GenSpriteEntry(ini, "SPR_AIRFIELD_APRON_B", SPR_AIRFIELD_APRON_B, 1);
		GenSpriteEntry(ini, "SPR_AIRFIELD_APRON_C", SPR_AIRFIELD_APRON_C, 1);
		GenSpriteEntry(ini, "SPR_AIRFIELD_APRON_D", SPR_AIRFIELD_APRON_D, 1);
		GenSpriteEntry(ini, "SPR_AIRFIELD_RUNWAY_NEAR_END", SPR_AIRFIELD_RUNWAY_NEAR_END, 1);
		GenSpriteEntry(ini, "SPR_AIRFIELD_RUNWAY_MIDDLE", SPR_AIRFIELD_RUNWAY_MIDDLE, 1);
		GenSpriteEntry(ini, "SPR_AIRFIELD_RUNWAY_FAR_END", SPR_AIRFIELD_RUNWAY_FAR_END, 1);
		fclose(ini);
	}
	{
		FILE *ini = fopen("newgrf/base/test/rail_gen.ini", "wb");
//		GenSpriteEntry(ini, "SPR_RAIL_SINGLE_X", SPR_RAIL_SINGLE_X, 1, 12, 5);
//		GenSpriteEntry(ini, "SPR_RAIL_SINGLE_Y", SPR_RAIL_SINGLE_Y, 1, 12, 5);
//		GenSpriteEntry(ini, "SPR_RAIL_SINGLE_NORTH", SPR_RAIL_SINGLE_NORTH, 1, 12, 4);
//		GenSpriteEntry(ini, "SPR_RAIL_SINGLE_SOUTH", SPR_RAIL_SINGLE_SOUTH, 1, 12, 20);
//		GenSpriteEntry(ini, "SPR_RAIL_SINGLE_EAST", SPR_RAIL_SINGLE_EAST, 1, 42, 6);
//		GenSpriteEntry(ini, "SPR_RAIL_SINGLE_WEST", SPR_RAIL_SINGLE_WEST, 1, 10, 6);
		GenSpriteEntry(ini, "SPR_RAIL_TRACK_Y", SPR_RAIL_TRACK_Y, 1);
		GenSpriteEntry(ini, "SPR_RAIL_TRACK_X", SPR_RAIL_TRACK_X, 1);
		GenSpriteEntry(ini, "SPR_RAIL_TRACK_NORTH", 1013, 1);
		GenSpriteEntry(ini, "SPR_RAIL_TRACK_SOUTH", 1014, 1);
		GenSpriteEntry(ini, "SPR_RAIL_TRACK_EAST", 1015, 1);
		GenSpriteEntry(ini, "SPR_RAIL_TRACK_WEST", 1016, 1);
		GenSpriteEntry(ini, "SPR_RAIL_TRACK_CROSS", 1017, 1);
		GenSpriteEntry(ini, "SPR_RAIL_JUNC_GRND_SW", 1018, 1);
		GenSpriteEntry(ini, "SPR_RAIL_JUNC_GRND_NE", 1019, 1);
		GenSpriteEntry(ini, "SPR_RAIL_JUNC_GRND_SE", 1020, 1);
		GenSpriteEntry(ini, "SPR_RAIL_JUNC_GRND_NW", 1021, 1);
		GenSpriteEntry(ini, "SPR_RAIL_JUNC_GRND_CROSS", 1022, 1);
		// SLOPES
		GenSpriteEntry(ini, "SPR_RAIL_TRACK_N_S", SPR_RAIL_TRACK_N_S, 1);
		GenSpriteEntry(ini, "SPR_RAIL_TRACK_E_W", 1036, 1);
		GenSpriteEntry(ini, "SPR_CROSSING_OFF_X_RAIL", SPR_CROSSING_OFF_X_RAIL, 1);
		GenSpriteEntry(ini, "SPR_CROSSING_OFF_Y_RAIL", 1371, 1);
		GenSpriteEntry(ini, "SPR_CROSSING_OFF_X_RAIL_B", 1372, 1);
		GenSpriteEntry(ini, "SPR_CROSSING_OFF_Y_RAIL_B", 1373, 1);
		fclose(ini);
	}
	{
		FILE *ini = fopen("newgrf/base/test/mono_gen.ini", "wb");
//		GenSpriteEntry(ini, "SPR_MONO_SINGLE_X", SPR_MONO_SINGLE_X, 1, 12, 5);
//		GenSpriteEntry(ini, "SPR_MONO_SINGLE_Y", SPR_MONO_SINGLE_Y, 1, 12, 5);
//		GenSpriteEntry(ini, "SPR_MONO_SINGLE_NORTH", SPR_MONO_SINGLE_NORTH, 1, 12, 4);
//		GenSpriteEntry(ini, "SPR_MONO_SINGLE_SOUTH", SPR_MONO_SINGLE_SOUTH, 1, 12, 20);
//		GenSpriteEntry(ini, "SPR_MONO_SINGLE_EAST", SPR_MONO_SINGLE_EAST, 1, 42, 6);
//		GenSpriteEntry(ini, "SPR_MONO_SINGLE_WEST", SPR_MONO_SINGLE_WEST, 1, 10, 6);
		GenSpriteEntry(ini, "SPR_MONO_TRACK_Y", SPR_MONO_TRACK_Y, 1);
		GenSpriteEntry(ini, "SPR_MONO_TRACK_X", SPR_MONO_TRACK_X, 1);
		GenSpriteEntry(ini, "SPR_MONO_TRACK_NORTH", 1095, 1);
		GenSpriteEntry(ini, "SPR_MONO_TRACK_SOUTH", 1096, 1);
		GenSpriteEntry(ini, "SPR_MONO_TRACK_EAST", 1097, 1);
		GenSpriteEntry(ini, "SPR_MONO_TRACK_WEST", 1098, 1);
		GenSpriteEntry(ini, "SPR_MONO_TRACK_CROSS", 1099, 1);
		GenSpriteEntry(ini, "SPR_MONO_JUNC_GRND_SW", 1100, 1);
		GenSpriteEntry(ini, "SPR_MONO_JUNC_GRND_NE", 1101, 1);
		GenSpriteEntry(ini, "SPR_MONO_JUNC_GRND_SE", 1102, 1);
		GenSpriteEntry(ini, "SPR_MONO_JUNC_GRND_NW", 1103, 1);
		GenSpriteEntry(ini, "SPR_MONO_JUNC_GRND_CROSS", 1104, 1);
		// SLOPES
		GenSpriteEntry(ini, "SPR_MONO_TRACK_N_S", SPR_MONO_TRACK_N_S, 1);
		GenSpriteEntry(ini, "SPR_MONO_TRACK_E_W", 1118, 1);
		GenSpriteEntry(ini, "SPR_CROSSING_OFF_X_MONO", SPR_CROSSING_OFF_X_MONO, 1);
		GenSpriteEntry(ini, "SPR_CROSSING_OFF_Y_MONO", 1383, 1);
		GenSpriteEntry(ini, "SPR_CROSSING_OFF_X_MONO_B", 1384, 1);
		GenSpriteEntry(ini, "SPR_CROSSING_OFF_Y_MONO_B", 1385, 1);
		fclose(ini);
	}
	{
		FILE *ini = fopen("newgrf/base/test/mglv_gen.ini", "wb");
//		GenSpriteEntry(ini, "SPR_MGLV_SINGLE_X", SPR_MGLV_SINGLE_X, 1, 12, 5);
//		GenSpriteEntry(ini, "SPR_MGLV_SINGLE_Y", SPR_MGLV_SINGLE_Y, 1, 12, 5);
//		GenSpriteEntry(ini, "SPR_MGLV_SINGLE_NORTH", SPR_MGLV_SINGLE_NORTH, 1, 12, 4);
//		GenSpriteEntry(ini, "SPR_MGLV_SINGLE_SOUTH", SPR_MGLV_SINGLE_SOUTH, 1, 12, 20);
//		GenSpriteEntry(ini, "SPR_MGLV_SINGLE_EAST", SPR_MGLV_SINGLE_EAST, 1, 42, 6);
//		GenSpriteEntry(ini, "SPR_MGLV_SINGLE_WEST", SPR_MGLV_SINGLE_WEST, 1, 10, 6);
		GenSpriteEntry(ini, "SPR_MGLV_TRACK_Y", SPR_MGLV_TRACK_Y, 1);
		GenSpriteEntry(ini, "SPR_MGLV_TRACK_X", SPR_MGLV_TRACK_X, 1);
		GenSpriteEntry(ini, "SPR_MGLV_TRACK_NORTH", 1177, 1);
		GenSpriteEntry(ini, "SPR_MGLV_TRACK_SOUTH", 1178, 1);
		GenSpriteEntry(ini, "SPR_MGLV_TRACK_EAST", 1179, 1);
		GenSpriteEntry(ini, "SPR_MGLV_TRACK_WEST", 1180, 1);
		GenSpriteEntry(ini, "SPR_MGLV_TRACK_CROSS", 1181, 1);
		GenSpriteEntry(ini, "SPR_MGLV_JUNC_GRND_SW", 1182, 1);
		GenSpriteEntry(ini, "SPR_MGLV_JUNC_GRND_NE", 1183, 1);
		GenSpriteEntry(ini, "SPR_MGLV_JUNC_GRND_SE", 1184, 1);
		GenSpriteEntry(ini, "SPR_MGLV_JUNC_GRND_NW", 1185, 1);
		GenSpriteEntry(ini, "SPR_MGLV_JUNC_GRND_CROSS", 1186, 1);
		// SLOPES
		GenSpriteEntry(ini, "SPR_MGLV_TRACK_N_S", SPR_MGLV_TRACK_N_S, 1);
		GenSpriteEntry(ini, "SPR_MGLV_TRACK_E_W", 1200, 1);
		GenSpriteEntry(ini, "SPR_CROSSING_OFF_X_MGLV", SPR_CROSSING_OFF_X_MAGLEV, 1);
		GenSpriteEntry(ini, "SPR_CROSSING_OFF_Y_MGLV", 1395, 1);
		GenSpriteEntry(ini, "SPR_CROSSING_OFF_X_MGLV_B", 1396, 1);
		GenSpriteEntry(ini, "SPR_CROSSING_OFF_Y_MGLV_B", 1397, 1);
		fclose(ini);
	}
	{
		FILE *ini = fopen("newgrf/base/test/vehicle_gen.ini", "wb");
		{
			int count = lengthof(_engine_sprite_base);
			for (int i = 0; i < count; i++)
			{
				fprintf(ini,
						"[TRAIN_VEHICLE_%i]\r\n" \
						"model = vehicle.obj\r\n" \
						"image = vehicle.png\r\n\r\n", i);

				fprintf(ini,
						"[SPR_TRAIN_VEHICLE_%i]\r\n" \
						"grfid = BASE\r\n" \
						"sprite = ", i);

				for (int j = 0; j < 2; j++)
				{
					for (Direction dir = DIR_BEGIN; dir < DIR_END; dir++)
					{
						SpriteID sprite = ((dir + _engine_sprite_add[i]) & _engine_sprite_and[i]) + _engine_sprite_base[i] + (j > 0 ? _wagon_full_adder[i] : 0);
						fprintf(ini, "%s%i", dir || j ? ", " : "", sprite);
					}
				}

				fprintf(ini, "\r\nobject = TRAIN_VEHICLE_%i\r\n\r\n", i);
			}
		}
		{
			static const uint16 _roadveh_images[] = {
				0xCD4, 0xCDC, 0xCE4, 0xCEC, 0xCF4, 0xCFC, 0xD0C, 0xD14,
				0xD24, 0xD1C, 0xD2C, 0xD04, 0xD1C, 0xD24, 0xD6C, 0xD74,
				0xD7C, 0xC14, 0xC1C, 0xC24, 0xC2C, 0xC34, 0xC3C, 0xC4C,
				0xC54, 0xC64, 0xC5C, 0xC6C, 0xC44, 0xC5C, 0xC64, 0xCAC,
				0xCB4, 0xCBC, 0xD94, 0xD9C, 0xDA4, 0xDAC, 0xDB4, 0xDBC,
				0xDCC, 0xDD4, 0xDE4, 0xDDC, 0xDEC, 0xDC4, 0xDDC, 0xDE4,
				0xE2C, 0xE34, 0xE3C, 0xC14, 0xC1C, 0xC2C, 0xC3C, 0xC4C,
				0xC5C, 0xC64, 0xC6C, 0xC74, 0xC84, 0xC94, 0xCA4
			};

			static const uint16 _roadveh_full_adder[] = {
				 0,  88,   0,   0,   0,   0,  48,  48,
				48,  48,   0,   0,  64,  64,   0,  16,
				16,   0,  88,   0,   0,   0,   0,  48,
				48,  48,  48,   0,   0,  64,  64,   0,
				16,  16,   0,  88,   0,   0,   0,   0,
				48,  48,  48,  48,   0,   0,  64,  64,
				 0,  16,  16,   0,   8,   8,   8,   8,
				 0,   0,   0,   8,   8,   8,   8
			};

			int count = lengthof(_roadveh_images);
			for (int i = 0; i < count; i++)
			{
				fprintf(ini,
						"[ROAD_VEHICLE_%i]\r\n" \
						"model = vehicle.obj\r\n" \
						"image = vehicle.png\r\n\r\n", i);

				fprintf(ini,
						"[SPR_ROAD_VEHICLE_%i]\r\n" \
						"grfid = BASE\r\n" \
						"sprite = ", i);

				for (int j = 0; j < 2; j++)
				{
					for (Direction dir = DIR_BEGIN; dir < DIR_END; dir++)
					{
						SpriteID sprite = dir + _roadveh_images[i] + (j ? _roadveh_full_adder[i] : 0);
						fprintf(ini, "%s%i", dir || j ? ", " : "", sprite);
					}
				}

				fprintf(ini, "\r\nobject = ROAD_VEHICLE_%i\r\n\r\n", i);
			}
		}
		fclose(ini);
	}
/**/
}

void LoadConfigFiles()
{
//	GenTestSet();
/*
	{
		static const byte tileh_to_shoresprite[32] ={
			0, 1, 2, 3, 4, 16, 6, 7, 8, 9, 17, 11, 12, 13, 14, 0,
			0, 0, 0, 0, 0,  0, 0, 0, 0, 0,  0,  5,  0, 10, 15, 0,
		};

		for (int tileh = 0; tileh < 32; tileh++)
		{
			const char *name = nullptr;
			switch (tileh)
			{
			case SLOPE_W: name = "SLOPE_W"; break;
			case SLOPE_S: name = "SLOPE_S"; break;
			case SLOPE_E: name = "SLOPE_E"; break;
			case SLOPE_N: name = "SLOPE_N"; break;
			case SLOPE_NW: name = "SLOPE_NW"; break;
			case SLOPE_SW: name = "SLOPE_SW"; break;
			case SLOPE_SE: name = "SLOPE_SE"; break;
			case SLOPE_NE: name = "SLOPE_NE"; break;
			case SLOPE_EW: name = "SLOPE_EW"; break;
			case SLOPE_NS: name = "SLOPE_NS"; break;
			case SLOPE_NWS: name = "SLOPE_NWS"; break;
			case SLOPE_WSE: name = "SLOPE_WSE"; break;
			case SLOPE_SEN: name = "SLOPE_SEN"; break;
			case SLOPE_ENW: name = "SLOPE_ENW"; break;
			case SLOPE_STEEP_W: name = "SLOPE_STEEP_W"; break;
			case SLOPE_STEEP_S: name = "SLOPE_STEEP_S"; break;
			case SLOPE_STEEP_E: name = "SLOPE_STEEP_E"; break;
			case SLOPE_STEEP_N: name = "SLOPE_STEEP_N"; break;
			default: continue;
			}
			printf("%s = %i\r\n", name, SPR_SHORE_BASE + tileh_to_shoresprite[tileh]);
		}
	}
*/
/*
	int count = lengthof(_engine_sprite_base);
	for (int i = 0; i < count; i++)
	{
		for (int j = 0; j < 2; j++)
		{
			for (Direction dir = DIR_BEGIN; dir < DIR_END; dir++)
			{
				SpriteID sprite = ((dir + _engine_sprite_add[i]) & _engine_sprite_and[i]) + _engine_sprite_base[i] + (j > 0 ? _wagon_full_adder[i] : 0);
				SaveSprite(sprite);
			}
		}
	}
*/
	
//	for (int i = 0; i <= 19; i++) SaveSprite(3981 + i); // GRASS
//	for (int i = 0; i <= 19; i++) SaveSprite(4000 + i); // ROUGHT
//	for (int i = 0; i <= 4; i++) SaveSprite(4019 + i); // ROUGHT
//	for (int i = 0; i < 16 * 5; i++) SaveSprite(8311 + i); // RIVER
//	for (int i = 0; i < 60 * 13; i++) SaveSprite(8391 + i); // RIVER
//	SaveSprite(5935);
//	SaveSprite(5928);
//	SaveSprite(5937);
	
//	for (int i = 5569; i <= 5623; i++) SaveSprite(i); // rail selection
//	for (int i = 5623; i <= 5659; i++) SaveSprite(i); // cenetary

	ConfigScanner scanner;
	scanner.Scan(".ini", NEWGRF_DIR);
}
