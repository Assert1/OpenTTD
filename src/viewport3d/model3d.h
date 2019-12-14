#ifndef MODEL3D_H
#define MODEL3D_H

#include "../stdafx.h"

#include "../3rdparty/glad/include/glad/glad.h"

#include <vector>
#include <string>

#define INVALID_MODEL	((uint32)(-1))		// invalid image index

PACK(struct ModelVertex
{
	struct
	{
		float x;
		float y;
		float z;
	} pos;
	struct
	{
		float x;
		float y;
	} tex;
	struct
	{
		float x;
		float y;
		float z;
	} nrm;
});

struct ModelEntry
{
	typedef std::pair<const char*, uint32> Key;
	uint32 first_vertex;
	uint32 vertex_count;
	uint32 first_index;
	uint32 index_count;
	float bbmin[3]; // bounding box min
	float bbmax[3]; // bounding box max
	float radius; // bounding sphere radius
};

extern uint32 LoadModelFile(const char *path);
extern void UpdateModels(GLuint &vertex, GLuint &index);
extern const ModelEntry *GetModelEntry(uint32 index);
extern void FreeModels();

#endif /* MODEL3D_H */