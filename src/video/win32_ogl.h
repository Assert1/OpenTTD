/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file win32_v.h Base of the Windows OpenGL video driver. */

#ifndef VIDEO_WIN32_OGL_H
#define VIDEO_WIN32_OGL_H

#include "win32_v.h"

/** The video driver for windows OpenGL. */
class VideoDriver_Win32_OGL : public VideoDriver_Win32 {
public:
	/* virtual */ void DoCreateWindow();
	/* virtual */ void DoDestroyWindow();
	/* virtual */ void DoPaintWindow(void *hdc);
	/* virtual */ void DoQueryNewPalette() {};
	/* virtual */ const char *DoStart();
	/* virtual */ void DoStop();
	/* virtual */ void DoFlush() {};
	/* virtual */ void DoClientSizeChanged(int width, int height);
	/* virtual */ bool DoAfterBlitterChange();
	/* virtual */ bool IsDrawThreaded() { return false; };

	/* virtual */ const char *GetName() const { return "win32ogl"; }
	/* virtual */ bool Hardware() { return true; }
};

/** The factory for Windows' OpenGL video driver. */
class FVideoDriver_Win32_OGL : public DriverFactoryBase {
public:
	FVideoDriver_Win32_OGL() : DriverFactoryBase(Driver::DT_VIDEO, 10, "win32ogl", "Win32 OpenGL Video Driver") {}
	/* virtual */ Driver *CreateInstance() const { return new VideoDriver_Win32_OGL(); }
};

#endif /* VIDEO_WIN32_OGL_H */
