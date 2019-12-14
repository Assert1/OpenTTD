/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file gfx3d.cpp
 * Entry for 3D viewports drawing. Overrides screen "dirty blocks" logic with screen "dirty rects" for GUI redraw. All the viewports are updated each frame.
 */

#include "../stdafx.h"
#include "../gfx_func.h"
#include "../blitter/opengl.hpp"
#include "../core/alloc_func.hpp"
#include "../core/geometry_type.hpp"
#include "../core/smallvec_type.hpp"
#include "../table/string_colours.h"
#include "../window_gui.h"
#include "viewport3d.h"

#include "../3rdparty/OpenGL/glew.h"
#include <gl/gl.h>

int _draw3d = 0; // draw the viewports in 3D

/* defines a rect of the screen for a GUI redraw logic */
struct DirtyRect
{
	DirtyRect *next_free; // linked list of a free rects for the pool allocator

	Rect own; // own rect coords
	DirtyRect *sub[2]; // splitted subrects
	int dirty; // is dirty
};

static DirtyRect *_dirty_rect_free = nullptr; // free rects in the pool
static DirtyRect *_dirty_rect = nullptr; // root screen rect
static int _dirty_rect_counter = 0; // just a counter of an allocated rects for debug

/* vertex for stencil mask filling */
PACK(struct Vertex
{
	struct
	{
		float x;
		float y;
	} pos;
});

static std::vector<Vertex> _vertex;		// batch vertex array for stencil mask filling
static std::vector<uint32> _index;		// batch index array for stencil mask filling
static GLuint _vertex_buffer = 0;		// OpenGL vertex buffer for stencil mask filling
static GLuint _index_buffer = 0;		// OpenGL index buffer for stencil mask filling

static GLuint _null_program = 0;		// null program for the stencil mask filling
static GLuint _null_vertex_format;		// null program vertex format
static GLint _null_attribs_link[1];		// null program attributes links
static GLint _null_uniforms_link[1];	// null program uniforms links

extern int _opengl_ver;

extern uint _dirty_block_colour;
extern void DrawOverlappedWindow(Window *w, int left, int top, int right, int bottom);

/* some things from the blitter */
extern GLuint ShaderLoad(const char *file, GLenum type, const char *opt = nullptr);
extern GLuint ProgramLink(GLuint vs, GLuint fs);

/* Allocate a new dirty rect from the pool */
static DirtyRect *AllocRect(int left, int top, int right, int bottom)
{
	_dirty_rect_counter++;

	DirtyRect *ret;
	if (_dirty_rect_free)
	{
		ret = _dirty_rect_free;
		_dirty_rect_free = ret->next_free;

		ret->own.left = left;
		ret->own.top = top;
		ret->own.right = right;
		ret->own.bottom = bottom;
		ret->sub[0] = nullptr;
		ret->sub[1] = nullptr;
		ret->dirty = 0;
		return ret;
	}

	ret = MallocT<DirtyRect>(64); // we never frees this pool, so it's ok
	for (int i = 0; i < 64; i++)
	{
		ret[i].next_free = &ret[i+1];
		for (int s = 0; s < 2; s++)	ret[i].sub[s] = nullptr;
	}
	_dirty_rect_free = &ret[1];
	ret[63].next_free = nullptr;

	ret->own.left = left;
	ret->own.top = top;
	ret->own.right = right;
	ret->own.bottom = bottom;
	ret->sub[0] = nullptr;
	ret->sub[1] = nullptr;
	ret->dirty = 0;
	return ret;
}

/* Return dirty rect to the pool */
static void FreeRect(DirtyRect *rect)
{
	if (!rect) return;

	for (int s = 0; s < 2; s++)	FreeRect(rect->sub[s]);
	_dirty_rect_counter--;

	rect->next_free = _dirty_rect_free;
	_dirty_rect_free = rect;
}

/* Mark a part of the rect dirty, splitting it in subrects recursive */
static void MarkRectDirty(DirtyRect *rect, int left, int top, int right, int bottom)
{
	Rect *v = &rect->own;
	if ((right <= v->left) || (bottom <= v->top) || (left >= v->right) || (top >= v->bottom)) return; // no intersection

	if (rect->sub[0]) // subrects defined
	{
		MarkRectDirty(rect->sub[0], left, top, right, bottom);
		MarkRectDirty(rect->sub[1], left, top, right, bottom);
		return;
	}
	if (rect->dirty) return; // whole rect already dirty

	if (left > v->left)
	{
		rect->sub[0] = AllocRect(v->left, v->top, left, v->bottom);
		rect->sub[1] = AllocRect(left, v->top, v->right, v->bottom);
		MarkRectDirty(rect->sub[1], left, top, right, bottom);
		return;
	}
	if (right < v->right)
	{
		rect->sub[0] = AllocRect(right, v->top, v->right, v->bottom);
		rect->sub[1] = AllocRect(v->left, v->top, right, v->bottom);
		MarkRectDirty(rect->sub[1], left, top, right, bottom);
		return;
	}
	if (top > v->top)
	{
		rect->sub[0] = AllocRect(v->left, v->top, v->right, top);
		rect->sub[1] = AllocRect(v->left, top, v->right, v->bottom);
		MarkRectDirty(rect->sub[1], left, top, right, bottom);
		return;
	}
	if (bottom < v->bottom)
	{
		rect->sub[0] = AllocRect(v->left, bottom, v->right, v->bottom);
		rect->sub[1] = AllocRect(v->left, v->top, v->right, bottom);
		MarkRectDirty(rect->sub[1], left, top, right, bottom);
		return;
	}

	rect->dirty = 1;
}

/* Mark a part of the screen dirty */
void MarkRectsDirty(int left, int top, int right, int bottom)
{
	if (!_dirty_rect) _dirty_rect = AllocRect(0, 0, _screen.width, _screen.height);
	MarkRectDirty(_dirty_rect, left, top, right, bottom);
}

/* Draw stuff for debug */
static void DrawRectDebug(DirtyRect *rect)
{
	if (!rect) return;
	if (rect->sub[0]) // subrects defined
	{
		DrawRectDebug(rect->sub[0]);
		DrawRectDebug(rect->sub[1]);
		return;
	}
	if (!rect->dirty) return;

	uint8 color = _string_colourmap[_dirty_block_colour & 0xF];

	Rect *v = &rect->own;
	Blitter *blitter = BlitterFactory::GetCurrentBlitter();
	blitter->DrawRect(blitter->MoveTo(nullptr, v->left, v->top), v->right-v->left, v->bottom-v->top, color, 1);
}

/* Fill buffers with clear rects */
static void FillClearRect(DirtyRect *rect)
{
	if (rect->sub[0]) // subrects defined
	{
		FillClearRect(rect->sub[0]);
		FillClearRect(rect->sub[1]);
		return;
	}
	if (rect->dirty) return; // whole rect is dirty

	Rect *r = &rect->own;
/*
	// we can call original 2D viewport drawing code there for debug
	RedrawScreenRect(r->left, r->top, r->right, r->bottom);
/**/
	float dx = r->left;
	float dy = r->top;
	float dw = r->right - r->left;
	float dh = r->bottom - r->top;

	/* just add this rect */
	uint32 index = (uint32)(_vertex.size());
	_index.resize(_index.size() + 6);
	uint32 *i = &_index[_index.size() - 6];
	
	i[0] = index + 0; i[1] = index + 1; i[2] = index + 3;
	i[3] = index + 1; i[4] = index + 2; i[5] = index + 3;

	_vertex.resize(_vertex.size() + 6);
	Vertex *v = &_vertex[_vertex.size() - 6];

	v[0].pos.x = dx;      v[0].pos.y = dy;
	v[1].pos.x = dx + dw; v[1].pos.y = dy;
	v[2].pos.x = dx + dw; v[2].pos.y = dy + dh;
	v[3].pos.x = dx;      v[3].pos.y = dy + dh;
}

/* Allocate and load null program for stencil mask filling */
static void UpdateNullProgram()
{
	if (_null_program) return;

	GLuint vs = ShaderLoad("shader/null.vert", GL_VERTEX_SHADER);
	GLuint fs = ShaderLoad("shader/null.frag", GL_FRAGMENT_SHADER);
	_null_program = ProgramLink(vs, fs);

	_null_attribs_link[0] = glGetAttribLocation(_null_program, "in_pos");

	_null_uniforms_link[0] = glGetUniformLocation(_null_program, "dim_pos");

	glGenVertexArrays(1, &_null_vertex_format);

	glBindVertexArray(_null_vertex_format);
	for (int i = 0; i < 1; i++) glEnableVertexAttribArray((GLuint)(_null_attribs_link[i]));
	if (_opengl_ver > 0)
	{
		for (int i = 0; i < 1; i++) glVertexAttribBinding((GLuint)(_null_attribs_link[i]), 0);
		glVertexAttribFormat((GLuint)(_null_attribs_link[0]), 2, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(Vertex, pos)));
	}
	glBindVertexArray(0);
}

/* Allocate and update buffers for stencil mask filling */
static void UpdateBuffers(uint &vertex_count, uint &index_count)
{
	if (!_vertex_buffer) glGenBuffers(1, &_vertex_buffer);
	if (!_index_buffer) glGenBuffers(1, &_index_buffer);

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
}

/* Draw buffers for stencil mask filling */
static void DrawBuffers()
{
	uint vertex_count = 0;
	uint index_count = 0;
	UpdateBuffers(vertex_count, index_count);
	if (!vertex_count || !index_count) return;

	UpdateNullProgram();
	glUseProgram(_null_program);
	{
		glUniform4f(_null_uniforms_link[0], +2.0f/(float)(_screen.width), -2.0f/(float)(_screen.height), -1.0f, +1.0f);

		glBindVertexArray(_null_vertex_format);
		if (_opengl_ver > 0)
		{
			glBindVertexBuffer(0, _vertex_buffer, 0, sizeof(Vertex));
		}
		else
		{
			glBindBuffer(GL_ARRAY_BUFFER, _vertex_buffer);
			glVertexAttribPointer((GLuint)(_null_attribs_link[0]), 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(cpp_offsetof(Vertex, pos)));
			glBindBuffer(GL_ARRAY_BUFFER, 0);
		}
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _index_buffer);
		glDrawElements(GL_TRIANGLES, (GLuint)(index_count), GL_UNSIGNED_INT, nullptr);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		glBindVertexArray(0);
	}
	glUseProgram(0);
}

/* Redraw a viewport of the window */
static void RedrawWindowViewport(Window *vp)
{
	int left = max(vp->viewport->left, 0);
	int top = max(vp->viewport->top, 0);
	int right = min(vp->viewport->left + vp->viewport->width, _screen.width);
	int bottom = min(vp->viewport->top + vp->viewport->height, _screen.height);
	if (!(right - left) || !(bottom - top)) return;

	/* we need to identify visible regions of the viewport */
	DirtyRect *_redraw_rect = AllocRect(left, top, right, bottom);
	{
		Window *w;
		FOR_ALL_WINDOWS_FROM_BACK_FROM(w, vp->z_front)
		{
			/* mark regions of the overlapping windows as dirty */
			MarkRectDirty(_redraw_rect, max(w->left, 0), max(w->top, 0), min(w->left + w->width, _screen.width), min(w->top + w->height, _screen.height));
		}
		FillClearRect(_redraw_rect); // clear rects now defines a viewport area for redraw, cache it in the draw buffers
	}
	FreeRect(_redraw_rect);

	DrawPrepareViewport3D(vp->viewport); // prepare for the viewport drawing

	/* tell the blitter, to flush all cached data, and properly setup a frame buffer for us */
	Blitter_OpenGL *blitter = (Blitter_OpenGL*)(BlitterFactory::GetCurrentBlitter());
	blitter->Start3D();
	{
		/* to draw only in the viewport rect */
		glScissor(vp->viewport->left, _screen.height - vp->viewport->top - vp->viewport->height, vp->viewport->width, vp->viewport->height);
		glEnable(GL_SCISSOR_TEST);

		/* clear stencil mask and depth */
		glStencilMask(0xFF);
		glClearStencil(0x00);
		glClear(GL_STENCIL_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);

		/* configure to fill the stencil mask, color + pal */
//		glColorMaski(0, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
		glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glStencilOp(GL_REPLACE, GL_REPLACE, GL_REPLACE);

		glDisable(GL_DEPTH_TEST);
		glDisable(GL_CULL_FACE);
				
		glStencilFunc(GL_ALWAYS, 0xFF, 0);
		glEnable(GL_STENCIL_TEST);
		DrawBuffers(); // fill in the stencil mask with a visible region of the viewport
		glDisable(GL_STENCIL_TEST);

		glEnable(GL_DEPTH_TEST);
		glEnable(GL_CULL_FACE);

		/* configure to draw the viewport */
		glViewport(vp->viewport->left, _screen.height - vp->viewport->top - vp->viewport->height, vp->viewport->width, vp->viewport->height);

		glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glColorMaski(1, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE); // not using global screen blit palette resolver
		glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
		glStencilMask(0);

		glEnable(GL_STENCIL_TEST);
		glStencilFunc(GL_NOTEQUAL, 0x00, 0xFF);

		{
			DrawPixelInfo *old_dpi = _cur_dpi;
			DrawPixelInfo bk = _screen;
			bk.width  = vp->viewport->width;
			bk.height = vp->viewport->height;
			_cur_dpi = &bk; // setup for overlay drawing

			DrawViewport3D(vp->viewport); // now draw the viewport image
			blitter->Flush3D(vp->viewport->width, vp->viewport->height);

			_cur_dpi = old_dpi;
		}

		glStencilFunc(GL_ALWAYS, 0, 0);
		glDisable(GL_STENCIL_TEST);

		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

		glDisable(GL_SCISSOR_TEST);
	};
	blitter->Finish3D(); // switch out blitter to 2D mode
}

/* Redraw dirty rects of a window */
static void RedrawWindowRect(Window *w, DirtyRect *rect)
{
	if (!rect) return;

	if (rect->sub[0]) // subrects defined
	{
		RedrawWindowRect(w, rect->sub[0]);
		RedrawWindowRect(w, rect->sub[1]);
		return;
	}
	if (!rect->dirty) return; // whole rect is not dirty

	Rect *v = &rect->own;
	if ((v->right <= w->left) || (v->bottom <= w->top) || (v->left > (w->left + w->width)) || (v->top > (w->top + w->height))) return; // no intersection

	int left = max(v->left, w->left);
	int top = max(v->top, w->top);
	int right = min(v->right, w->left + w->width);
	int bottom = min(v->bottom, w->top + w->height);
	DrawOverlappedWindow(w, left, top, right, bottom);
}

/* Redraw all of the windows and reset a GUI dirty rects */
void RedrawDirtyRects()
{
	DrawPrepare3D(); // prepare our 3D space

	DrawPixelInfo *old_dpi = _cur_dpi;
	DrawPixelInfo bk = _screen;
	_cur_dpi = &bk;

	Window *w;
	FOR_ALL_WINDOWS_FROM_BACK(w) // redraw all windows one by one from the back
	{
		if (!MayBeShown(w)) continue;

		RedrawWindowRect(w, _dirty_rect); // redraw dirty GUI part of the window
		if (w->viewport) RedrawWindowViewport(w); // if a window have the viewport, redraw it's visible regions
	}
	_cur_dpi = old_dpi;
//	DrawRectDebug(_dirty_rect);
	FreeRect(_dirty_rect);
	_dirty_rect = nullptr;
}
