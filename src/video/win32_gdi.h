/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file win32_v.h Windows GDI video driver. */

#ifndef VIDEO_WIN32_GDI_H
#define VIDEO_WIN32_GDI_H

#include "win32_v.h"

/** The video driver for windows GDI. */
class VideoDriver_Win32_GDI : public VideoDriver_Win32 {
public:
	void DoCreateWindow() override {};
	void DoDestroyWindow() override {};
	void DoPaintWindow(void *hdc) override ;
	void DoQueryNewPalette() override ;
	const char *DoStart() override ;
	void DoStop() override ;
	void DoFlush() override ;
	void DoClientSizeChanged(int width, int height) override ;
	bool DoAfterBlitterChange() override ;
	bool IsDrawThreaded() override { return true; };

	const char *GetName() const override { return "win32"; }
};

/** The factory for Windows' GDI video driver. */
class FVideoDriver_Win32_GDI : public DriverFactoryBase {
public:
	FVideoDriver_Win32_GDI() : DriverFactoryBase(Driver::DT_VIDEO, 9, "win32", "Win32 GDI Video Driver") {}
	Driver *CreateInstance() const override { return new VideoDriver_Win32_GDI(); };
};

#endif /* VIDEO_WIN32_H */
