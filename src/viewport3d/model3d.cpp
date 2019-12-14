#include "model3d.h"
#include "math3d.h"

#include <ctype.h>
#include "../fileio_func.h"

#include <map>

static std::vector<ModelEntry> _model_entrys;
static std::vector<ModelVertex> _vertex;
static std::vector<uint32> _index;
static GLuint _vertex_buffer = 0;
static GLuint _index_buffer = 0;
static int _buffer_dirty = 0;

class LessFctr {
public:
	bool operator () (const ModelEntry::Key &a, const ModelEntry::Key &b) const { return (a.second < b.second);	}
};
typedef std::multimap<ModelEntry::Key, uint32, LessFctr> EntrysMap;
static EntrysMap _entrys_map;

PACK(struct Point3f { float x; float y; float z; });
PACK(struct Point3i { int x; int y; int z; });

uint32 LoadModelFile(const char *path)
{
	ModelEntry::Key key(path, murmur3_32(path, strlen(path), 'OTTD'));
	EntrysMap::iterator it = _entrys_map.lower_bound(key);
	if (it != _entrys_map.end())
	{
		do
		{
			if (!strcmp(it->first.first, key.first)) return it->second;
			it++;

		} while ((it != _entrys_map.end()) && (it->first.second == key.second));
	}

	std::vector<char> text;
	{
		size_t size;
		FILE *f = FioFOpenFile(path, "rb", Subdirectory::NEWGRF_DIR, &size);
		if (!f)
		{
			error("Model '%s' not found.", path);
			return INVALID_MODEL;
		}
		text.resize(size + 1);
		size_t result = fread(text.data(), 1, size, f);
		text[text.size() - 1] = 0;
		FioFCloseFile(f);
		if (result != size)
		{
			error("Can't read model '%s'.", path);
			return INVALID_MODEL;
		}
	}
	
	_model_entrys.emplace_back();
	ModelEntry *model = &_model_entrys.back();
	_entrys_map.insert(std::pair<ModelEntry::Key, uint32>(key, (uint32)(_model_entrys.size() - 1)));

	///

	std::vector<Point3f> vva;
	std::vector<Point3f> vta;
	std::vector<Point3f> vna;
	std::vector<Point3i> fva;

	model->first_vertex = (uint32)(_vertex.size());
	model->first_index = (uint32)(_index.size());

	char *t = text.data();
	while (*t)
	{	
		while (*t && isspace(*t)) t++;
		switch (*t)
		{
		case 'f':
			{
				int idx[3][3];
				if (sscanf(t, "f %i/%i/%i %i/%i/%i %i/%i/%i", &idx[0][0], &idx[0][1], &idx[0][2], &idx[1][0], &idx[1][1], &idx[1][2], &idx[2][0], &idx[2][1], &idx[2][2]) == 9)
				{
					int exists = -1;
					for (int n = 0; n < 3; n++)
					{
						for (size_t i = 0; i < fva.size(); i++)
						{
							if ((idx[n][0] != fva[i].x) || (idx[n][1] != fva[i].y) || (idx[n][2] != fva[i].z)) continue;
							exists = (int)(i);
							break;
						}

						if (exists >= 0)
						{
							_index.push_back(exists);
							exists = -1;
							continue;
						}
						_index.push_back((uint32)(fva.size()));

						fva.emplace_back();
						Point3i &p = fva.back();
						p.x = idx[n][0];
						p.y = idx[n][1];
						p.z = idx[n][2];
					}
				}
			}
			break;

		case 'v':
			if (t[1] == 'n')
			{
				float vn[3];
				if (sscanf(t, "vn %f %f %f", &vn[0], &vn[1], &vn[2]) == 3)
				{
					vna.emplace_back();
					Point3f &p = vna.back();
					p.x = vn[0];
					p.y = vn[1];
					p.z = vn[2];
				}
			}
			else if (t[1] == 't')
			{
				float vt[3];
				if (sscanf(t, "vt %f %f %f", &vt[0], &vt[1], &vt[2]) == 3)
				{
					vta.emplace_back();
					Point3f &p = vta.back();
					p.x = vt[0];
					p.y = vt[1];
					p.z = vt[2];
				}
			}
			else
			{
				float v[3];
				if (sscanf(t, "v %f %f %f", &v[0], &v[1], &v[2]) == 3)
				{
					vva.emplace_back();
					Point3f &p = vva.back();
					p.x = v[0];
					p.y = v[1];
					p.z = v[2];
				}
			}
			break;
		}
		while (*t && (*t != '\n')) t++;
	}

	///

	vectSet3(model->bbmin, +FLT_MAX);
	vectSet3(model->bbmax, -FLT_MAX);
	model->radius = 0.0;

	if (fva.size())
	{
		for (size_t i = 0; i < vva.size(); i++)
		{
			vectMin3(model->bbmin, model->bbmin, FP(vva[i]));
			vectMax3(model->bbmax, model->bbmax, FP(vva[i]));
			model->radius = max(model->radius, vectLength3(FP(vva[i])));
		}

		_vertex.resize(_vertex.size() + fva.size());
		ModelVertex *dst = &_vertex[_vertex.size() - fva.size()];
		for (size_t i = 0; i < fva.size(); i++)
		{
			ModelVertex *v = dst + i;
			vectCopy3(FP(v->pos), FP(vva[fva[i].x - 1]));
			vectCopy2(FP(v->tex), FP(vta[fva[i].y - 1]));
			vectCopy3(FP(v->nrm), FP(vna[fva[i].z - 1]));
			v->tex.y = 1.0f - v->tex.y;
		}
	}

	model->index_count = (uint32)(_index.size() - model->first_index);
	model->vertex_count = (uint32)(_vertex.size() - model->first_vertex);
	_buffer_dirty = 1;

	///

	return (uint32)(_model_entrys.size() - 1);
}

void UpdateModels(GLuint &vertex, GLuint &index)
{
	if (!_vertex_buffer) glGenBuffers(1, &_vertex_buffer);
	if (!_index_buffer) glGenBuffers(1, &_index_buffer);

	vertex = _vertex_buffer;
	index = _index_buffer;

	if (!_buffer_dirty) return;

	uint32 vertex_count = (uint32)(_vertex.size());
	uint32 index_count = (uint32)(_index.size());
	{
		glBindBuffer(GL_ARRAY_BUFFER, _vertex_buffer);
		glBufferData(GL_ARRAY_BUFFER, vertex_count * sizeof(ModelVertex), _vertex.data(), GL_STATIC_DRAW);
		glBindBuffer(GL_ARRAY_BUFFER, 0);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _index_buffer);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, index_count * sizeof(uint32), _index.data(), GL_STATIC_DRAW);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	}

	_buffer_dirty = 0;
}

const ModelEntry *GetModelEntry(uint32 index)
{
	if (index >= _model_entrys.size()) return nullptr;
	return &_model_entrys[index];
}

void FreeModels()
{
	_vertex.clear();
	_index.clear();

	_model_entrys.clear();
	_entrys_map.clear();
}
