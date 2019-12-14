#ifndef IMAGE3D_H
#define IMAGE3D_H

#include "../stdafx.h"

#include "../3rdparty/OpenGL/glew.h"
#include <gl/gl.h>

#include <vector>
#include <string>

#define INVALID_IMAGE			((uint32)(-1))		// invalid image index
#define IMAGE3D_ATLAS_SIZE		1024
#define IMAGE3D_ATLAS_SIZE_F	((float)(IMAGE3D_ATLAS_SIZE))

struct ImageEntry
{
	typedef std::pair<const char*, uint32> Key;

	uint32 layer;		// atlas layer
	uint32 offs_x;		// x offset in layer
	uint32 offs_y;		// y offset in layer
	uint32 size_x;		// size x
	uint32 size_y;		// size y
};

extern uint32 LoadImageFile(const char *path);
extern void UpdateImages(GLuint &texture, uint32 &layers);
extern const ImageEntry *GetImageEntry(uint32 index);
extern void FreeImages();

#endif /* ATLAS3D_H */