/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file win32_v.h Base of the Windows video driver. */

#ifndef VIDEO_WIN32_H
#define VIDEO_WIN32_H

#include "video_driver.hpp"

/** The video driver for windows. */
class VideoDriver_Win32 : public VideoDriver {
public:
	const char *Start(const char * const *param) override;

	void Stop() override;

	void MakeDirty(int left, int top, int width, int height) override;

	void MainLoop() override;

	bool ChangeResolution(int w, int h) override;

	bool ToggleFullscreen(bool fullscreen) override;

	bool AfterBlitterChange() override;

	void AcquireBlitterLock() override;

	void ReleaseBlitterLock() override;

	bool ClaimMousePointer() override;

	void EditBoxLostFocus() override;

	bool MakeWindow(bool full_screen);

public:
	virtual void DoCreateWindow() = 0;
	virtual void DoDestroyWindow() = 0;
	virtual void DoPaintWindow(void *hdc) = 0;
	virtual void DoQueryNewPalette() = 0;
	virtual const char *DoStart() = 0;
	virtual void DoStop() = 0;
	virtual void DoFlush() = 0;
	virtual void DoClientSizeChanged(int width, int height) = 0;
	virtual bool DoAfterBlitterChange() = 0;
	virtual bool IsDrawThreaded() { return true; };
};

#endif /* VIDEO_WIN32_H */
