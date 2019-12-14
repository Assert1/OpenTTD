/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file opengl.hpp OpenGL blitter. */

#ifndef BLITTER_OPENGL_HPP
#define BLITTER_OPENGL_HPP

#include "factory.hpp"

#include "../core/smallvec_type.hpp"
#include "../gfx_func.h"

#include "../3rdparty/glad/include/glad/glad.h"

#include <vector>

/** OpenGL blitter. */
class Blitter_OpenGL : public Blitter
{
private:
	uint8 _pal_data[256 * 4];			// screen animation palette data
	GLuint _pal_texture;				// screen animation palette texture
	int _pal_dirty;						// screen animation palette texture needs an update

	struct PalEntry	{ const byte *data;	}; // link to remap data
	std::vector<uint32> _recol_pal_map;	// map of recolor PaletteIDs to recolor palettes cache entrys
	std::vector<PalEntry> _recol_pal;	// recolor palettes cache
	GLuint _recol_pal_texture;			// palettes cache texture
	uint32 _atlas_table_dim;			// palettes cache texture viewports atlas edge table dimemsion (hack for the viewport)
	int _recol_pal_dirty;				// palettes cache texture needs an update

	struct AtlasLayer
	{
		uint32 x_offs;	// current col
		uint32 y_offs;	// current row
		uint32 height;	// block height
		std::vector<Colour> data_c;	// colors
		std::vector<uint8> data_m;	// mapping indexes
		int dirty; // layer needs an update
	};
	std::vector<AtlasLayer> _atlas;	// sprites data
	GLuint _atlas_texture_c;		// atlas texture 2D array of colors
	GLuint _atlas_texture_m;		// atlas texture 2D array of mapping indexes
	int _atlas_dirty;				// atlas texture needs an update

	struct AtlasSprite
	{
		uint32 atlas;	// atlas index
		uint32 scale;	// sprite base scale
		uint32 x_offs;	// x offset in the atlas
		uint32 y_offs;	// y offset in the atlas
	};

	GLuint _checker_texture;		// checker pattern texture

	GLuint _frame;					// screen frame buffer
	GLuint _frame_buffer_c;			// screen color renderbuffer (for multisampling)
	GLuint _frame_buffer_m;			// screen mapping renderbuffer (for multisampling)
	GLuint _frame_buffer_ds;		// screen depth stencil buffer for the 3D viewport
	GLuint _frame_buffer_copy_c;	// screen buffer copy color frame buffer
	GLuint _frame_buffer_copy_m;	// screen buffer copy index map frame buffer
	GLuint _frame_texture_c;		// screen color texture
	GLuint _frame_texture_m;		// screen mapping texture
	GLuint _frame_texture_copy_c;	// screen texture copy color frame buffer
	GLuint _frame_texture_copy_m;	// screen texture copy mapping frame buffer
	GLuint _frame_tmp_c;			// screen temporary color storage for scroll
	GLuint _frame_tmp_m;			// screen temporary mapping storage for scroll
	GLuint _frame_tmp_copy_c;		// screen scroll color frame buffer
	GLuint _frame_tmp_copy_m;		// screen scroll mapping frame buffer
	int _frame_dirty;				// frame buffer needs update (resized)
	int _flushed;					// frame buffer flushed

	GLuint _batch_program;			// batch program
	GLuint _batch_vertex_format;	// batch vertex format
	GLint _batch_attribs_link[5];	// batch attributes
	GLint _batch_uniforms_link[8];	// batch uniforms
	
	GLuint _blit_program;			// blit program
	GLuint _blit_vertex_format;		// blit vertex format
	GLint _blit_attribs_link[2];	// blit attributes
	GLint _blit_uniforms_link[4];	// blit uniforms

	PACK(struct Vertex
	{
		struct { float x; float y; float z;	float w; } pos; // screen x, y, z, lod
		struct { float x; float y; float a; float p; } tex; // sprite x, y, atlas, recol
		uint32 blend; // results: recol, remap, fixed, checker
		uint32 color; // fixed color
		uint32 fade;  // fade control
	});
	std::vector<Vertex> _vertex;		// batch vertex array
	std::vector<uint32> _index;			// batch index array
	GLuint _vertex_buffer;				// batch vertex buffer
	GLuint _index_buffer;				// batch index buffer
	float _overlay_z;					// z value to draw (for 3D viewport overlay)

	GLuint _pixel_texture;				// SetPixel texture
	GLuint _pixel_program;				// SetPixel program
	GLuint _pixel_vertex_format;		// SetPixel vertex format
	GLint _pixel_attribs_link[2];		// SetPixel attributes
	GLint _pixel_uniforms_link[3];		// SetPixel uniforms
	std::vector<uint8> _pixel_buffer;	// SetPixel buffer
	Rect _pixel_area;					// SetPixel area
	int _pixel_size_x;					// SetPixel buffer size x
	int _pixel_size_y;					// SetPixel buffer size y
	int _pixel_flush;					// SetPixel buffer needs flush

	struct Scroll { int x0; int y0; int x1; int y1; int w; int h; };
	Scroll _scroll;			// data for the scrolling request

	int _size_x;			// screen size x
	int _size_y;			// screen size y
	int _multisample_set;

public:
	Blitter_OpenGL();
	~Blitter_OpenGL();

private:
	void UpdatePal();
	void UpdatePalCache();
	void UpdateAtlas();
	void UpdateChecker();
	void UpdateFrame();
	void UpdateBatchProgram();
	void UpdateBlitProgram();
	void UpdatePixelProgram();
	void UpdateBuffers(uint &vertex_count, uint &index_count);
	void FlushPixels();
	void ReserveQuad();
	void DrawBuffers(int size_x, int size_y);
	void BlitScreen();
	void Resolve(bool copy);

public:
	uint32 CachePal(PaletteID pal); // cache recolor palette
	uint32 RecolPalCount() const { return (uint32)(_recol_pal.size() + 1); }; // count of recolor palettes + atlas edge table
	GLuint RecolPalTexture() const { return _recol_pal_texture; }; // recolor palettes cache texture
	GLuint PalTexture() const { return _pal_texture; }; // screen animation palette texture
	void SetAtlasTableDim(uint32 dim) { _atlas_table_dim = dim; _recol_pal_dirty = 1; };
	void SetOverlayZ(float z) { _overlay_z = z; };
	bool CopySprite(SpriteID s, uint32 &size_x, uint32 &size_y, void *data);

public:
	uint8 GetScreenDepth() override { return 32; }
	void Draw(Blitter::BlitterParams *bp, BlitterMode mode, ZoomLevel zoom) override;
	void DrawColourMappingRect(void *dst, int width, int height, PaletteID pal) override;
	Sprite *Encode(const SpriteLoader::Sprite *sprite, AllocatorProc *allocator) override;
	void *MoveTo(void *video, int x, int y) override;
	void SetPixel(void *video, int x, int y, uint8 colour) override;
	void DrawRect(void *video, int width, int height, uint8 colour, int checker) override;
	void DrawLine(void *video, int x, int y, int x2, int y2, int screen_width, int screen_height, uint8 colour, int width, int dash) override;
	void CopyFromBuffer(void *video, const void *src, int width, int height) override;
	void CopyToBuffer(const void *video, void *dst, int width, int height) override;
	void CopyImageToBuffer(const void *video, void *dst, int width, int height, int dst_pitch) override;
	void ScrollBuffer(void *video, int &left, int &top, int &width, int &height, int scroll_x, int scroll_y) override;
	int BufferSize(int width, int height) override { return width * height * 4; };
	void PaletteAnimate(const Palette &palette) override;
	Blitter::PaletteAnimation UsePaletteAnimation() override { return Blitter::PALETTE_ANIMATION_BLITTER; };
	int Hardware() override { return true; };
	void PostResize() override;

	void Flush() override;
	void Finish() override;

	void Start3D();
	void Flush3D(int size_x, int size_y);
	void Finish3D();

	const char *GetName() override { return "opengl"; }
	int GetBytesPerPixel() override { return 4; }
};

/** Factory for the blitter that does nothing. */
class FBlitter_OpenGL: public BlitterFactory {
public:
	FBlitter_OpenGL() : BlitterFactory("opengl", "OpenGL gate Blitter") {}
	/* virtual */ Blitter *CreateInstance() { return new Blitter_OpenGL(); }
};

#endif /* BLITTER_OPENGL_HPP */
