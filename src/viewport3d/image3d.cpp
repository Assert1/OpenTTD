#include "image3d.h"
#include "math3d.h"

#include <ctype.h>
#include "../fileio_func.h"

#include <png.h>
#include <setjmp.h>

#include <map>

#define ATLAS_BLOCK_SIZE	32
#define ATLAS_SIZE			IMAGE3D_ATLAS_SIZE
#define ATLAS_BLOCKS		(ATLAS_SIZE / ATLAS_BLOCK_SIZE)
#define ATLAS_BLOCKS_COUNT	(ATLAS_BLOCKS * ATLAS_BLOCKS)

struct AtlasLayer
{
	uint32 free; // free blocks count
	uint8 used[ATLAS_BLOCKS_COUNT];	// used blocks mask
	std::vector<uint8> data_c;		// RGBP pixels data
	int dirty;
};

static std::vector<AtlasLayer> _atlas_layers;	// layers
static std::vector<ImageEntry> _image_entrys;	// entrys
static GLuint _atlas_texture = 0;	// OpenGL 2DArray texture
static int _atlas_dirty = 0;

class LessFctr {
public:
	bool operator () (const ImageEntry::Key &a, const ImageEntry::Key &b) const { return (a.second < b.second); }
};
typedef std::multimap<ImageEntry::Key, uint32, LessFctr> EntrysMap;
static EntrysMap _entrys_map;

void CDECL png_read_data_fn(png_structp png, png_bytep data, png_size_t length)
{
	png_voidp ptr = png_get_io_ptr(png);
	memcpy(data, ptr, (size_t)(length));
	png_set_read_fn(png, ((uint8*)(ptr))+(size_t)(length), png_read_data_fn);
}

uint32 LoadImageFile(const char *path)
{
	ImageEntry::Key key(path, murmur3_32(path, strlen(path), 'OTTD'));
	EntrysMap::iterator it = _entrys_map.lower_bound(key);
	if (it != _entrys_map.end())
	{
		do
		{
			if (!strcmp(it->first.first, key.first)) return it->second;
			it++;

		} while ((it != _entrys_map.end()) && (it->first.second == key.second));
	}

	std::vector<uint8> bytes;
	{
		size_t size;
		FILE *f = FioFOpenFile(path, "rb", Subdirectory::NEWGRF_DIR, &size);
		if (!f)
		{
			error("Image '%s' not found.", path);
			return INVALID_IMAGE;
		}
		bytes.resize(size);
		size_t result = fread(bytes.data(), 1, size, f);
		FioFCloseFile(f);
		if (result != size)
		{
			error("Can't read image '%s'.", path);
			return INVALID_IMAGE;
		}
	}

	_image_entrys.emplace_back();
	ImageEntry *image = &_image_entrys.back();
	_entrys_map.insert(std::pair<ImageEntry::Key, uint32>(key, (uint32)(_image_entrys.size() - 1)));

	///

	std::vector<uint8> pixels;
	{
		png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
		if (!png)
		{
			error("Can't read image '%s'.", path);
			return false;
		}

		png_infop info = png_create_info_struct(png);
		if (!info)
		{
			error("Can't read image '%s'.", path);
			png_destroy_read_struct(&png, nullptr, nullptr);
			return false;
		};
		if (setjmp(png_jmpbuf(png)))
		{
			error("Can't read image '%s'.", path);
			png_destroy_read_struct(&png, &info, nullptr);
			return false;
		};
		png_set_read_fn(png, bytes.data(), png_read_data_fn);
		png_read_info(png, info);
		
		image->size_x = png_get_image_width(png, info);
		image->size_y = png_get_image_height(png, info);
		if (png_get_bit_depth(png, info) == 16) png_set_strip_16(png);
		if (png_get_bit_depth(png, info) < 8) png_set_packing(png);
		switch (png_get_color_type(png, info))
		{
		case PNG_COLOR_TYPE_GRAY:
			png_set_gray_to_rgb(png);
			png_set_add_alpha(png, 0xFF, PNG_FILLER_AFTER);
			break;

		case PNG_COLOR_TYPE_GRAY_ALPHA:
			png_set_gray_to_rgb(png);
			png_set_tRNS_to_alpha(png);
			break;

		case PNG_COLOR_TYPE_PALETTE:
			png_set_palette_to_rgb(png);
			if (png_get_valid(png, info, PNG_INFO_tRNS))
			{
				png_set_tRNS_to_alpha(png);
				break;
			};
			png_set_add_alpha(png, 0xFF, PNG_FILLER_AFTER);
			break;

		case PNG_COLOR_TYPE_RGB:
			png_set_add_alpha(png, 0xFF, PNG_FILLER_AFTER);
			break;

		case PNG_COLOR_TYPE_RGB_ALPHA:
			png_set_tRNS_to_alpha(png);
			break;
		};

		pixels.resize(image->size_x * image->size_y * 4);
		png_bytep row = (png_bytep)(alloca(image->size_x * 4));
		int passCount = png_set_interlace_handling(png);
		for (int pass = 0; pass < passCount; pass++)
		{
			for (uint32 y = 0; y < image->size_y; y++)
			{
				png_read_row(png, nullptr, row);
				memcpy(&pixels[y * image->size_x * 4], &row[0], image->size_x * 4);
			}
		}
		png_read_end(png, nullptr);
		png_destroy_read_struct(&png, &info, nullptr);
	}

	///

	{
		std::vector<uint8> gapc;
		std::vector<uint8> gapn;
		gapc.resize(pixels.size() / 4);
		gapn.resize(pixels.size() / 4);
		for (size_t i = 0; i < gapc.size(); i++) gapc[i] = pixels[i * 4 + 3] ? 0 : 1;

		int gaps;
		int size_y = image->size_y;
		int size_x = image->size_x;
		int max_y = size_y - 1;
		int max_x = size_x - 1;
		do
		{
			gaps = 0;
			for (int y = 0; y < size_y; y++)
			{
				for (int x = 0; x < size_x; x++)
				{
					int i = y * size_x + x;
					gapn[i] = gapc[i];
					if (!gapc[i]) continue;
					gaps++;

					int ncol = 0;
					if (x > 0)
					{
						int idx = (y + 0) * size_x + (x - 1);
						if (!gapc[idx]) ncol++;
					}
					if (x < max_x)
					{
						int idx = (y + 0) * size_x + (x + 1);
						if (!gapc[idx]) ncol++;
					}
					if (y > 0)
					{
						int idx = (y - 1) * size_x + (x + 0);
						if (!gapc[idx]) ncol++;
					}
					if (y < max_y)
					{
						int idx = (y + 1) * size_x + (x + 0);
						if (!gapc[idx]) ncol++;
					}
					if (ncol <= 0) continue;

					for (int c = 0; c < 3; c++)
					{
						int col = 0;
						if (x > 0)
						{
							int idx = (y + 0) * size_x + (x - 1);
							if (!gapc[idx]) col += pixels[idx * 4 + c];
						}
						if (x < max_x)
						{
							int idx = (y + 0) * size_x + (x + 1);
							if (!gapc[idx]) col += pixels[idx * 4 + c];
						}
						if (y > 0)
						{
							int idx = (y - 1) * size_x + (x + 0);
							if (!gapc[idx]) col += pixels[idx * 4 + c];
						}
						if (y < max_y)
						{
							int idx = (y + 1) * size_x + (x + 0);
							if (!gapc[idx]) col += pixels[idx * 4 + c];
						}
						pixels[i * 4 + c] = (uint8)(col / ncol);
						gapn[i] = 0;
					}
				}
			}
			gapc.swap(gapn);
		}
		while (gaps && (gaps != (size_x * size_y)));
	}

	///

	AtlasLayer *atlas = nullptr;
	uint32 blocks_x = CeilDiv(image->size_x, ATLAS_BLOCK_SIZE);
	uint32 blocks_y = CeilDiv(image->size_y, ATLAS_BLOCK_SIZE);
	for (size_t i = 0; !atlas && (i < _atlas_layers.size()); i++)
	{
		AtlasLayer *a = &_atlas_layers[i];
		if (a->free < (blocks_x * blocks_y)) continue;

		for (uint y = 0; !atlas && (y < (ATLAS_BLOCKS - blocks_y)); y++)
		{
			for (uint x = 0; x < (ATLAS_BLOCKS - blocks_x); x++)
			{
				bool clear = true;
				for (uint by = 0; clear && (by < blocks_y); by++)
				{
					for (uint bx = 0; bx < blocks_x; bx++)
					{
						if (!a->used[(y + by) * ATLAS_BLOCKS + (x + bx)]) continue;
						clear = false;
						break;
					}
				}
				if (!clear) continue;

				image->layer = (uint32)(i);
				image->offs_x = x * ATLAS_BLOCK_SIZE;
				image->offs_y = y * ATLAS_BLOCK_SIZE;
				atlas = a;
				break;
			}
		}
	}

	if (!atlas)
	{
		image->layer = (uint32)(_atlas_layers.size());
		image->offs_x = 0;
		image->offs_y = 0;

		_atlas_layers.emplace_back();
		atlas = &_atlas_layers.back();
		atlas->free = ATLAS_BLOCKS_COUNT;
		memset(atlas->used, 0, ATLAS_BLOCKS_COUNT);
		atlas->data_c.resize(ATLAS_SIZE * ATLAS_SIZE * 4);
		atlas->dirty = 0;

		_atlas_dirty = 1;
	}

	uint32 pbx = (image->offs_x / ATLAS_BLOCK_SIZE);
	uint32 pby = (image->offs_y / ATLAS_BLOCK_SIZE);

	for (uint by = 0; by < blocks_y; by++)
	{
		for (uint bx = 0; bx < blocks_x; bx++)
		{
			atlas->used[(pby + by) * ATLAS_BLOCKS + (pbx + bx)] = 1;
		}
	}
	atlas->free -= (blocks_x * blocks_y);
	atlas->dirty = 1;

	///

	int dst_pitch = ATLAS_SIZE;
	int src_pitch = image->size_x;
	uint32 *dst = (uint32*)(&atlas->data_c[(image->offs_y * dst_pitch + image->offs_x) * 4]);
	const uint32 *src = (uint32*)(pixels.data());

	uint32 size_x = image->size_x;
	uint32 size_y = image->size_y;
	uint32 fill_y = blocks_y * ATLAS_BLOCK_SIZE - size_y;
	
	for (uint32 y = 0; y < size_y; y++)
	{
		memcpy(dst + y * dst_pitch, src + y * src_pitch, src_pitch * 4); // fill image
	}
	for (uint32 y = 0; y < fill_y; y++)
	{
		memcpy(dst + (y + size_y) * dst_pitch, src + (size_y - 1) * src_pitch, src_pitch * 4); // fill bottom region
	}
	for (uint32 y = 0; y < (blocks_y * ATLAS_BLOCK_SIZE); y++)
	{
		for (uint32 x = size_x; x < (blocks_x * ATLAS_BLOCK_SIZE); x++)
		{
			*(dst + y * dst_pitch + x) = *(src + y * src_pitch + src_pitch - 1); // fill right region
		}
	}

	///

	return (uint32)(_image_entrys.size() - 1);
}

void UpdateImages(GLuint &texture, uint32 &layers)
{
	if (!_atlas_texture) glGenTextures(1, &_atlas_texture);
	texture = _atlas_texture;

	layers = (uint32)(_atlas_layers.size());

	int dirty = 0;
	for (size_t i = 0; i < layers; i++)
	{
		if (!_atlas_layers[i].dirty) continue;
		dirty = 1;
		break;
	}
	if (!_atlas_dirty && !dirty) return;

	glBindTexture(GL_TEXTURE_2D_ARRAY, _atlas_texture);
	{
		if (_atlas_dirty)
		{
			uint8 *tmp_c = MallocT<uint8>(layers * ATLAS_SIZE * ATLAS_SIZE * 4);
			for (size_t i = 0; i < layers; i++)
			{
				AtlasLayer *a = &_atlas_layers[i];
				memcpy(&tmp_c[i * ATLAS_SIZE * ATLAS_SIZE * 4], a->data_c.data(), ATLAS_SIZE * ATLAS_SIZE * 4);
				a->dirty = 0;
			}

			uint n = 0;
			uint size = ATLAS_SIZE;
			while (size >= ATLAS_BLOCKS)
			{
				glTexImage3D(GL_TEXTURE_2D_ARRAY, n, GL_RGBA8, size, size, (GLsizei)(layers), 0, GL_RGBA, GL_UNSIGNED_BYTE, tmp_c);

				size /= 2;
				for (uint i = 0; i < layers; i++)
				{
					uint8 *i_src = tmp_c + i * size * size * 4 * 4;
					uint8 *i_dst = tmp_c + i * size * size * 4;
					for (uint y = 0; y < size; y++)
					{
						for (uint x = 0; x < size; x++)
						{
							for (int c = 0; c < 3; c++)
							{
								i_dst[(y * size + x) * 4 + c] = 
									((int)(i_src[((y * 2 + 0) * size * 2 + (x * 2 + 0)) * 4 + c]) +
									 (int)(i_src[((y * 2 + 0) * size * 2 + (x * 2 + 1)) * 4 + c]) +
									 (int)(i_src[((y * 2 + 1) * size * 2 + (x * 2 + 0)) * 4 + c]) +
									 (int)(i_src[((y * 2 + 1) * size * 2 + (x * 2 + 1)) * 4 + c])) / 4;
							}
							i_dst[(y * size + x) * 4 + 3] = i_src[((y * 2 + 0) * size * 2 + (x * 2 + 0)) * 4 + 3];
						}
					}
				}
				n++;
			}
			free(tmp_c);

			glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, n - 1);
		}
		else
		{
			uint8 *tmp_c = MallocT<uint8>(ATLAS_SIZE * ATLAS_SIZE * 4);
			for (uint32 i = 0; i < layers; i++)
			{
				if (!_atlas_layers[i].dirty) continue;

				AtlasLayer *a = &_atlas_layers[i];
				memcpy(tmp_c, a->data_c.data(), ATLAS_SIZE * ATLAS_SIZE * 4);
				a->dirty = 0;

				uint n = 0;
				uint size = ATLAS_SIZE;
				while (size > ATLAS_BLOCK_SIZE)
				{
					glTexSubImage3D(GL_TEXTURE_2D_ARRAY, n, 0, 0, (GLint)(i), size, size, 1, GL_RGBA, GL_UNSIGNED_BYTE, tmp_c);

					size /= 2;
					uint8 *i_src = tmp_c;
					uint8 *i_dst = tmp_c;
					for (uint y = 0; y < size; y++)
					{
						for (uint x = 0; x < size; x++)
						{
							for (int c = 0; c < 3; c++)
							{
								i_dst[(y * size + x) * 4 + c] = 
									((int)(i_src[((y * 2 + 0) * size * 2 + (x * 2 + 0)) * 4 + c]) +
									 (int)(i_src[((y * 2 + 0) * size * 2 + (x * 2 + 1)) * 4 + c]) +
									 (int)(i_src[((y * 2 + 1) * size * 2 + (x * 2 + 0)) * 4 + c]) +
									 (int)(i_src[((y * 2 + 1) * size * 2 + (x * 2 + 1)) * 4 + c])) / 4;
							}
							i_dst[(y * size + x) * 4 + 3] = i_src[((y * 2 + 0) * size * 2 + (x * 2 + 0)) * 4 + 3];
						}
					}
					n++;
				}
			}
			free(tmp_c);
		}
	}
	glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

	_atlas_dirty = 0;
}

const ImageEntry *GetImageEntry(uint32 index)
{
	if (index >= _image_entrys.size()) return nullptr;
	return &_image_entrys[index];
}

void FreeImages()
{
	_atlas_layers.clear();
	_image_entrys.clear();
	_entrys_map.clear();
}
