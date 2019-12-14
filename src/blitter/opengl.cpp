/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file opengl.cpp Implementation of the OpenGL blitter. */

#include "../stdafx.h"
#include "../core/math_func.hpp"
#include "../zoom_func.h"
#include "../fileio_func.h"
#include "../table/sprites.h"
#include "../settings_type.h"
#include "opengl.hpp"
#include "common.hpp"
#include <cmath>

#include "../safeguards.h"

#define ATLAS_SIZE		1024		// atlas texture layer dimension
#define ATLAS_ALIGN		8			// alignment of sprites in the atlas
#define DOWNSCALE		2			// downscale of the color sprites

#define glBlitFramebufferLT(sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, mask, filter) glBlitFramebuffer(sx0, _size_y - (sy0), sx1, _size_y - (sy1), dx0, _size_y - (dy0), dx1, _size_y - (dy1), mask, filter);

extern DrawPixelInfo _screen;

/** Instantiation of the OpenGL blitter factory. */
static FBlitter_OpenGL iFBlitter_OpenGL;

static inline Colour LookupColourInPalette(uint index)
{
	return _cur_palette.palette[index];
}

/* Load an OpengGL shader */
GLuint ShaderLoad(const char *file, GLenum type, const char *opts = nullptr)
{
	std::vector<char> text;
	{
		size_t size;
		FILE *f = FioFOpenFile(file, "rb", Subdirectory::BASE_DIR, &size);
		if (!f)
		{
			error("Shader '%s' not found.", file);
			return (GLuint)(-1);
		}
		text.resize(size + 1);
		size_t result = fread(text.data(), 1, size, f);
		text[text.size() - 1] = 0;
		FioFCloseFile(f);
		if (result != size)
		{
			error("Can't read shader '%s'.", file);
			return (GLuint)(-1);
		}
	}

	std::string header;
	if (GLAD_GL_VERSION_4_3)
	{
		header += "#version 400\n";
		header += "#extension GL_ARB_texture_query_lod : require\n";
	}
	else if (GLAD_GL_VERSION_3_3)
	{
		header += "#version 330\n";
	}
	else
	{
		header += "#version 300 es\n";
		header += "precision highp float;\n";
		header += "precision highp sampler2DArray;\n";
		header += "precision highp sampler2DShadow;\n";
	}
	if (opts)
	{
		header += std::string(opts);
	}
	if (!GLAD_GL_VERSION_4_3)
	{
		header += "#define _GL_VERSION_3_3\n";
	}

	GLuint shader = glCreateShader(type);
	{
		const char *strings[2] = { header.c_str(), text.data() };
		glShaderSource(shader, 2, strings, nullptr);
	}
	glCompileShader(shader);
	{
		GLint status;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
		{
			GLint logSize = 0;
			glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logSize);
			if (logSize)
			{
				std::vector<char> log;
				log.resize(logSize);
				glGetShaderInfoLog(shader, (GLsizei)(log.size()), &logSize, log.data());
				DEBUG(driver, 0, "Shader '%s' compilation:\r\n%s", file, log.data());
//				OutputDebugstringA(log.Get(0));
			}
		}
		if (status != GL_TRUE)
		{
			error("Shader '%s' compilation fault.", file);
			glDeleteShader(shader);
			return (GLuint)(-1);
		}
	}
	return shader;
}

/* Link an OpenGL program */
GLuint ProgramLink(GLuint vs, GLuint fs)
{
	GLuint program = glCreateProgram();
	glAttachShader(program, vs);
	glAttachShader(program, fs);
	glLinkProgram(program);
	glDeleteShader(vs);
	glDeleteShader(fs);
	{
		GLint status;
		glGetProgramiv(program, GL_LINK_STATUS, &status);
		{
			GLint logSize = 0;
			glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logSize);
			if(logSize)
			{
				std::vector<char> log;
				log.resize(logSize);
				glGetProgramInfoLog(program, (GLsizei)(log.size()), &logSize, log.data());
				DEBUG(driver, 0, "Program linkage:\r\n%s", log.data());
//				OutputDebugstringA(log.Get(0));
			}
		}
		if (status != GL_TRUE)
		{
			error("Program linkage fault.");
			glDeleteProgram(program);
			return (GLuint)(-1);
		}
	}
	return program;
}

Blitter_OpenGL::Blitter_OpenGL()
{
	_pal_texture = 0;
	_pal_dirty = 0;

	_recol_pal_texture = 0;
	_recol_pal_dirty = 0;
	_recol_pal_map.resize(1);
	_recol_pal_map[0] = 0;
	_atlas_table_dim = 0;

	_atlas_texture_c = 0;
	_atlas_texture_m = 0;
	_atlas_dirty = 0;

	_checker_texture = 0;

	_frame = 0;
	_frame_buffer_ds = 0;
	_frame_dirty = 1;
	_flushed = 0;

	_batch_program = 0;
	_blit_program = 0;

	_vertex_buffer = 0;
	_index_buffer = 0;
	_overlay_z = 0.5f;

	_pixel_texture = 0;
	_pixel_program = 0;
	_pixel_flush = 0;

	_scroll.w = 0;
	_scroll.h = 0;

	ReserveQuad();

	_size_x = _screen.width;
	_size_y = _screen.height;

	_pixel_size_x = _size_x;
	_pixel_size_y = _size_y;
	_pixel_buffer.resize(_pixel_size_x * _pixel_size_y * 4);
	memset(_pixel_buffer.data(), 0, _pixel_size_x * _pixel_size_y * 4);

	_pixel_area.left = _pixel_size_x;
	_pixel_area.top = _pixel_size_y;
	_pixel_area.right = 0;
	_pixel_area.bottom = 0;

	_multisample_set = _settings_client.gui.opengl_multisample;
}

Blitter_OpenGL::~Blitter_OpenGL()
{
}

/* Copy the sprite color data for debug */
bool Blitter_OpenGL::CopySprite(SpriteID s, uint32 &size_x, uint32 &size_y, void *data)
{
	const Sprite *sp = GetSprite(s, ST_NORMAL);
	AtlasSprite *asp = (AtlasSprite*)(sp->data);

	uint32 sx = sp->width  >> DOWNSCALE;
	uint32 sy = sp->height >> DOWNSCALE;
	if (data && ((size_x < sx) || (size_y < sy))) return false;

	if (!data)
	{
		size_x = sx;
		size_y = sy;
		return true;
	}

	uint8 *dst = (uint8*)(data);
	Colour *src_c = &_atlas[asp->atlas].data_c[asp->y_offs * ATLAS_SIZE + asp->x_offs];
	uint8  *src_m = &_atlas[asp->atlas].data_m[asp->y_offs * ATLAS_SIZE + asp->x_offs];
	for (uint32 y = 0; y < sy; y++)
	{
		for (uint32 x = 0; x < sx; x++)
		{
			dst[0] = src_c->b;
			dst[1] = src_c->g;
			dst[2] = src_c->r;
			dst[3] = src_m[0];

			src_c++;
			src_m++;
			dst += 4;
		}
		src_c += ATLAS_SIZE - sx;
		src_m += ATLAS_SIZE - sx;
		dst += (size_x - sx) * 4;
	}
	return true;
}

/* Cache recolor palette */
uint32 Blitter_OpenGL::CachePal(PaletteID pal)
{
	if ((_recol_pal_map.size() > pal) && (_recol_pal_map[pal] < _recol_pal.size()))return _recol_pal_map[pal];
	
	if (pal >= _recol_pal_map.size())
	{
		size_t size = _recol_pal_map.size();
		_recol_pal_map.resize(pal + 1);
		for (size_t i = size; i < _recol_pal_map.size(); i++) _recol_pal_map[i] = (uint32)(-1);
	}

	_recol_pal_map[pal] = (uint32)(_recol_pal.size());
	_recol_pal_dirty = 1;

	_recol_pal.emplace_back();
	PalEntry &pe = _recol_pal.back();
	pe.data = (GetNonSprite(pal, ST_RECOLOUR) + 1);
	return _recol_pal_map[pal];
}

/* Update screen animation palette */
void Blitter_OpenGL::UpdatePal()
{
	if (!_pal_dirty) return;
	if (!_pal_texture)
	{
		glGenTextures(1, &_pal_texture);

		glBindTexture(GL_TEXTURE_1D, _pal_texture);
		glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA8, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAX_LEVEL, 1);
		glBindTexture(GL_TEXTURE_1D, 0);
	}
	
	glBindTexture(GL_TEXTURE_1D, _pal_texture);
	glTexSubImage1D(GL_TEXTURE_1D, 0, 0, 256, GL_RGBA, GL_UNSIGNED_BYTE, _pal_data);
	glBindTexture(GL_TEXTURE_1D, 0);

	_pal_dirty = 0;
}

/* Update recolor palette cache */
void Blitter_OpenGL::UpdatePalCache()
{
	if (!_recol_pal_dirty) return;
	if (!_recol_pal_texture) glGenTextures(1, &_recol_pal_texture);
	
	size_t pal_count = _recol_pal.size();
	uint8 *pixels = MallocT<uint8>(256 * (pal_count + 1));
	for (size_t i = 0; i < pal_count; i++)
	{
		PalEntry *p = &_recol_pal[i];
		memcpy(&pixels[i * 256], p->data, 256);
	}

	/* this is an atlas edge offset table for the OpenGL 3.3 viewport rendering path,
	 * it's placed there, to not make a new texture for it, or use the shader uniform */
	{
		uint8 *table = &pixels[256 * pal_count];
		for (size_t i = 0; i < 256; i++)
		{
			float delta_max_sqr = (float)(i * 4);
			float lod = log2f(sqrtf(delta_max_sqr));
			int n = (int)(ceilf(max(lod, 0.0f)) + 0.5f);
			float edge = (0.5f / (float)(_atlas_table_dim >> n)) * (2 * _atlas_table_dim);
			table[i] = (int)(edge + 0.5f);
		}
	}
	
	glBindTexture(GL_TEXTURE_2D, _recol_pal_texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 256, (GLsizei)(pal_count + 1), 0, GL_RED, GL_UNSIGNED_BYTE, pixels);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1);
	glBindTexture(GL_TEXTURE_2D, 0);

	free(pixels);
	_recol_pal_dirty = 0;
}

/* Update atlas texture */
void Blitter_OpenGL::UpdateAtlas()
{
	size_t atlas_count = _atlas.size();
	int dirty = atlas_count > 0 ? _atlas[atlas_count - 1].dirty : 0;

	if (!_atlas_dirty && !dirty) return;

	if (!_atlas_texture_c) glGenTextures(1, &_atlas_texture_c);
	if (!_atlas_texture_m) glGenTextures(1, &_atlas_texture_m);

	glBindTexture(GL_TEXTURE_2D_ARRAY, _atlas_texture_c);
	{
		if (_atlas_dirty)
		{
			uint8 *tmp_c = MallocT<uint8>(atlas_count * ATLAS_SIZE * ATLAS_SIZE * 4);
			for (size_t i = 0; i < atlas_count; i++)
			{
				AtlasLayer *a = &_atlas[i];
				memcpy(&tmp_c[i * ATLAS_SIZE * ATLAS_SIZE * 4], a->data_c.data(), ATLAS_SIZE * ATLAS_SIZE * 4);
			}

			uint n = 0;
			uint size = ATLAS_SIZE;
			while (size > 16)
			{
				glTexImage3D(GL_TEXTURE_2D_ARRAY, n, GL_RGBA8, size, size, (GLsizei)(atlas_count), 0, GL_RGBA, GL_UNSIGNED_BYTE, tmp_c);

				size /= 2;
				for (uint i = 0; i < atlas_count; i++)
				{
					uint8 *i_src = tmp_c + i * size * size * 4 * 4;
					uint8 *i_dst = tmp_c + i * size * size * 4;
					for (uint y = 0; y < size; y++)
					{
						for (uint x = 0; x < size; x++)
						{
							i_dst[(y * size + x) * 4 + 0] = i_src[((y * 2 + 0) * size * 2 + (x * 2 + 0)) * 4 + 0];
							i_dst[(y * size + x) * 4 + 1] = i_src[((y * 2 + 0) * size * 2 + (x * 2 + 0)) * 4 + 1];
							i_dst[(y * size + x) * 4 + 2] = i_src[((y * 2 + 0) * size * 2 + (x * 2 + 0)) * 4 + 2];
							i_dst[(y * size + x) * 4 + 3] = i_src[((y * 2 + 0) * size * 2 + (x * 2 + 0)) * 4 + 3];
						}
					}
				}
				n++;
			}
			free(tmp_c);

			glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
			glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, 4);
		}
		else
		{
			size_t i = atlas_count - 1;
			AtlasLayer *a = &_atlas[i];
			uint8 *tmp_c = MallocT<uint8>(ATLAS_SIZE * ATLAS_SIZE * 4);
			memcpy(tmp_c, a->data_c.data(), ATLAS_SIZE * ATLAS_SIZE * 4);

			uint n = 0;
			uint size = ATLAS_SIZE;
			while (size > 16)
			{
				glTexSubImage3D(GL_TEXTURE_2D_ARRAY, n, 0, 0, (GLint)(i), size, size, 1, GL_RGBA, GL_UNSIGNED_BYTE, tmp_c);

				size /= 2;
				for (uint y = 0; y < size; y++)
				{
					for (uint x = 0; x < size; x++)
					{
						tmp_c[(y * size + x) * 4 + 0] = tmp_c[((y * 2 + 0) * size * 2 + (x * 2 + 0)) * 4 + 0];
						tmp_c[(y * size + x) * 4 + 1] = tmp_c[((y * 2 + 0) * size * 2 + (x * 2 + 0)) * 4 + 1];
						tmp_c[(y * size + x) * 4 + 2] = tmp_c[((y * 2 + 0) * size * 2 + (x * 2 + 0)) * 4 + 2];
						tmp_c[(y * size + x) * 4 + 3] = tmp_c[((y * 2 + 0) * size * 2 + (x * 2 + 0)) * 4 + 3];
					}
				}
				n++;
			}
			free(tmp_c);
		}
	}
	glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

	glBindTexture(GL_TEXTURE_2D_ARRAY, _atlas_texture_m);
	{
		if (_atlas_dirty)
		{
			uint8 *tmp_m = MallocT<uint8>(atlas_count * ATLAS_SIZE * ATLAS_SIZE);
			for (uint i = 0; i < atlas_count; i++)
			{
				AtlasLayer *a = &_atlas[i];
				memcpy(&tmp_m[i * ATLAS_SIZE * ATLAS_SIZE], a->data_m.data(), ATLAS_SIZE * ATLAS_SIZE);
				a->dirty = 0;
			}

			uint n = 0;
			uint size = ATLAS_SIZE;
			while (size > 16)
			{
				glTexImage3D(GL_TEXTURE_2D_ARRAY, n, GL_R8, size, size, (GLsizei)(atlas_count), 0, GL_RED, GL_UNSIGNED_BYTE, tmp_m);

				size /= 2;
				for (size_t i = 0; i < atlas_count; i++)
				{
					uint8 *i_src = tmp_m + i * size * size * 4;
					uint8 *i_dst = tmp_m + i * size * size;
					for (uint y = 0; y < size; y++)
					{
						for (uint x = 0; x < size; x++)
						{
							i_dst[y * size + x] = i_src[(y * 2 + 0) * size * 2 + (x * 2 + 0)];
						}
					}
				}
				n++;
			}
			free(tmp_m);

			glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
			glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, 4);
		}
		else
		{
			size_t i = atlas_count - 1;
			AtlasLayer *a = &_atlas[i];
			uint8 *tmp_m = MallocT<uint8>(ATLAS_SIZE * ATLAS_SIZE);
			memcpy(tmp_m, a->data_m.data(), ATLAS_SIZE * ATLAS_SIZE);

			uint n = 0;
			uint size = ATLAS_SIZE;
			while (size > 16)
			{
				glTexSubImage3D(GL_TEXTURE_2D_ARRAY, n, 0, 0, (GLint)(i), size, size, 1, GL_RED, GL_UNSIGNED_BYTE, tmp_m);

				size /= 2;
				for (uint y = 0; y < size; y++)
				{
					for (uint x = 0; x < size; x++)
					{
						tmp_m[y * size + x] = tmp_m[(y * 2 + 0) * size * 2 + (x * 2 + 0)];
					}
				}
				n++;
			}
			free(tmp_m);
		}
	}
	glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
	
	if (dirty) _atlas[atlas_count - 1].dirty = 0;
	_atlas_dirty = 0;
}

/* Update checker texture */
void Blitter_OpenGL::UpdateChecker()
{
	if (_checker_texture) return;

	uint8 pixels[16*16];
	for (uint32 y = 0; y < 16; y++)
	{
		for (uint32 x = 0; x < 16; x++)
		{
			pixels[y * 16 + x]=((x + (y % 2)) % 2) ? 255 : 0;
		}
	}

	glGenTextures(1, &_checker_texture);
	glBindTexture(GL_TEXTURE_2D, _checker_texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 16, 16, 0, GL_RED, GL_UNSIGNED_BYTE, pixels);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1);
	glBindTexture(GL_TEXTURE_2D, 0);
}

/* Update frame buffer objects */
void Blitter_OpenGL::UpdateFrame()
{
	if (!_frame_dirty && (_settings_client.gui.opengl_multisample == _multisample_set)) return;

	if (!_frame_buffer_ds)
	{
		glGenFramebuffers(1, &_frame);

		glGenTextures(1, &_frame_texture_c);
		glGenTextures(1, &_frame_texture_m);

		glGenRenderbuffers(1, &_frame_buffer_ds);

		glGenFramebuffers(1, &_frame_texture_copy_c);
		glGenFramebuffers(1, &_frame_texture_copy_m);

		glGenFramebuffers(1, &_frame_tmp_copy_c);
		glGenFramebuffers(1, &_frame_tmp_copy_m);
	}
	else
	{
		if (_multisample_set > 0)
		{
			glDeleteRenderbuffers(1, &_frame_buffer_c);
			glDeleteRenderbuffers(1, &_frame_buffer_m);

			glDeleteFramebuffers(1, &_frame_buffer_copy_c);
			glDeleteFramebuffers(1, &_frame_buffer_copy_m);

			glDeleteRenderbuffers(1, &_frame_tmp_c);
			glDeleteRenderbuffers(1, &_frame_tmp_m);
		}
		else
		{
			glDeleteTextures(1, &_frame_tmp_c);
			glDeleteTextures(1, &_frame_tmp_m);
		}
	}
	{
		if (_settings_client.gui.opengl_multisample > 0)
		{
			glGenRenderbuffers(1, &_frame_buffer_c);
			glGenRenderbuffers(1, &_frame_buffer_m);

			glGenFramebuffers(1, &_frame_buffer_copy_c);
			glGenFramebuffers(1, &_frame_buffer_copy_m);

			glGenRenderbuffers(1, &_frame_tmp_c);
			glGenRenderbuffers(1, &_frame_tmp_m);
		}
		else
		{
			glGenTextures(1, &_frame_tmp_c);
			glGenTextures(1, &_frame_tmp_m);
		}
	}
	
	_size_x = _screen.width;
	_size_y = _screen.height;
	_multisample_set = _settings_client.gui.opengl_multisample;

	int sc[7][2] =
	{
		{ 1,  1 }, // disabled
		{ 2,  2 }, // MSAA 2x
		{ 4,  4 }, // MSAA 4x
		{ 4,  8 }, // CSAA 8x
		{ 8,  8 }, // MSAA 8x
		{ 4, 16 }, // CSAA 16x
		{ 8, 16 }, // CSAA 16xQ
	};

	int samples = sc[_multisample_set][0];
	int coverage = sc[_multisample_set][1];

	glBindTexture(GL_TEXTURE_2D, _frame_texture_c);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, _size_x, _size_y, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);

	glBindTexture(GL_TEXTURE_2D, _frame_texture_m);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, _size_x, _size_y, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);

	glBindFramebuffer(GL_FRAMEBUFFER, _frame);
	if (samples > 1)
	{
		bool csaa = GLAD_GL_NV_framebuffer_multisample_coverage;

		glBindRenderbuffer(GL_RENDERBUFFER, _frame_buffer_ds);
		if (csaa)
		{
			glRenderbufferStorageMultisampleCoverageNV(GL_RENDERBUFFER, coverage, samples, GL_DEPTH24_STENCIL8, _size_x, _size_y);
		}
		else
		{
			glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, _size_x, _size_y);
		}
		glBindRenderbuffer(GL_RENDERBUFFER, 0);

		glBindRenderbuffer(GL_RENDERBUFFER, _frame_buffer_c);
		if (csaa)
		{
			glRenderbufferStorageMultisampleCoverageNV(GL_RENDERBUFFER, coverage, samples, GL_RGBA8, _size_x, _size_y);
		}
		else
		{
			glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, _size_x, _size_y);
		}		
		glBindRenderbuffer(GL_RENDERBUFFER, 0);

		glBindRenderbuffer(GL_RENDERBUFFER, _frame_buffer_m);
		if (csaa)
		{
			glRenderbufferStorageMultisampleCoverageNV(GL_RENDERBUFFER, coverage, samples, GL_R8, _size_x, _size_y);
		}
		else
		{
			glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_R8, _size_x, _size_y);
		}
		glBindRenderbuffer(GL_RENDERBUFFER, 0);

		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, _frame_buffer_c);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_RENDERBUFFER, _frame_buffer_m);

		glBindRenderbuffer(GL_RENDERBUFFER, _frame_tmp_c);
		if (csaa)
		{
			glRenderbufferStorageMultisampleCoverageNV(GL_RENDERBUFFER, coverage, samples, GL_RGBA8, _size_x, _size_y);
		}
		else
		{
			glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, _size_x, _size_y);
		}
		glBindRenderbuffer(GL_RENDERBUFFER, 0);

		glBindRenderbuffer(GL_RENDERBUFFER, _frame_tmp_m);
		if (csaa)
		{
			glRenderbufferStorageMultisampleCoverageNV(GL_RENDERBUFFER, coverage, samples, GL_R8, _size_x, _size_y);
		}
		else
		{
			glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_R8, _size_x, _size_y);
		}
		glBindRenderbuffer(GL_RENDERBUFFER, 0);
	}
	else
	{
		glBindRenderbuffer(GL_RENDERBUFFER, _frame_buffer_ds);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, _size_x, _size_y);
		glBindRenderbuffer(GL_RENDERBUFFER, 0);

		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _frame_texture_c, 0);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, _frame_texture_m, 0);

		glBindTexture(GL_TEXTURE_2D, _frame_tmp_c);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, _size_x, _size_y, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glBindTexture(GL_TEXTURE_2D, 0);

		glBindTexture(GL_TEXTURE_2D, _frame_tmp_m);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, _size_x, _size_y, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glBindTexture(GL_TEXTURE_2D, 0);
	}
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, _frame_buffer_ds);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, _frame_buffer_ds);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	if (_multisample_set > 0)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, _frame_buffer_copy_c);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, _frame_buffer_c);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		glBindFramebuffer(GL_FRAMEBUFFER, _frame_buffer_copy_m);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, _frame_buffer_m);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		
		glBindFramebuffer(GL_FRAMEBUFFER, _frame_tmp_copy_c);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, _frame_tmp_c);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		glBindFramebuffer(GL_FRAMEBUFFER, _frame_tmp_copy_m);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, _frame_tmp_m);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}
	else
	{
		glBindFramebuffer(GL_FRAMEBUFFER, _frame_tmp_copy_c);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _frame_tmp_c, 0);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		glBindFramebuffer(GL_FRAMEBUFFER, _frame_tmp_copy_m);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _frame_tmp_m, 0);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	glBindFramebuffer(GL_FRAMEBUFFER, _frame_texture_copy_c);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _frame_texture_c, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	glBindFramebuffer(GL_FRAMEBUFFER, _frame_texture_copy_m);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _frame_texture_m, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	
	MarkWholeScreenDirty();
	_frame_dirty = 0;
}

/* Update batch draw OpenGL program */
void Blitter_OpenGL::UpdateBatchProgram()
{
	if (_batch_program) return;
	
	GLuint vs = ShaderLoad("shader/batch.vert", GL_VERTEX_SHADER);
	GLuint fs = ShaderLoad("shader/batch.frag", GL_FRAGMENT_SHADER);
	_batch_program = ProgramLink(vs, fs);
	
	_batch_attribs_link[0] = glGetAttribLocation(_batch_program, "in_pos");
	_batch_attribs_link[1] = glGetAttribLocation(_batch_program, "in_tex");
	_batch_attribs_link[2] = glGetAttribLocation(_batch_program, "in_blend");
	_batch_attribs_link[3] = glGetAttribLocation(_batch_program, "in_color");
	_batch_attribs_link[4] = glGetAttribLocation(_batch_program, "in_fade");
	
	glGenVertexArrays(1, &_batch_vertex_format);

	glBindVertexArray(_batch_vertex_format);
	for (int i = 0; i < 5; i++) glEnableVertexAttribArray((GLuint)(_batch_attribs_link[i]));
	if (GLAD_GL_VERSION_4_3)
	{
		for (int i = 0; i < 5; i++) glVertexAttribBinding((GLuint)(_batch_attribs_link[i]), 0);
		glVertexAttribFormat((GLuint)(_batch_attribs_link[0]), 4, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(Vertex, pos)));
		glVertexAttribFormat((GLuint)(_batch_attribs_link[1]), 4, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(Vertex, tex)));
		glVertexAttribFormat((GLuint)(_batch_attribs_link[2]), 4, GL_UNSIGNED_BYTE, GL_TRUE, (GLuint)(cpp_offsetof(Vertex, blend)));
		glVertexAttribFormat((GLuint)(_batch_attribs_link[3]), 4, GL_UNSIGNED_BYTE, GL_TRUE, (GLuint)(cpp_offsetof(Vertex, color)));
		glVertexAttribFormat((GLuint)(_batch_attribs_link[4]), 4, GL_UNSIGNED_BYTE, GL_TRUE, (GLuint)(cpp_offsetof(Vertex, fade)));
	}
	glBindVertexArray(0);

	_batch_uniforms_link[0] = glGetUniformLocation(_batch_program, "dim_pos");
	_batch_uniforms_link[1] = glGetUniformLocation(_batch_program, "dim_tex");
	_batch_uniforms_link[2] = glGetUniformLocation(_batch_program, "pal");
	_batch_uniforms_link[3] = glGetUniformLocation(_batch_program, "recol_pal");
	_batch_uniforms_link[4] = glGetUniformLocation(_batch_program, "checker");
	_batch_uniforms_link[5] = glGetUniformLocation(_batch_program, "atlas_c");
	_batch_uniforms_link[6] = glGetUniformLocation(_batch_program, "atlas_m");
	_batch_uniforms_link[7] = glGetUniformLocation(_batch_program, "color_c");
}

/* Update blit OpenGL program */
void Blitter_OpenGL::UpdateBlitProgram()
{
	if (_blit_program) return;

	GLuint vs = ShaderLoad("shader/blit.vert", GL_VERTEX_SHADER);
	GLuint fs = ShaderLoad("shader/blit.frag", GL_FRAGMENT_SHADER);
	_blit_program = ProgramLink(vs, fs);

	_blit_attribs_link[0] = glGetAttribLocation(_blit_program, "in_pos");
	_blit_attribs_link[1] = glGetAttribLocation(_blit_program, "in_tex");

	glGenVertexArrays(1, &_blit_vertex_format);

	glBindVertexArray(_blit_vertex_format);
	for (int i = 0; i < 2; i++) glEnableVertexAttribArray((GLuint)(_blit_attribs_link[i]));
	if (GLAD_GL_VERSION_4_3)
	{
		for (int i = 0; i < 2; i++) glVertexAttribBinding((GLuint)(_blit_attribs_link[i]), 0);
		glVertexAttribFormat((GLuint)(_blit_attribs_link[0]), 2, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(Vertex, pos)));
		glVertexAttribFormat((GLuint)(_blit_attribs_link[1]), 4, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(Vertex, tex)));
	}
	glBindVertexArray(0);

	_blit_uniforms_link[0] = glGetUniformLocation(_blit_program, "dim_pos");
	_blit_uniforms_link[1] = glGetUniformLocation(_blit_program, "pal");
	_blit_uniforms_link[2] = glGetUniformLocation(_blit_program, "screen_c");
	_blit_uniforms_link[3] = glGetUniformLocation(_blit_program, "screen_m");
}

void Blitter_OpenGL::UpdatePixelProgram()
{
	if (_pixel_program) return;
	
	GLuint vs = ShaderLoad("shader/pixels.vert", GL_VERTEX_SHADER);
	GLuint fs = ShaderLoad("shader/pixels.frag", GL_FRAGMENT_SHADER);
	_pixel_program = ProgramLink(vs, fs);

	_pixel_attribs_link[0] = glGetAttribLocation(_pixel_program, "in_pos");
	_pixel_attribs_link[1] = glGetAttribLocation(_pixel_program, "in_tex");

	glGenVertexArrays(1, &_pixel_vertex_format);

	glBindVertexArray(_pixel_vertex_format);
	for (int i = 0; i < 2; i++) glEnableVertexAttribArray((GLuint)(_pixel_attribs_link[i]));
	if (GLAD_GL_VERSION_4_3)
	{
		for (int i = 0; i < 2; i++) glVertexAttribBinding((GLuint)(_pixel_attribs_link[i]), 0);
		glVertexAttribFormat((GLuint)(_pixel_attribs_link[0]), 2, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(Vertex, pos)));
		glVertexAttribFormat((GLuint)(_pixel_attribs_link[1]), 4, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(Vertex, tex)));
	}
	glBindVertexArray(0);

	_pixel_uniforms_link[0] = glGetUniformLocation(_pixel_program, "dim_pos");
	_pixel_uniforms_link[1] = glGetUniformLocation(_pixel_program, "dim_tex");
	_pixel_uniforms_link[2] = glGetUniformLocation(_pixel_program, "pixels");
}

/* Reserve a screen draw quad in the batch buffer */
void Blitter_OpenGL::ReserveQuad()
{
	uint32 index = (uint32)(_vertex.size());
	_index.resize(_index.size() + 6);
	uint32 *i = &_index[_index.size() - 6];

	i[0] = index + 0; i[1] = index + 1; i[2] = index + 3;
	i[3] = index + 1; i[4] = index + 2; i[5] = index + 3;

	_vertex.resize(_vertex.size() + 4);
	Vertex *v = &_vertex[_vertex.size() - 4];

	v[0].pos.x = 0;		  v[0].pos.y = 0;
	v[1].pos.x = _size_x; v[1].pos.y = 0;
	v[2].pos.x = _size_x; v[2].pos.y = _size_y;
	v[3].pos.x = 0;		  v[3].pos.y = _size_y;

	v[0].tex.x = 0; v[0].tex.y = 0;
	v[1].tex.x = 1; v[1].tex.y = 0;
	v[2].tex.x = 1; v[2].tex.y = 1;
	v[3].tex.x = 0; v[3].tex.y = 1;
}

/* Update batch buffers */
void Blitter_OpenGL::UpdateBuffers(uint &vertex_count, uint &index_count)
{
	if (!_vertex_buffer) glGenBuffers(1, &_vertex_buffer);
	if (!_index_buffer)  glGenBuffers(1, &_index_buffer);

	vertex_count = (uint)(_vertex.size());
	index_count = (uint)(_index.size());
	{
		glBindBuffer(GL_ARRAY_BUFFER, _vertex_buffer);
		glBufferData(GL_ARRAY_BUFFER, vertex_count * sizeof(Vertex), _vertex.data(), GL_STREAM_DRAW);
		glBindBuffer(GL_ARRAY_BUFFER, 0);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _index_buffer);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, index_count * sizeof(uint32), _index.data(), GL_STREAM_DRAW);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

		_index.clear();
		_vertex.clear();
	}

	ReserveQuad();
}

/* Flush SetPixel buffer */
void Blitter_OpenGL::FlushPixels()
{
	if (!_pixel_flush) return;

	if (!_pixel_texture)
	{
		glGenTextures(1, &_pixel_texture);

		glBindTexture(GL_TEXTURE_2D, _pixel_texture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, _pixel_size_x, _pixel_size_y, 0, GL_RGBA, GL_UNSIGNED_BYTE, _pixel_buffer.data());
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glBindTexture(GL_TEXTURE_2D, 0);
	}
	
	Flush();

	glBindFramebuffer(GL_FRAMEBUFFER, _frame);
	GLuint drawBuffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
	glReadBuffer(GL_COLOR_ATTACHMENT0);
	glDrawBuffers(2, drawBuffers);

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);

	glEnable(GL_BLEND);
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);

	glUseProgram(_pixel_program);
	{
		float dx =  2.0f / (float)(_size_x);
		float dy = -2.0f / (float)(_size_y);
		float tx = _pixel_area.left / (float)(_pixel_size_x);
		float ty = _pixel_area.top / (float)(_pixel_size_y);
		float tw = (_pixel_area.right - _pixel_area.left) / (float)(_pixel_size_x);
		float th = (_pixel_area.bottom - _pixel_area.top) / (float)(_pixel_size_y);

		glUniform4f(_pixel_uniforms_link[0], dx * tw, dy * th, 2.0f * tx - 1.0f, -2.0f * ty + 1.0f);
		glUniform4f(_pixel_uniforms_link[1], tw, th, tx, ty);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, _pixel_texture);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, _pixel_size_x);
		glTexSubImage2D(GL_TEXTURE_2D, 0, _pixel_area.left, _pixel_area.top, _pixel_area.right - _pixel_area.left, _pixel_area.bottom - _pixel_area.top, GL_RGBA, GL_UNSIGNED_BYTE, &_pixel_buffer[(_pixel_area.top * _pixel_size_x + _pixel_area.left) * 4]);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
		glUniform1i(_pixel_uniforms_link[2], 0);

		glBindVertexArray(_pixel_vertex_format);
		if (GLAD_GL_VERSION_4_3)
		{
			glBindVertexBuffer(0, _vertex_buffer, 0, sizeof(Vertex));
		}
		else
		{
			glBindBuffer(GL_ARRAY_BUFFER, _vertex_buffer);
			glVertexAttribPointer((GLuint)(_pixel_attribs_link[0]), 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(cpp_offsetof(Vertex, pos)));
			glVertexAttribPointer((GLuint)(_pixel_attribs_link[1]), 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(cpp_offsetof(Vertex, tex)));
			glBindBuffer(GL_ARRAY_BUFFER, 0);
		}
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _index_buffer);
		glDrawElements(GL_TRIANGLES, (GLuint)(6), GL_UNSIGNED_INT, nullptr);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		glBindVertexArray(0);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, 0);
	}
	glUseProgram(0);

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);

	glDisable(GL_BLEND);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	if ((_pixel_size_x != _size_x) || (_pixel_size_y != _size_y))
	{
		_pixel_size_x = _size_x;
		_pixel_size_y = _size_y;
		_pixel_buffer.resize(_pixel_size_x * _pixel_size_y * 4);
		memset(_pixel_buffer.data(), 0, _pixel_size_x * _pixel_size_y * 4);

		glBindTexture(GL_TEXTURE_2D, _pixel_texture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, _pixel_size_x, _pixel_size_y, 0, GL_RGBA, GL_UNSIGNED_BYTE, _pixel_buffer.data());
		glBindTexture(GL_TEXTURE_2D, 0);
	}
	memset(_pixel_buffer.data(), 0, _pixel_size_x * _pixel_size_y * 4);

	_pixel_area.left = _pixel_size_x;
	_pixel_area.top = _pixel_size_y;
	_pixel_area.right = 0;
	_pixel_area.bottom = 0;

	_pixel_flush = 0;
}

/* Draw batch buffer */
void Blitter_OpenGL::DrawBuffers(int size_x, int size_y)
{
	uint vertex_count = 0;
	uint index_count = 0;
	UpdateBuffers(vertex_count, index_count);
	if (!vertex_count || !index_count) return;

	glUseProgram(_batch_program);
	{
		glUniform4f(_batch_uniforms_link[0], +2.0f / (float)(size_x), -2.0f / (float)(size_y), -1.0f, +1.0f);
		glUniform4f(_batch_uniforms_link[1], 1.0f / (float)(ATLAS_SIZE), 1.0f / (float)(ATLAS_SIZE), 1.0f, 1.0f / (float)(_recol_pal.size() + 1));

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_1D, _pal_texture);
		glUniform1i(_batch_uniforms_link[2], 0);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, _recol_pal_texture);
		glUniform1i(_batch_uniforms_link[3], 1);

		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, _checker_texture);
		glUniform1i(_batch_uniforms_link[4], 2);

		glActiveTexture(GL_TEXTURE3);
		glBindTexture(GL_TEXTURE_2D_ARRAY, _atlas_texture_c);
		glUniform1i(_batch_uniforms_link[5], 3);

		glActiveTexture(GL_TEXTURE4);
		glBindTexture(GL_TEXTURE_2D_ARRAY, _atlas_texture_m);
		glUniform1i(_batch_uniforms_link[6], 4);

		glActiveTexture(GL_TEXTURE5);
		glBindTexture(GL_TEXTURE_2D, (_multisample_set > 0) ? _frame_texture_c : _frame_tmp_c);
		glUniform1i(_batch_uniforms_link[7], 5);

		glBindVertexArray(_batch_vertex_format);
		if (GLAD_GL_VERSION_4_3)
		{
			glBindVertexBuffer(0, _vertex_buffer, 0, sizeof(Vertex));
		}
		else
		{
			glBindBuffer(GL_ARRAY_BUFFER, _vertex_buffer);
			glVertexAttribPointer((GLuint)(_batch_attribs_link[0]), 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(cpp_offsetof(Vertex, pos)));
			glVertexAttribPointer((GLuint)(_batch_attribs_link[1]), 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(cpp_offsetof(Vertex, tex)));
			glVertexAttribPointer((GLuint)(_batch_attribs_link[2]), 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (void*)(cpp_offsetof(Vertex, blend)));
			glVertexAttribPointer((GLuint)(_batch_attribs_link[3]), 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (void*)(cpp_offsetof(Vertex, color)));
			glVertexAttribPointer((GLuint)(_batch_attribs_link[4]), 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (void*)(cpp_offsetof(Vertex, fade)));
			glBindBuffer(GL_ARRAY_BUFFER, 0);
		}
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _index_buffer);
		glDrawElements(GL_TRIANGLES, (GLuint)(index_count - 6), GL_UNSIGNED_INT, (void*)(sizeof(uint32) * 6));
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		glBindVertexArray(0);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, 0);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, 0);

		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, 0);

		glActiveTexture(GL_TEXTURE3);
		glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

		glActiveTexture(GL_TEXTURE4);
		glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

		glActiveTexture(GL_TEXTURE5);
		glBindTexture(GL_TEXTURE_2D, 0);
	}
	glUseProgram(0);
}

/* Blit screen */
void Blitter_OpenGL::BlitScreen()
{
	glUseProgram(_blit_program);
	{
		glUniform4f(_blit_uniforms_link[0], +2.0f / (float)(_size_x), +2.0f / (float)(_size_y), -1.0f, -1.0f);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_1D, _pal_texture);
		glUniform1i(_blit_uniforms_link[1], 0);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, _frame_texture_c);
		glUniform1i(_blit_uniforms_link[2], 1);

		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, _frame_texture_m);
		glUniform1i(_blit_uniforms_link[3], 2);

		glBindVertexArray(_blit_vertex_format);
		if (GLAD_GL_VERSION_4_3)
		{
			glBindVertexBuffer(0, _vertex_buffer, 0, sizeof(Vertex));
		}
		else
		{
			glBindBuffer(GL_ARRAY_BUFFER, _vertex_buffer);
			glVertexAttribPointer((GLuint)(_blit_attribs_link[0]), 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(cpp_offsetof(Vertex, pos)));
			glVertexAttribPointer((GLuint)(_blit_attribs_link[1]), 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(cpp_offsetof(Vertex, tex)));
			glBindBuffer(GL_ARRAY_BUFFER, 0);
		}
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _index_buffer);
		glDrawElements(GL_TRIANGLES, (GLuint)(6), GL_UNSIGNED_INT, nullptr);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		glBindVertexArray(0);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, 0);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, 0);

		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, 0);
	}
	glUseProgram(0);
}

/* Resolve frame textures */
void Blitter_OpenGL::Resolve(bool copy)
{
	if (_multisample_set > 0)
	{
		glBindFramebuffer(GL_READ_FRAMEBUFFER, _frame_buffer_copy_c);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _frame_texture_copy_c);
		glBlitFramebufferLT(0, 0, _size_x, _size_y, 0, 0, _size_x, _size_y, GL_COLOR_BUFFER_BIT, GL_NEAREST);

		glBindFramebuffer(GL_READ_FRAMEBUFFER, _frame_buffer_copy_m);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _frame_texture_copy_m);
		glBlitFramebufferLT(0, 0, _size_x, _size_y, 0, 0, _size_x, _size_y, GL_COLOR_BUFFER_BIT, GL_NEAREST);

		glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
	}
	else
	{
		if (copy)
		{
			glBindFramebuffer(GL_READ_FRAMEBUFFER, _frame_texture_copy_c);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _frame_tmp_copy_c);
			glBlitFramebufferLT(0, 0, _size_x, _size_y, 0, 0, _size_x, _size_y, GL_COLOR_BUFFER_BIT, GL_NEAREST);

			glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
		}
	}
}

void Blitter_OpenGL::Draw(Blitter::BlitterParams *bp, BlitterMode mode, ZoomLevel zoom)
{
	FlushPixels();

	AtlasSprite *sp = (AtlasSprite*)(bp->sprite);

	float rp = 0;
	uint8 recol = 0;
	uint8 remap = 0;
	uint32 color = 0;
	PaletteID pal = (bp->pal & (~USE_PAL_REMAP));
	if (mode != BM_NORMAL)
	{
		if (bp->pal & USE_PAL_REMAP)
		{
			CachePal(pal);
			rp = _recol_pal_map[pal] + 0.5f; // half texel
			recol = 255;
		}
		else
		{
			color = pal;
			remap = 255;
		}
	}

	uint32 altas = sp->atlas;
	uint32 blend = (recol << 24) | (remap << 16); // setup blend mode

	uint32 fade = 0;
	if ((mode != BM_NORMAL) && (mode != BM_COLOUR_REMAP)) // setup fade mode
	{
		if (mode == BM_TRANSPARENT) fade = 0x3F0000FF;
		if (mode == BM_CRASH_REMAP) fade = 0xFF00A8FF;
		if (mode == BM_BLACK_REMAP) fade = 0xFF0000FF;
	}
	
	// destanation rect
	int p = (int)((intptr_t)(bp->dst));
	int dx = (p % _size_x) + bp->left;
	int dy = (p / _size_x) + bp->top;
	int dw = bp->width;
	int dh = bp->height;

	// sprite rect with respect to zoom and position in atlas
	float zm = (1 << zoom);
	float scale = (1 << sp->scale);
	float sx = (float)(sp->x_offs) + (float)(bp->skip_left * zm) / scale;
	float sy = (float)(sp->y_offs) + (float)(bp->skip_top  * zm) / scale;
	float sw = (float)(dw * zm) / scale;
	float sh = (float)(dh * zm) / scale;
	float lod = max(zoom - (int)(sp->scale), 0);

	uint32 index = (uint32)(_vertex.size());
	_index.resize(_index.size() + 6);
	uint32 *i = &_index[_index.size() - 6];

	i[0] = index + 0; i[1] = index + 1; i[2] = index + 3;
	i[3] = index + 1; i[4] = index + 2; i[5] = index + 3;

	_vertex.resize(_vertex.size() + 4);
	Vertex *v = &_vertex[_vertex.size() - 4];

	v[0].pos.x = dx;      v[0].pos.y = dy;      v[0].pos.z = _overlay_z; v[0].pos.w = lod;
	v[1].pos.x = dx + dw; v[1].pos.y = dy;      v[1].pos.z = _overlay_z; v[1].pos.w = lod;
	v[2].pos.x = dx + dw; v[2].pos.y = dy + dh; v[2].pos.z = _overlay_z; v[2].pos.w = lod;
	v[3].pos.x = dx;      v[3].pos.y = dy + dh; v[3].pos.z = _overlay_z; v[3].pos.w = lod;

	v[0].tex.x = sx;      v[0].tex.y = sy;
	v[1].tex.x = sx + sw; v[1].tex.y = sy;
	v[2].tex.x = sx + sw; v[2].tex.y = sy + sh;
	v[3].tex.x = sx;      v[3].tex.y = sy + sh;

	v[0].tex.a = altas; v[1].tex.a = altas;	v[2].tex.a = altas; v[3].tex.a = altas;
	v[0].tex.p = rp;    v[1].tex.p = rp;    v[2].tex.p = rp;    v[3].tex.p = rp;
	v[0].blend = blend; v[1].blend = blend; v[2].blend = blend; v[3].blend = blend;
	v[0].color = color; v[1].color = color; v[2].color = color; v[3].color = color;
	v[0].fade  = fade;  v[1].fade  = fade;  v[2].fade  = fade;  v[3].fade  = fade;
}

void Blitter_OpenGL::DrawColourMappingRect(void *dst, int width, int height, PaletteID pal)
{
	FlushPixels();

	// scaling factor to fit screen coords from atlas space
	float s_k = (1.0f / (float)(ATLAS_SIZE));
	float s_w = (1.0f / (float)(_size_x)) / s_k;
	float s_h = (1.0f / (float)(_size_y)) / s_k;

	// destanation rect
	int p = (int)((intptr_t)(dst));
	int dx = (p % _size_x);
	int dy = (p / _size_x);
	int dw = width;
	int dh = height;

	uint32 blend = 0xFF<<8;
	uint32 color = 0xFFFFFFFF;

	uint32 fade = 0;
	if (pal == PALETTE_TO_TRANSPARENT) fade = 0x650000FF;
	if (pal == PALETTE_NEWSPAPER)
	{
		Flush();
		Resolve(true);
		fade = 0xFFFFFFFF;
	}

	uint32 index = (uint32)(_vertex.size());
	_index.resize(_index.size() + 6);
	uint32 *i = &_index[_index.size() - 6];

	i[0] = index + 0; i[1] = index + 1; i[2] = index + 3;
	i[3] = index + 1; i[4] = index + 2; i[5] = index + 3;

	_vertex.resize(_vertex.size() + 4);
	Vertex *v = &_vertex[_vertex.size() - 4];

	v[0].pos.x = dx;      v[0].pos.y = dy;      v[0].pos.z = _overlay_z; v[3].pos.w = 0.0f;
	v[1].pos.x = dx + dw; v[1].pos.y = dy;      v[1].pos.z = _overlay_z; v[3].pos.w = 0.0f;
	v[2].pos.x = dx + dw; v[2].pos.y = dy + dh; v[2].pos.z = _overlay_z; v[3].pos.w = 0.0f;
	v[3].pos.x = dx;      v[3].pos.y = dy + dh; v[3].pos.z = _overlay_z; v[3].pos.w = 0.0f;

	// screen rect mapping for readback
	v[0].tex.x = s_w * (dx);       v[0].tex.y = s_h * (_size_y - (dy));
	v[1].tex.x = s_w * (dx + dw);  v[1].tex.y = s_h * (_size_y - (dy));
	v[2].tex.x = s_w * (dx + dw);  v[2].tex.y = s_h * (_size_y - (dy + dh));
	v[3].tex.x = s_w * (dx);       v[3].tex.y = s_h * (_size_y - (dy + dh));

	v[0].tex.a = 0;     v[1].tex.a = 0;	    v[2].tex.a = 0;     v[3].tex.a = 0;
	v[0].tex.p = 0;     v[1].tex.p = 0;     v[2].tex.p = 0;     v[3].tex.p = 0;
	v[0].blend = blend; v[1].blend = blend; v[2].blend = blend; v[3].blend = blend;
	v[0].color = color; v[1].color = color; v[2].color = color; v[3].color = color;
	v[0].fade  = fade;  v[1].fade  = fade;  v[2].fade  = fade;  v[3].fade  = fade;
}

Sprite *Blitter_OpenGL::Encode(const SpriteLoader::Sprite *sprite, AllocatorProc *allocator)
{
	Sprite *s = (Sprite*)(allocator(sizeof(Sprite) + sizeof(AtlasSprite)));

	// downscale sprites to nominal zoom
	uint32 scale = (sprite->type == ST_NORMAL) ? DOWNSCALE : 0;
	uint32 sw = (sprite->width >> scale);
	uint32 sh = (sprite->height >> scale);

	AtlasLayer *a = nullptr;
	if (_atlas.size() > 0) // try to add sprite to last atlas
	{
		a = &_atlas.back();
		if ((a->x_offs + sw) >= ATLAS_SIZE)
		{
			a->x_offs = 0;
			a->y_offs += Ceil(a->height, ATLAS_ALIGN);
			a->height = 0;
		}
		if ((a->y_offs + sh) >= ATLAS_SIZE) a = nullptr;
	}
	if (!a) // no space in last atlas, create new
	{
		_atlas.emplace_back();
		a = &_atlas.back();
		a->x_offs = 0;
		a->y_offs = 0;
		a->height = 0;
		a->data_c.resize(ATLAS_SIZE * ATLAS_SIZE);
		a->data_m.resize(ATLAS_SIZE * ATLAS_SIZE);
		_atlas_dirty = 1;
	}

	AtlasSprite *sp = (AtlasSprite*)(s->data); // save info about sprite location in the atlas
	sp->atlas  = (uint32)(_atlas.size() - 1);
	sp->scale  = scale;
	sp->x_offs = a->x_offs;
	sp->y_offs = a->y_offs;

	// copy sprite pixels to atlas
	{
		SpriteLoader::CommonPixel *src = sprite->data;
		uint8  *dst_m = a->data_m.data() + a->y_offs * ATLAS_SIZE + a->x_offs;
		Colour *dst_c = a->data_c.data() + a->y_offs * ATLAS_SIZE + a->x_offs;

		uint32 count = (1 << scale);
		for (uint32 y = 0; y < sh; y++)
		{
			for (uint32 x = 0; x < sw; x++)
			{
#ifdef _DEBUG
				if (src->m)
				{
					Colour c = LookupColourInPalette(src->m);
					dst_c->r = c.b;
					dst_c->g = c.g;
					dst_c->b = c.r;
					dst_c->a = c.a;
				}
				else
#endif
				{
					dst_c->r = src->b;
					dst_c->g = src->g;
					dst_c->b = src->r;
					dst_c->a = src->a;
				}
				(*dst_m) = src->m;

				src += count;
				dst_c++;
				dst_m++;
			}
			src += sprite->width * (count - 1);
			dst_c += (ATLAS_SIZE - sw);
			dst_m += (ATLAS_SIZE - sw);
		}
	}
	a->x_offs += Ceil(sw, ATLAS_ALIGN);
	a->height = max(a->height, (uint32)(sh));
	a->dirty = 1;

	s->height = sprite->height;
	s->width  = sprite->width;
	s->x_offs = sprite->x_offs;
	s->y_offs = sprite->y_offs;
	return s;
}

void *Blitter_OpenGL::MoveTo(void *video, int x, int y)
{
	return (void*)((intptr_t)((int)((intptr_t)(video)) + (y * _size_x + x)));
}

void Blitter_OpenGL::SetPixel(void *video, int x, int y, uint8 colour)
{
	int p = (int)((intptr_t)(video));
	int dx = min((p % _size_x) + x, _pixel_size_x - 1);
	int dy = min((p / _size_x) + y, _pixel_size_y - 1);

	Colour c = LookupColourInPalette(colour);
	uint8 *dst = &_pixel_buffer[(dy * _pixel_size_x + dx) * 4];
	dst[0] = c.r;
	dst[1] = c.g;
	dst[2] = c.b;
	dst[3] = c.a;

	_pixel_area.left = min(_pixel_area.left, dx);
	_pixel_area.top = min(_pixel_area.top, dy);
	_pixel_area.right = max(_pixel_area.right, dx + 1);
	_pixel_area.bottom = max(_pixel_area.bottom, dy + 1);

	_pixel_flush = 1;
}

void Blitter_OpenGL::DrawRect(void *video, int width, int height, uint8 colour, int checker)
{
	FlushPixels();

	// destanation rect
	int p = (int)((intptr_t)(video));
	int dx = (p % _size_x);
	int dy = (p / _size_x);
	int dw = width;
	int dh = height;

	Colour c = LookupColourInPalette(colour); Swap(c.r, c.b); // ARGB-> ABGR
	uint32 color = c.data;
	uint32 blend = (0xFF << 8) | (checker ? 0xFF : 0);
	uint32 fade  = 0;

	// scaling factor to fit checker pattern coords from the atlas space
	int h = (checker-1);
	float s_k = (1.0f / (float)(ATLAS_SIZE));
	float s_c = (1.0f / 16.0f) / s_k;

	uint32 index = (uint32)(_vertex.size());
	_index.resize(_index.size() + 6);
	uint32 *i = &_index[_index.size() - 6];

	i[0] = index + 0; i[1] = index + 1; i[2] = index + 3;
	i[3] = index + 1; i[4] = index + 2; i[5] = index + 3;

	_vertex.resize(_vertex.size() + 4);
	Vertex *v = &_vertex[_vertex.size() - 4];

	v[0].pos.x = dx;      v[0].pos.y = dy;      v[0].pos.z = _overlay_z; v[0].pos.w = 0.0f;
	v[1].pos.x = dx + dw; v[1].pos.y = dy;      v[1].pos.z = _overlay_z; v[1].pos.w = 0.0f;
	v[2].pos.x = dx + dw; v[2].pos.y = dy + dh; v[2].pos.z = _overlay_z; v[2].pos.w = 0.0f;
	v[3].pos.x = dx;      v[3].pos.y = dy + dh; v[3].pos.z = _overlay_z; v[3].pos.w = 0.0f;
		
	v[0].tex.x = s_c * (h);       v[0].tex.y = s_c * (0);
	v[1].tex.x = s_c * (h + dw);  v[1].tex.y = s_c * (0);
	v[2].tex.x = s_c * (h + dw);  v[2].tex.y = s_c * (0 + dh);
	v[3].tex.x = s_c * (h);       v[3].tex.y = s_c * (0 + dh);

	v[0].tex.a = 0;     v[1].tex.a = 0;	    v[2].tex.a = 0;     v[3].tex.a = 0;
	v[0].tex.p = 0;	    v[1].tex.p = 0;	    v[2].tex.p = 0;	    v[3].tex.p = 0;
	v[0].blend = blend; v[1].blend = blend; v[2].blend = blend; v[3].blend = blend;
	v[0].color = color; v[1].color = color; v[2].color = color; v[3].color = color;
	v[0].fade  = fade;  v[1].fade  = fade;  v[2].fade  = fade;  v[3].fade  = fade;
}

void Blitter_OpenGL::DrawLine(void *video, int x, int y, int x2, int y2, int screen_width, int screen_height, uint8 colour, int width, int dash)
{
	FlushPixels();

	// destanation rect
	int p = (int)((intptr_t)(video));
	int px = (p % _size_x);
	int py = (p / _size_x);

	Colour c = LookupColourInPalette(colour); Swap(c.r, c.b); // ARGB-> ABGR
	uint32 color = c.data;
	uint32 blend = (0xFF << 8) | (dash ? 0xFF : 0);
	uint32 fade  = 0;

	// scaling factor to fit checker pattern coords from the line space
	float s_k = (1.0f / (float)(ATLAS_SIZE));
	float s_c = (1.0f / 16.0f) / s_k;

	uint32 index = (uint32)(_vertex.size());
	_index.resize(_index.size() + 6);
	uint32 *i = &_index[_index.size() - 6];

	i[0] = index + 0; i[1] = index + 1; i[2] = index + 3;
	i[3] = index + 1; i[4] = index + 2; i[5] = index + 3;

	_vertex.resize(_vertex.size() + 4);
	Vertex *v = &_vertex[_vertex.size() - 4];

	float dx = (x2 - x);
	float dy = (y2 - y);
	float length = sqrtf(dx * dx + dy * dy);
	
	float nx = -dy / length;
	float ny =  dx / length;

	float sx = x;
	float sy = y;
	float fx = x2;
	float fy = y2;
	
	float ts = 0.0f;
	float tf = 0.0f + (dash ? (length / (float)(dash)) : 1.0f);

	if (fx < sx) { Swap(sx, fx); Swap(sy, fy); Swap(ts, tf); }
	if (sx < 0.0f)
	{
		float frac = 1.0f - fx / (fx - sx);
		sy += (fy - sy) * frac;
		ts += (tf - ts) * frac;
		sx = 0.0f;
	}
	if (fx > screen_width)
	{
		float frac = 1.0f - (screen_width - sx) / (fx - sx);
		fy -= (fy - sy) * frac;
		tf -= (tf - ts) * frac;
		fx = screen_width;
	}

	if (fy < sy) { Swap(sx, fx); Swap(sy, fy); Swap(ts, tf); }
	if (sy < 0.0f)
	{
		float frac = 1.0f - fy / (fy - sy);
		sx += (fx - sx) * frac;
		ts += (tf - ts) * frac;
		sy = 0.0f;
	}
	if (fy > screen_height)
	{
		float frac = 1.0f - (screen_height - sy) / (fy - sy);
		fx -= (fx - sx) * frac;
		tf -= (tf - ts) * frac;
		fy = screen_height;
	}

	float w = (float)(width) / 2.0f;
	v[0].pos.x = px + sx + nx * w; v[0].pos.y = py + sy + ny * w; v[0].pos.z = _overlay_z; v[0].pos.w = 0.0f;
	v[1].pos.x = px + fx + nx * w; v[1].pos.y = py + fy + ny * w; v[1].pos.z = _overlay_z; v[1].pos.w = 0.0f;
	v[2].pos.x = px + fx - nx * w; v[2].pos.y = py + fy - ny * w; v[2].pos.z = _overlay_z; v[2].pos.w = 0.0f;
	v[3].pos.x = px + sx - nx * w; v[3].pos.y = py + sy - ny * w; v[3].pos.z = _overlay_z; v[3].pos.w = 0.0f;

	v[0].tex.x = s_c * ts; v[0].tex.y = s_c * 1.0;
	v[1].tex.x = s_c * tf; v[1].tex.y = s_c * 1.0;
	v[2].tex.x = s_c * tf; v[2].tex.y = s_c * 0.0;
	v[3].tex.x = s_c * ts; v[3].tex.y = s_c * 0.0;

	v[0].tex.a = 0;     v[1].tex.a = 0;	    v[2].tex.a = 0;     v[3].tex.a = 0;
	v[0].tex.p = 0;	    v[1].tex.p = 0;	    v[2].tex.p = 0;	    v[3].tex.p = 0;
	v[0].blend = blend; v[1].blend = blend; v[2].blend = blend; v[3].blend = blend;
	v[0].color = color; v[1].color = color; v[2].color = color; v[3].color = color;
	v[0].fade  = fade;  v[1].fade  = fade;  v[2].fade  = fade;  v[3].fade  = fade;
}

void Blitter_OpenGL::CopyFromBuffer(void *video, const void *src, int width, int height)
{
	DEBUG(driver, 0, "***Blitter_OpenGL::CopyFromBuffer***");
	NOT_REACHED();
}

void Blitter_OpenGL::CopyToBuffer(const void *video, void *dst, int width, int height)
{
	DEBUG(driver, 0, "***Blitter_OpenGL::CopyToBuffer***");
	NOT_REACHED();
}

void Blitter_OpenGL::CopyImageToBuffer(const void *video, void *dst, int width, int height, int dst_pitch)
{
	DEBUG(driver, 0, "***Blitter_OpenGL::CopyImageToBuffer***");
	NOT_REACHED();
}

void Blitter_OpenGL::ScrollBuffer(void *video, int &left, int &top, int &width, int &height, int scroll_x, int scroll_y)
{
	FlushPixels();

	int src;
	int dst;
	if (scroll_y > 0) {
		/* Calculate pointers */
		dst = (int)((intptr_t)(video)) + left + (top + height - 1) * _screen.pitch;
		src = dst - scroll_y * _screen.pitch;

		/* Decrease height and increase top */
		top += scroll_y;
		height -= scroll_y;
		assert(height > 0);

		/* Adjust left & width */
		if (scroll_x >= 0) {
			dst += scroll_x;
			left += scroll_x;
			width -= scroll_x;
		}
		else
		{
			src -= scroll_x;
			width += scroll_x;
		}

		src -= _screen.pitch * (height - 1);
		dst -= _screen.pitch * (height - 1);
	}
	else
	{
		/* Calculate pointers */
		dst = (int)((intptr_t)(video)) + left + top * _screen.pitch;
		src = dst - scroll_y * _screen.pitch;

		/* Decrease height. (scroll_y is <=0). */
		height += scroll_y;
		assert(height > 0);

		/* Adjust left & width */
		if (scroll_x >= 0) {
			dst += scroll_x;
			left += scroll_x;
			width -= scroll_x;
		}
		else
		{
			src -= scroll_x;
			width += scroll_x;
		}

		/* the y-displacement may be 0 therefore we have to use memmove,
		* because source and destination may overlap */
		src += _screen.pitch;
		dst += _screen.pitch;
	}

	_scroll.x0 = (src % _size_x);
	_scroll.y0 = (src / _size_x);
	_scroll.x1 = (dst % _size_x);
	_scroll.y1 = (dst / _size_x);
	_scroll.w = width;
	_scroll.h = height;

	Flush();
}

void Blitter_OpenGL::PaletteAnimate(const Palette &palette)
{
	uint8 *dst = _pal_data;
	const Colour *src = palette.palette;
	for (size_t i = 0; i < 256; i++)
	{
		dst[0] = src->r;
		dst[1] = src->g;
		dst[2] = src->b;
		dst[3] = src->a;
		dst += 4;
		src++;
	}
	_pal_dirty = 1;
}

void Blitter_OpenGL::PostResize()
{
	_frame_dirty = 1;
}

void Blitter_OpenGL::Flush()
{
	UpdatePal();
	UpdatePalCache();
	UpdateAtlas();
	UpdateChecker();
	UpdateFrame();
	UpdateBatchProgram();
	UpdateBlitProgram();
	UpdatePixelProgram();

	///

	glViewport(0, 0, _size_x, _size_y);

	glBindFramebuffer(GL_FRAMEBUFFER, _frame);
	GLuint drawBuffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
	glReadBuffer(GL_COLOR_ATTACHMENT0);
	glDrawBuffers(2, drawBuffers);

	if (_multisample_set > 0 && GLAD_GL_VERSION_3_3) glEnable(GL_MULTISAMPLE);

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);

	glEnable(GL_BLEND);
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
	
	DrawBuffers(_size_x, _size_y); // render base data
	
	glDisable(GL_BLEND);

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);

	if (_multisample_set > 0 && GLAD_GL_VERSION_3_3) glDisable(GL_MULTISAMPLE);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	///

	// perform scrolling
	if ((_scroll.w > 0) && (_scroll.h > 0))
	{
		Scroll *s = &_scroll;

		GLuint frame_c = (_multisample_set > 0) ? _frame_buffer_copy_c : _frame_texture_copy_c;
		GLuint frame_m = (_multisample_set > 0) ? _frame_buffer_copy_m : _frame_texture_copy_m;

		glBindFramebuffer(GL_READ_FRAMEBUFFER, frame_c);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _frame_tmp_copy_c);
		glBlitFramebufferLT(s->x0, s->y0, s->x0 + s->w, s->y0 + s->h, s->x1, s->y1, s->x1 + s->w, s->y1 + s->h, GL_COLOR_BUFFER_BIT, GL_NEAREST);

		glBindFramebuffer(GL_READ_FRAMEBUFFER, frame_m);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _frame_tmp_copy_m);
		glBlitFramebufferLT(s->x0, s->y0, s->x0 + s->w, s->y0 + s->h, s->x1, s->y1, s->x1 + s->w, s->y1 + s->h, GL_COLOR_BUFFER_BIT, GL_NEAREST);

		glBindFramebuffer(GL_READ_FRAMEBUFFER, _frame_tmp_copy_c);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, frame_c);
		glBlitFramebufferLT(s->x1, s->y1, s->x1 + s->w, s->y1 + s->h, s->x1, s->y1, s->x1 + s->w, s->y1 + s->h, GL_COLOR_BUFFER_BIT, GL_NEAREST);

		glBindFramebuffer(GL_READ_FRAMEBUFFER, _frame_tmp_copy_m);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, frame_m);
		glBlitFramebufferLT(s->x1, s->y1, s->x1 + s->w, s->y1 + s->h, s->x1, s->y1, s->x1 + s->w, s->y1 + s->h, GL_COLOR_BUFFER_BIT, GL_NEAREST);

		glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

		_scroll.w = 0;
		_scroll.h = 0;
	}

	_flushed = 1;
}

void Blitter_OpenGL::Finish()
{
	if (!_flushed) Flush();

	Resolve(false);

	///

	glViewport(0, 0, _size_x, _size_y);

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);

	BlitScreen(); // animate screen

	glEnable(GL_BLEND);
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);

	DrawBuffers(_size_x, _size_y); // render overlay images, such as cursor, chat, etc

	glDisable(GL_BLEND);

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);

	glFlush();

	_flushed = 0;
}

void Blitter_OpenGL::Start3D()
{
	FlushPixels();
	Flush();

	glBindFramebuffer(GL_FRAMEBUFFER, _frame);
	GLuint drawBuffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
	glReadBuffer(GL_COLOR_ATTACHMENT0);
	glDrawBuffers(2, drawBuffers);

	if (_multisample_set > 0 && GLAD_GL_VERSION_3_3) glEnable(GL_MULTISAMPLE);
}

void Blitter_OpenGL::Flush3D(int size_x, int size_y)
{
	glDisable(GL_CULL_FACE);

	glEnable(GL_BLEND);
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);

	glDepthFunc(GL_LEQUAL);

	DrawBuffers(size_x, size_y); // render data

	glDisable(GL_BLEND);

	glEnable(GL_CULL_FACE);
}

void Blitter_OpenGL::Finish3D()
{
	if (_multisample_set > 0 && GLAD_GL_VERSION_3_3) glDisable(GL_MULTISAMPLE);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	_overlay_z = 0.5f; // restore overlay z
}
