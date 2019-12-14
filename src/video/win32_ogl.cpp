/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file win32_ogl.cpp Implementation of the Windows OpenGL video driver. */

#include "../stdafx.h"
#include "../gfx_func.h"
#include "../blitter/factory.hpp"
#include "../core/math_func.hpp"
#include "win32_ogl.h"
#include <windows.h>

#include "../3rdparty/glad/include/glad/glad.h"

#include "../safeguards.h"

static struct {
	HDC dc;
	HGLRC rc;
} _wnd;

extern HWND _wnd_main_wnd;
extern int _wnd_width;
extern int _wnd_height;

extern Palette _local_palette;

static FVideoDriver_Win32_OGL iFVideoDriver_Win32_OGL;

#define WGL_CONTEXT_DEBUG_BIT_ARB				0x0001
#define WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB	0x0002
#define WGL_CONTEXT_FLAGS_ARB					0x2094

#define WGL_CONTEXT_MAJOR_VERSION_ARB			0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB			0x2092
#define WGL_CONTEXT_ES_PROFILE_BIT_EXT			0x0004

typedef HGLRC(WINAPI * PFNWGLCREATECONTEXTATTRIBSARBPROC) (HDC hDC, HGLRC hShareContext, const int* attribList);

#define DebugMsg(...)	debug("OpenGL", __VA_ARGS__)

void APIENTRY DebugMessage(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam)
{
	const char *s = "UNKNOWN";
	switch (source)
	{
	case GL_DEBUG_SOURCE_API: s = "SOURCE_API"; break;
	case GL_DEBUG_SOURCE_WINDOW_SYSTEM: s = "SOURCE_WINDOW_SYSTEM"; break;
	case GL_DEBUG_SOURCE_SHADER_COMPILER: s = "SOURCE_SHADER_COMPILER"; break;
	case GL_DEBUG_SOURCE_THIRD_PARTY: s = "SOURCE_THIRD_PARTY"; break;
	case GL_DEBUG_SOURCE_APPLICATION: s = "SOURCE_APPLICATION"; break;
	case GL_DEBUG_SOURCE_OTHER: s = "SOURCE_OTHER"; break;
	}

	const char *t = "UNKNOWN";
	switch (type)
	{
	case GL_DEBUG_TYPE_ERROR: t = "ERROR"; break;
	case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: t = "DEPRECATED_BEHAVIOR"; break;
	case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: t = "UNDEFINED_BEHAVIOR"; break;
	case GL_DEBUG_TYPE_PORTABILITY: t = "PORTABILITY"; break;
	case GL_DEBUG_TYPE_PERFORMANCE: t = "PERFORMANCE"; break;
	case GL_DEBUG_TYPE_MARKER: t = "MARKER"; break;
	case GL_DEBUG_TYPE_PUSH_GROUP: t = "PUSH_GROUP"; break;
	case GL_DEBUG_TYPE_POP_GROUP: t = "POP_GROUP"; break;
	case GL_DEBUG_TYPE_OTHER: t = "OTHER"; break;
	}

	const char *l = "UNKNOWN";
	switch (severity)
	{
	case GL_DEBUG_SEVERITY_NOTIFICATION: l = "NOTIFICATION"; break;
	case GL_DEBUG_SEVERITY_LOW: l = "LOW"; break;
	case GL_DEBUG_SEVERITY_MEDIUM: l = "MEDIUM"; break;
	case GL_DEBUG_SEVERITY_HIGH: l = "HIGH"; break;
	}

	DebugMsg("%s %s:%i %s: %s", s, t, id, l, message);
}

void VideoDriver_Win32_OGL::DoClientSizeChanged(int w, int h)
{
	if ((w == _screen.width) && (h == _screen.height)) return;

	_wnd_width = w;
	_wnd_height = h;

	_screen.width = _wnd_width;
	_screen.pitch = _wnd_width;
	_screen.height = _wnd_height;
	_screen.dst_ptr = nullptr;

	_cur_palette.first_dirty = 0;
	_cur_palette.count_dirty = 256;
	_local_palette = _cur_palette;

	BlitterFactory::GetCurrentBlitter()->PostResize();

	GameSizeChanged();
}

void VideoDriver_Win32_OGL::DoCreateWindow()
{
	PIXELFORMATDESCRIPTOR pfd={ 0 };
	pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
	pfd.nVersion = 1;
	pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL/* | PFD_DOUBLEBUFFER*/;
	pfd.iPixelType = PFD_TYPE_RGBA;
	pfd.cColorBits = 32;
	pfd.cDepthBits = 24;
	pfd.cStencilBits = 8;
	pfd.iLayerType = PFD_MAIN_PLANE;
	_wnd.dc = GetDC(_wnd_main_wnd);
	int index = ChoosePixelFormat(_wnd.dc, &pfd);
	SetPixelFormat(_wnd.dc, index, &pfd);
}

void VideoDriver_Win32_OGL::DoDestroyWindow()
{
	ReleaseDC(_wnd_main_wnd, _wnd.dc);
	DestroyWindow(_wnd_main_wnd);
}

/** Do palette animation and blit to the window. */
void VideoDriver_Win32_OGL::DoPaintWindow(void *pdc)
{
	Blitter *blitter = BlitterFactory::GetCurrentBlitter();
	if(_local_palette.count_dirty != 0)
	{
		blitter->PaletteAnimate(_local_palette);
		_local_palette.count_dirty = 0;
	}

	blitter->Finish();
//	SwapBuffers(_wnd.dc);
}

void *GetAnyGLFuncAddress(const char *name)
{
	void *p = (void *)(wglGetProcAddress(name));
	if ((p == 0) || (p == (void*)(0x1)) || (p == (void*)(0x2)) || (p == (void*)(0x3)) || (p == (void*)(-1)))
	{
		HMODULE module = LoadLibraryA("opengl32.dll");
		p = (void *)(GetProcAddress(module, name));
	}
	return p;
}

const char *VideoDriver_Win32_OGL::DoStart()
{
	_cur_palette.first_dirty = 0;
	_cur_palette.count_dirty = 256;
	_local_palette = _cur_palette;

	HGLRC imm = wglCreateContext(_wnd.dc);
	wglMakeCurrent(_wnd.dc, imm);
	
	int flags = 0;
#ifdef _DEBUG
	flags |= WGL_CONTEXT_DEBUG_BIT_ARB/* | WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB*/;
#endif
	int attribs[] = {
//		WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
//		WGL_CONTEXT_MINOR_VERSION_ARB, 3,
//		WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
//		WGL_CONTEXT_MINOR_VERSION_ARB, 3,
//		GL_CONTEXT_PROFILE_MASK, GL_CONTEXT_CORE_PROFILE_BIT,
//		GL_CONTEXT_PROFILE_MASK, WGL_CONTEXT_ES_PROFILE_BIT_EXT,
		GL_CONTEXT_PROFILE_MASK, GL_CONTEXT_COMPATIBILITY_PROFILE_BIT,
		WGL_CONTEXT_FLAGS_ARB, flags,
		0, 0
	};

	PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC)(GetAnyGLFuncAddress("wglCreateContextAttribsARB"));
	if (!wglCreateContextAttribsARB) return "wglCreateContextAttribsARB is required!";
	_wnd.rc = wglCreateContextAttribsARB(_wnd.dc, nullptr, attribs);
	wglMakeCurrent(_wnd.dc, NULL);
	wglDeleteContext(imm);

	wglMakeCurrent(_wnd.dc, _wnd.rc);
	if (!gladLoadGLLoader((GLADloadproc)(GetAnyGLFuncAddress))) return "Failed to init glad!";
	if (!GLAD_GL_VERSION_3_3) return "OpenGL 3.3 or greater is required!";

#ifdef _DEBUG
	glDebugMessageCallback(&DebugMessage, nullptr);

	GLuint id = 0;
	glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, &id, true); // enable all
	id = 131185; glDebugMessageControl(GL_DEBUG_SOURCE_API, GL_DEBUG_TYPE_OTHER, GL_DONT_CARE, 1, &id, false); // disable messages about stream draw buffers in the video memory

	glEnable(GL_DEBUG_OUTPUT);
	glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
#endif
	
	const GLubyte *glslVersion = glGetString(GL_SHADING_LANGUAGE_VERSION);
	DebugMsg("Vendor:       %s", glGetString(GL_VENDOR));
	DebugMsg("Renderer:     %s", glGetString(GL_RENDERER));
	DebugMsg("Version:      %s", glGetString(GL_VERSION));
	DebugMsg("GLSL Version: %s", glslVersion ? (const char*)(glslVersion) : "NONE");

	return nullptr;
}

void VideoDriver_Win32_OGL::DoStop()
{
	wglMakeCurrent(nullptr, nullptr);
	wglDeleteContext(_wnd.rc);
	ReleaseDC(_wnd_main_wnd, _wnd.dc);
}

bool VideoDriver_Win32_OGL::DoAfterBlitterChange()
{
	return this->MakeWindow(_fullscreen);
}
