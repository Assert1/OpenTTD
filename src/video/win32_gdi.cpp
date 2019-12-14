/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file win32_gdi.cpp Implementation of the Windows GDI video driver. */

#include "../stdafx.h"
#include "../gfx_func.h"
#include "../blitter/factory.hpp"
#include "../core/math_func.hpp"
#include "win32_gdi.h"
#include <windows.h>

#include "../safeguards.h"

static struct {
	HBITMAP dib_sect;
	void *buffer_bits;
	HPALETTE gdi_palette;
} _wnd;

extern HWND _wnd_main_wnd;
extern int _wnd_width;
extern int _wnd_height;

extern Palette _local_palette;

static FVideoDriver_Win32_GDI iFVideoDriver_Win32_GDI;

static void MakePalette()
{
	LOGPALETTE *pal = (LOGPALETTE*)alloca(sizeof(LOGPALETTE) + (256 - 1) * sizeof(PALETTEENTRY));

	pal->palVersion = 0x300;
	pal->palNumEntries = 256;

	for (uint i = 0; i != 256; i++) {
		pal->palPalEntry[i].peRed   = _cur_palette.palette[i].r;
		pal->palPalEntry[i].peGreen = _cur_palette.palette[i].g;
		pal->palPalEntry[i].peBlue  = _cur_palette.palette[i].b;
		pal->palPalEntry[i].peFlags = 0;

	}
	_wnd.gdi_palette = CreatePalette(pal);
	if (_wnd.gdi_palette == nullptr) usererror("CreatePalette failed!\n");

	_cur_palette.first_dirty = 0;
	_cur_palette.count_dirty = 256;
	_local_palette = _cur_palette;
}

static void UpdatePalette(HDC dc, uint start, uint count)
{
	RGBQUAD rgb[256];
	uint i;

	for (i = 0; i != count; i++) {
		rgb[i].rgbRed   = _local_palette.palette[start + i].r;
		rgb[i].rgbGreen = _local_palette.palette[start + i].g;
		rgb[i].rgbBlue  = _local_palette.palette[start + i].b;
		rgb[i].rgbReserved = 0;
	}

	SetDIBColorTable(dc, start, count, rgb);
}

static bool AllocateDibSection(int w, int h, bool force = false);

void VideoDriver_Win32_GDI::DoClientSizeChanged(int w, int h)
{
	/* allocate new dib section of the new size */
	if (AllocateDibSection(w, h)) {
		/* mark all palette colours dirty */
		_cur_palette.first_dirty = 0;
		_cur_palette.count_dirty = 256;
		_local_palette = _cur_palette;

		BlitterFactory::GetCurrentBlitter()->PostResize();

		GameSizeChanged();
	}
}

#ifdef _DEBUG
/* Keep this function here..
 * It allows you to redraw the screen from within the MSVC debugger */
int RedrawScreenDebug()
{
	HDC dc, dc2;
	static int _fooctr;
	HBITMAP old_bmp;
	HPALETTE old_palette;

	UpdateWindows();

	dc = GetDC(_wnd_main_wnd);
	dc2 = CreateCompatibleDC(dc);

	old_bmp = (HBITMAP)SelectObject(dc2, _wnd.dib_sect);
	old_palette = SelectPalette(dc, _wnd.gdi_palette, FALSE);
	BitBlt(dc, 0, 0, _wnd_width, _wnd_height, dc2, 0, 0, SRCCOPY);
	SelectPalette(dc, old_palette, TRUE);
	SelectObject(dc2, old_bmp);
	DeleteDC(dc2);
	ReleaseDC(_wnd_main_wnd, dc);

	return _fooctr++;
}
#endif

/** Do palette animation and blit to the window. */
void VideoDriver_Win32_GDI::DoPaintWindow(void *pdc)
{
	HDC dc = (HDC)(pdc);
	HDC dc2 = CreateCompatibleDC(dc);
	HBITMAP old_bmp = (HBITMAP)SelectObject(dc2, _wnd.dib_sect);
	HPALETTE old_palette = SelectPalette(dc, _wnd.gdi_palette, FALSE);

	if (_cur_palette.count_dirty != 0) {
		Blitter *blitter = BlitterFactory::GetCurrentBlitter();

		switch (blitter->UsePaletteAnimation()) {
			case Blitter::PALETTE_ANIMATION_VIDEO_BACKEND:
				UpdatePalette(dc2, _local_palette.first_dirty, _local_palette.count_dirty);
				break;

			case Blitter::PALETTE_ANIMATION_BLITTER:
				blitter->PaletteAnimate(_local_palette);
				break;

			case Blitter::PALETTE_ANIMATION_NONE:
				break;

			default:
				NOT_REACHED();
		}
		_cur_palette.count_dirty = 0;
	}

	BitBlt(dc, 0, 0, _wnd_width, _wnd_height, dc2, 0, 0, SRCCOPY);
	SelectPalette(dc, old_palette, TRUE);
	SelectObject(dc2, old_bmp);
	DeleteDC(dc2);
}

void VideoDriver_Win32_GDI::DoFlush()
{
	GdiFlush();
}
void VideoDriver_Win32_GDI::DoQueryNewPalette()
{
	HDC hDC = GetWindowDC(_wnd_main_wnd);
	HPALETTE hOldPalette = SelectPalette(hDC, _wnd.gdi_palette, FALSE);
	UINT nChanged = RealizePalette(hDC);

	SelectPalette(hDC, hOldPalette, TRUE);
	ReleaseDC(_wnd_main_wnd, hDC);
	if (nChanged != 0) InvalidateRect(_wnd_main_wnd, nullptr, FALSE);
}

static bool AllocateDibSection(int w, int h, bool force)
{
	BITMAPINFO *bi;
	HDC dc;
	uint bpp = BlitterFactory::GetCurrentBlitter()->GetScreenDepth();

	w = max(w, 64);
	h = max(h, 64);

	if (bpp == 0) usererror("Can't use a blitter that blits 0 bpp for normal visuals");

	if (!force && w == _screen.width && h == _screen.height) return false;

	bi = (BITMAPINFO*)alloca(sizeof(BITMAPINFOHEADER) + sizeof(RGBQUAD) * 256);
	memset(bi, 0, sizeof(BITMAPINFOHEADER) + sizeof(RGBQUAD) * 256);
	bi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);

	bi->bmiHeader.biWidth = _wnd_width = w;
	bi->bmiHeader.biHeight = -(_wnd_height = h);

	bi->bmiHeader.biPlanes = 1;
	bi->bmiHeader.biBitCount = BlitterFactory::GetCurrentBlitter()->GetScreenDepth();
	bi->bmiHeader.biCompression = BI_RGB;

	if (_wnd.dib_sect) DeleteObject(_wnd.dib_sect);

	dc = GetDC(0);
	_wnd.dib_sect = CreateDIBSection(dc, bi, DIB_RGB_COLORS, (VOID**)&_wnd.buffer_bits, nullptr, 0);
	if (_wnd.dib_sect == nullptr) usererror("CreateDIBSection failed");
	ReleaseDC(0, dc);

	_screen.width = w;
	_screen.pitch = (bpp == 8) ? Align(w, 4) : w;
	_screen.height = h;
	_screen.dst_ptr = _wnd.buffer_bits;

	return true;
}

const char *VideoDriver_Win32_GDI::DoStart()
{
	MakePalette();
	AllocateDibSection(_cur_resolution.width, _cur_resolution.height);
	return nullptr;
}

void VideoDriver_Win32_GDI::DoStop()
{
	DeleteObject(_wnd.gdi_palette);
	DeleteObject(_wnd.dib_sect);
}

bool VideoDriver_Win32_GDI::DoAfterBlitterChange()
{
	return AllocateDibSection(_screen.width, _screen.height, true) && this->MakeWindow(_fullscreen);
}
