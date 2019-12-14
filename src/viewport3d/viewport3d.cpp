#include "../stdafx.h"
#include "../core/alloc_func.hpp"
#include "../vehicle_base.h"
#include "../engine_base.h"
#include "../newgrf.h"
#include "../vehicle_func.h"
#include "../aircraft.h"
#include "../train.h"
#include "../track_func.h"
#include "../roadveh.h"
#include "../ship.h"
#include "../pbs.h"
#include "../disaster_vehicle.h"
#include "../spritecache.h"
#include "../tunnel_map.h"
#include "../bridge_map.h"
#include "../tunnelbridge_map.h"
#include "../zoom_func.h"
#include "../viewport_func.h"
#include "../table/sprites.h"
#include "../tree_map.h"
#include "../town.h"
#include "../station_base.h"
#include "../company_func.h"
#include "../strings_func.h"
#include "../signs_base.h"
#include "../signs_func.h"
#include "../gfx_layout.h"
#include "../waypoint_func.h"
#include "../waypoint_base.h"
#include "../framerate_type.h"

#include "../gfx_func.h"
#include "../table/string_colours.h"

#include "math3d.h"
#include "config3d.h"
#include "image3d.h"
#include "model3d.h"
#include "link3d.h"
#include "viewport3d.h"

#include "../blitter/opengl.hpp"

/// settings

static bool _use_shadows_set = _settings_client.gui.view3d_use_shadows; // current state of the shadow setting
static int _shadows_res_set = _settings_client.gui.view3d_shadows_res; // current state of the shadow resolution setting
static int _multisample_set = _settings_client.gui.opengl_multisample; // current state of the multisample setting, needed for the alpha test mode

///

extern int _opengl_ver;				// we are running 4.3 or 3.3?

extern TileInfo *_cur_ti;			// info about currently drawn tile
extern DrawPixelInfo *_cur_dpi;		// 2D drawing control data
extern DrawPixelInfo _screen;		// screen drawing control data
extern byte _misc_grf_features;

static int _initialized = 0;		// subsystem intialized
static int _reset = 0;				// subsystem needs reset (landscape dimension changed)

static float BASE_SCALE = 2.82842708f;			// base scale for the rendering (from original orthogonal projection)
//static float TILE_HEIGHT_ACT = 3.26598620f;	// visual height of TILE_HEIGHT
static float TILE_HEIGHT_ACT = 4.0f;			// visual height of TILE_HEIGHT
static float TILE_HEIGHT_SCALE = TILE_HEIGHT_ACT / (float)(TILE_HEIGHT); // z scale
static float TILE_WATER_OFFSET = -0.4f;			// offset of the water line below the tile level

static float TILE_HEIGHT_F = (float)(TILE_HEIGHT);
static float TILE_SIZE_F = (float)(TILE_SIZE);

#define SHADOW_MAP_SIZE (2048 >> Clamp(_shadows_res_set, 0, 3))

enum SignType
{
	VPST_STATION,
	VPST_WAYPOINT,
	VPST_TOWN,
	VPST_SIGN,
	VPST_EFFECT,
};

// cache structure to draw on screen signs, labels and text effects
struct LandSign
{
	SignType type;
	int pos[3];
	union
	{
		StationID station;
		SignID sign;
		TownID town;
		TextEffectID effect;
		uint16 data;
	} id;
	const ViewportSign *sign;
	Layouter layout;
	Layouter layout_small;
	Layouter layout_small_shadow;
	Colours colour;
};

static float _frustum[6][4];				// current view frustum clip planes
static float _xyz_to_shadow[16];			// current view shadow transform matrix
static float _xyz_to_shadow_tex[16];		// current view shadow texture lookup matrix

static GLuint _sampler[4];					// sampler objects
static GLuint _atlas_texture;				// current atlas texture from the image manager
static uint32 _atlas_layers;				// current atlas layers count from the image manager

static GLuint _model_vertex_buffer;			// current vertex buffer from the model manager
static GLuint _model_index_buffer;			// current index buffer from the model manager
static GLuint _model_instance_buffer = 0;	// instancing stream buffer

static GLuint _land_program = 0;			// program to draw the landscape
static GLuint _land_vertex_format;
static GLint _land_attribs_link[5];
static GLint _land_uniforms_link[10];

static GLuint _land_sel_program = 0;		// program to draw the landscape selection
static GLuint _land_sel_vertex_format;
static GLint _land_sel_attribs_link[4];
static GLint _land_sel_uniforms_link[6];

static GLuint _land_object_program = 0;		// program to draw the land objects
static GLuint _land_object_vertex_format;
static GLint _land_object_attribs_link[9];
static GLint _land_object_uniforms_link[10];

static GLuint _object_program = 0;			// program to draw the objects
static GLuint _object_vertex_format;
static GLint _object_attribs_link[9];
static GLint _object_uniforms_link[10];

static GLuint _fill_land_program = 0;		// program to fill the landscape Z
static GLuint _fill_land_vertex_format;
static GLint _fill_land_attribs_link[3];
static GLint _fill_land_uniforms_link[5];

static GLuint _fill_object_program = 0;		// program to fill the objects Z, and draw they transparently
static GLuint _fill_object_vertex_format;
static GLint _fill_object_attribs_link[7];
static GLint _fill_object_uniforms_link[5];

static GLuint _shadow_frame[5];				// frame buffers for different shadow map levels of detail
static GLuint _shadow_texture = 0;			// shadow map texture
static int _shadow_level;					// selected shadow map levels of detail

static uint32 _transparent_image = 0;		// link to transparent image

static bool _last_signs_transparent_set = false;	// current transparency setting for the signs
static uint32 _last_objects_transparent_set = 0;	// current transparency setting for the objects
static uint32 _last_objects_invisibility_set = 0;	// current invisibility setting for the objects

// stuff from the blitter to load programs
extern GLuint ShaderLoad(const char *file, GLenum type, const char *opt = nullptr);
extern GLuint ProgramLink(GLuint vs, GLuint fs);

// layout precompiler
extern int DrawLayoutLine(const ParagraphLayouter::Line &line, int y, int left, int right, StringAlignment align = SA_LEFT, bool underline = false, bool truncation = false);

// object instance data
PACK(struct ModelInstance
{
	struct
	{
		float px; // x offset in the atlas
		float py; // y offset in the atlas
		float sx; // x scale of the atlas coords
		float sy; // y scale of the atlas coords 
	} loc;
	struct
	{
		float mip_x; // IMAGE_ATLAS_SIZE / IMAGE_SIZE by x
		float mip_y; // IMAGE_ATLAS_SIZE / IMAGE_SIZE by y
		float layer; // atlas layer index
		float pal; // recolor palette index
	} mip;
	struct { float x; float y; float z; float w; } matr[3]; // transform matrix
	uint32 sun;
});

PACK(struct DrawElementsIndirectCmd
{
	GLuint count;
	GLuint instanceCount;
	GLuint firstIndex;
	GLuint baseVertex;
	GLuint baseInstance;
});

#define ZOOMED_BIT					30
#define SPRITE_ZOOMED_FLAG			(1 << ZOOMED_BIT) // flag for cutting of some sprites at the hi zoom levels

#define MODEL_CACHE_MASK_SOLID		1 // to select solid objects
#define MODEL_CACHE_MASK_TRANSP		2 // to select transparent objects

// cached instance data
struct ModelInstanceCache
{
	ModelInstance data; // object data
	uint32 model; // model link
	uint32 mask; // MODEL_CACHE_MASK
	uint32 zoom; // zoom visibility mask
};

PACK(struct TileVertex
{
	struct { float x; float y; float z; } pos[2]; // position
	struct
	{
		float x; // x image coords
		float y; // y image coords
		float layer; // layer index
		float pal; // palette
	} tex[2];
	struct
	{
		float px; // offset x
		float py; // offset y
		float sx; // scale x
		float sy; // scale y
	} loc[2];
	struct
	{
		float mip_x; // ATLAS_SIZE / IMAGE_SIZE by x
		float mip_y; // ATLAS_SIZE / IMAGE_SIZE by y
	} mip[2];
	struct { float x; float y; float z; } nrm; // normal
});

#define LAND_SEG_BITS			3
#define LAND_SEG_RES			(1 << LAND_SEG_BITS) // segment dimension in tiles
#define LAND_SEG_TILE_COUNT		(LAND_SEG_RES * LAND_SEG_RES) // segment tiles count

// draw request
struct DrawLink
{
	SpriteID sprite;	// original sprite id for info and flags
	PaletteID palette;	// original palette id for info
	int offset[3];		// local tile offset in units
	uint32 link;		// config link
	uint32 pal;			// palette cache index
};

struct LandTile
{
	std::vector<DrawLink> draw[2]; // draw requests: self generated (foundations), emitted by the tile draw call
	DrawLink sel;	// selection draw request
	Slope slope;	// original slope
	Foundation f;	// foundation
	Slope fslope;	// slope after foundation
	uint layout;	// triangle layout (EW or NS)
	uint height[6];	// height in units of each triangle vertex
	uint zbase;		// height in TILE_HEIGHT_ACT units of the tile base
	float zmin;		// min tile z for segment bounder
	float zmax;		// max tile z for segment bounder
	int dirty;		// level of dirtness (2 - full, 1 - foundations and draw cache)
};

struct LandSeg
{
	uint32 index;	// index of the segment
	LandTile tile[LAND_SEG_TILE_COUNT];	// segment tiles
	std::vector<ModelInstanceCache> draw_cache; // tiles objects draw cache
	uint32 idx_min; // first tile to draw
	uint32 idx_max; // last tile to draw
	uint32 sel_min; // first selected tile
	uint32 sel_max; // last selected tile
	std::list<TextEffectID> effects; // text effects
	std::vector<LandSign> labels; // labels (stations, waypoints, towns signs)
	std::list<SignID> signs; // signs
	float bbmin[3]; // bounder min
	float bbmax[3]; // bounder max
	int dirty;		// level of dirtness (2 - full, 1 - foundations and draw cache)
};

struct LandData
{
	uint32 size_x;					// x tiles allocated
	uint32 size_y;					// y tiles allocated
	std::vector<LandSeg> segs;		// segments info
	std::vector<LandTile*> tiles;	// direct pointers to tiles
	std::vector<TileVertex> vertex;	// 6 vertex for each tile
	GLuint vertex_buffer;			// landscape vertex buffer
	int buffer_dirty;				// landscape vertex buffer need an update
	int dirty;						// landscape data needs an update

	LandData()
	{
		size_x = 0;
		size_y = 0;
		vertex_buffer = 0;
		buffer_dirty = 0;
		dirty = 0;
	}
};

// data for the tracked vehicles
struct VehicleData
{
	VehicleID index;		// vehicle index
	std::vector<DrawLink> draw; // draw requests
	std::vector<ModelInstanceCache> draw_cache; // instance cache
	bool linked;			// is any draw request exsists for this vehicle?

	TileIndex tile;			// the tile wher it is
	Trackdir track;			// current following direction
	Trackdir track_prev;	// previous following direction
	float frame;			// last road veh frame
	int frame_inc;			// road veh frame inc
	float posf[3];			// floating point position
	float angle;			// rotation about Z
	float slope;			// rotation about slope
	float matr[16];			// transform matrix
	float matr_inv[16];		// inverse of transform matrix
	float bbmin[3];			// transformed by the matr bounder min
	float bbmax[3];			// transformed by the matr bounder max
	float bbmin_ref[3];		// untransformed bounder min for click test
	float bbmax_ref[3];		// untransformed bounder max for click test

	VehicleData()
	{
		tile = 0;
		track = INVALID_TRACKDIR;
		track_prev = INVALID_TRACKDIR;
		frame = 0.0f;
		frame_inc = 0;
		posf[0] = 0.0f;
		posf[1] = 0.0f;
		posf[2] = 0.0f;
		angle = 0.0f;
		slope = 0.0f;
	}
};

static LandData _land; // landscape data
static std::vector<VehicleData*> _veh_cache; // trached vehicle cache

static std::vector<LandSeg*> _draw_seg;		// segments to draw on the viewport
static std::vector<VehicleData*> _draw_veh;	// vehicles to draw on the viewport

// draw data
static std::vector<GLint> _draw_seg_first;
static std::vector<GLint> _draw_sel_first;
static std::vector<GLsizei> _draw_seg_count;
static std::vector<GLsizei> _draw_sel_count;
static std::vector<DrawElementsIndirectCmd> _land_object_instance_cmd;
static std::vector<DrawElementsIndirectCmd> _object_instance_cmd;
static std::vector<DrawElementsIndirectCmd> _depth_instance_cmd;
static size_t _model_instance_buffer_size = 0;

static std::vector<LandSign> _signs;		// draw info about signs
static std::vector<LandSign> _text_effects; // draw info about text effects

static int _info_tile = -1;		// tile index to show the drawing stack
static LandTile *_current_tile;	// current tile for draw call
int _redirect_draw = 0;			// redirect a drawing calls of the tile from a viewport

static float _sun_dir[3];		// direction to the sun
static float _box_points[8][4] =
{
	{ -1.0f, +1.0f, -1.0f, 1.0f }, // 0 LTN
	{ +1.0f, +1.0f, -1.0f, 1.0f }, // 1 RTN
	{ +1.0f, -1.0f, -1.0f, 1.0f }, // 2 RBN
	{ -1.0f, -1.0f, -1.0f, 1.0f }, // 3 LBN

	{ -1.0f, +1.0f, +1.0f, 1.0f }, // 4 LTF
	{ +1.0f, +1.0f, +1.0f, 1.0f }, // 5 RTF
	{ +1.0f, -1.0f, +1.0f, 1.0f }, // 6 RBF
	{ -1.0f, -1.0f, +1.0f, 1.0f }, // 7 LBF
};

static float _hs = TILE_HEIGHT_SCALE / 2.0f;
static float _auto_slope[][16] =
{
	{
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		-_hs, 0.0f, 1.0f, TILE_HEIGHT_ACT,
		0.0f, 0.0f, 0.0f, 1.0f,
	},
	{
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, +_hs, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f,
	},
	{
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		+_hs, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f,
	},
	{
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, -_hs, 1.0f, TILE_HEIGHT_ACT,
		0.0f, 0.0f, 0.0f, 1.0f,
	},
	{
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		+_hs, -_hs, 1.0f, TILE_HEIGHT_ACT,
		0.0f, 0.0f, 0.0f, 1.0f,
	},
	{
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		+_hs, +_hs, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f,
	},
	{
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		-_hs, +_hs, 1.0f, TILE_HEIGHT_ACT,
		0.0f, 0.0f, 0.0f, 1.0f,
	},
	{
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		-_hs, -_hs, 1.0f, TILE_HEIGHT_ACT * 2.0f,
		0.0f, 0.0f, 0.0f, 1.0f,
	},
};

#define LAND_SEGS_X		CeilDiv(_land.size_x, LAND_SEG_RES)
#define LAND_SEGS_Y		CeilDiv(_land.size_y, LAND_SEG_RES)

// model instances accumulator
struct ModelInstanceData
{
	std::vector<ModelInstance*> data;
	uint32 model;
	uint32 shade;
};
static std::vector<int> _draw_model; // index of _draw_model_data for each model
static std::vector<ModelInstanceData> _draw_model_data; // models instances draw list
static std::vector<int> _draw_model_data_used; // times used of each _draw_model_data
static std::vector<int> _draw_model_data_set; // _draw_model_data used list

/* ensure cache size of the _draw_model */
static void AllocInstancePool(uint32 model, uint32 shade)
{
	if (_draw_model.size() <= model)
	{
		size_t s = _draw_model.size();
		_draw_model.resize(model + 1);
		for (size_t i = s; i < _draw_model.size(); i++) _draw_model[i] = -1;
	}
	if (_draw_model[model] < 0)
	{
		int data = (int)(_draw_model_data.size());
		_draw_model[model] = data;
		_draw_model_data.emplace_back();
		_draw_model_data_used.emplace_back(0);
		_draw_model_data[data].model = model;
		_draw_model_data[data].shade = shade;
	}
	if (_draw_model_data[_draw_model[model]].shade != shade)
	{
		error("different shade modes for the same model!");
	}
}

static void FreeInstancePool()
{
	_draw_model.clear();
	_draw_model_data.clear();
	_draw_model_data_used.clear();
	_draw_model_data_set.clear();
}

/* place model instances to the draw list */ 
static void AddModelInstances(std::vector<ModelInstanceCache> &src, uint32 mask, uint32 zoom)
{
	for (ModelInstanceCache &c : src)
	{
		if (!(c.mask & mask) || !(c.zoom & zoom)) continue;

		int data = _draw_model[c.model];
		if (_draw_model_data_used[data] == 0) _draw_model_data_set.emplace_back(data);
		_draw_model_data_used[data]++;

		_draw_model_data[data].data.emplace_back(&c.data);
	}
}

/* upload models instances data to the GPU */
static void MakeInstanceData(bool color)
{
	if (!_model_instance_buffer) glGenBuffers(1, &_model_instance_buffer);

	size_t size = 0;
	for (int data : _draw_model_data_set)
	{
		ModelInstanceData *d = &_draw_model_data[data];
		size += d->data.size();
	}

	if (size > _model_instance_buffer_size) _model_instance_buffer_size = size;

	glBindBuffer(GL_ARRAY_BUFFER, _model_instance_buffer);
	glBufferData(GL_ARRAY_BUFFER, _model_instance_buffer_size * sizeof(ModelInstance), NULL, GL_STREAM_DRAW);
	ModelInstance *instances = (ModelInstance*)(glMapBufferRange(GL_ARRAY_BUFFER, 0, _model_instance_buffer_size * sizeof(ModelInstance), GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT));

	GLuint base = 0;
	_land_object_instance_cmd.clear();
	_object_instance_cmd.clear();
	_depth_instance_cmd.clear();
	for (int data : _draw_model_data_set)
	{
		ModelInstanceData *d = &_draw_model_data[data];
		const ModelEntry *mdl = GetModelEntry(d->model);

		DrawElementsIndirectCmd *inst = NULL;
		if (color)
		{
			if (d->shade)
			{
				_land_object_instance_cmd.emplace_back();
				inst = &_land_object_instance_cmd.back();
			}
			else
			{
				_object_instance_cmd.emplace_back();
				inst = &_object_instance_cmd.back();
			}
		}
		else
		{
			_depth_instance_cmd.emplace_back();
			inst = &_depth_instance_cmd.back();
		}
		inst->count = mdl->index_count;
		inst->instanceCount = (GLuint)(d->data.size());
		inst->firstIndex = mdl->first_index;
		inst->baseVertex = mdl->first_vertex;
		inst->baseInstance = base;
		base += inst->instanceCount;

		for (ModelInstance *i : d->data)
		{
			(*instances) = (*i);
			instances++;
		}
		d->data.clear();

		_draw_model_data_used[data] = 0;
	}
	_draw_model_data_set.clear();

	glUnmapBuffer(GL_ARRAY_BUFFER);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

/* update a text effect (recompile layouts) */
void UpdateTextEffect3D(TextEffectID id, StringID string, uint64 params_1, uint64 params_2)
{
	if (id >= _text_effects.size()) return;
	LandSign *s = &_text_effects[id];

	SetDParam(0, params_1);
	SetDParam(1, params_2);

	char buf[DRAW_STRING_BUFFER];
	{
		GetString(buf, string, lastof(buf));
		s->layout = Layouter(buf, INT32_MAX, TC_FROMSTRING, FS_NORMAL);
	}
	{
		GetString(buf, string - 1, lastof(buf));
		s->layout_small = Layouter(buf, INT32_MAX, TC_FROMSTRING, FS_NORMAL);
	}
}

/* a new text effect was added */
void AddTextEffect3D(TextEffectID id, int x, int y, int z, StringID string, uint64 params_1, uint64 params_2)
{
	if (id == INVALID_TE_ID) return;
	if (id >= _text_effects.size())
	{
		size_t s = _text_effects.size();
		_text_effects.resize(id + 1);
		for (size_t i = s; i < _text_effects.size(); i++) _text_effects[i].id.data = INVALID_SIGN;
	}

	LandSign *s = &_text_effects[id];
	if (s->id.data != INVALID_TE_ID)
	{
		int sx = Clamp(s->pos[0] / TILE_SIZE / LAND_SEG_RES, 0, LAND_SEGS_X - 1);
		int sy = Clamp(s->pos[1] / TILE_SIZE / LAND_SEG_RES, 0, LAND_SEGS_Y - 1);
		_land.segs[sy * LAND_SEGS_X + sx].effects.remove(id);
	};
	s->type = VPST_EFFECT;
	s->id.data = id;
	s->pos[0] = x;
	s->pos[1] = y;
	s->pos[2] = z;
	s->sign = nullptr;
	s->colour = INVALID_COLOUR;

	{
		int sx = Clamp(s->pos[0] / TILE_SIZE / LAND_SEG_RES, 0, LAND_SEGS_X - 1);
		int sy = Clamp(s->pos[1] / TILE_SIZE / LAND_SEG_RES, 0, LAND_SEGS_Y - 1);
		_land.segs[sy * LAND_SEGS_X + sx].effects.push_back(id);
	}
	UpdateTextEffect3D(id, string, params_1, params_2);
}

/* remove a text effect from tracking */
void RemoveTextEffect3D(TextEffectID id)
{
	if (id == INVALID_TE_ID) return;
	LandSign *s = &_text_effects[id];
	s->id.data = INVALID_TE_ID;
	s->layout = Layouter();
	s->layout_small = Layouter();
}

/* update a sign (recompile layouts) */
void UpdateSign3D(SignID id)
{
	LandSign *s = &_signs[id];
	const Sign *si = Sign::Get(id);

	SetDParam(0, si->index);
	SetDParam(1, 0);

	char buf[DRAW_STRING_BUFFER];
	{
		GetString(buf, STR_WHITE_SIGN, lastof(buf));
		s->layout = Layouter(buf, INT32_MAX, TC_BLACK, FS_NORMAL);
	}
	{
		GetString(buf, (IsTransparencySet(TO_SIGNS) || si->owner == OWNER_DEITY) ? STR_VIEWPORT_SIGN_SMALL_WHITE : STR_VIEWPORT_SIGN_SMALL_BLACK, lastof(buf));
		s->layout_small = Layouter(buf, INT32_MAX, TC_BLACK, FS_NORMAL);
	}
}

/* a new sign was added */
void AddSign3D(SignID id)
{
	if (id == INVALID_SIGN) return;
	if (id >= _signs.size())
	{
		size_t s = _signs.size();
		_signs.resize(id + 1);
		for (size_t i = s; i < _signs.size(); i++) _signs[i].id.data = INVALID_SIGN;
	}

	const Sign *si = Sign::Get(id);
	LandSign *s = &_signs[id];
	if (s->id.data != INVALID_SIGN)
	{
		int sx = Clamp(s->pos[0] / TILE_SIZE / LAND_SEG_RES, 0, LAND_SEGS_X - 1);
		int sy = Clamp(s->pos[1] / TILE_SIZE / LAND_SEG_RES, 0, LAND_SEGS_Y - 1);
		_land.segs[sy * LAND_SEGS_X + sx].signs.remove(id);
	}
	s->type = VPST_SIGN;
	s->id.data = id;
	s->pos[0] = si->x;
	s->pos[1] = si->y;
	s->pos[2] = si->z;
	s->sign = &si->sign;
	s->colour = (si->owner == OWNER_NONE) ? COLOUR_GREY : (si->owner == OWNER_DEITY ? INVALID_COLOUR : _company_colours[si->owner]);

	{
		int sx = Clamp(s->pos[0] / TILE_SIZE / LAND_SEG_RES, 0, LAND_SEGS_X - 1);
		int sy = Clamp(s->pos[1] / TILE_SIZE / LAND_SEG_RES, 0, LAND_SEGS_Y - 1);
		_land.segs[sy * LAND_SEGS_X + sx].signs.push_back(id);
	}
	UpdateSign3D(id);
}

/* remove a sign from tracking */
static void RemoveSign3D(SignID id)
{
	LandSign *s = &_signs[id];
	s->id.data = INVALID_SIGN;
	s->layout = Layouter();
	s->layout_small = Layouter();
}

/* add a label to the segment */
static void AddLabel(LandSeg *seg, SignType type, uint16 id, int x, int y, const ViewportSign *sign, StringID string_normal, StringID string_small, StringID string_small_shadow, uint64 params_1, uint64 params_2, Colours colour = INVALID_COLOUR)
{
	TextColour c = TC_BLACK;
	if (IsTransparencySet(TO_SIGNS)) c = (TextColour)(_colour_gradient[colour][6] | TC_IS_PALETTE_COLOUR);

	seg->labels.emplace_back();
	LandSign &s = seg->labels.back();
	s.type = type;
	s.id.data = id;
	s.pos[0] = x;
	s.pos[1] = y;
	s.pos[2] = GetSlopePixelZ(x, y);
	s.sign = sign;
	s.colour = colour;

	SetDParam(0, params_1);
	SetDParam(1, params_2);

	char buf[DRAW_STRING_BUFFER];
	{
		GetString(buf, string_normal, lastof(buf));
		s.layout = Layouter(buf, INT32_MAX, c, FS_NORMAL);
	}
	if (string_small != STR_NULL)
	{
		GetString(buf, string_small, lastof(buf));
		s.layout_small = Layouter(buf, INT32_MAX, c, FS_NORMAL);
	}
	if (string_small_shadow != STR_NULL)
	{
		GetString(buf, string_small_shadow, lastof(buf));
		s.layout_small_shadow = Layouter(buf, INT32_MAX, TC_BLACK, FS_NORMAL);
	}
}

/* recompile layouts of all signs */
static void UpdateSigns()
{
	for (size_t i = 0; i < _land.segs.size(); i++)
	{
		LandSeg *s = &_land.segs[i];
		for (auto id : s->signs) UpdateSign3D(id);
		if (!s->labels.size()) continue;
		s->dirty = max(s->dirty, 1);
		_land.dirty = 1;
	}
}

/* get tracked vehicle data */
static VehicleData *GetVehCache(VehicleID v)
{
	size_t len = _veh_cache.size();
	if (len <= v)
	{
		_veh_cache.resize(v + 1);
		for (size_t i = len; i <= v; i++) _veh_cache[i] = nullptr;
	}
	if (!_veh_cache[v])
	{
		_veh_cache[v] = new VehicleData();
		_veh_cache[v]->index = v;
	}
	return _veh_cache[v];
}

/* clear data for the tracked vehicles */
static void ClearVehCache()
{
	size_t len = _veh_cache.size();
	for (size_t i = 0; i < len; i++) delete _veh_cache[i];
	_veh_cache.clear();
}

inline uint32 TileXF(float x) { return (uint32)(Clamp(((int)(x) / TILE_SIZE), 0, _land.size_x - 1)); }
inline uint32 TileYF(float y) { return (uint32)(Clamp(((int)(y) / TILE_SIZE), 0, _land.size_y - 1)); }

/* get Z height of a tile in a given point */
static float GetLandZ(float x, float y, uint32 ref = INVALID_TILE, Slope *slope = nullptr)
{
	uint32 tx = TileXF(x);
	uint32 ty = TileYF(y);
	uint32 ti = TileXY(tx, ty);

	if (ref == INVALID_TILE) ref = ti;
	if ((ti != ref) && IsTileType(ref, MP_TUNNELBRIDGE) && (!IsTileType(ti, MP_TUNNELBRIDGE) || !IsBridge(ti) || (GetTunnelBridgeDirection(ref) != ReverseDiagDir(GetTunnelBridgeDirection(ti)))))
	{
		DiagDirection dir = GetTunnelBridgeDirection(ref);
		if (DiagdirBetweenTiles(ref, ti) == dir)
		{
			LandTile *t = _land.tiles[ref];
			if (slope) (*slope) = SLOPE_FLAT;
			if (IsTunnel(ref)) return t->zbase * TILE_HEIGHT_ACT;
			return (t->zbase + 1) * TILE_HEIGHT_ACT;
		}
	}
	
	uint32 sx = (tx / LAND_SEG_RES);
	uint32 sy = (ty / LAND_SEG_RES);
	uint32 si = sy * LAND_SEGS_X + sx;
	LandSeg *s = &_land.segs[si];

	uint32 stx = (tx - sx * LAND_SEG_RES);
	uint32 sty = (ty - sy * LAND_SEG_RES);
	uint32 sti = sty * LAND_SEG_RES + stx;
	LandTile *t = &s->tile[sti];

	float fx = x - tx * TILE_SIZE_F;
	float fy = y - ty * TILE_SIZE_F;
		
	if (IsTileType(ti, MP_TUNNELBRIDGE))
	{
		if (slope) (*slope) = SLOPE_FLAT;
		if (IsTunnel(ti)) return t->zbase * TILE_HEIGHT_ACT;
		if (t->fslope != SLOPE_FLAT) return (t->zbase + 1) * TILE_HEIGHT_ACT;
		switch (GetTunnelBridgeDirection(ti))
		{
		case DIAGDIR_SW: if (slope) (*slope) = SLOPE_SW; return (t->zbase + (fx / TILE_SIZE_F)) * TILE_HEIGHT_ACT;
		case DIAGDIR_SE: if (slope) (*slope) = SLOPE_SE; return (t->zbase + (fy / TILE_SIZE_F)) * TILE_HEIGHT_ACT;
		case DIAGDIR_NE: if (slope) (*slope) = SLOPE_NE; return (t->zbase + (1.0f - (fx / TILE_SIZE_F))) * TILE_HEIGHT_ACT;
		case DIAGDIR_NW: if (slope) (*slope) = SLOPE_NW; return (t->zbase + (1.0f - (fy / TILE_SIZE_F))) * TILE_HEIGHT_ACT;
		}
	}
	if (slope) (*slope) = t->slope;
	uint32 face = t->layout ? ((fx > fy) ? 0 : 1) : (((TILE_SIZE_F - fx) > fy) ? 0 : 1);

	TileVertex *v = &_land.vertex[(si * LAND_SEG_TILE_COUNT + sti) * 6 + face * 3];
	float d = -vectDot3(FP(v[0].nrm), FP(v[0].pos[1]));
	return -(d + v[0].nrm.x * x + v[0].nrm.y * y) / v[0].nrm.z;
}

void DrawTileSprite3D(SpriteID sprite, PaletteID pal, int x, int y, int z, bool force)
{
	if (!force)
	{
		if (sprite == SPR_EMPTY_BOUNDING_BOX) return;
		if (IsInsideBS(sprite, SPR_FOUNDATION_BASE + 1, 15)) return; // skip base foundations
		if (IsInsideBS(sprite, SPR_SLOPES_BASE, NORMAL_AND_HALFTILE_FOUNDATION_SPRITE_COUNT)) return; // skip extended foundations
/*
		if (IsInsideBS(sprite, SPR_RAIL_SINGLE_X, 58) || IsInsideBS(sprite, SPR_MONO_SINGLE_X, 58) || IsInsideBS(sprite, SPR_MGLV_SINGLE_X, 58)) // track
		{
			TrackBits tb;
			if (IsRailDepotTile(_cur_ti->tile))
			{
				tb = TrackToTrackBits(GetRailDepotTrack(_cur_ti->tile));
			}
			else
			{
				tb = GetTrackBits(_cur_ti->tile);
			}
			while (tb)
			{
				Track track = FindFirstTrack(tb);
				tb &= ~TrackToTrackBits(track);


			}
			return;
		}
/**/
	}

	Blitter_OpenGL *blitter = (Blitter_OpenGL*)(BlitterFactory::GetCurrentBlitter());

	SpriteID s = (sprite & SPRITE_MASK);
	PaletteID p = (pal & PALETTE_MASK);

	uint32 link = GetSpriteLink(GetOriginGRFID(s), GetOriginID(s), s, p);
	if ((link == INVALID_LINK) && (_info_tile < 0)) return;

	uint32 i = force ? 0 : 1;
	LandTile *t = _current_tile;
	
	t->draw[i].emplace_back();
	DrawLink &dl = t->draw[i].back();
	dl.sprite = (sprite & (~SPRITE_ZOOMED_FLAG));
	dl.palette = pal;
	dl.link = link;
	dl.pal = blitter->CachePal(p);
	for (int i = 0; i < 3; i++) dl.offset[i] = 0;
	if (z >= 0)
	{
		dl.offset[0] = x;
		dl.offset[1] = y;
		dl.offset[2] = (z >= 0) ? (z - t->zbase * TILE_HEIGHT) : 0;
		if (!force && IsTileType(_cur_ti->tile, MP_TREES) && IsInsideMM(sprite, 1576, 2010)) // realign trees
		{
			dl.offset[0] = x + 4;
			dl.offset[1] = y + 2;
			dl.offset[2] = GetPartialPixelZ(dl.offset[0] & 0xF, dl.offset[1] & 0xF, t->slope);
			dl.sprite |= SPRITE_ZOOMED_FLAG;
		}
		if (!force && IsInsideMM(sprite, 5623, 5660)) dl.sprite |= SPRITE_ZOOMED_FLAG; // catenary zoom cutoff
	}
}

void DrawTileSelection3D(SpriteID sprite, PaletteID pal)
{
	Blitter_OpenGL *blitter = (Blitter_OpenGL*)(BlitterFactory::GetCurrentBlitter());

	SpriteID s = (sprite & SPRITE_MASK);
	PaletteID p = (pal & PALETTE_MASK);

	DrawLink *dl = &_current_tile->sel;
	dl->sprite = (sprite & (~SPRITE_ZOOMED_FLAG));
	dl->palette = pal;
	dl->link = GetSpriteLink(GetOriginGRFID(s), GetOriginID(s), s, p);
	dl->pal = blitter->CachePal(p);
	dl->offset[0] = 0;
	dl->offset[1] = 0;
	dl->offset[2] = (TileHeight(_cur_ti->tile) - _current_tile->zbase) * TILE_HEIGHT;
/*
	uint32 grfid = GetOriginGRFID(s);
	uint32 id = GetOriginID(s);
	printf("SEL3D %08X:%i : %i:%i %c\r\n", grfid, id, s, p, (dl->sprite & (1 << TRANSPARENT_BIT)) ? 'T' : ' ');
/**/
}

static void MarkTileDirty(TileIndex tile, int level)
{
	uint32 tx = Clamp(TileX(tile), 0, _land.size_x - 1);
	uint32 ty = Clamp(TileY(tile), 0, _land.size_y - 1);

	uint32 sx = (tx / LAND_SEG_RES);
	uint32 sy = (ty / LAND_SEG_RES);
	uint32 si = sy * LAND_SEGS_X + sx;
	LandSeg *s = &_land.segs[si];
	s->dirty = max(s->dirty, level);

	uint32 stx = (tx - sx * LAND_SEG_RES);
	uint32 sty = (ty - sy * LAND_SEG_RES);
	uint32 sti = sty * LAND_SEG_RES + stx;
	LandTile *t = &s->tile[sti];
	t->dirty = max(t->dirty, level);

	_land.dirty = 1;
}

static void MarkLandDirty()
{
	for (size_t i = 0; i < _land.segs.size(); i++)
	{
		LandSeg *s = &_land.segs[i];
		s->dirty = 2;

		for (size_t j = 0; j < LAND_SEG_TILE_COUNT; j++) s->tile[j].dirty = 2;
	}
	_land.dirty = 1;
}

static void MarkNearblyTilesDirty(TileIndex tile)
{
	for (DiagDirection d = DIAGDIR_BEGIN; d < DIAGDIR_END; d++)
	{
		MarkTileDirty(tile + TileOffsByDiagDir(d), 1);
	}
}

void MarkTileDirty3D(TileIndex tile)
{
	if ((MapSizeX() != _land.size_x) || (MapSizeY() != _land.size_y) || _reset)
	{
		_reset = 1;
		return;
	}
	MarkTileDirty(tile, 2);
}

uint TileEdgeHeight(TileIndex ti, DiagDirection dd)
{
	// N, W, E, W, S, E
	// N, W, S, N, S, E
	
	// NW = 0 1
	// WS = layout ? 1 2 : 3 4
	// SE = 4 5
	// EN = layout ? 5 3 : 2 0

	LandTile *t = _land.tiles[ti];
	uint *h = t->height;
	switch (dd)
	{
	case DIAGDIR_NE: return t->layout ? (h[5] | (h[3] << 16)) : (h[2] | (h[0] << 16));
	case DIAGDIR_SE: return h[4] | (h[5] << 16);
	case DIAGDIR_SW: return t->layout ? (h[1] | (h[2] << 16)) : (h[3] | (h[4] << 16));
	case DIAGDIR_NW: return h[0] | (h[1] << 16);
	}
	return 0;
}

bool IsEdgeNeedsFoundation(TileIndex ti, DiagDirection d)
{
	DiagDirection r = ReverseDiagDir(d);
	TileIndexDiff tn = TileOffsByDiagDir(d);

	uint32 h1 = TileEdgeHeight(ti, d);
	uint32 h2 = TileEdgeHeight(Clamp(ti + tn, 0, MapSize()), r);
	if (((h1 & 0xFFFF) > (h2 >> 16)) || ((h1 >> 16) > (h2 & 0xFFFF))) return true;
	return false;
}

static void UpdateLand()
{
	if ((MapSizeX() != _land.size_x) || (MapSizeY() != _land.size_y) || _reset)
	{
		ClearVehCache();

		/* reload all of the resources */
		FreeInstancePool();
		FreeSpriteLinks();
		FreeImages();
		FreeModels();
		
		/* transparent image for the gaps */
		_transparent_image = LoadImageFile("base/transp.png");

		/* allocate data */
		_land.size_x = MapSizeX();
		_land.size_y = MapSizeY();
		_land.segs.resize(LAND_SEGS_X * LAND_SEGS_Y);
		_land.tiles.resize(_land.size_x * _land.size_y);
		_land.vertex.resize(_land.size_x * _land.size_y * 6);
		
		/* mark everything dirty */
		for (size_t i = 0; i < _land.segs.size(); i++)
		{
			LandSeg *s = &_land.segs[i];
			s->index = (uint32)(i);
			s->idx_min = (uint32)((i + 0) * LAND_SEG_TILE_COUNT);
			s->idx_max = (uint32)((i + 1) * LAND_SEG_TILE_COUNT);
			s->effects.clear();
			s->labels.clear();
			s->signs.clear();
			s->bbmin[0] = ((i % LAND_SEGS_X) + 0) * LAND_SEG_RES * TILE_SIZE;
			s->bbmin[1] = ((i / LAND_SEGS_X) + 0) * LAND_SEG_RES * TILE_SIZE;
			s->bbmax[0] = ((i % LAND_SEGS_X) + 1) * LAND_SEG_RES * TILE_SIZE;
			s->bbmax[1] = ((i / LAND_SEGS_X) + 1) * LAND_SEG_RES * TILE_SIZE;
			for (size_t j = 0; j < LAND_SEG_TILE_COUNT; j++)
			{
				LandTile *t = &s->tile[j];

				uint x = (uint)((i % LAND_SEGS_X) * LAND_SEG_RES + (j % LAND_SEG_RES));
				uint y = (uint)((i / LAND_SEGS_X) * LAND_SEG_RES + (j / LAND_SEG_RES));

				TileIndex ti = TileXY(x, y);
				_land.tiles[ti] = t;

				t->dirty = 2;
			}
			s->dirty = 2;
		}
		_land.buffer_dirty = 1;
		_land.dirty = 1;

		{
			_signs.clear();

			const Sign *si;
			FOR_ALL_SIGNS(si) AddSign3D(si->index);

			// relocate existing text effects (for game loading)
			for (size_t i = 0; i < _text_effects.size(); i++)
			{
				LandSign *s = &_text_effects[i];
				int sx = Clamp(s->pos[0] / TILE_SIZE / LAND_SEG_RES, 0, LAND_SEGS_X - 1);
				int sy = Clamp(s->pos[1] / TILE_SIZE / LAND_SEG_RES, 0, LAND_SEGS_Y - 1);
				_land.segs[sy * LAND_SEGS_X + sx].effects.push_back((TextEffectID)(i));
			}
		}
		_reset = 0;
	}

	if (!_land.vertex_buffer) glGenBuffers(1, &_land.vertex_buffer);
	
	if (!_land.dirty) return;
	_land.dirty = 0;
	
	_cur_dpi = &_screen; // some drawing functions wants to know a current zoom
	glBindBuffer(GL_ARRAY_BUFFER, _land.vertex_buffer);
	for (size_t i = 0; i < _land.segs.size(); i++)
	{
		LandSeg *s = &_land.segs[i];
		if (s->dirty < 2) continue;
		s->dirty = 1;

		s->sel_min = UINT_MAX;
		s->sel_max = 0;

		uint32 upd_min = UINT_MAX;
		uint32 upd_max = 0;
		for (size_t j = 0; j < LAND_SEG_TILE_COUNT; j++)
		{
			uint32 vi = (uint32)(i * LAND_SEG_TILE_COUNT + j);

			LandTile *t = &s->tile[j];
			if (t->dirty < 2)
			{
				if (t->sel.link != INVALID_LINK)
				{
					s->sel_min = min(s->sel_min, vi + 0);
					s->sel_max = max(s->sel_max, vi + 1);
				}
				continue;
			}
			t->dirty = 1;

			upd_min = min(upd_min, vi + 0);
			upd_max = max(upd_max, vi + 1);

			/* tile to update */
			uint x1 = (uint)((i % LAND_SEGS_X) * LAND_SEG_RES + (j % LAND_SEG_RES));
			uint y1 = (uint)((i / LAND_SEGS_X) * LAND_SEG_RES + (j / LAND_SEG_RES));
			uint x2 = min(x1 + 1, MapMaxX());
			uint y2 = min(y1 + 1, MapMaxY());

			uint32 ti = TileXY(x1, y1);
			MarkNearblyTilesDirty(ti);
			
			uint hn = TileHeight(ti);
			uint hw = TileHeight(TileXY(x2, y1));
			uint he = TileHeight(TileXY(x1, y2));
			uint hs = TileHeight(TileXY(x2, y2));
			
			uint hminnw = min(hn, hw);
			uint hmines = min(he, hs);
			uint hmin = min(hminnw, hmines);
			
			uint hmaxnw = max(hn, hw);
			uint hmaxes = max(he, hs);
			uint hmax = max(hmaxnw, hmaxes);
			
			t->draw[1].clear();
			t->sel.sprite = 0;
			t->sel.palette = 0;
			t->sel.link = INVALID_LINK;
			t->sel.pal = 0;

			t->zmin = hmin * TILE_HEIGHT_ACT;
			t->zmax = hmax * TILE_HEIGHT_ACT;

			t->slope = SLOPE_FLAT;
			if (hn != hmin) t->slope |= SLOPE_N;
			if (hw != hmin) t->slope |= SLOPE_W;
			if (he != hmin) t->slope |= SLOPE_E;
			if (hs != hmin) t->slope |= SLOPE_S;
			if ((hmax - hmin) == 2) t->slope |= SLOPE_STEEP;

			TileInfo info;
			_cur_ti = &info;
			info.x = x1 * TILE_SIZE;
			info.y = y1 * TILE_SIZE;
			info.tileh = t->slope;
			info.tile = ti;
			info.z = hmin * TILE_HEIGHT;

			t->fslope = t->slope;
			{
				t->f = _tile_type_procs[GetTileType(ti)]->get_foundation_proc(ti, t->fslope);
				hmin += ApplyFoundationToSlope(t->f, &t->fslope);

				hn = hmin;
				hw = hmin;
				he = hmin;
				hs = hmin;
			}

			Slope eslope = (t->fslope & SLOPE_ELEVATED);
			{
				if (eslope & SLOPE_W) hw++;
				if (eslope & SLOPE_S) hs++;
				if (eslope & SLOPE_E) he++;
				if (eslope & SLOPE_N) hn++;
				if (t->fslope & SLOPE_STEEP)
				{					
					if (eslope == SLOPE_NWS) hw++;
					if (eslope == SLOPE_WSE) hs++;
					if (eslope == SLOPE_SEN) he++;
					if (eslope == SLOPE_ENW) hn++;
				}

				hminnw = min(hn, hw);
				hmines = min(he, hs);
				hmin = min(hminnw, hmines);

				hmaxnw = max(hn, hw);
				hmaxes = max(he, hs);
				hmax = max(hmaxnw, hmaxes);

				t->zbase = hmin;
				t->zmax = hmax * TILE_HEIGHT_ACT;
			}

			TileType tile_type = GetTileType(ti);
			if (tile_type != MP_VOID)
			{
				_current_tile = t;
				_redirect_draw = 1;
				_tile_type_procs[tile_type]->draw_tile_proc(&info);
				DrawTileSelection(&info);
				_redirect_draw = 0;
			}

			/* tile vertex layout */
			int layout[6][3] =
			{
				{ 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, // N, W, E
				{ 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 }, // W, S, E
			};
			t->layout = 0;
			Slope hslope = (t->fslope & SLOPE_HALFTILE_MASK);
			if (hslope)
			{
				if ((hslope == SLOPE_HALFTILE_W) || (hslope == SLOPE_HALFTILE_E) || (hslope == (SLOPE_HALFTILE_E | SLOPE_HALFTILE_W))) t->layout = 1;
			}
			else
			{
				if ((eslope == SLOPE_E) || (eslope == SLOPE_W) || (eslope == SLOPE_EW) || (eslope == SLOPE_NWS) || (eslope == SLOPE_SEN)) t->layout = 1;
			}
			if (t->layout)
			{
				layout[2][0] = 1; layout[2][1] = 1; // N, W, S
				layout[3][0] = 0; layout[3][1] = 0; // N, S, E

				if (hslope == SLOPE_HALFTILE_W) { if (eslope & SLOPE_W) { layout[0][2]++; layout[2][2]++; } else layout[1][2]++; }
				if (hslope == SLOPE_HALFTILE_E) { if (eslope & SLOPE_E) { layout[3][2]++; layout[4][2]++; } else layout[5][2]++; }
			}
			else
			{
				if (hslope == SLOPE_HALFTILE_N) { if (eslope & SLOPE_N) { layout[1][2]++; layout[2][2]++; } else layout[0][2]++; }
				if (hslope == SLOPE_HALFTILE_S) { if (eslope & SLOPE_S) { layout[3][2]++; layout[5][2]++; } else layout[1][2]++; }
			}

			{
				uint hgt_sel[2][2] ={ { hn, he }, { hw, hs } };
				for (int n = 0; n < 6; n++)
				{
					int dx = layout[n][0];
					int dy = layout[n][1];
					int dz = layout[n][2];
					t->height[n] = (hgt_sel[dx][dy] + dz);
				}
			}

			TileVertex *vertex = &_land.vertex[vi * 6];
			if (tile_type == MP_VOID)
			{
				/* collapse this tile */
				for (int n = 0; n < 6; n++)
				{
					for (int k = 0; k < 2; k++)
					{
						vertex[n].pos[k].x = 0.0f;
						vertex[n].pos[k].y = 0.0f;
						vertex[n].pos[k].z = 0.0f;
					}
				}
				t->dirty = 0;
				continue;
			}

			DrawLink *draw_land[2] = { nullptr, nullptr };
			for (size_t n = 0; n < t->draw[1].size(); n++)
			{
				DrawLink *draw = &t->draw[1][n];
				if (draw->link == INVALID_LINK) continue;

				const SpriteLink *link = GetSpriteLink(draw->link);
				t->zmax = max(t->zmax, (t->zbase + draw->offset[2] / TILE_HEIGHT_F) * TILE_HEIGHT_ACT + link->bbmax[2]);
				if (link->land.image != INVALID_IMAGE)
				{
					if (!draw_land[0]) draw_land[0] = draw; else
					if (!draw_land[1]) draw_land[1] = draw;
				}
			}
			if (!draw_land[1]) draw_land[1] = draw_land[0];
			if (!hslope) draw_land[0] = draw_land[1];

			float offset[6] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
			{
				bool has_shore = false;
				bool has_water = HasTileWaterGround(ti);
				switch (tile_type)
				{
				case MP_WATER:
					switch (GetWaterTileType(ti))
					{
					case WATER_TILE_CLEAR: has_water = true; break;
					case WATER_TILE_COAST: has_water = true; has_shore = true; break;
					}
					break;

				case MP_RAILWAY: /* Shore or flooded halftile */
					if ((GetRailGroundType(ti) != RAIL_GROUND_WATER)) break;
					has_water = true;
					has_shore = true;
					break;

				case MP_TREES: /* trees on shore */
					if (GetTreeGround(ti) != TREE_GROUND_SHORE) break;
					has_water = true;
					has_shore = true;
					break;
				}
				if (has_water)
				{
					if (has_shore)
					{
						if (hslope || IsSlopeWithOneCornerRaised(eslope))
						{
							uint32 face = 0;
							if ((eslope == SLOPE_N) || (eslope == SLOPE_W)) face = 1;
							for (int i = 0; i < 3; i++) offset[i + face * 3] = -TILE_WATER_OFFSET;
						}
					}
					else
					{
						for (int i = 0; i < 6; i++) offset[i] = -TILE_WATER_OFFSET;
					}
				}
			}

			/* fill the tile vertex position */
			for (int n = 0; n < 6; n++)
			{
				int dx = layout[n][0];
				int dy = layout[n][1];

				vertex[n].pos[0].x = (x1 + dx) * TILE_SIZE;
				vertex[n].pos[0].y = (y1 + dy) * TILE_SIZE;
				vertex[n].pos[0].z = t->height[n] * TILE_HEIGHT_ACT - offset[n];

				vertex[n].pos[1].x = (x1 + dx) * TILE_SIZE;
				vertex[n].pos[1].y = (y1 + dy) * TILE_SIZE;
				vertex[n].pos[1].z = t->height[n] * TILE_HEIGHT_ACT;
			}
			
			if (draw_land[0])
			{
				/* fill the tile vertex texture coords */
				const SpriteLink *link[2] =
				{
					GetSpriteLink(draw_land[0]->link),
					GetSpriteLink(draw_land[1]->link),
				};
				const LandTexLink *tl[2] =
				{
					&link[0]->land,
					&link[1]->land,
				};
				const ImageEntry *img[2] =
				{
					GetImageEntry(tl[0]->image),
					GetImageEntry(tl[1]->image),
				};

				int tex_sel[2][2] = { { 0, 3 }, { 1, 2 } };
				for (int f = 0; f < 2; f++)
				{
					uint32 face = f;
					if ((eslope == SLOPE_N) || (eslope == SLOPE_W)) face = 1 - f;
					for (int n = 0; n < 3; n++)
					{
						int v = f * 3 + n;
						int dx = layout[v][0];
						int dy = layout[v][1];
						
						vertex[v].tex[0].x = tl[face]->coord[tex_sel[dx][dy]][0] * IMAGE3D_ATLAS_SIZE; // in texels
						vertex[v].tex[0].y = tl[face]->coord[tex_sel[dx][dy]][1] * IMAGE3D_ATLAS_SIZE; // in texels
						vertex[v].tex[0].layer = (float)(img[face]->layer); // atlas index
						vertex[v].tex[0].pal = (float)(draw_land[face]->pal) + 0.5f; // palette index

						vertex[v].loc[0].px = img[face]->offs_x / IMAGE3D_ATLAS_SIZE_F;
						vertex[v].loc[0].py = img[face]->offs_y / IMAGE3D_ATLAS_SIZE_F;
						vertex[v].loc[0].sx = img[face]->size_x / IMAGE3D_ATLAS_SIZE_F;
						vertex[v].loc[0].sy = img[face]->size_y / IMAGE3D_ATLAS_SIZE_F;

						vertex[v].mip[0].mip_x = IMAGE3D_ATLAS_SIZE / (float)(img[face]->size_x);
						vertex[v].mip[0].mip_y = IMAGE3D_ATLAS_SIZE / (float)(img[face]->size_y);
					}
				}
			}
			else
			{
				const ImageEntry *img = GetImageEntry(_transparent_image);
				for (int n = 0; n < 6; n++)
				{
					vertex[n].tex[0].x = 0.5f * IMAGE3D_ATLAS_SIZE;
					vertex[n].tex[0].y = 0.5f * IMAGE3D_ATLAS_SIZE;
					vertex[n].tex[0].layer = img->layer;
					vertex[n].tex[0].pal = 0.0f;

					vertex[n].loc[0].px = img->offs_x / IMAGE3D_ATLAS_SIZE_F;
					vertex[n].loc[0].py = img->offs_y / IMAGE3D_ATLAS_SIZE_F;
					vertex[n].loc[0].sx = img->size_x / IMAGE3D_ATLAS_SIZE_F;
					vertex[n].loc[0].sy = img->size_y / IMAGE3D_ATLAS_SIZE_F;

					vertex[n].mip[0].mip_x = IMAGE3D_ATLAS_SIZE / (float)(img->size_x);
					vertex[n].mip[0].mip_y = IMAGE3D_ATLAS_SIZE / (float)(img->size_y);
				}
			}

			bool sel = false;
			if (t->sel.link != INVALID_LINK)
			{
				const SpriteLink *link = GetSpriteLink(t->sel.link);
				if (!link->objects.size())
				{
					sel = true;
					s->sel_min = min(s->sel_min, vi + 0);
					s->sel_max = max(s->sel_max, vi + 1);

					/* fill the tile vertex texture coords */
					const LandTexLink *tl = &link->land;
					const ImageEntry *img = GetImageEntry(tl->image);

					int tex_sel[2][2] ={ { 0, 3 }, { 1, 2 } };
					for (int n = 0; n < 6; n++)
					{
						int dx = layout[n][0];
						int dy = layout[n][1];

						vertex[n].tex[1].x = tl->coord[tex_sel[dx][dy]][0] * IMAGE3D_ATLAS_SIZE; // in texels
						vertex[n].tex[1].y = tl->coord[tex_sel[dx][dy]][1] * IMAGE3D_ATLAS_SIZE; // in texels
						vertex[n].tex[1].layer = (float)(img->layer); // atlas index
						vertex[n].tex[1].pal = (float)(t->sel.pal) + 0.5f; // palette index

						vertex[n].loc[1].px = img->offs_x / IMAGE3D_ATLAS_SIZE_F;
						vertex[n].loc[1].py = img->offs_y / IMAGE3D_ATLAS_SIZE_F;
						vertex[n].loc[1].sx = img->size_x / IMAGE3D_ATLAS_SIZE_F;
						vertex[n].loc[1].sy = img->size_y / IMAGE3D_ATLAS_SIZE_F;

						vertex[n].mip[1].mip_x = IMAGE3D_ATLAS_SIZE / (float)(img->size_x);
						vertex[n].mip[1].mip_y = IMAGE3D_ATLAS_SIZE / (float)(img->size_y);
					}
				}
				else
				{
					t->draw[1].push_back(t->sel);
				}
			}
			if (!sel)
			{
				const ImageEntry *img = GetImageEntry(_transparent_image);
				for (int n = 0; n < 6; n++)
				{
					vertex[n].tex[1].x = 0.5f * IMAGE3D_ATLAS_SIZE;
					vertex[n].tex[1].y = 0.5f * IMAGE3D_ATLAS_SIZE;
					vertex[n].tex[1].layer = img->layer;
					vertex[n].tex[1].pal = 0.0f;

					vertex[n].loc[1].px = img->offs_x / IMAGE3D_ATLAS_SIZE_F;
					vertex[n].loc[1].py = img->offs_y / IMAGE3D_ATLAS_SIZE_F;
					vertex[n].loc[1].sx = img->size_x / IMAGE3D_ATLAS_SIZE_F;
					vertex[n].loc[1].sy = img->size_y / IMAGE3D_ATLAS_SIZE_F;

					vertex[n].mip[1].mip_x = IMAGE3D_ATLAS_SIZE / (float)(img->size_x);
					vertex[n].mip[1].mip_y = IMAGE3D_ATLAS_SIZE / (float)(img->size_y);
				}
			}

			/* compute the tile vertex normals */
			float nrm[2][3];
			vectTriNormal(nrm[0], FP(vertex[0].pos[0]), FP(vertex[1].pos[0]), FP(vertex[2].pos[0]));
			vectTriNormal(nrm[1], FP(vertex[3].pos[0]), FP(vertex[4].pos[0]), FP(vertex[5].pos[0]));
			for (int n = 0; n < 3; n++) vectCopy3(FP(vertex[n].nrm), nrm[0]);
			for (int n = 3; n < 6; n++) vectCopy3(FP(vertex[n].nrm), nrm[1]);
		}

		/* recompute segment AABB */
		s->bbmin[2] = +FLT_MAX;
		s->bbmax[2] = -FLT_MAX;
		for (size_t j = 0; j < LAND_SEG_TILE_COUNT; j++)
		{
			LandTile *t = &s->tile[j];
			s->bbmin[2] = min(s->bbmin[2], t->zmin);
			s->bbmax[2] = max(s->bbmax[2], t->zmax);
		}

		if (_land.buffer_dirty) continue;

		/* update the vertex data for this segment */
		glBufferSubData(GL_ARRAY_BUFFER, upd_min * sizeof(TileVertex) * 6, (upd_max - upd_min) * sizeof(TileVertex) * 6, _land.vertex.data() + upd_min * 6);
	}
	_cur_dpi = NULL;

	for (size_t i = 0; i < _land.segs.size(); i++)
	{
		LandSeg *s = &_land.segs[i];
		if (!s->dirty) continue;
		s->dirty = 0;

		/* Update labels */
		s->labels.clear();
		for (size_t j = 0; j < LAND_SEG_TILE_COUNT; j++)
		{
			uint tx = (uint)((i % LAND_SEGS_X) * LAND_SEG_RES + (j % LAND_SEG_RES));
			uint ty = (uint)((i / LAND_SEGS_X) * LAND_SEG_RES + (j / LAND_SEG_RES));
			TileIndex ti = TileXY(tx, ty);
			
			if (IsTileType(ti, MP_STATION))
			{
				BaseStation *st = BaseStation::GetByTile(ti);
				if (st->xy == ti)
				{
					if (Station::IsExpected(st))
					{
						/* Station */
						AddLabel(s, VPST_STATION, st->index,
								 tx * TILE_SIZE, ty * TILE_SIZE, &st->sign,
								 STR_VIEWPORT_STATION, STR_VIEWPORT_STATION + 1, STR_NULL, st->index, st->facilities,
								 (st->owner == OWNER_NONE || !st->IsInUse()) ? COLOUR_GREY : _company_colours[st->owner]);
					}
					else
					{
						/* Waypoint */
						AddLabel(s, VPST_WAYPOINT, st->index,
								 tx * TILE_SIZE, ty * TILE_SIZE, &st->sign,
								 STR_VIEWPORT_WAYPOINT, STR_VIEWPORT_WAYPOINT + 1, STR_NULL,
								 st->index, st->facilities, (st->owner == OWNER_NONE || !st->IsInUse()) ? COLOUR_GREY : _company_colours[st->owner]);
					}
				}
			}
			{
				/* Town */
				Town *tn = ClosestTownFromTile(ti, 1);
				if (tn && (tn->xy == ti))
				{
					AddLabel(s, VPST_TOWN, tn->index,
							 tx * TILE_SIZE, ty * TILE_SIZE, &tn->cache.sign,
							 _settings_client.gui.population_in_label ? STR_VIEWPORT_TOWN_POP : STR_VIEWPORT_TOWN,
							 STR_VIEWPORT_TOWN_TINY_WHITE, STR_VIEWPORT_TOWN_TINY_BLACK,
							 tn->index, tn->cache.population);
				}
			}
		}

		/* Update the foundations */
		for (size_t j = 0; j < LAND_SEG_TILE_COUNT; j++)
		{
			LandTile *t = &s->tile[j];
			if (!t->dirty) continue;
			t->dirty = 0;

			uint tx = (uint)((i % LAND_SEGS_X) * LAND_SEG_RES + (j % LAND_SEG_RES));
			uint ty = (uint)((i / LAND_SEGS_X) * LAND_SEG_RES + (j / LAND_SEG_RES));
			TileIndex ti = TileXY(tx, ty);

			{
				t->draw[0].clear();
				if (!IsFoundation(t->f)) continue;

				uint32 fsides = 0; // 4 bits for each diag dir side
				for (DiagDirection d = DIAGDIR_BEGIN; d < DIAGDIR_END; d++)
				{
					if (!IsEdgeNeedsFoundation(ti, d)) continue;
					fsides |= (1 << d);
				}

				_current_tile = t;
				if (t->zbase <= 1)
				{
					// TODO: check for shores
					uint32 fsides_shore = fsides;
					DrawTileSprite3D(SPR_FOUNDATION_BASE + fsides_shore, 0, 0, 0, 0, true);
				}
				if (IsLeveledFoundation(t->f))
				{
					DrawTileSprite3D(SPR_FOUNDATION_BASE + fsides, 0, 0, 0, -1, true);
				}
				else if (IsInclinedFoundation(t->f))
				{
					uint32 side = 0;
					switch (t->fslope)
					{
					case SLOPE_NE: side = 0; break;
					case SLOPE_NW: side = 1; break;
					case SLOPE_SW: side = 2; break;
					case SLOPE_SE: side = 3; break;
					}

					if (IsSteepSlope(t->slope)) DrawTileSprite3D(SPR_FOUNDATION_BASE + fsides, 0, 0, 0, -1, true);
					fsides = ((fsides << side) & 0x0F) | (((fsides << side) & 0xF0) >> 4);

					SpriteID inc_base = SPR_SLOPES_BASE + fsides;
					DrawTileSprite3D(inc_base + 16 * side, 0, 0, 0, -1, true);
				}
				else if (IsSpecialRailFoundation(t->f))
				{
					uint32 side = 0;
					switch (t->slope & SLOPE_ELEVATED)
					{
					case SLOPE_NE: side = 3; break;
					case SLOPE_NW: side = 2; break;
					case SLOPE_SW: side = 1; break;
					case SLOPE_SE: side = 0; break;
					}

					Corner corner = GetRailFoundationCorner(t->f);
					if ((corner == CORNER_N) || (corner == CORNER_S)) side = (side + 2) % 4;
					fsides = ((fsides << side) & 0x0F) | (((fsides << side) & 0xF0) >> 4);

					SpriteID inc_base = SPR_SLOPES_BASE + fsides;
					DrawTileSprite3D(inc_base + 16 * side, 0, 0, 0, (t->zbase + (IsSteepSlope(t->slope) ? 1 : 0)) * TILE_HEIGHT, true);
				}
				else // diagonal foundation
				{
#define SideOffset(a, b) (((fsides & (1 << a)) ? 2 : 0) | ((fsides & (1 << b)) ? 1 : 0))

					Corner corner;
					if ((t->slope == SLOPE_EW) || (t->slope == SLOPE_NS))
					{
						corner = GetHalftileFoundationCorner(t->f);
					}
					else
					{
						corner = GetHighestSlopeCorner(t->slope);
					}
					if ((t->f == FOUNDATION_STEEP_BOTH) || (t->f == FOUNDATION_STEEP_LOWER))
					{
						int z_lo = t->zbase * TILE_HEIGHT;

						uint32 side_offset;
						Corner corner_low = (Corner)((corner + 2) % 4);
						switch (corner_low)
						{
						case CORNER_W: side_offset = SideOffset(DIAGDIR_NW, DIAGDIR_SW); break;
						case CORNER_S: side_offset = SideOffset(DIAGDIR_SW, DIAGDIR_SE); break;
						case CORNER_E: side_offset = SideOffset(DIAGDIR_SE, DIAGDIR_NE); break;
						case CORNER_N: side_offset = SideOffset(DIAGDIR_NE, DIAGDIR_NW); break;
						}

						// low part
						SpriteID diag_base = SPR_SLOPES_BASE + 16 * 4 + corner_low * 4;
						DrawTileSprite3D(diag_base + side_offset, 0, 0, 0, z_lo, true);
					}
					if (t->f != FOUNDATION_STEEP_LOWER)
					{
						int z_hi = (t->zbase + 1) * TILE_HEIGHT;
						if (IsSteepSlope(t->slope) && (t->f != FOUNDATION_STEEP_BOTH)) z_hi = (t->zbase + 2) * TILE_HEIGHT;

						// hi part
						uint32 side_offset;
						switch (corner)
						{
						case CORNER_W: side_offset = SideOffset(DIAGDIR_NW, DIAGDIR_SW); break;
						case CORNER_S: side_offset = SideOffset(DIAGDIR_SW, DIAGDIR_SE); break;
						case CORNER_E: side_offset = SideOffset(DIAGDIR_SE, DIAGDIR_NE); break;
						case CORNER_N: side_offset = SideOffset(DIAGDIR_NE, DIAGDIR_NW); break;
						}

						SpriteID diag_base = SPR_SLOPES_BASE + 16 * 4 + corner * 4;
						DrawTileSprite3D(diag_base + side_offset, 0, 0, 0, z_hi, true);
						if (t->zbase <= 1)
						{
							// TODO: check for shores
							DrawTileSprite3D(diag_base, 0, 0, 0, 0, true);
						}
					}
#undef SideOffset
				}
			}
		}

		/* Update the segment drawing cache */
		s->draw_cache.clear();
		for (size_t j = 0; j < LAND_SEG_TILE_COUNT; j++)
		{
			LandTile *t = &s->tile[j];
			for (size_t n = 0; n < 2; n++)
			{
				for (DrawLink &draw : t->draw[n])
				{
					if (draw.link == INVALID_LINK) continue;
					
					const SpriteLink *sl = GetSpriteLink(draw.link);
					uint32 mask = (draw.sprite & (1 << TRANSPARENT_BIT)) ? MODEL_CACHE_MASK_TRANSP : MODEL_CACHE_MASK_SOLID;
					uint32 zoom = (draw.sprite & SPRITE_ZOOMED_FLAG) ? 0x0F : 0xFF;
					for (const ObjectLink &ol : sl->objects)
					{
						const ImageEntry *img = GetImageEntry(ol.image);

						s->draw_cache.emplace_back();
						ModelInstanceCache &cache = s->draw_cache.back();
						ModelInstance *inst = &cache.data;

						AllocInstancePool(ol.model, ol.shade);
						cache.model = ol.model;
						cache.mask = mask;
						cache.zoom = zoom;

						inst->loc.px = (float)(img->offs_x) / IMAGE3D_ATLAS_SIZE_F;
						inst->loc.py = (float)(img->offs_y) / IMAGE3D_ATLAS_SIZE_F;
						inst->loc.sx = (float)(img->size_x) / IMAGE3D_ATLAS_SIZE_F;
						inst->loc.sy = (float)(img->size_y) / IMAGE3D_ATLAS_SIZE_F;

						inst->mip.mip_x = IMAGE3D_ATLAS_SIZE_F / (float)(img->size_x);
						inst->mip.mip_y = IMAGE3D_ATLAS_SIZE_F / (float)(img->size_y);
						inst->mip.layer = (float)(img->layer);
						inst->mip.pal   = (float)(draw.pal) + 0.5f;

						inst->sun = 0;

						float matr[16];
						if (ol.slope)
						{
							if (IsInclinedSlope(t->fslope))
							{
								DiagDirection dir = GetInclinedSlopeDirection(t->fslope);
								matrMul(matr, _auto_slope[dir], ol.matr);
								inst->sun = dir + 1;
							}
							else if (IsSteepSlope(t->fslope))
							{
								Corner corner = GetHighestSlopeCorner(t->fslope);
								matrMul(matr, _auto_slope[corner + 4], ol.matr);
								inst->sun = corner + 5;
							}
							else
							{
								matrCopy(matr, ol.matr);
								if (IsSlopeWithThreeCornersRaised(t->fslope) || IsHalftileSlope(t->fslope)) matrPreTranslate(matr, 0, 0, TILE_HEIGHT_ACT);
							}
						}
						else
						{
							matrCopy(matr, ol.matr);
						}
						uint tx = (uint)((s->index % LAND_SEGS_X) * LAND_SEG_RES + (j % LAND_SEG_RES));
						uint ty = (uint)((s->index / LAND_SEGS_X) * LAND_SEG_RES + (j / LAND_SEG_RES));
						matrPreTranslate(matr, tx * TILE_SIZE + draw.offset[0], ty * TILE_SIZE + draw.offset[1], (t->zbase + draw.offset[2] / TILE_HEIGHT_F) * TILE_HEIGHT_ACT);

						vectCopy4(FP(inst->matr[0]), &matr[0]);
						vectCopy4(FP(inst->matr[1]), &matr[4]);
						vectCopy4(FP(inst->matr[2]), &matr[8]);
					}
				}
			}
		}
	}

	if (!_land.buffer_dirty) return;
	_land.buffer_dirty = 0;

	/* reallocate the whole vertex buffer */
	glBufferData(GL_ARRAY_BUFFER, _land.vertex.size() * sizeof(TileVertex), _land.vertex.data(), GL_DYNAMIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

static DiagDirection TrackdirToDiagdir(Trackdir td)
{
	return (DiagDirection)((td & 0x01) | ((td >> 2) & 0x02));
}

static bool TrainCorrectTurn(float *pos, float &angle, TileIndex tile, Trackdir tdp, Trackdir tdc, Trackdir tdn)
{	
	static const float ts = TILE_SIZE_F;
	static const float dr = ts / 4.0f * sqrtf(2.0f);
	static const float tr = tanf(RAD(67.5f)) * dr;

	static const float coord[16][5] =
	{
		{ ts,     ts / 2,      0, ts / 2,  45.0f }, // TRACKDIR_X_NE
		{ ts / 2,      0, ts / 2, ts    , 135.0f }, // TRACKDIR_Y_SE
		{ ts / 2,      0,      0, ts / 2,  90.0f }, // TRACKDIR_UPPER_E
		{ ts,     ts / 2, ts / 2, ts    ,  90.0f }, // TRACKDIR_LOWER_E
		{ ts / 2,      0, ts,     ts / 2, 180.0f }, // TRACKDIR_LEFT_S
		{      0, ts / 2, ts / 2, ts    , 180.0f }, // TRACKDIR_RIGHT_S
		{      0,      0,      0,      0,   0.0f }, // TRACKDIR_RVREV_NE
		{      0,      0,      0,      0,   0.0f }, // TRACKDIR_RVREV_SE
		{      0, ts / 2, ts,     ts / 2, 225.0f }, // TRACKDIR_X_SW
		{ ts / 2, ts,     ts / 2,      0, 315.0f }, // TRACKDIR_Y_NW
		{      0, ts / 2, ts / 2,      0, 270.0f }, // TRACKDIR_UPPER_W
		{ ts / 2, ts,     ts,     ts / 2, 270.0f }, // TRACKDIR_LOWER_W
		{ ts,     ts / 2, ts / 2,      0,   0.0f }, // TRACKDIR_LEFT_N
		{ ts / 2, ts,          0, ts / 2,   0.0f }, // TRACKDIR_RIGHT_N
		{      0,      0,      0,      0,   0.0f }, // TRACKDIR_RVREV_SW
		{      0,      0,      0,      0,   0.0f }, // TRACKDIR_RVREV_NW
	};
	
	float post[2] = { TileX(tile) * ts, TileY(tile) * ts };
	float posf[2] = { pos[0] - post[0], pos[1] - post[1] };

	const float *cp = coord[tdc];
	float fx = (cp[2] - cp[0]);
	float fy = (cp[3] - cp[1]);
	float dx = (posf[0] - cp[0]);
	float dy = (posf[1] - cp[1]);
	float sqr = (fx * fx + fy * fy);
	float t = (fx * dx + fy * dy) / sqr;

	if(IsDiagonalTrackdir(tdc)) // diagonal dirs
	{
		if ((NextTrackdir(tdp) != tdc) && ((t * TILE_SIZE) < dr)) // in
		{
			int sign = (TrackdirToTrackdirBits(tdp) & (TRACKDIR_BIT_LOWER_W | TRACKDIR_BIT_LEFT_N | TRACKDIR_BIT_UPPER_E | TRACKDIR_BIT_RIGHT_S)) ? +1 : -1;

			float len = 1.0f / sqrtf(sqr);
			float zx = cp[0] + fx * dr * len;
			float zy = cp[1] + fy * dr * len;
			float cx = zx - fy * sign * tr * len;
			float cy = zy + fx * sign * tr * len;

			float ax = (posf[0] - cx);
			float ay = (posf[1] - cy);
			angle = GRAD(atan2f(ax, ay));
			if (fabsf(angDiff(angle, coord[tdc][4], -180.0f, +180.0f)) < 90.0f) angle += 180.0f;

			float len_r = 1.0f / sqrtf(ax * ax + ay * ay);
			pos[0] = post[0] + cx + ax * tr * len_r;
			pos[1] = post[1] + cy + ay * tr * len_r;

			return true;
		}
		if ((NextTrackdir(tdc) != tdn) && ((t * TILE_SIZE) > (TILE_SIZE - dr))) // out
		{
			int sign = (TrackdirToTrackdirBits(tdn) & (TRACKDIR_BIT_LOWER_W | TRACKDIR_BIT_LEFT_N | TRACKDIR_BIT_UPPER_E | TRACKDIR_BIT_RIGHT_S)) ? +1 : -1;

			float len = 1.0f / sqrtf(sqr);
			float zx = cp[0] + fx * (TILE_SIZE - dr) * len;
			float zy = cp[1] + fy * (TILE_SIZE - dr) * len;
			float cx = zx - fy * sign * tr * len;
			float cy = zy + fx * sign * tr * len;
			
			float ax = (posf[0] - cx);
			float ay = (posf[1] - cy);
			angle = GRAD(atan2f(ax, ay));
			if (fabsf(angDiff(angle, coord[tdc][4], -180.0f, +180.0f)) < 90.0f) angle += 180.0f;

			float len_r = 1.0f / sqrtf(ax * ax + ay * ay);
			pos[0] = post[0] + cx + ax * tr * len_r;
			pos[1] = post[1] + cy + ay * tr * len_r;

			return true;
		}
		return false;
	}

	if (((NextTrackdir(tdp) != tdc) && (t < 0.5f)) || ((NextTrackdir(tdc) != tdn) && (t > 0.5f))) // curve
	{
		int sign = (TrackdirToTrackdirBits(tdc) & (TRACKDIR_BIT_LOWER_W | TRACKDIR_BIT_LEFT_N | TRACKDIR_BIT_UPPER_E | TRACKDIR_BIT_RIGHT_S)) ? +1 : -1;

		float len = 1.0f / sqrtf(sqr);
		float zx = cp[0] + fx * 0.5f;
		float zy = cp[1] + fy * 0.5f;
		float cx = zx - fy * sign * tr * len;
		float cy = zy + fx * sign * tr * len;
		
		float ax = (posf[0] - cx);
		float ay = (posf[1] - cy);
		angle = GRAD(atan2f(ax, ay));
		if (fabsf(angDiff(angle, coord[tdc][4], -180.0f, +180.0f)) < 90.0f) angle += 180.0f;

		float len_r = 1.0f / sqrtf(ax * ax + ay * ay);
		pos[0] = post[0] + cx + ax * tr * len_r;
		pos[1] = post[1] + cy + ay * tr * len_r;

		return true;
	}
	{
		Track ct = TrackdirToTrack(tdc);
		if ((ct == TRACK_UPPER) || (ct == TRACK_LOWER)) pos[0] += 1.0f;
	}
	return false;
}

static bool TruckCorrectTurn(float frac, float frame, float *pos, float &angle, TileIndex tile, Trackdir track, Direction dir)
{
	static const float ts = TILE_SIZE_F;
	static const float tr[6] =
	{
		0.0f,
		ts *  6.0f / 16.0f,
		ts * 10.0f / 16.0f,
		ts *  4.0f / 16.0f,
	};
	
	static const int ref[16][7] =
	{
		{ 0, 0, 0,  0,    0,  0,  0 }, // TRACKDIR_X_NE
		{ 0, 0, 0,  0,    0,  0,  0 }, // TRACKDIR_Y_SE
		{ 0, 0, 2, 17,  -90, +1,  0 }, // TRACKDIR_UPPER_E
		{ 2, 2, 1, 11,  180,  0,  0 }, // TRACKDIR_LOWER_E
		{ 2, 0, 1, 10,  270,  0,  0 }, // TRACKDIR_LEFT_S
		{ 0, 2, 2, 17, -180,  0, -1 }, // TRACKDIR_RIGHT_S
		{ 1, 1, 3,  0,    1,  0,  0 }, // TRACKDIR_RVREV_NE
		{ 1, 1, 3,  0,    1,  0,  0 }, // TRACKDIR_RVREV_SE
		{ 0, 0, 0,  0,    0,  0,  0 }, // TRACKDIR_X_SW
		{ 0, 0, 0,  0,    0,  0,  0 }, // TRACKDIR_Y_NW
		{ 0, 0, 1, 10,  360,  0,  0 }, // TRACKDIR_UPPER_W
		{ 2, 2, 2, 18, -270, +1,  0 }, // TRACKDIR_LOWER_W
		{ 2, 0, 2, 18, -360,  0,  0 }, // TRACKDIR_LEFT_N
		{ 0, 2, 1, 11,   90,  0,  0 }, // TRACKDIR_RIGHT_N
		{ 1, 1, 3,  0,    1,  0,  0 }, // TRACKDIR_RVREV_SW
		{ 1, 1, 3,  0,    1,  0,  0 }, // TRACKDIR_RVREV_NW
	};

	float post[2] = { TileX(tile) * ts, TileY(tile) * ts };
	float posf[2] = { pos[0] - post[0], pos[1] - post[1] };

	if (!IsValidTrackdir(track)) return false;
	if (TrackdirToTrackdirBits(track) & (TRACKDIR_BIT_X_NE | TRACKDIR_BIT_Y_SE | TRACKDIR_BIT_X_SW | TRACKDIR_BIT_Y_NW))
	{
		TileIndexDiffC diff = TileIndexDiffCByDir(dir);
		pos[0] += frac * diff.x;
		pos[1] += frac * diff.y;
		switch (track)
		{
		case TRACKDIR_X_NE: pos[1] += 1.0f; pos[0] += 1.0f; break;
		case TRACKDIR_X_SW: pos[1] += 1.0f; break;
		case TRACKDIR_Y_NW: pos[0] += 1.0f; pos[1] += 1.0f; break;
		case TRACKDIR_Y_SE: pos[0] += 1.0f; break;
		}
		return false;
	}
	
	bool rev = (!_settings_game.vehicle.road_side);
	if (rev) track = ReverseTrackdir(track);

	float cx = ref[track][0] * ts / 2.0f;
	float cy = ref[track][1] * ts / 2.0f;

	float s = fabsf(ref[track][4]);
	float d = ((ref[track][4] > 0) ? 1.0f : -1.0f) * 90.0f;
	float p = frame / (float)(ref[track][3]);
	if (rev) p = (1.0f - p);

	float r = tr[ref[track][2]];
	pos[0] = post[0] + cx + sinf(RAD(s + p * d)) * r;
	pos[1] = post[1] + cy + cosf(RAD(s + p * d)) * r;
	angle = (s + p * d);
	if (rev) angle += 180.0f;
	return true;
}

static bool IsNormalVehicle(Vehicle *v)
{
	switch (v->type)
	{
	case VEH_TRAIN:
	case VEH_ROAD:
	case VEH_SHIP:
		break;

	case VEH_AIRCRAFT:
		if ((v->subtype == AIR_SHADOW) || (v->subtype == AIR_ROTOR)) return false;
		break;

	case VEH_EFFECT:
		return false;

	case VEH_DISASTER:
		switch (v->subtype)
		{
		case ST_ZEPPELINER_SHADOW:
		case ST_SMALL_UFO_SHADOW:
		case ST_AIRPLANE_SHADOW:
		case ST_HELICOPTER_SHADOW:
		case ST_HELICOPTER_ROTORS:
		case ST_BIG_UFO_SHADOW:
		case ST_BIG_UFO_DESTROYER_SHADOW:
			return false;
		}
		break;
	}
	return true;
}

/* update vehicle tracked data */
static void UpdateVehicle(VehicleData *c, Vehicle *v)
{
	Blitter_OpenGL *blitter = (Blitter_OpenGL*)(BlitterFactory::GetCurrentBlitter());

	/* update the vehicle sprites */
	{
		PaletteID pal = PAL_NONE;
		if (v->vehstatus & VS_DEFPAL) pal = (v->vehstatus & VS_CRASHED) ? PALETTE_CRASH : GetVehiclePalette(v);

		c->draw.clear();
		c->linked = false;
		for (uint i = 0; i < v->sprite_seq.count; i++)
		{
			PaletteID pal2 = v->sprite_seq.seq[i].pal;
			if (!pal2 || (v->vehstatus & VS_CRASHED)) pal2 = pal;

			SpriteID sprite = (v->sprite_seq.seq[i].sprite & (~SPRITE_ZOOMED_FLAG));

			SpriteID s = sprite & SPRITE_MASK;
			PaletteID p = pal2 & SPRITE_MASK;
			uint32 link = GetSpriteLink(GetOriginGRFID(s), GetOriginID(s), s, p);
			if ((link == INVALID_LINK) && (_info_tile < 0)) continue;
			
			c->draw.emplace_back();			
			DrawLink &dl = c->draw.back();
			dl.sprite = sprite;
			dl.palette = pal2;
			dl.link = GetSpriteLink(GetOriginGRFID(s), GetOriginID(s), s, p);
			dl.pal = blitter->CachePal(p);
			for (int i = 0; i < 3; i++) dl.offset[i] = 0;

			if (link == INVALID_LINK) continue;
			c->linked = true;
		}
	}

	float posf[3] =
	{
		(float)(v->x_pos),
		(float)(v->y_pos),
		(float)(v->z_pos) * TILE_HEIGHT_SCALE,
	};

	float frac = (float)(v->First()->progress) / 255.0f;
	TileIndexDiffC diff = TileIndexDiffCByDir(v->direction);
	c->slope = 0.0f;

	/* correct the vehicle position and rotation */
	if (!(v->vehstatus & VS_CRASHED))
	{
		bool reversed = false;
		bool update_z = false;
		bool corrected = false;
		if (v->type == VEH_TRAIN)
		{
			Train *train = (Train*)(v);
			if (train->IsRearDualheaded()) reversed = true;

			TileIndex ntile = v->tile;
			Trackdir ndir = INVALID_TRACKDIR;
			Trackdir cdir = TrackDirectionToTrackdir(TrackBitsToTrack(train->track), v->direction);
			if (!(train->track & TRACK_BIT_DEPOT))
			{
				posf[0] += frac * diff.x;
				posf[1] += frac * diff.y;
				if (!(train->track & TRACK_BIT_WORMHOLE) && (cdir != INVALID_TRACKDIR))
				{
					Vehicle *prev = v->Previous();
					while (prev)
					{
						if (prev->tile != ntile)
						{
							ndir = TrackDirectionToTrackdir(TrackBitsToTrack(((Train*)(prev))->track), prev->direction);
							ntile = prev->tile;
							break;
						}
						prev = prev->Previous();
					}
					if (ntile == v->tile) // in the same tile as head or head
					{
						Trackdir td = TrackDirectionToTrackdir(TrackBitsToTrack(((Train*)(v->First()))->track), v->direction);
						if (IsValidTrackdirForRoadVehicle(td))
						{
							DiagDirection enterdir = TrackdirToExitdir(td);
							ntile = v->tile + TileOffsByDiagDir(enterdir);

							TrackBits res_tracks = (TrackBits)(GetReservedTrackbits(ntile) & DiagdirReachesTracks(enterdir));
							if (res_tracks != TRACK_BIT_NONE)
							{
								Track rt = FindFirstTrack(res_tracks);
								ndir = FindFirstTrackdir(TrackToTrackdirBits(rt) & TrackdirReachesTrackdirs(cdir));
							}
						}
					}

					if (c->tile != v->tile)
					{
						c->tile = v->tile;
						c->track_prev = c->track;
						c->track = cdir;
					}

					Trackdir pdir = c->track_prev;
					if (ndir == INVALID_TRACKDIR) ndir = NextTrackdir(cdir); // assume current dir
					if (pdir == INVALID_TRACKDIR) pdir = NextTrackdir(cdir); // assume current dir
					if (cdir != ReverseTrackdir(pdir))
					{
						corrected = TrainCorrectTurn(posf, c->angle, v->tile, pdir, cdir, ndir);
						if (corrected && reversed) c->angle += 180.0f;
					}
				}
				update_z = true;
			}
		}
		if (v->type == VEH_ROAD)
		{
			RoadVehicle *truck = (RoadVehicle*)(v);
			float frame = truck->frame + c->frame_inc + frac;
			if (frame < c->frame)
			{
				if ((c->frame - frame) > 2.0f)
				{
					c->frame_inc = 0;
					c->frame = 0.0f;
				}
				else
				{
					c->frame_inc++;
				}
			}
			c->frame = truck->frame + c->frame_inc + frac;

			uint32 state = truck->state;
			if (!v->IsInDepot() && !IsStandardRoadStopTile(v->tile))
			{
				Trackdir track = (Trackdir)(state & RVSB_TRACKDIR_MASK);
				if (state & RVSB_IN_DT_ROAD_STOP) track = (Trackdir)(state & RVSB_ROAD_STOP_TRACKDIR_MASK);
				if (state == 0xFF) track = DiagDirToDiagTrackdir(DirToDiagDir(v->direction));
				corrected = TruckCorrectTurn(frac, c->frame, posf, c->angle, v->tile, track, v->direction);
				update_z = true;
			}
			else
			{
				posf[0] += frac * diff.x;
				posf[1] += frac * diff.y;
			}
		}
		if (v->type == VEH_SHIP)
		{
			Ship *ship = (Ship*)(v);
			if (!(ship->state & TRACK_BIT_DEPOT)) update_z = true;
			posf[0] -= frac * diff.x;
			posf[1] -= frac * diff.y;
		}
		if (v->type == VEH_AIRCRAFT)
		{
			posf[0] += frac * diff.x;
			posf[1] += frac * diff.y;
		}

		if (!corrected)
		{
			static const float dirRotation[8]=
			{
				135.0f, // North
				180.0f, // Northeast
			   -135.0f, // East
				-90.0f, // Southeast
				-45.0f, // South
				  0.0f, // Southwest
				 45.0f, // West
				 90.0f, // Northwest
			};

			float tang = angClamp(dirRotation[v->direction] + (reversed ? 180.0f : 0.0f), -180.0f, +180.0f);
			float diff = angDiff(tang, c->angle, -180.0f, +180.0f);

			float dpx = (posf[0] - c->posf[0]);
			float dpy = (posf[1] - c->posf[1]);
			float dd = sqrtf(dpx * dpx + dpy * dpy);
			float da = max(dd * 15.0f, 7.5f);
			switch (v->type)
			{
			case VEH_SHIP:	   da = max(dd *  7.5f, 7.5f); break;
			case VEH_AIRCRAFT: da = max(dd * 30.0f, 7.5f); break;
			}
			if (v->type != VEH_SHIP)
			{
				if (fabsf(diff) >  90.0f) c->angle = tang - 90.0f * (diff / fabsf(diff));
				if (fabsf(diff) > 135.0f) c->angle = tang;
			}
			if (v->type != VEH_TRAIN)
			{
				diff = angDiff(tang, c->angle, -180.0f, +180.0f);
				if (diff > 0.0f) { c->angle = min(c->angle + da, c->angle + diff); };
				if (diff < 0.0f) { c->angle = max(c->angle - da, c->angle + diff); };
			}
			else
			{
				c->angle = tang;
			}
		}

		/* update z coord */
		if (update_z)
		{
			float length = TILE_SIZE_F / 4.0f - 1.0f;
			if (c->draw.size() && (c->draw[0].link != INVALID_LINK)) length = GetSpriteLink(c->draw[0].link)->length;

			float pt[2][3] =
			{
				{ +length, 0.0f, 0.0f },
				{ -length, 0.0f, 0.0f },
			};
			vectRotateZ(pt[0], RAD(c->angle));
			vectRotateZ(pt[1], RAD(c->angle));
			vectAdd3(pt[0], pt[0], posf);
			vectAdd3(pt[1], pt[1], posf);
			
			Slope s1, s2;
			float z1 = GetLandZ(pt[0][0], pt[0][1], v->tile, &s1);
			float z2 = GetLandZ(pt[1][0], pt[1][1], v->tile, &s2);

			posf[2] = (z1 + z2) / 2.0f;
			c->slope = -GRAD(atan2f(z1 - z2, length * 2));
		}
	}
	c->posf[0] = posf[0];
	c->posf[1] = posf[1];
	c->posf[2] = posf[2];

	/* compute vehicle transform */
	matrSetRotateZ(c->matr, RAD(c->angle));
	matrRotateY(c->matr, RAD(c->slope));
	matrPreTranslate(c->matr, c->posf[0], c->posf[1], c->posf[2]);

	/* compute vehicle iverse transform */
	matrSetRotateY(c->matr_inv, -RAD(c->slope));
	matrRotateZ(c->matr_inv, -RAD(c->angle));
	matrTranslate(c->matr_inv, -c->posf[0], -c->posf[1], -c->posf[2]);
	
	/* update vehicle bounding box */
	{
		vectSet3(c->bbmin, +FLT_MAX);
		vectSet3(c->bbmax, -FLT_MAX);
		vectSet3(c->bbmin_ref, +FLT_MAX);
		vectSet3(c->bbmax_ref, -FLT_MAX);
		for (uint i = 0; i < c->draw.size(); i++)
		{
			if (c->draw[i].link == INVALID_LINK) continue;

			const SpriteLink *sl = GetSpriteLink(c->draw[i].link);
			vectMin3(c->bbmin_ref, c->bbmin_ref, sl->bbmin);
			vectMax3(c->bbmax_ref, c->bbmax_ref, sl->bbmax);

			float bbmin[3];
			float bbmax[3];
			matrTransformAABB(bbmin, bbmax, sl->bbmin, sl->bbmax, c->matr);
			vectMin3(c->bbmin, c->bbmin, bbmin);
			vectMax3(c->bbmax, c->bbmax, bbmax);
		}
	}
}

static void UpdateVehicles()
{
	Vehicle *v;
	FOR_ALL_VEHICLES(v)
	{
		if (!IsNormalVehicle(v)) continue;

		VehicleData *c = GetVehCache((uint32)(vehicle_index));
		UpdateVehicle(c, v);
	}
}

static void UpdateSamplers()
{
	if (_sampler[0]) return;

	glGenSamplers(4, &_sampler[0]);

	glSamplerParameteri(_sampler[0], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glSamplerParameteri(_sampler[0], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glSamplerParameteri(_sampler[0], GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glSamplerParameteri(_sampler[0], GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glSamplerParameteri(_sampler[1], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glSamplerParameteri(_sampler[1], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glSamplerParameteri(_sampler[1], GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glSamplerParameteri(_sampler[1], GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glSamplerParameteri(_sampler[2], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glSamplerParameteri(_sampler[2], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glSamplerParameteri(_sampler[2], GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
	glSamplerParameteri(_sampler[2], GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glSamplerParameteri(_sampler[3], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glSamplerParameteri(_sampler[3], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glSamplerParameteri(_sampler[3], GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glSamplerParameteri(_sampler[3], GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glSamplerParameteri(_sampler[3], GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
	glSamplerParameteri(_sampler[3], GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
}

static void UpdateShadowFrame()
{
	if (_shadow_texture || !_use_shadows_set) return;

	glGenTextures(1, &_shadow_texture);

	glGenFramebuffers(5, _shadow_frame);

	glBindTexture(GL_TEXTURE_2D, _shadow_texture);
	glTexStorage2D(GL_TEXTURE_2D, 5, glewIsSupported("GL_ARB_depth_buffer_float") ? GL_DEPTH_COMPONENT32F : GL_DEPTH_COMPONENT32, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);
	glBindTexture(GL_TEXTURE_2D, 0);

	for (int i = 0; i < 5; i++)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, _shadow_frame[i]);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, _shadow_texture, i);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}
}

static void UpdateLandProgram()
{
	if (_land_program) return;

	const char *opts = _use_shadows_set ? "#define SHADOWS\r\n" : "";
	GLuint vs = ShaderLoad("shader/land.vert", GL_VERTEX_SHADER, opts);
	GLuint fs = ShaderLoad("shader/land.frag", GL_FRAGMENT_SHADER, opts);
	_land_program = ProgramLink(vs, fs);

	_land_attribs_link[0] = glGetAttribLocation(_land_program, "in_pos");
	_land_attribs_link[1] = glGetAttribLocation(_land_program, "in_tex");
	_land_attribs_link[2] = glGetAttribLocation(_land_program, "in_loc");
	_land_attribs_link[3] = glGetAttribLocation(_land_program, "in_mip");
	_land_attribs_link[4] = glGetAttribLocation(_land_program, "in_nrm");

	glGenVertexArrays(1, &_land_vertex_format);

	glBindVertexArray(_land_vertex_format);
	for (int i = 0; i < 5; i++) glEnableVertexAttribArray((GLuint)(_land_attribs_link[i]));
	if (_opengl_ver > 0)
	{
		for (int i = 0; i < 5; i++) glVertexAttribBinding((GLuint)(_land_attribs_link[i]), 0);
		glVertexAttribFormat((GLuint)(_land_attribs_link[0]), 3, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(TileVertex, pos[0])));
		glVertexAttribFormat((GLuint)(_land_attribs_link[1]), 4, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(TileVertex, tex[0])));
		glVertexAttribFormat((GLuint)(_land_attribs_link[2]), 4, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(TileVertex, loc[0])));
		glVertexAttribFormat((GLuint)(_land_attribs_link[3]), 2, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(TileVertex, mip[0])));
		glVertexAttribFormat((GLuint)(_land_attribs_link[4]), 3, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(TileVertex, nrm)));
	}
	glBindVertexArray(0);

	_land_uniforms_link[0] = glGetUniformLocation(_land_program, "proj");
	_land_uniforms_link[1] = glGetUniformLocation(_land_program, "proj_shadow");
	_land_uniforms_link[2] = glGetUniformLocation(_land_program, "dim_tex");
	_land_uniforms_link[3] = glGetUniformLocation(_land_program, "pal");
	_land_uniforms_link[4] = glGetUniformLocation(_land_program, "recol_pal");
	_land_uniforms_link[5] = glGetUniformLocation(_land_program, "atlas_c");
	_land_uniforms_link[6] = glGetUniformLocation(_land_program, "atlas_m");
	_land_uniforms_link[7] = glGetUniformLocation(_land_program, "shadow");
	_land_uniforms_link[8] = glGetUniformLocation(_land_program, "sun_dir");
	_land_uniforms_link[9] = glGetUniformLocation(_land_program, "ambient");
}

static void UpdateLandSelProgram()
{
	if (_land_sel_program) return;

	GLuint vs = ShaderLoad("shader/land_sel.vert", GL_VERTEX_SHADER);
	GLuint fs = ShaderLoad("shader/land_sel.frag", GL_FRAGMENT_SHADER);
	_land_sel_program = ProgramLink(vs, fs);

	_land_sel_attribs_link[0] = glGetAttribLocation(_land_sel_program, "in_pos");
	_land_sel_attribs_link[1] = glGetAttribLocation(_land_sel_program, "in_tex");
	_land_sel_attribs_link[2] = glGetAttribLocation(_land_sel_program, "in_loc");
	_land_sel_attribs_link[3] = glGetAttribLocation(_land_sel_program, "in_mip");

	glGenVertexArrays(1, &_land_sel_vertex_format);

	glBindVertexArray(_land_sel_vertex_format);
	for (int i = 0; i < 4; i++) glEnableVertexAttribArray((GLuint)(_land_sel_attribs_link[i]));
	if (_opengl_ver > 0)
	{
		for (int i = 0; i < 4; i++) glVertexAttribBinding((GLuint)(_land_sel_attribs_link[i]), 0);
		glVertexAttribFormat((GLuint)(_land_sel_attribs_link[0]), 3, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(TileVertex, pos[1])));
		glVertexAttribFormat((GLuint)(_land_sel_attribs_link[1]), 4, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(TileVertex, tex[1])));
		glVertexAttribFormat((GLuint)(_land_sel_attribs_link[2]), 4, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(TileVertex, loc[1])));
		glVertexAttribFormat((GLuint)(_land_sel_attribs_link[3]), 2, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(TileVertex, mip[1])));
	}
	glBindVertexArray(0);

	_land_sel_uniforms_link[0] = glGetUniformLocation(_land_sel_program, "proj");
	_land_sel_uniforms_link[1] = glGetUniformLocation(_land_sel_program, "dim_tex");
	_land_sel_uniforms_link[2] = glGetUniformLocation(_land_sel_program, "pal");
	_land_sel_uniforms_link[3] = glGetUniformLocation(_land_sel_program, "recol_pal");
	_land_sel_uniforms_link[4] = glGetUniformLocation(_land_sel_program, "atlas_c");
	_land_sel_uniforms_link[5] = glGetUniformLocation(_land_sel_program, "atlas_m");
}

static void UpdateLandObjectProgram()
{
	if (_land_object_program) return;

	char opts[64];
	sprintf_s(opts, "%s%s", _use_shadows_set ? "#define SHADOWS\r\n" : "", _multisample_set ? "#define MULTISAMPLE\r\n" : "");
	GLuint vs = ShaderLoad("shader/land_object.vert", GL_VERTEX_SHADER, opts);
	GLuint fs = ShaderLoad("shader/land_object.frag", GL_FRAGMENT_SHADER, opts);
	_land_object_program = ProgramLink(vs, fs);

	_land_object_attribs_link[0] = glGetAttribLocation(_land_object_program, "in_pos");
	_land_object_attribs_link[1] = glGetAttribLocation(_land_object_program, "in_tex");
	_land_object_attribs_link[2] = glGetAttribLocation(_land_object_program, "in_nrm");
	_land_object_attribs_link[3] = glGetAttribLocation(_land_object_program, "in_loc");
	_land_object_attribs_link[4] = glGetAttribLocation(_land_object_program, "in_mip");
	_land_object_attribs_link[5] = glGetAttribLocation(_land_object_program, "in_matr_x");
	_land_object_attribs_link[6] = glGetAttribLocation(_land_object_program, "in_matr_y");
	_land_object_attribs_link[7] = glGetAttribLocation(_land_object_program, "in_matr_z");
	_land_object_attribs_link[8] = glGetAttribLocation(_land_object_program, "in_sun_dir");

	glGenVertexArrays(1, &_land_object_vertex_format);

	glBindVertexArray(_land_object_vertex_format);
	for (int i = 0; i < 9; i++) glEnableVertexAttribArray((GLuint)(_land_object_attribs_link[i]));
	if (_opengl_ver > 0)
	{
		for (int i = 0; i < 3; i++) glVertexAttribBinding((GLuint)(_land_object_attribs_link[i]), 0);
		for (int i = 3; i < 9; i++) glVertexAttribBinding((GLuint)(_land_object_attribs_link[i]), 1);
		glVertexAttribFormat((GLuint)(_land_object_attribs_link[0]), 3, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(ModelVertex, pos)));
		glVertexAttribFormat((GLuint)(_land_object_attribs_link[1]), 2, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(ModelVertex, tex)));
		glVertexAttribFormat((GLuint)(_land_object_attribs_link[2]), 3, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(ModelVertex, nrm)));
		glVertexAttribFormat((GLuint)(_land_object_attribs_link[3]), 4, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(ModelInstance, loc)));
		glVertexAttribFormat((GLuint)(_land_object_attribs_link[4]), 4, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(ModelInstance, mip)));
		glVertexAttribFormat((GLuint)(_land_object_attribs_link[5]), 4, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(ModelInstance, matr[0])));
		glVertexAttribFormat((GLuint)(_land_object_attribs_link[6]), 4, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(ModelInstance, matr[1])));
		glVertexAttribFormat((GLuint)(_land_object_attribs_link[7]), 4, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(ModelInstance, matr[2])));
		glVertexAttribIFormat((GLuint)(_land_object_attribs_link[8]), 1, GL_UNSIGNED_INT, (GLuint)(cpp_offsetof(ModelInstance, sun)));
	}
	glBindVertexArray(0);

	_land_object_uniforms_link[0] = glGetUniformLocation(_land_object_program, "proj");
	_land_object_uniforms_link[1] = glGetUniformLocation(_land_object_program, "proj_shadow");
	_land_object_uniforms_link[2] = glGetUniformLocation(_land_object_program, "dim_tex");
	_land_object_uniforms_link[3] = glGetUniformLocation(_land_object_program, "pal");
	_land_object_uniforms_link[4] = glGetUniformLocation(_land_object_program, "recol_pal");
	_land_object_uniforms_link[5] = glGetUniformLocation(_land_object_program, "atlas_c");
	_land_object_uniforms_link[6] = glGetUniformLocation(_land_object_program, "atlas_m");
	_land_object_uniforms_link[7] = glGetUniformLocation(_land_object_program, "shadow");
	_land_object_uniforms_link[8] = glGetUniformLocation(_land_object_program, "sun_dir");
	_land_object_uniforms_link[9] = glGetUniformLocation(_land_object_program, "ambient");
}

static void UpdateObjectProgram()
{
	if (_object_program) return;

	char opts[64];
	sprintf_s(opts, "%s%s", _use_shadows_set ? "#define SHADOWS\r\n" : "", _multisample_set ? "#define MULTISAMPLE\r\n" : "");
	GLuint vs = ShaderLoad("shader/object.vert", GL_VERTEX_SHADER, opts);
	GLuint fs = ShaderLoad("shader/object.frag", GL_FRAGMENT_SHADER, opts);
	_object_program = ProgramLink(vs, fs);

	_object_attribs_link[0] = glGetAttribLocation(_object_program, "in_pos");
	_object_attribs_link[1] = glGetAttribLocation(_object_program, "in_tex");
	_object_attribs_link[2] = glGetAttribLocation(_object_program, "in_nrm");
	_object_attribs_link[3] = glGetAttribLocation(_object_program, "in_loc");
	_object_attribs_link[4] = glGetAttribLocation(_object_program, "in_mip");
	_object_attribs_link[5] = glGetAttribLocation(_object_program, "in_matr_x");
	_object_attribs_link[6] = glGetAttribLocation(_object_program, "in_matr_y");
	_object_attribs_link[7] = glGetAttribLocation(_object_program, "in_matr_z");
	_object_attribs_link[8] = glGetAttribLocation(_object_program, "in_sun_dir");

	glGenVertexArrays(1, &_object_vertex_format);

	glBindVertexArray(_object_vertex_format);
	for (int i = 0; i < 9; i++) glEnableVertexAttribArray((GLuint)(_object_attribs_link[i]));
	if (_opengl_ver > 0)
	{
		for (int i = 0; i < 3; i++) glVertexAttribBinding((GLuint)(_object_attribs_link[i]), 0);
		for (int i = 3; i < 9; i++) glVertexAttribBinding((GLuint)(_object_attribs_link[i]), 1);
		glVertexAttribFormat((GLuint)(_object_attribs_link[0]), 3, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(ModelVertex, pos)));
		glVertexAttribFormat((GLuint)(_object_attribs_link[1]), 2, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(ModelVertex, tex)));
		glVertexAttribFormat((GLuint)(_object_attribs_link[2]), 3, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(ModelVertex, nrm)));
		glVertexAttribFormat((GLuint)(_object_attribs_link[3]), 4, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(ModelInstance, loc)));
		glVertexAttribFormat((GLuint)(_object_attribs_link[4]), 4, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(ModelInstance, mip)));
		glVertexAttribFormat((GLuint)(_object_attribs_link[5]), 4, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(ModelInstance, matr[0])));
		glVertexAttribFormat((GLuint)(_object_attribs_link[6]), 4, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(ModelInstance, matr[1])));
		glVertexAttribFormat((GLuint)(_object_attribs_link[7]), 4, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(ModelInstance, matr[2])));
		glVertexAttribIFormat((GLuint)(_object_attribs_link[8]), 1, GL_UNSIGNED_INT, (GLuint)(cpp_offsetof(ModelInstance, sun)));
	}
	glBindVertexArray(0);

	_object_uniforms_link[0] = glGetUniformLocation(_object_program, "proj");
	_object_uniforms_link[1] = glGetUniformLocation(_object_program, "proj_shadow");
	_object_uniforms_link[2] = glGetUniformLocation(_object_program, "dim_tex");
	_object_uniforms_link[3] = glGetUniformLocation(_object_program, "pal");
	_object_uniforms_link[4] = glGetUniformLocation(_object_program, "recol_pal");	
	_object_uniforms_link[5] = glGetUniformLocation(_object_program, "atlas_c");
	_object_uniforms_link[6] = glGetUniformLocation(_object_program, "atlas_m");
	_object_uniforms_link[7] = glGetUniformLocation(_object_program, "shadow");
	_object_uniforms_link[8] = glGetUniformLocation(_object_program, "sun_dir");
	_object_uniforms_link[9] = glGetUniformLocation(_object_program, "ambient");
}

static void UpdateFillLandProgram()
{
	if (_fill_land_program) return;

	GLuint vs = ShaderLoad("shader/fill_land.vert", GL_VERTEX_SHADER);
	GLuint fs = ShaderLoad("shader/fill_land.frag", GL_FRAGMENT_SHADER);
	_fill_land_program = ProgramLink(vs, fs);

	_fill_land_attribs_link[0] = glGetAttribLocation(_fill_land_program, "in_pos");
	_fill_land_attribs_link[1] = glGetAttribLocation(_fill_land_program, "in_tex");
	_fill_land_attribs_link[2] = glGetAttribLocation(_fill_land_program, "in_loc");
//	_fill_land_attribs_link[3] = glGetAttribLocation(_fill_land_program, "in_mip");
	
	glGenVertexArrays(1, &_fill_land_vertex_format);

	glBindVertexArray(_fill_land_vertex_format);
	for (int i = 0; i < 3; i++) glEnableVertexAttribArray((GLuint)(_fill_land_attribs_link[i]));
	if (_opengl_ver > 0)
	{
		for (int i = 0; i < 3; i++) glVertexAttribBinding((GLuint)(_fill_land_attribs_link[i]), 0);
		glVertexAttribFormat((GLuint)(_fill_land_attribs_link[0]), 3, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(TileVertex, pos[0])));
		glVertexAttribFormat((GLuint)(_fill_land_attribs_link[1]), 4, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(TileVertex, tex[0])));
		glVertexAttribFormat((GLuint)(_fill_land_attribs_link[2]), 4, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(TileVertex, loc[0])));
//		glVertexAttribFormat((GLuint)(_fill_land_attribs_link[3]), 2, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(TileVertex, mip[0])));
	}
	glBindVertexArray(0);

	_fill_land_uniforms_link[0] = glGetUniformLocation(_fill_land_program, "proj");
	_fill_land_uniforms_link[1] = glGetUniformLocation(_fill_land_program, "dim_tex");
	_fill_land_uniforms_link[2] = glGetUniformLocation(_fill_land_program, "pal");
	_fill_land_uniforms_link[3] = glGetUniformLocation(_fill_land_program, "recol_pal");
	_fill_land_uniforms_link[4] = glGetUniformLocation(_fill_land_program, "atlas_m");
}

static void UpdateFillObjectProgram()
{
	if (_fill_object_program) return;

	GLuint vs = ShaderLoad("shader/fill_object.vert", GL_VERTEX_SHADER);
	GLuint fs = ShaderLoad("shader/fill_object.frag", GL_FRAGMENT_SHADER);
	_fill_object_program = ProgramLink(vs, fs);

	_fill_object_attribs_link[0] = glGetAttribLocation(_fill_object_program, "in_pos");
	_fill_object_attribs_link[1] = glGetAttribLocation(_fill_object_program, "in_tex");
	_fill_object_attribs_link[2] = glGetAttribLocation(_fill_object_program, "in_loc");
	_fill_object_attribs_link[3] = glGetAttribLocation(_fill_object_program, "in_mip");
	_fill_object_attribs_link[4] = glGetAttribLocation(_fill_object_program, "in_matr_x");
	_fill_object_attribs_link[5] = glGetAttribLocation(_fill_object_program, "in_matr_y");
	_fill_object_attribs_link[6] = glGetAttribLocation(_fill_object_program, "in_matr_z");

	glGenVertexArrays(1, &_fill_object_vertex_format);

	glBindVertexArray(_fill_object_vertex_format);
	for (int i = 0; i < 7; i++) glEnableVertexAttribArray((GLuint)(_fill_object_attribs_link[i]));
	if (_opengl_ver > 0)
	{
		for (int i = 0; i < 2; i++) glVertexAttribBinding((GLuint)(_fill_object_attribs_link[i]), 0);
		for (int i = 2; i < 7; i++) glVertexAttribBinding((GLuint)(_fill_object_attribs_link[i]), 1);
		glVertexAttribFormat((GLuint)(_fill_object_attribs_link[0]), 3, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(ModelVertex, pos)));
		glVertexAttribFormat((GLuint)(_fill_object_attribs_link[1]), 2, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(ModelVertex, tex)));
		glVertexAttribFormat((GLuint)(_fill_object_attribs_link[2]), 4, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(ModelInstance, loc)));
		glVertexAttribFormat((GLuint)(_fill_object_attribs_link[3]), 4, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(ModelInstance, mip)));
		glVertexAttribFormat((GLuint)(_fill_object_attribs_link[4]), 4, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(ModelInstance, matr[0])));
		glVertexAttribFormat((GLuint)(_fill_object_attribs_link[5]), 4, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(ModelInstance, matr[1])));
		glVertexAttribFormat((GLuint)(_fill_object_attribs_link[6]), 4, GL_FLOAT, GL_FALSE, (GLuint)(cpp_offsetof(ModelInstance, matr[2])));
	}
	glBindVertexArray(0);

	_fill_object_uniforms_link[0] = glGetUniformLocation(_fill_object_program, "proj");
	_fill_object_uniforms_link[1] = glGetUniformLocation(_fill_object_program, "dim_tex");
	_fill_object_uniforms_link[2] = glGetUniformLocation(_fill_object_program, "pal");
	_fill_object_uniforms_link[3] = glGetUniformLocation(_fill_object_program, "recol_pal");
	_fill_object_uniforms_link[4] = glGetUniformLocation(_fill_object_program, "atlas_m");
}

/* Reset graphicl resources related to the sadow map */
static void ResetShadowResources()
{
	if (_shadow_texture)
	{
		glDeleteTextures(1, &_shadow_texture);
		glDeleteFramebuffers(5, _shadow_frame);
		_shadow_texture = 0;
	}
	if (_land_program)
	{
		glDeleteProgram(_land_program);
		glDeleteVertexArrays(1, &_land_vertex_format);
		_land_program =0;
	}
	if (_land_object_program)
	{
		glDeleteProgram(_land_object_program);
		glDeleteVertexArrays(1, &_land_object_vertex_format);
		_land_object_program = 0;
	}
	if (_object_program)
	{
		glDeleteProgram(_object_program);
		glDeleteVertexArrays(1, &_land_vertex_format);
		_object_program = 0;
	}
}

/* Select the drawing data for landscape */
static void SelectLandDrawData(bool sel)
{
	_draw_seg_first.clear();
	_draw_seg_count.clear();

	_draw_sel_first.clear();
	_draw_sel_count.clear();

	for (size_t i = 0; i < _draw_seg.size(); i++)
	{
		LandSeg *s = _draw_seg[i];
		if (s->idx_min >= s->idx_max) continue;

		_draw_seg_first.emplace_back(s->idx_min * 6);
		_draw_seg_count.emplace_back((s->idx_max - s->idx_min) * 6);
	}
	if (!sel) return;

	for (size_t i = 0; i < _draw_seg.size(); i++)
	{
		LandSeg *s = _draw_seg[i];
		if (s->sel_min >= s->sel_max) continue;

		_draw_sel_first.emplace_back(s->sel_min * 6);
		_draw_sel_count.emplace_back((s->sel_max - s->sel_min) * 6);
	}
}

static void AddObjectsDrawData(const ViewPort *vp, bool solid, bool transp)
{
	uint32 mask = (solid ? MODEL_CACHE_MASK_SOLID : 0) | (transp ? MODEL_CACHE_MASK_TRANSP : 0);
	uint32 zoom = (1 << vp->zoom);
	for (LandSeg *s : _draw_seg) AddModelInstances(s->draw_cache, mask, zoom);
	for (VehicleData *c : _draw_veh) AddModelInstances(c->draw_cache, mask, zoom);
}

/* Draw lanscape and solid objects */
static void DrawDataColor(const ViewPort *vp)
{
	Blitter_OpenGL *blitter = (Blitter_OpenGL*)(BlitterFactory::GetCurrentBlitter());

	float up[3] = { 0.0f, 0.0f, 1.0f };
	float level_land = vectDot3(up, _sun_dir) * 1.0f;
	float level_obj  = vectDot3(up, _sun_dir) * 1.0f;
	static float amb_land = 1.6f;
	static float amb_obj  = 0.5f;

	float sun_dir[9 * 3];
	{
		float ax[3] ={ 1.0f, 0.0f, 0.0f };
		float ay[3] ={ 0.0f, 1.0f, 0.0f };
		vectCopy3(sun_dir, _sun_dir);
		for (int i = 0; i < 8; i++)
		{
			float *s = &sun_dir[(i + 1) * 3];

			float sx[3];
			float sy[3];
			matrApplyNoTrans33(sx, _auto_slope[i], ax); sx[2] = -sx[2];
			matrApplyNoTrans33(sy, _auto_slope[i], ay); sy[2] = -sy[2];
			vectNormalize3(sx);
			vectNormalize3(sy);

			float sz[3];
			vectCross3(sz, sx, sy);
			s[0] = sx[0] * _sun_dir[0] + sy[0] * _sun_dir[1] + sz[0] * _sun_dir[2];
			s[1] = sx[1] * _sun_dir[0] + sy[1] * _sun_dir[1] + sz[1] * _sun_dir[2];
			s[2] = sx[2] * _sun_dir[0] + sy[2] * _sun_dir[1] + sz[2] * _sun_dir[2];
		}
	}
	
	glSamplerParameterf(_sampler[3], GL_TEXTURE_MIN_LOD, _shadow_level);
	glSamplerParameterf(_sampler[3], GL_TEXTURE_MAX_LOD, _shadow_level + 1);

	glEnable(GL_POLYGON_OFFSET_FILL);

	glUseProgram(_land_program);
	{
		glUniform4fv(_land_uniforms_link[0], 4, vp->xyz_to_ogl); // world transform
		if (_use_shadows_set) glUniform4fv(_land_uniforms_link[1], 3, _xyz_to_shadow_tex); // shadow transform
		glUniform4f(_land_uniforms_link[2], 1.0f / IMAGE3D_ATLAS_SIZE_F, 1.0f / IMAGE3D_ATLAS_SIZE_F, 1.0f, 1.0f / (float)(blitter->RecolPalCount())); // tex_dim

		glActiveTexture(GL_TEXTURE0);
		glBindSampler(0, _sampler[0]);
		glBindTexture(GL_TEXTURE_1D, blitter->PalTexture());
		glUniform1i(_land_uniforms_link[3], 0);

		glActiveTexture(GL_TEXTURE1);
		glBindSampler(1, _sampler[0]);
		glBindTexture(GL_TEXTURE_2D, blitter->RecolPalTexture());
		glUniform1i(_land_uniforms_link[4], 1);

		glActiveTexture(GL_TEXTURE2);
		glBindSampler(2, _sampler[1]);
		glBindTexture(GL_TEXTURE_2D_ARRAY, _atlas_texture);
		glUniform1i(_land_uniforms_link[5], 2);

		glActiveTexture(GL_TEXTURE3);
		glBindSampler(3, _sampler[2]);
		glBindTexture(GL_TEXTURE_2D_ARRAY, _atlas_texture);
		glUniform1i(_land_uniforms_link[6], 3);
		
		if (_use_shadows_set)
		{
			glActiveTexture(GL_TEXTURE4);
			glBindSampler(4, _sampler[3]);
			glBindTexture(GL_TEXTURE_2D, _shadow_texture);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, _shadow_level);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, _shadow_level + 1);
			glUniform1i(_land_uniforms_link[7], 4);
		}

		glUniform3f(_land_uniforms_link[8], _sun_dir[0], _sun_dir[1], _sun_dir[2]); // sun_dir
		glUniform3f(_land_uniforms_link[9], level_land, amb_land, 1.0f - level_land); // ambient

		glDepthFunc(GL_LESS);
		glPolygonOffset(0.0f, 32.0f);

		glBindVertexArray(_land_vertex_format);
		if (_opengl_ver > 0)
		{
			glBindVertexBuffer(0, _land.vertex_buffer, 0, sizeof(TileVertex));
		}
		else
		{
			glBindBuffer(GL_ARRAY_BUFFER, _land.vertex_buffer);
			glVertexAttribPointer((GLuint)(_land_attribs_link[0]), 3, GL_FLOAT, GL_FALSE, sizeof(TileVertex), (void*)(cpp_offsetof(TileVertex, pos[0])));
			glVertexAttribPointer((GLuint)(_land_attribs_link[1]), 4, GL_FLOAT, GL_FALSE, sizeof(TileVertex), (void*)(cpp_offsetof(TileVertex, tex[0])));
			glVertexAttribPointer((GLuint)(_land_attribs_link[2]), 4, GL_FLOAT, GL_FALSE, sizeof(TileVertex), (void*)(cpp_offsetof(TileVertex, loc[0])));
			glVertexAttribPointer((GLuint)(_land_attribs_link[3]), 2, GL_FLOAT, GL_FALSE, sizeof(TileVertex), (void*)(cpp_offsetof(TileVertex, mip[0])));
			glVertexAttribPointer((GLuint)(_land_attribs_link[4]), 3, GL_FLOAT, GL_FALSE, sizeof(TileVertex), (void*)(cpp_offsetof(TileVertex, nrm)));
			glBindBuffer(GL_ARRAY_BUFFER, 0);
		}
		glMultiDrawArrays(GL_TRIANGLES, _draw_seg_first.data(), _draw_seg_count.data(), (GLsizei)(_draw_seg_first.size()));
		glBindVertexArray(0);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_1D, 0);
		glBindSampler(0, 0);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, 0);
		glBindSampler(1, 0);

		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
		glBindSampler(2, 0);

		glActiveTexture(GL_TEXTURE3);
		glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
		glBindSampler(3, 0);
		
		if (_use_shadows_set)
		{
			glActiveTexture(GL_TEXTURE4);
			glBindTexture(GL_TEXTURE_2D, 0);
			glBindSampler(4, 0);
		}
	}
	glUseProgram(0);

	glDisable(GL_POLYGON_OFFSET_FILL);

	glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
	glEnable(GL_SAMPLE_ALPHA_TO_ONE);

	glUseProgram(_land_object_program);
	{
		glUniform4fv(_land_object_uniforms_link[0], 4, vp->xyz_to_ogl); // world transform
		if (_use_shadows_set) glUniform4fv(_land_object_uniforms_link[1], 3, _xyz_to_shadow_tex); // shadow transform
		glUniform4f(_land_object_uniforms_link[2], 1.0f, 1.0f, 1.0f, 1.0f / (float)(blitter->RecolPalCount())); // tex_dim

		glActiveTexture(GL_TEXTURE0);
		glBindSampler(0, _sampler[0]);
		glBindTexture(GL_TEXTURE_1D, blitter->PalTexture());
		glUniform1i(_land_object_uniforms_link[3], 0);

		glActiveTexture(GL_TEXTURE1);
		glBindSampler(1, _sampler[0]);
		glBindTexture(GL_TEXTURE_2D, blitter->RecolPalTexture());
		glUniform1i(_land_object_uniforms_link[4], 1);

		glActiveTexture(GL_TEXTURE2);
		glBindSampler(2, _sampler[1]);
		glBindTexture(GL_TEXTURE_2D_ARRAY, _atlas_texture);
		glUniform1i(_land_object_uniforms_link[5], 2);

		glActiveTexture(GL_TEXTURE3);
		glBindSampler(3, _sampler[2]);
		glBindTexture(GL_TEXTURE_2D_ARRAY, _atlas_texture);
		glUniform1i(_land_object_uniforms_link[6], 3);

		if (_use_shadows_set)
		{
			glActiveTexture(GL_TEXTURE4);
			glBindSampler(4, _sampler[3]);
			glBindTexture(GL_TEXTURE_2D, _shadow_texture);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, _shadow_level);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, _shadow_level + 1);
			glUniform1i(_land_object_uniforms_link[7], 4);
		}
		
		glUniform3fv(_land_object_uniforms_link[8], 9, sun_dir); // sun_dir
		glUniform3f(_land_object_uniforms_link[9], level_land, amb_land, 1.0f - level_land); // ambient

		glDepthFunc(GL_LESS);

		glBindVertexArray(_land_object_vertex_format);
		if (_opengl_ver > 0)
		{
			glBindVertexBuffer(0, _model_vertex_buffer, 0, sizeof(ModelVertex));
			glBindVertexBuffer(1, _model_instance_buffer, 0, sizeof(ModelInstance));
			glVertexBindingDivisor(1, 1);

			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _model_index_buffer);
			for (const DrawElementsIndirectCmd &cmd : _land_object_instance_cmd)
			{
				glDrawElementsInstancedBaseVertexBaseInstance(GL_TRIANGLES, cmd.count, GL_UNSIGNED_INT, (void*)(cmd.firstIndex * sizeof(GLuint)), cmd.instanceCount, cmd.baseVertex, cmd.baseInstance);
			}
//			glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, _land_object_instance_cmd.data(), (GLsizei)(_land_object_instance_cmd.size()), sizeof(DrawElementsIndirectCmd));
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		}
		else
		{
			glBindBuffer(GL_ARRAY_BUFFER, _model_vertex_buffer);
			glVertexAttribPointer((GLuint)(_land_object_attribs_link[0]), 3, GL_FLOAT, GL_FALSE, sizeof(ModelVertex), (void*)(cpp_offsetof(ModelVertex, pos)));
			glVertexAttribPointer((GLuint)(_land_object_attribs_link[1]), 2, GL_FLOAT, GL_FALSE, sizeof(ModelVertex), (void*)(cpp_offsetof(ModelVertex, tex)));
			glVertexAttribPointer((GLuint)(_land_object_attribs_link[2]), 3, GL_FLOAT, GL_FALSE, sizeof(ModelVertex), (void*)(cpp_offsetof(ModelVertex, nrm)));
			glBindBuffer(GL_ARRAY_BUFFER, 0);

			glBindBuffer(GL_ARRAY_BUFFER, _model_instance_buffer);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _model_index_buffer);
			for (int i = 3; i < 9; i++) glVertexAttribDivisor((GLuint)(_land_object_attribs_link[i]), 1);
			for (const DrawElementsIndirectCmd &cmd : _land_object_instance_cmd)
			{
				size_t offset = cmd.baseInstance * sizeof(ModelInstance);
				glVertexAttribPointer((GLuint)(_land_object_attribs_link[3]), 4, GL_FLOAT, GL_FALSE, sizeof(ModelInstance), (void*)(offset + cpp_offsetof(ModelInstance, loc)));
				glVertexAttribPointer((GLuint)(_land_object_attribs_link[4]), 4, GL_FLOAT, GL_FALSE, sizeof(ModelInstance), (void*)(offset + cpp_offsetof(ModelInstance, mip)));
				glVertexAttribPointer((GLuint)(_land_object_attribs_link[5]), 4, GL_FLOAT, GL_FALSE, sizeof(ModelInstance), (void*)(offset + cpp_offsetof(ModelInstance, matr[0])));
				glVertexAttribPointer((GLuint)(_land_object_attribs_link[6]), 4, GL_FLOAT, GL_FALSE, sizeof(ModelInstance), (void*)(offset + cpp_offsetof(ModelInstance, matr[1])));
				glVertexAttribPointer((GLuint)(_land_object_attribs_link[7]), 4, GL_FLOAT, GL_FALSE, sizeof(ModelInstance), (void*)(offset + cpp_offsetof(ModelInstance, matr[2])));
				glVertexAttribIPointer((GLuint)(_land_object_attribs_link[8]), 1, GL_UNSIGNED_INT, sizeof(ModelInstance), (void*)(offset + cpp_offsetof(ModelInstance, sun)));

				glDrawElementsInstancedBaseVertex(GL_TRIANGLES, cmd.count, GL_UNSIGNED_INT, (void*)(cmd.firstIndex * sizeof(uint32)), cmd.instanceCount, cmd.baseVertex);
			}
			for (int i = 3; i < 9; i++) glVertexAttribDivisor((GLuint)(_land_object_attribs_link[i]), 1);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
			glBindBuffer(GL_ARRAY_BUFFER, 0);
		}
		glBindVertexArray(0);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_1D, 0);
		glBindSampler(0, 0);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, 0);
		glBindSampler(1, 0);

		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
		glBindSampler(2, 0);

		glActiveTexture(GL_TEXTURE3);
		glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
		glBindSampler(3, 0);

		if (_use_shadows_set)
		{
			glActiveTexture(GL_TEXTURE4);
			glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
			glBindSampler(4, 0);
		}
	}
	glUseProgram(0);

	glEnable(GL_POLYGON_OFFSET_FILL);

	glUseProgram(_land_sel_program);
	{
		glUniform4fv(_land_sel_uniforms_link[0], 4, vp->xyz_to_ogl); // world transform
		glUniform4f(_land_sel_uniforms_link[1], 1.0f / IMAGE3D_ATLAS_SIZE_F, 1.0f / IMAGE3D_ATLAS_SIZE_F, 1.0f, 1.0f / (float)(blitter->RecolPalCount())); // tex_dim

		glActiveTexture(GL_TEXTURE0);
		glBindSampler(0, _sampler[0]);
		glBindTexture(GL_TEXTURE_1D, blitter->PalTexture());
		glUniform1i(_land_sel_uniforms_link[2], 0);

		glActiveTexture(GL_TEXTURE1);
		glBindSampler(1, _sampler[0]);
		glBindTexture(GL_TEXTURE_2D, blitter->RecolPalTexture());
		glUniform1i(_land_sel_uniforms_link[3], 1);

		glActiveTexture(GL_TEXTURE2);
		glBindSampler(2, _sampler[1]);
		glBindTexture(GL_TEXTURE_2D_ARRAY, _atlas_texture);
		glUniform1i(_land_sel_uniforms_link[4], 2);

		glActiveTexture(GL_TEXTURE3);
		glBindSampler(3, _sampler[2]);
		glBindTexture(GL_TEXTURE_2D_ARRAY, _atlas_texture);
		glUniform1i(_land_sel_uniforms_link[5], 3);

		glDepthFunc(GL_LEQUAL);
		glPolygonOffset(0.0f, -256.0f);
		glDepthMask(GL_FALSE);

		glBindVertexArray(_land_sel_vertex_format);
		if (_opengl_ver > 0)
		{
			glBindVertexBuffer(0, _land.vertex_buffer, 0, sizeof(TileVertex));
		}
		else
		{
			glBindBuffer(GL_ARRAY_BUFFER, _land.vertex_buffer);
			glVertexAttribPointer((GLuint)(_land_sel_attribs_link[0]), 3, GL_FLOAT, GL_FALSE, sizeof(TileVertex), (void*)(cpp_offsetof(TileVertex, pos[1])));
			glVertexAttribPointer((GLuint)(_land_sel_attribs_link[1]), 4, GL_FLOAT, GL_FALSE, sizeof(TileVertex), (void*)(cpp_offsetof(TileVertex, tex[1])));
			glVertexAttribPointer((GLuint)(_land_sel_attribs_link[2]), 4, GL_FLOAT, GL_FALSE, sizeof(TileVertex), (void*)(cpp_offsetof(TileVertex, loc[1])));
			glVertexAttribPointer((GLuint)(_land_sel_attribs_link[3]), 2, GL_FLOAT, GL_FALSE, sizeof(TileVertex), (void*)(cpp_offsetof(TileVertex, mip[1])));
			glBindBuffer(GL_ARRAY_BUFFER, 0);
		}
		glMultiDrawArrays(GL_TRIANGLES, _draw_sel_first.data(), _draw_sel_count.data(), (GLsizei)(_draw_sel_first.size()));
		glBindVertexArray(0);

		glDepthMask(GL_TRUE);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, 0);
		glBindSampler(0, 0);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, 0);
		glBindSampler(1, 0);

		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
		glBindSampler(2, 0);

		glActiveTexture(GL_TEXTURE3);
		glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
		glBindSampler(3, 0);
	}
	glUseProgram(0);

	glDisable(GL_POLYGON_OFFSET_FILL);

	glUseProgram(_object_program);
	{
		glUniform4fv(_object_uniforms_link[0], 4, vp->xyz_to_ogl); // world transform
		if (_use_shadows_set) glUniform4fv(_object_uniforms_link[1], 3, _xyz_to_shadow_tex); // shadow transform
		glUniform4f(_object_uniforms_link[2], 1.0f, 1.0f, 1.0f, 1.0f / (float)(blitter->RecolPalCount())); // tex_dim

		glActiveTexture(GL_TEXTURE0);
		glBindSampler(0, _sampler[0]);
		glBindTexture(GL_TEXTURE_1D, blitter->PalTexture());
		glUniform1i(_object_uniforms_link[3], 0);

		glActiveTexture(GL_TEXTURE1);
		glBindSampler(1, _sampler[0]);
		glBindTexture(GL_TEXTURE_2D, blitter->RecolPalTexture());
		glUniform1i(_object_uniforms_link[4], 1);

		glActiveTexture(GL_TEXTURE2);
		glBindSampler(2, _sampler[1]);
		glBindTexture(GL_TEXTURE_2D_ARRAY, _atlas_texture);
		glUniform1i(_object_uniforms_link[5], 2);

		glActiveTexture(GL_TEXTURE3);
		glBindSampler(3, _sampler[2]);
		glBindTexture(GL_TEXTURE_2D_ARRAY, _atlas_texture);
		glUniform1i(_object_uniforms_link[6], 3);

		if (_use_shadows_set)
		{
			glActiveTexture(GL_TEXTURE4);
			glBindSampler(4, _sampler[3]);
			glBindTexture(GL_TEXTURE_2D, _shadow_texture);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, _shadow_level);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, _shadow_level + 1);
			glUniform1i(_object_uniforms_link[7], 4);
		}

		glUniform3fv(_object_uniforms_link[8], 9, sun_dir); // sun_dir
		glUniform3f(_object_uniforms_link[9], level_obj, amb_obj, 1.0f - level_obj); // ambient

		glDepthFunc(GL_LESS);

		glBindVertexArray(_object_vertex_format);
		if (_opengl_ver > 0)
		{
			glBindVertexBuffer(0, _model_vertex_buffer, 0, sizeof(ModelVertex));
			glBindVertexBuffer(1, _model_instance_buffer, 0, sizeof(ModelInstance));
			glVertexBindingDivisor(1, 1);

			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _model_index_buffer);
			for (const DrawElementsIndirectCmd &cmd : _object_instance_cmd)
			{
				glDrawElementsInstancedBaseVertexBaseInstance(GL_TRIANGLES, cmd.count, GL_UNSIGNED_INT, (void*)(cmd.firstIndex * sizeof(GLuint)), cmd.instanceCount, cmd.baseVertex, cmd.baseInstance);
			}
//			glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, _object_instance_cmd.data(), (GLsizei)(_object_instance_cmd.size()), sizeof(DrawElementsIndirectCmd));
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		}
		else
		{
			glBindBuffer(GL_ARRAY_BUFFER, _model_vertex_buffer);
			glVertexAttribPointer((GLuint)(_object_attribs_link[0]), 3, GL_FLOAT, GL_FALSE, sizeof(ModelVertex), (void*)(cpp_offsetof(ModelVertex, pos)));
			glVertexAttribPointer((GLuint)(_object_attribs_link[1]), 2, GL_FLOAT, GL_FALSE, sizeof(ModelVertex), (void*)(cpp_offsetof(ModelVertex, tex)));
			glVertexAttribPointer((GLuint)(_object_attribs_link[2]), 3, GL_FLOAT, GL_FALSE, sizeof(ModelVertex), (void*)(cpp_offsetof(ModelVertex, nrm)));
			glBindBuffer(GL_ARRAY_BUFFER, 0);

			glBindBuffer(GL_ARRAY_BUFFER, _model_instance_buffer);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _model_index_buffer);
			for (int i = 3; i < 9; i++) glVertexAttribDivisor((GLuint)(_object_attribs_link[i]), 1);
			for (const DrawElementsIndirectCmd &cmd : _object_instance_cmd)
			{
				size_t offset = cmd.baseInstance * sizeof(ModelInstance);
				glVertexAttribPointer((GLuint)(_object_attribs_link[3]), 4, GL_FLOAT, GL_FALSE, sizeof(ModelInstance), (void*)(offset + cpp_offsetof(ModelInstance, loc)));
				glVertexAttribPointer((GLuint)(_object_attribs_link[4]), 4, GL_FLOAT, GL_FALSE, sizeof(ModelInstance), (void*)(offset + cpp_offsetof(ModelInstance, mip)));
				glVertexAttribPointer((GLuint)(_object_attribs_link[5]), 4, GL_FLOAT, GL_FALSE, sizeof(ModelInstance), (void*)(offset + cpp_offsetof(ModelInstance, matr[0])));
				glVertexAttribPointer((GLuint)(_object_attribs_link[6]), 4, GL_FLOAT, GL_FALSE, sizeof(ModelInstance), (void*)(offset + cpp_offsetof(ModelInstance, matr[1])));
				glVertexAttribPointer((GLuint)(_object_attribs_link[7]), 4, GL_FLOAT, GL_FALSE, sizeof(ModelInstance), (void*)(offset + cpp_offsetof(ModelInstance, matr[2])));
				glVertexAttribIPointer((GLuint)(_object_attribs_link[8]), 1, GL_UNSIGNED_INT, sizeof(ModelInstance), (void*)(offset + cpp_offsetof(ModelInstance, sun)));

				glDrawElementsInstancedBaseVertex(GL_TRIANGLES, cmd.count, GL_UNSIGNED_INT, (void*)(cmd.firstIndex * sizeof(uint32)), cmd.instanceCount, cmd.baseVertex);
			}
			for (int i = 3; i < 9; i++) glVertexAttribDivisor((GLuint)(_object_attribs_link[i]), 1);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
			glBindBuffer(GL_ARRAY_BUFFER, 0);
		}
		glBindVertexArray(0);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_1D, 0);
		glBindSampler(0, 0);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, 0);
		glBindSampler(1, 0);

		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
		glBindSampler(2, 0);

		glActiveTexture(GL_TEXTURE3);
		glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
		glBindSampler(3, 0);

		if (_use_shadows_set)
		{
			glActiveTexture(GL_TEXTURE4);
			glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
			glBindSampler(4, 0);
		}
	}
	glUseProgram(0);

	glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
	glDisable(GL_SAMPLE_ALPHA_TO_ONE);
}

/* Draw transparent objects */
static void DrawDataTransp(const ViewPort *vp)
{
	Blitter_OpenGL *blitter = (Blitter_OpenGL*)(BlitterFactory::GetCurrentBlitter());
	
	glEnable(GL_BLEND);
	glBlendColor(0.0f, 0.0f, 0.0f, 0.2f);
	glBlendFuncSeparate(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA, GL_ONE, GL_ZERO);

	glDepthMask(GL_FALSE);

	glUseProgram(_fill_object_program);
	{
		glUniform4fv(_fill_object_uniforms_link[0], 4, vp->xyz_to_ogl); // world transform
		glUniform4f (_fill_object_uniforms_link[1], 1.0f, 1.0f, 1.0f, 1.0f / (float)(blitter->RecolPalCount())); // tex_dim

		glActiveTexture(GL_TEXTURE0);
		glBindSampler(0, _sampler[0]);
		glBindTexture(GL_TEXTURE_2D, blitter->RecolPalTexture());
		glUniform1i(_fill_object_uniforms_link[3], 0);

		glActiveTexture(GL_TEXTURE1);
		glBindSampler(1, _sampler[2]);
		glBindTexture(GL_TEXTURE_2D_ARRAY, _atlas_texture);
		glUniform1i(_fill_object_uniforms_link[4], 1);

		glDepthFunc(GL_LESS);

		glBindVertexArray(_fill_object_vertex_format);
		if (_opengl_ver > 0)
		{
			glBindVertexBuffer(0, _model_vertex_buffer, 0, sizeof(ModelVertex));
			glBindVertexBuffer(1, _model_instance_buffer, 0, sizeof(ModelInstance));
			glVertexBindingDivisor(1, 1);

			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _model_index_buffer);
			for (const DrawElementsIndirectCmd &cmd : _object_instance_cmd)
			{
				glDrawElementsInstancedBaseVertexBaseInstance(GL_TRIANGLES, cmd.count, GL_UNSIGNED_INT, (void*)(cmd.firstIndex * sizeof(GLuint)), cmd.instanceCount, cmd.baseVertex, cmd.baseInstance);
			}
//			glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, _object_instance_cmd.data(), (GLsizei)(_object_instance_cmd.size()), sizeof(DrawElementsIndirectCmd));
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		}
		else
		{
			glBindBuffer(GL_ARRAY_BUFFER, _model_vertex_buffer);
			glVertexAttribPointer((GLuint)(_fill_object_attribs_link[0]), 3, GL_FLOAT, GL_FALSE, sizeof(ModelVertex), (void*)(cpp_offsetof(ModelVertex, pos)));
			glVertexAttribPointer((GLuint)(_fill_object_attribs_link[1]), 2, GL_FLOAT, GL_FALSE, sizeof(ModelVertex), (void*)(cpp_offsetof(ModelVertex, tex)));
			glBindBuffer(GL_ARRAY_BUFFER, 0);

			glBindBuffer(GL_ARRAY_BUFFER, _model_instance_buffer);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _model_index_buffer);
			for (int i = 2; i < 7; i++) glVertexAttribDivisor((GLuint)(_fill_object_attribs_link[i]), 1);
			for (const DrawElementsIndirectCmd &cmd : _object_instance_cmd)
			{
				size_t offset = cmd.baseInstance * sizeof(ModelInstance);
				glVertexAttribPointer((GLuint)(_fill_object_attribs_link[2]), 4, GL_FLOAT, GL_FALSE, sizeof(ModelInstance), (void*)(offset + cpp_offsetof(ModelInstance, loc)));
				glVertexAttribPointer((GLuint)(_fill_object_attribs_link[3]), 4, GL_FLOAT, GL_FALSE, sizeof(ModelInstance), (void*)(offset + cpp_offsetof(ModelInstance, mip)));
				glVertexAttribPointer((GLuint)(_fill_object_attribs_link[4]), 4, GL_FLOAT, GL_FALSE, sizeof(ModelInstance), (void*)(offset + cpp_offsetof(ModelInstance, matr[0])));
				glVertexAttribPointer((GLuint)(_fill_object_attribs_link[5]), 4, GL_FLOAT, GL_FALSE, sizeof(ModelInstance), (void*)(offset + cpp_offsetof(ModelInstance, matr[1])));
				glVertexAttribPointer((GLuint)(_fill_object_attribs_link[6]), 4, GL_FLOAT, GL_FALSE, sizeof(ModelInstance), (void*)(offset + cpp_offsetof(ModelInstance, matr[2])));

				glDrawElementsInstancedBaseVertex(GL_TRIANGLES, cmd.count, GL_UNSIGNED_INT, (void*)(cmd.firstIndex * sizeof(uint32)), cmd.instanceCount, cmd.baseVertex);
			}
			for (int i = 2; i < 7; i++) glVertexAttribDivisor((GLuint)(_fill_object_attribs_link[i]), 0);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
			glBindBuffer(GL_ARRAY_BUFFER, 0);
		}
		glBindVertexArray(0);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, 0);
		glBindSampler(0, 0);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
		glBindSampler(1, 0);
	}
	glUseProgram(0);

	glDepthMask(GL_TRUE);
}

/* Fill the shadow map depth */
static void DrawDataDepth()
{
	Blitter_OpenGL *blitter = (Blitter_OpenGL*)(BlitterFactory::GetCurrentBlitter());
	
	glViewport(0, 0, SHADOW_MAP_SIZE >> _shadow_level, SHADOW_MAP_SIZE >> _shadow_level);
	glBindFramebuffer(GL_FRAMEBUFFER, _shadow_frame[_shadow_level]);
	glClear(GL_DEPTH_BUFFER_BIT);
	glDrawBuffer(GL_NONE);
	
	glEnable(GL_POLYGON_OFFSET_FILL);

	glUseProgram(_fill_land_program);
	{
		glUniform4fv(_fill_land_uniforms_link[0], 4, _xyz_to_shadow); // shadow transform
		glUniform4f (_fill_land_uniforms_link[1], 1.0f / IMAGE3D_ATLAS_SIZE_F, 1.0f / IMAGE3D_ATLAS_SIZE_F, 1.0f, 1.0f / (float)(blitter->RecolPalCount())); // tex_dim

		glActiveTexture(GL_TEXTURE0);
		glBindSampler(0, _sampler[0]);
		glBindTexture(GL_TEXTURE_2D, blitter->RecolPalTexture());
		glUniform1i(_fill_land_uniforms_link[3], 0);

		glActiveTexture(GL_TEXTURE1);
		glBindSampler(1, _sampler[2]);
		glBindTexture(GL_TEXTURE_2D_ARRAY, _atlas_texture);
		glUniform1i(_fill_land_uniforms_link[4], 1);
		
		glDepthFunc(GL_LESS);
		glPolygonOffset(1.0f, 4096.0f); // may be 1.0f is even too much

		glBindVertexArray(_fill_land_vertex_format);
		if (_opengl_ver > 0)
		{
			glBindVertexBuffer(0, _land.vertex_buffer, 0, sizeof(TileVertex));
		}
		else
		{
			glBindBuffer(GL_ARRAY_BUFFER, _land.vertex_buffer);
			glVertexAttribPointer((GLuint)(_fill_land_attribs_link[0]), 3, GL_FLOAT, GL_FALSE, sizeof(TileVertex), (void*)(cpp_offsetof(TileVertex, pos[0])));
			glVertexAttribPointer((GLuint)(_fill_land_attribs_link[1]), 4, GL_FLOAT, GL_FALSE, sizeof(TileVertex), (void*)(cpp_offsetof(TileVertex, tex[0])));
			glVertexAttribPointer((GLuint)(_fill_land_attribs_link[2]), 4, GL_FLOAT, GL_FALSE, sizeof(TileVertex), (void*)(cpp_offsetof(TileVertex, loc[0])));
//			glVertexAttribPointer((GLuint)(_fill_land_attribs_link[3]), 2, GL_FLOAT, GL_FALSE, sizeof(TileVertex), (void*)(cpp_offsetof(TileVertex, mip[0])));
			glBindBuffer(GL_ARRAY_BUFFER, 0);
		}
		glMultiDrawArrays(GL_TRIANGLES, _draw_seg_first.data(), _draw_seg_count.data(), (GLsizei)(_draw_seg_first.size()));
		glBindVertexArray(0);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, 0);
		glBindSampler(0, 0);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
		glBindSampler(1, 0);
	}
	glUseProgram(0);

	glUseProgram(_fill_object_program);
	{
		glUniform4fv(_fill_object_uniforms_link[0], 4, _xyz_to_shadow); // shadow transform
		glUniform4f (_fill_object_uniforms_link[1], 1.0f, 1.0f, 1.0f, 1.0f / (float)(blitter->RecolPalCount())); // tex_dim

		glActiveTexture(GL_TEXTURE0);
		glBindSampler(0, _sampler[0]);
		glBindTexture(GL_TEXTURE_2D, blitter->RecolPalTexture());
		glUniform1i(_fill_object_uniforms_link[3], 0);

		glActiveTexture(GL_TEXTURE1);
		glBindSampler(1, _sampler[2]);
		glBindTexture(GL_TEXTURE_2D_ARRAY, _atlas_texture);
		glUniform1i(_fill_object_uniforms_link[4], 1);

		glDepthFunc(GL_LESS);
		glPolygonOffset(1.0f, 4096.0f); // may be 1.0f is even too much

		glBindVertexArray(_fill_object_vertex_format);
		if (_opengl_ver > 0)
		{
			glBindVertexBuffer(0, _model_vertex_buffer, 0, sizeof(ModelVertex));
			glBindVertexBuffer(1, _model_instance_buffer, 0, sizeof(ModelInstance));
			glVertexBindingDivisor(1, 1);

			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _model_index_buffer);
			for (const DrawElementsIndirectCmd &cmd : _depth_instance_cmd)
			{
				glDrawElementsInstancedBaseVertexBaseInstance(GL_TRIANGLES, cmd.count, GL_UNSIGNED_INT, (void*)(cmd.firstIndex * sizeof(GLuint)), cmd.instanceCount, cmd.baseVertex, cmd.baseInstance);
			}
//			glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, _depth_instance_cmd.data(), (GLsizei)(_depth_instance_cmd.size()), sizeof(DrawElementsIndirectCmd));
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		}
		else
		{
			glBindBuffer(GL_ARRAY_BUFFER, _model_vertex_buffer);
			glVertexAttribPointer((GLuint)(_fill_object_attribs_link[0]), 3, GL_FLOAT, GL_FALSE, sizeof(ModelVertex), (void*)(cpp_offsetof(ModelVertex, pos)));
			glVertexAttribPointer((GLuint)(_fill_object_attribs_link[1]), 2, GL_FLOAT, GL_FALSE, sizeof(ModelVertex), (void*)(cpp_offsetof(ModelVertex, tex)));
			glBindBuffer(GL_ARRAY_BUFFER, 0);

			glBindBuffer(GL_ARRAY_BUFFER, _model_instance_buffer);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _model_index_buffer);
			for (int i = 2; i < 7; i++) glVertexAttribDivisor((GLuint)(_fill_object_attribs_link[i]), 1);
			for (const DrawElementsIndirectCmd &cmd : _depth_instance_cmd)
			{
				size_t offset = cmd.baseInstance * sizeof(ModelInstance);
				glVertexAttribPointer((GLuint)(_fill_object_attribs_link[2]), 4, GL_FLOAT, GL_FALSE, sizeof(ModelInstance), (void*)(offset + cpp_offsetof(ModelInstance, loc)));
				glVertexAttribPointer((GLuint)(_fill_object_attribs_link[3]), 4, GL_FLOAT, GL_FALSE, sizeof(ModelInstance), (void*)(offset + cpp_offsetof(ModelInstance, mip)));
				glVertexAttribPointer((GLuint)(_fill_object_attribs_link[4]), 4, GL_FLOAT, GL_FALSE, sizeof(ModelInstance), (void*)(offset + cpp_offsetof(ModelInstance, matr[0])));
				glVertexAttribPointer((GLuint)(_fill_object_attribs_link[5]), 4, GL_FLOAT, GL_FALSE, sizeof(ModelInstance), (void*)(offset + cpp_offsetof(ModelInstance, matr[1])));
				glVertexAttribPointer((GLuint)(_fill_object_attribs_link[6]), 4, GL_FLOAT, GL_FALSE, sizeof(ModelInstance), (void*)(offset + cpp_offsetof(ModelInstance, matr[2])));

				glDrawElementsInstancedBaseVertex(GL_TRIANGLES, cmd.count, GL_UNSIGNED_INT, (void*)(cmd.firstIndex * sizeof(uint32)), cmd.instanceCount, cmd.baseVertex);
			}
			for (int i = 2; i < 7; i++) glVertexAttribDivisor((GLuint)(_fill_object_attribs_link[i]), 0);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
			glBindBuffer(GL_ARRAY_BUFFER, 0);
		}
		glBindVertexArray(0);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, 0);
		glBindSampler(0, 0);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
		glBindSampler(1, 0);
	}
	glUseProgram(0);

	glDisable(GL_POLYGON_OFFSET_FILL);
	
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

/* Update the viewport transform matrices */
static void UpdateViewportTransform(ViewPort *vp)
{
	float zm = (float)(1 << vp->zoom);
	float zoom = ZOOM_LVL_BASE / zm;

	float vl = vp->virtual_left;
	float vt = vp->virtual_top;
	float vw = vp->virtual_width;
	float vh = vp->virtual_height;
/*
	// original world -> viewport transform matrix:
///	{
///		-2.0f * zoom, +2.0f * zoom,         0.0f, -vl / z,
///		+1.0f * zoom, +1.0f * zoom, -1.0f * zoom, -vt / z,
///		        ZZZZ,         ZZZZ,         ZZZZ,    0.0f,
///		        0.0f,         0.0f,         0.0f,    1.0f,
///	}
	matrSetRotateYXZ(vp->xyz_to_vp, RAD(60.0f), RAD(0.0f), RAD(135.0f));
//	BASE_SCALE = -2.0f / _xyz_to_vp[0];
	matrPreScale(vp->xyz_to_vp, BASE_SCALE * zoom, -BASE_SCALE * zoom, -BASE_SCALE * zoom);
	matrPreTranslate(vp->xyz_to_vp, -vl / zm, -vt / zm, 0.0f);

	// we should take care about additional Z scale saved in TILE_HEIGHT_SCALE
//	TILE_HEIGHT_SCALE = (-1.0 * zoom / _xyz_to_vp[6]);
//	TILE_HEIGHT_ACT = TILE_HEIGHT_SCALE * TILE_HEIGHT;
	matrInverse(vp->vp_to_xyz, vp->xyz_to_vp);
*/
	float zaxis[3] = { 1.0f, 0.0f, 0.0f };
	vectRotateY(zaxis, RAD(vp->spin_y));
	vectRotateZ(zaxis, RAD(vp->spin_z + ((vp->spin_y == 90.0f) ? 180.0f : 0.0f)));

	float xaxis[3];
	float up[3] = { 0.0f, 0.0f, 1.0f };
	vectCross3(xaxis, up, zaxis);
	vectNormalize3(xaxis);

	float yaxis[3];
	vectCross3(yaxis, zaxis, xaxis);

	vectCopy3(&vp->xyz_to_vp[0], xaxis);
	vectCopy3(&vp->xyz_to_vp[4], yaxis);
	vectCopy3(&vp->xyz_to_vp[8], zaxis);

	float pos[3] ={ vp->cx, vp->cy, vp->cz };
	vp->xyz_to_vp[3]  = -vectDot3(xaxis, pos);
	vp->xyz_to_vp[7]  = -vectDot3(yaxis, pos);
	vp->xyz_to_vp[11] = -vectDot3(zaxis, pos);
	vp->xyz_to_vp[15] = 1.0f;

	matrPreScale(vp->xyz_to_vp, -BASE_SCALE * zoom, -BASE_SCALE * zoom, -BASE_SCALE * zoom);
	matrInverse43(vp->vp_to_xyz, vp->xyz_to_vp);

	///

	float ww = vp->width;
	float wh = vp->height;

	float vp_to_ogl[16];
	if (_settings_client.gui.view3d_use_perspective)
	{
		float offset = 1000.0f;
		float scale = 1.0f / 3.0f;
		/* setup a simple projetion matrix, with a hope of the Z range will be enought */
		matrSetFrustum(vp_to_ogl, -ww * scale / 2.0f, ww * scale / 2.0f, wh * scale / 2.0f, -wh * scale / 2.0f, 0.0f + offset, 8000.0f + offset);
		matrTranslate(vp_to_ogl, 0.0f, 0.0f, -4000.0f + offset);
	}
	else
	{
		/* compute depth range from the view port bounds */
		float z_tp[4] ={ 0.0f, -wh / 2.0f, 0.0f, 1.0f };
		float z_bp[4] ={ 0.0f, +wh / 2.0f, 0.0f, 1.0f };

		z_tp[2] = -(mval(vp->vp_to_xyz, 0, 2) * z_tp[0] +
					mval(vp->vp_to_xyz, 1, 2) * z_tp[1] +
					mval(vp->vp_to_xyz, 3, 2) * z_tp[3]) / mval(vp->vp_to_xyz, 2, 2);

		z_bp[2] = -(mval(vp->vp_to_xyz, 0, 2) * z_bp[0] +
					mval(vp->vp_to_xyz, 1, 2) * z_bp[1] +
					mval(vp->vp_to_xyz, 3, 2) * z_bp[3] - TILE_HEIGHT_ACT * 256.0f) / mval(vp->vp_to_xyz, 2, 2);

		/* increase depth range to skip roundoff errors */
		float z_n = z_bp[2] + 16.0f;
		float z_f = z_tp[2] - 16.0f;
		matrSetOrtho(vp_to_ogl, -ww / 2.0f, ww / 2.0f, wh / 2.0f, -wh / 2.0f, -z_n, -z_f);
	}

	///

	/* matrix for drawing the world */
	matrMul(vp->xyz_to_ogl, vp_to_ogl, vp->xyz_to_vp);
	matrInverse44(vp->ogl_to_xyz, vp->xyz_to_ogl);
}

/* Called for the 3D viewport position update */
void SetViewportPosition3D(ViewportData *vp)
{
	if (vp->follow_vehicle != INVALID_VEHICLE)
	{
		VehicleData *v = GetVehCache(vp->follow_vehicle);
		vp->cx = v->posf[0];
		vp->cy = v->posf[1];
		vp->cz = v->posf[2];

		// add floating spinning?

		UpdateViewportTransform(vp);
		return;
	}

	float ww = vp->virtual_width;
	float wh = vp->virtual_height;
	float x = vp->dest_scrollpos_x + ww / 2.0f;
	float y = vp->dest_scrollpos_y + wh / 2.0f;
	vp->cx = (y * 2 - x) / (float)(1 << (2 + ZOOM_LVL_SHIFT));
	vp->cy = (y * 2 + x) / (float)(1 << (2 + ZOOM_LVL_SHIFT));
	vp->cz = 0.0f;

	UpdateViewportTransform(vp);
}

/* Called for the 3D viewport scrolling */
void ScrollViewport3D(ViewportData *vp)
{
	int delta_x = vp->dest_scrollpos_x - vp->scrollpos_x;
	int delta_y = vp->dest_scrollpos_y - vp->scrollpos_y;
	if (!delta_x && !delta_y) return;

	float zm = (float)(1 << vp->zoom);
	float zoom = ZOOM_LVL_BASE / zm;

	if (_middle_button_down)
	{
		float dx = (delta_x / zm) / 5.0f;
		float dy = (delta_y / zm) / 5.0f;

		vp->spin_z = vp->spin_z + dx;
		vp->spin_y = Clamp(vp->spin_y + dy, 15.0f, 90.0f);

		vp->dest_scrollpos_x = vp->scrollpos_x;
		vp->dest_scrollpos_y = vp->scrollpos_y;

		UpdateViewportTransform(vp);
		return;
	}

	float dx = delta_x / zm;
	float dy = delta_y / zm;

	float dir_xyz0[3];
	float dir_xyz1[3];
	float dir_vp0[3] = { dx, dy, 0.0f };
	float dir_vp1[3] = { dx, dy, 1.0f };
	matrApply33(dir_xyz0, vp->vp_to_xyz, dir_vp0);
	matrApply33(dir_xyz1, vp->vp_to_xyz, dir_vp1);

	float sub[3];
	float tmp[3];
	float k = (-dir_xyz0[2] / (dir_xyz1[2] - dir_xyz0[2]));
	vectSub3(sub, dir_xyz1, dir_xyz0);
	vectScale3(tmp, sub, k);
	vectAdd3(sub, dir_xyz0, tmp);

	vp->cx = sub[0];
	vp->cy = sub[1];
	vp->cz = 0.0f;

	float ww = vp->virtual_width;
	float wh = vp->virtual_height;
	int x = (int)((vp->cy - vp->cx) * 2.0f * ZOOM_LVL_BASE - ww / 2.0f);
	int y = (int)((vp->cy + vp->cx - vp->cz) * ZOOM_LVL_BASE - wh / 2.0f);
	
	vp->dest_scrollpos_x = x;
	vp->dest_scrollpos_y = y;

	UpdateViewportTransform(vp);
}

/* Called to remap screen coords to the XYZ coords, with respect to tiles height and foundations */
Point MapInvCoords3D(const ViewPort *vp, int x, int y, bool clamp_to_map)
{
	float ww2 = (float)(vp->width) / 2.0f;
	float wh2 = (float)(vp->height) / 2.0f;

	/* make a ray in the screen space */
	float pt0[4] = { (x - ww2) / ww2, -(y - wh2) / wh2, 0.0f, 1.0f };
	float pt1[4] = { (x - ww2) / ww2, -(y - wh2) / wh2, 1.0f, 1.0f };

	/* transform this ray from a screen space to XYZ space */
	float pos0[4];
	float pos1[4];
	matrApply44(pos0, vp->ogl_to_xyz, pt0);
	matrApply44(pos1, vp->ogl_to_xyz, pt1);
	vectScale3(pos0, pos0, 1.0f / pos0[3]); // XYZ pos of screen point depth 0.0
	vectScale3(pos1, pos1, 1.0f / pos1[3]); // XYZ pos of screen point depth 1.0

	/* ray direction */
	float dir[3];
	vectSub3(dir, pos1, pos0);
	vectNormalize3(dir);

	 /* compute intersection with max plane */
	float height = _settings_game.construction.max_heightlevel * TILE_HEIGHT_ACT;

	float cur[3];
	float k = -((pos0[2] - height) / dir[2]);
	vectScale3(cur, dir, k);
	vectAdd3(cur, pos0, cur);

	/* starting point of the search */
	int tx = (int)(TileXF(cur[0]));
	int ty = (int)(TileYF(cur[1]));

	// We MUST search by the segments bbox first, to speed up, but i am very lazy...

	/* search for the actual point, diagonal foundations can leak through in some cases, but it's ok */
	int edges[2] = { dir[0] > 0.0f ? 1 : 0, dir[1] > 0.0f ? 1 : 0 };
	while (true)
	{
		/* map bounds check */
		if ((tx < 0) || (tx >= (int)(_land.size_x))) break;
		if ((ty < 0) || (ty >= (int)(_land.size_y))) break;

		/* current tile */
		TileIndex ti = TileXY(tx, ty);

		int sx = (tx / LAND_SEG_RES);
		int sy = (ty / LAND_SEG_RES);
		int si = sy * LAND_SEGS_X + sx;

		int stx = (tx - sx * LAND_SEG_RES);
		int sty = (ty - sy * LAND_SEG_RES);
		int sti = sty * LAND_SEG_RES + stx;
		LandTile *t = _land.tiles[ti];

		/* for two faces in the tile */
		for (int f = 0; f < 2; f++)
		{
			TileVertex *v = &_land.vertex[(si * LAND_SEG_TILE_COUNT + sti) * 6 + f * 3]; // use computed vertex normal as plane normal
			if (vectDot3(FP(v[0].nrm), dir) > 0.0f) continue; // skip backfaces

			float d = -vectDot3(FP(v[0].nrm), FP(v[0].pos[0]));
			float k = -(vectDot3(pos0, FP(v[0].nrm)) + d) / vectDot3(dir, FP(v[0].nrm));

			/* intersection of the ray with a tile triangle plane */
			float tgt[3];
			vectScale3(tgt, dir, k);
			vectAdd3(tgt, pos0, tgt);

			/* check if we found an actual tile with the intersection point */
			float fx = tgt[0] - tx * TILE_SIZE_F;
			float fy = tgt[1] - ty * TILE_SIZE_F;
			int face = t->layout ? ((fx > fy) ? 0 : 1) : (((TILE_SIZE_F - fx) > fy) ? 0 : 1);
			if ((face == f) && (fx >= 0.0f) && (fy >= 0.0f) && (fx <= TILE_SIZE_F) && (fy <= TILE_SIZE_F))
			{
				Point ret;
				ret.x = Clamp((int)(tgt[0]), tx * TILE_SIZE, (tx + 1) * TILE_SIZE - 1);
				ret.y = Clamp((int)(tgt[1]), ty * TILE_SIZE, (ty + 1) * TILE_SIZE - 1);
				return ret;
			}
		}
		
		/* 2D intersection with current tile edges */
		float x2 = (tx + edges[0]) * TILE_SIZE;
		float y2 = (ty + edges[1]) * TILE_SIZE;
		float kx = (x2 - cur[0]) / dir[0];
		float ky = (y2 - cur[1]) / dir[1];

		/* move by x or y? */
		bool advance_y = (fabsf(kx) > fabsf(ky));

		/* advance a current point */
		if (advance_y)
		{
			cur[0] += dir[0] * ky;
			cur[1] += dir[1] * ky;
			cur[2] += dir[2] * ky;
		}
		else
		{
			cur[0] += dir[0] * kx;
			cur[1] += dir[1] * kx;
			cur[2] += dir[2] * kx;
		}
		
		/* check for a current z */
		{
			float fx = cur[0] - tx * TILE_SIZE_F;
			float fy = cur[1] - ty * TILE_SIZE_F;

			uint32 face = t->layout ? ((fx > fy) ? 0 : 1) : (((TILE_SIZE_F - fx) > fy) ? 0 : 1);

			TileVertex *v = &_land.vertex[(si * LAND_SEG_TILE_COUNT + sti) * 6 + face * 3];
			float d = -vectDot3(FP(v[0].nrm), FP(v[0].pos[0]));
			float z = -(d + v[0].nrm.x * cur[0] + v[0].nrm.y * cur[1]) / v[0].nrm.z;
			if (z >= cur[2]) break; // we are leaked throught (possibly foundation)
		}

		/* advance to the next tile */
		if (advance_y)
		{
			ty += ((dir[1] * ky) > 0) ? +1 : -1;
		}
		else
		{
			tx += ((dir[0] * kx) > 0) ? +1 : -1;
		}
	}

	Point ret;
	/* return a current point on the tile edge */
	ret.x = Clamp((int)(cur[0]), tx * TILE_SIZE, (tx + 1) * TILE_SIZE - 1);
	ret.y = Clamp((int)(cur[1]), ty * TILE_SIZE, (ty + 1) * TILE_SIZE - 1);
	return ret;
}

/* Called to ckeck clicks on the viewport vehicles */
Vehicle *CheckClickOnVehicle3D(const ViewPort *vp, int x, int y)
{
	float ww2 = (float)(vp->width)  / 2.0f;
	float wh2 = (float)(vp->height) / 2.0f;

	/* make a ray in the screen space */
	float pt0[4] = { (x - ww2) / ww2, -(y - wh2) / wh2, 0.0f, 1.0f };
	float pt1[4] = { (x - ww2) / ww2, -(y - wh2) / wh2, 1.0f, 1.0f };

	/* transform this ray from a screen space to XYZ space */
	float pos0[4];
	float pos1[4];
	matrApply44(pos0, vp->ogl_to_xyz, pt0);
	matrApply44(pos1, vp->ogl_to_xyz, pt1);
	vectScale3(pos0, pos0, 1.0f / pos0[3]); // XYZ pos of screen point of depth 0.0
	vectScale3(pos1, pos1, 1.0f / pos1[3]); // XYZ pos of screen point of depth 1.0

	/* ray direction */
	float dir[3];
	vectSub3(dir, pos1, pos0);
	vectNormalize3(dir);

	Vehicle *v;
	Vehicle *best_v = nullptr;
	float best_dist = FLT_MAX;
	FOR_ALL_VEHICLES(v)
	{
		if (!IsNormalVehicle(v)) continue;
		if (v->vehstatus & VS_HIDDEN) continue;

		VehicleData *c = GetVehCache((uint32)(vehicle_index));

		/* we need to transform the ray into a vehicle space first */
		float pos_t[3];
		float dir_t[3];
		matrApply33(pos_t, c->matr_inv, pos0);
		matrApplyNoTrans33(dir_t, c->matr_inv, dir);

		/* test the bounder hit */
		if (!rayTestAABB(pos_t, dir_t, c->bbmin_ref, c->bbmax_ref)) continue;

		/* is it the closest vehicle? */

		float tmp[3];
		vectSub3(tmp, pos0, c->posf);
		float dist = vectLength3(tmp);
		if (dist >= best_dist) continue;
		best_dist = dist;
		best_v = v;
	}
	return best_v;
}

/* Project the sign on screen */
static float ProjectSign(const ViewPort *vp, const LandSign *sg, int &x, int &y, int &w, int &h)
{
	Point pt = RemapCoords(sg->pos[0], sg->pos[1], sg->pos[2]);
	float z = (float)(pt.y - sg->sign->top); // extract sign Z
	z /= (sg->type == VPST_EFFECT) ? (float)(ZOOM_LVL_BASE) : (float)(ZOOM_LVL_BASE / 2);

	float pos[4] ={ (float)(sg->pos[0]), (float)(sg->pos[1]), (sg->pos[2] + z) * TILE_HEIGHT_SCALE, 1.0f };
	if (!frustumTestPoint(_frustum, pos)) return FLT_MAX;

	float prj[4];
	matrApply44(prj, vp->xyz_to_ogl, pos);
	vectScale3(prj, prj, 1.0f / prj[3]);

	Blitter_OpenGL *blitter = (Blitter_OpenGL*)(BlitterFactory::GetCurrentBlitter());
	blitter->SetOverlayZ(prj[2]);

	bool small = (vp->zoom >= ZOOM_LVL_OUT_16X);

	int sign_height     = VPSM_TOP + FONT_HEIGHT_NORMAL + VPSM_BOTTOM;
	int sign_half_width = (small ? sg->sign->width_small : sg->sign->width_normal) / 2;

	float w2 = (float)(vp->width  / 2.0f);
	float h2 = (float)(vp->height / 2.0f);
	int px = (int)(+prj[0] * w2 + w2 + 0.5f);
	int py = (int)(-prj[1] * h2 + h2 + 0.5f);

	x = px - sign_half_width;
	y = py;
	h = VPSM_TOP + (small ? FONT_HEIGHT_SMALL : FONT_HEIGHT_NORMAL) + VPSM_BOTTOM;
	w = small ? sg->sign->width_small : sg->sign->width_normal;
	return prj[2];
}

/* Draw the sign on screen */
static void DrawSign(const ViewPort *vp, const LandSign *sg)
{
	bool small = (vp->zoom >= ZOOM_LVL_OUT_16X);
	bool trans = (IsTransparencySet(TO_SIGNS) && (sg->type != VPST_SIGN));

	int x, y, h, w;
	float z = ProjectSign(vp, sg, x, y, w, h);
	if (z >= FLT_MAX) return;

	Blitter_OpenGL *blitter = (Blitter_OpenGL*)(BlitterFactory::GetCurrentBlitter());
	blitter->SetOverlayZ(z);

	TextColour c = TC_BLACK;
	if (trans) c = (TextColour)(_colour_gradient[sg->colour][6] | TC_IS_PALETTE_COLOUR);

	if (!small)
	{
		if (!trans && (sg->colour != INVALID_COLOUR)) DrawFrameRect(x, y, x + w, y + h, sg->colour, IsTransparencySet(TO_SIGNS) ? FR_TRANSPARENT : FR_NONE);
		DrawLayoutLine(*(sg->layout.front()), y + VPSM_TOP, x + VPSM_LEFT, x + w - 1 - VPSM_RIGHT, SA_HOR_CENTER, false, false);
	}
	else
	{
		int shadow_offset = 0;
		if (!trans && (sg->colour != INVALID_COLOUR)) DrawFrameRect(x, y, x + w, y + h, sg->colour, IsTransparencySet(TO_SIGNS) ? FR_TRANSPARENT : FR_NONE);
		if (sg->layout_small_shadow.size())
		{
			shadow_offset = 1;
			DrawLayoutLine(*(sg->layout_small_shadow.front()), y + VPSM_TOP, x + shadow_offset + VPSM_LEFT, x + shadow_offset + w - 1 - VPSM_RIGHT, SA_HOR_CENTER, false, false);
		}
		DrawLayoutLine(*(sg->layout_small.front()), y + VPSM_TOP - shadow_offset, x + VPSM_LEFT, x + w - 1 - VPSM_RIGHT, SA_HOR_CENTER, false, false);
	}
}

/* Check if the sign intersects with screen coords */
static float TestSign(const ViewPort *vp, const LandSign *sg, int sx, int sy)
{
	int x, y, h, w;
	float depth = ProjectSign(vp, sg, x, y, w, h);
	if ((sx >= x) && (sx <= (x + w)) && (sy >= y) && (sy <= (y + h))) return depth;
	return FLT_MAX;
}

/* Compute a 6 frustum planes from the matrix */
void ComputeFrustumPlanes(float frustum[6][4], const float *m)
{
	matrClipPlane(frustum[0], m, _box_points[0], _box_points[3], _box_points[4]); // Left
	matrClipPlane(frustum[1], m, _box_points[1], _box_points[5], _box_points[2]); // Right
	matrClipPlane(frustum[2], m, _box_points[0], _box_points[4], _box_points[1]); // Top
	matrClipPlane(frustum[3], m, _box_points[2], _box_points[7], _box_points[3]); // Bottom
	matrClipPlane(frustum[4], m, _box_points[0], _box_points[1], _box_points[2]); // Near
	matrClipPlane(frustum[5], m, _box_points[4], _box_points[6], _box_points[5]); // Far
}

/* Called to check clicks on the viewport signs and labels */
bool CheckClickOnViewportSign3D(const ViewPort *vp, int x, int y)
{
	/* compute viewing frustum planes */
	ComputeFrustumPlanes(_frustum, vp->ogl_to_xyz);

	/* list of the land segments to be checked */
	for (size_t i = 0; i < _land.segs.size(); i++)
	{
		LandSeg *s = &_land.segs[i];
		if (!frustumTestAABB(_frustum, s->bbmin, s->bbmax)) continue;
		_draw_seg.push_back(s);
	}

	/* things to ckeck */
	bool show_stations = HasBit(_display_opt, DO_SHOW_STATION_NAMES) && _game_mode != GM_MENU;
	bool show_waypoints = HasBit(_display_opt, DO_SHOW_WAYPOINT_NAMES) && _game_mode != GM_MENU;
	bool show_towns = HasBit(_display_opt, DO_SHOW_TOWN_NAMES) && _game_mode != GM_MENU;
	bool show_signs = HasBit(_display_opt, DO_SHOW_SIGNS) && !IsInvisibilitySet(TO_SIGNS);
	bool show_competitors = HasBit(_display_opt, DO_SHOW_COMPETITOR_SIGNS);

	/* project the signs to the screen, and find a nearest Z intersection */
	float best_depth = FLT_MAX;
	LandSign *best_sign = nullptr;
	for (size_t i = 0; i < _draw_seg.size(); i++)
	{
		LandSeg *s = _draw_seg[i];
		for (size_t k = 0; k < s->labels.size(); k++)
		{
			LandSign *sg = &s->labels[k];
			switch (sg->type)
			{
			case VPST_STATION:
				{
					if (!show_stations) continue;
					const BaseStation *st = BaseStation::Get(sg->id.station);
					if (!show_competitors && _local_company != st->owner && st->owner != OWNER_NONE) continue;
				}
				break;

			case VPST_WAYPOINT:
				{
					if (!show_waypoints) continue;
					const BaseStation *st = BaseStation::Get(sg->id.station);
					if (!show_competitors && _local_company != st->owner && st->owner != OWNER_NONE) continue;
				}
				break;

			case VPST_TOWN:
				if (!show_towns) continue;
				break;

			default:
				NOT_REACHED();
			}
			float depth = TestSign(vp, sg, x, y);
			if (best_depth <= depth) continue;
			best_depth = depth;
			best_sign = sg;
		}
		if (show_signs)
		{
			for (auto id = s->signs.begin(); id != s->signs.end(); id++)
			{
				LandSign *sg = &_signs[*id];
				Sign *si = Sign::GetIfValid(sg->id.sign);
				if (!si) continue;

				if (!show_competitors && _local_company != si->owner && si->owner != OWNER_DEITY) continue;
				float depth = TestSign(vp, sg, x, y);
				if (best_depth <= depth) continue;
				best_depth = depth;
				best_sign = sg;
			}
		}
	}
	_draw_seg.clear();

	if (!best_sign) return false; // no signs intersects
	switch (best_sign->type) // perform required action based on the sign type
	{
	case VPST_STATION:  ShowStationViewWindow(best_sign->id.station); break;
	case VPST_WAYPOINT: ShowWaypointWindow(Waypoint::From(BaseStation::Get(best_sign->id.station))); break;
	case VPST_TOWN:     ShowTownViewWindow(best_sign->id.town); break;
	case VPST_SIGN:     HandleClickOnSign(Sign::Get(best_sign->id.sign)); break;
	default:
		NOT_REACHED();
	}
	return true;
}

/* Called to prepare for the new frame */
void DrawPrepare3D()
{
	if (!_initialized) // initialize our subsystem if not yet initialized
	{
		_initialized = 1;
		LoadConfigFiles();

		Blitter_OpenGL *blitter = (Blitter_OpenGL*)(BlitterFactory::GetCurrentBlitter());
		blitter->SetAtlasTableDim(IMAGE3D_ATLAS_SIZE);
	}

	/* signs transparancy setting tracking */
	{
		bool signs_transparent = IsTransparencySet(TO_SIGNS);
		if (signs_transparent != _last_signs_transparent_set)
		{
			_last_signs_transparent_set = signs_transparent;
			UpdateSigns();
		}
	}

	/* objects transparancy settings tracking */
	{
		uint32 objects_transparent = _transparency_opt & ((1 << TO_TREES) | (1 << TO_HOUSES) | (1 << TO_INDUSTRIES) | (1 << TO_BUILDINGS) | (1 << TO_BRIDGES) | (1 << TO_STRUCTURES) | (1 << TO_CATENARY));
		if ((_game_mode != GM_MENU) && (objects_transparent != _last_objects_transparent_set))
		{
			_last_objects_transparent_set = objects_transparent;
			MarkLandDirty();
		}
	}

	/* objects visiblity settings tracking */
	{
		uint32 objects_invisibility = _invisibility_opt & ((1 << TO_TREES) | (1 << TO_HOUSES) | (1 << TO_INDUSTRIES) | (1 << TO_BUILDINGS) | (1 << TO_BRIDGES) | (1 << TO_STRUCTURES) | (1 << TO_CATENARY));
		if ((_game_mode != GM_MENU) && (objects_invisibility != _last_objects_invisibility_set))
		{
			_last_objects_invisibility_set = objects_invisibility;
			MarkLandDirty();
		}
	}

	/* shadows settings tracking */
	if ((_settings_client.gui.view3d_use_shadows != _use_shadows_set) || (_settings_client.gui.view3d_shadows_res != _shadows_res_set))
	{
		_use_shadows_set = _settings_client.gui.view3d_use_shadows;
		_shadows_res_set = _settings_client.gui.view3d_shadows_res;
		ResetShadowResources();
	}

	/* multisample setting tracking */
	if (_multisample_set != _settings_client.gui.opengl_multisample)
	{
		_multisample_set = _settings_client.gui.opengl_multisample;
		_use_shadows_set = !_use_shadows_set;
	}

	/* set the sun vector +- as in original viewport */
	{
		float sun[3] = { -1.0f, 0.0f, 0.0f };
		vectRotateY(sun, RAD(55.0f));
		vectRotateZ(sun, RAD(45.0f + 11.25f));
		vectCopy3(_sun_dir, sun);
	}

	UpdateLand();
	UpdateVehicles();
//	UpdateEffects();

	UpdateImages(_atlas_texture, _atlas_layers);
	UpdateModels(_model_vertex_buffer, _model_index_buffer);

	UpdateLandProgram();
	UpdateLandSelProgram();
	UpdateObjectProgram();
	UpdateLandObjectProgram();
	UpdateFillLandProgram();
	UpdateFillObjectProgram();
	UpdateSamplers();
	UpdateShadowFrame();
}

/* Called to prepare drawing into a viewport */
void DrawPrepareViewport3D(const ViewPort *vp)
{
	/* compute viewing frustum planes */
	ComputeFrustumPlanes(_frustum, vp->ogl_to_xyz);

	/* list of the land segments to draw */
	for (size_t i = 0; i < _land.segs.size(); i++)
	{
		LandSeg *s = &_land.segs[i];
		if (!frustumTestAABB(_frustum, s->bbmin, s->bbmax)) continue;
		_draw_seg.push_back(s);
	}

	/* list of the vehicles to draw */
	{
		Vehicle *v;
		FOR_ALL_VEHICLES(v)
		{
			if (!IsNormalVehicle(v)) continue;
			if (v->vehstatus & VS_HIDDEN) continue;

			VehicleData *c = GetVehCache((uint32)(vehicle_index));
			if (c->linked && !frustumTestAABB(_frustum, c->bbmin, c->bbmax)) continue;
			_draw_veh.push_back(c);
		}
	}

	/* update the vehicles draw cache */
	for (VehicleData *c : _draw_veh)
	{
		c->draw_cache.clear();
		for (DrawLink &draw : c->draw)
		{
			if (draw.link == INVALID_LINK) continue;

			const SpriteLink *sl = GetSpriteLink(draw.link);
			uint32 mask = (draw.sprite & (1 << TRANSPARENT_BIT)) ? MODEL_CACHE_MASK_TRANSP : MODEL_CACHE_MASK_SOLID;
			for (const ObjectLink &ol : sl->objects)
			{
				const ImageEntry *img = GetImageEntry(ol.image);

				c->draw_cache.emplace_back();
				ModelInstanceCache &cache = c->draw_cache.back();
				ModelInstance *inst = &cache.data;

				AllocInstancePool(ol.model, ol.shade);
				cache.model = ol.model;
				cache.mask = mask;
				cache.zoom = 0xFF;

				inst->loc.px = (float)(img->offs_x) / IMAGE3D_ATLAS_SIZE_F;
				inst->loc.py = (float)(img->offs_y) / IMAGE3D_ATLAS_SIZE_F;
				inst->loc.sx = (float)(img->size_x) / IMAGE3D_ATLAS_SIZE_F;
				inst->loc.sy = (float)(img->size_y) / IMAGE3D_ATLAS_SIZE_F;

				inst->mip.mip_x = IMAGE3D_ATLAS_SIZE_F / (float)(img->size_x);
				inst->mip.mip_y = IMAGE3D_ATLAS_SIZE_F / (float)(img->size_y);
				inst->mip.layer = (float)(img->layer);
				inst->mip.pal   = (float)(draw.pal) + 0.5f;

				inst->sun = 0;

				float matr[16];
				matrMul(matr, c->matr, ol.matr);
				vectCopy4(FP(inst->matr[0]), &matr[0]);
				vectCopy4(FP(inst->matr[1]), &matr[4]);
				vectCopy4(FP(inst->matr[2]), &matr[8]);
			}
		}
	}
	if (!_use_shadows_set) return; // if no shadows, we are done

	/* compute shadows matrix */
	{
		float zaxis[3];
		vectNeg3(zaxis, _sun_dir);

		float xaxis[3];
		float up[3] ={ 0.0f, 0.0f, 1.0f };
		vectCross3(xaxis, up, zaxis);
		vectNormalize3(xaxis);

		float yaxis[3];
		vectCross3(yaxis, zaxis, xaxis);

		float view[16];
		matrSetIdentity(view);
		vectCopy3(&view[0], xaxis);
		vectCopy3(&view[4], yaxis);
		vectCopy3(&view[8], zaxis);
		matrPreScale(view, -1.0f, -1.0f, -1.0f);

		float ext_min[3];
		float ext_max[3];
		vectSet3(ext_min, +FLT_MAX);
		vectSet3(ext_max, -FLT_MAX);
		for (size_t i = 0; i < _draw_seg.size(); i++)
		{
			LandSeg *s = _draw_seg[i];
			for (int p = 0; p < 8; p++)
			{
				float pos[3];
				for (int n = 0; n < 3; n++) pos[n] = (_box_points[p][n] > 0.0f) ? s->bbmax[n] : s->bbmin[n];

				float pos_t[3];
				matrApplyNoTrans33(pos_t, view, pos);
				vectMin3(ext_min, ext_min, pos_t);
				vectMax3(ext_max, ext_max, pos_t);
			}
		}

		float pos_t[3];
		vectAdd3(pos_t, ext_min, ext_max);
		vectScale3(pos_t, pos_t, 0.5f);

		view[3]  = -pos_t[0];
		view[7]  = -pos_t[1];
		view[11] = -pos_t[2];
		view[15] = 1.0f;

		float ww = (ext_max[0] - ext_min[0]);
		float wh = (ext_max[1] - ext_min[1]);
		float z_n = -(ext_max[2] - ext_min[2]) / 2.0f - 16.0f;
		float z_f = +(ext_max[2] - ext_min[2]) / 2.0f + 16.0f;
		int inc = Clamp(_settings_client.gui.view3d_shadows_res_inc, 0, 3);
		_shadow_level = min(FindLastBit((int)(SHADOW_MAP_SIZE / max(vp->width, vp->height)) >> inc), 4);

		float proj[16];
		matrSetOrtho(proj, -ww / 2.0f, ww / 2.0f, wh / 2.0f, -wh / 2.0f, z_n, z_f);
		matrMul(_xyz_to_shadow, proj, view);

		matrCopy(_xyz_to_shadow_tex, _xyz_to_shadow);
		matrPreScale(_xyz_to_shadow_tex, 0.5f, 0.5f, 0.5f);
		matrPreTranslate(_xyz_to_shadow_tex, 0.5f, 0.5f, 0.5f);
	}

	float shadow_to_xyz[16];
	matrInverse43(shadow_to_xyz, _xyz_to_shadow);

	/* the landscape can't self shade (at the selected sun vector), so, draw only a view segments */
	SelectLandDrawData(false);

	/* compute shadows frustum planes */
	float shadow_frustum[6][4];
	ComputeFrustumPlanes(shadow_frustum, shadow_to_xyz);

	static std::vector<LandSeg*> tmp_draw_seg;
	tmp_draw_seg.clear();

	/* list of the land segments to select objects from */
	_draw_seg.swap(tmp_draw_seg);
	{
		for (size_t i = 0; i < _land.segs.size(); i++)
		{
			LandSeg *s = &_land.segs[i];
			if (!frustumTestAABB(shadow_frustum, s->bbmin, s->bbmax)) continue;
			_draw_seg.push_back(s);
		}
		AddObjectsDrawData(vp, true, _settings_client.gui.view3d_shadows_from_transp);
	}
	_draw_seg.swap(tmp_draw_seg);

	/* draw the sahdow map */
	MakeInstanceData(false);
	DrawDataDepth();
}

/* Called to draw a 3d viewport */
void DrawViewport3D(const ViewPort *vp)
{
	SelectLandDrawData(true);

	/* draw a solid objects and a landscape */
	AddObjectsDrawData(vp, true, false);
	MakeInstanceData(true);
	DrawDataColor(vp);

	/* draw a transparent objects */
	AddObjectsDrawData(vp, false, true);
	MakeInstanceData(true);
	DrawDataTransp(vp);

	/// debug stuff
/**/
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	glMatrixMode(GL_PROJECTION);
	glLoadTransposeMatrixf(vp->xyz_to_ogl);
/**/
/*
	for (size_t i = 0; i < _draw_seg.size(); i++)
	{
		LandSeg *s = _draw_seg[i];
//		Colour c = _cur_palette.palette[_string_colourmap[color & 0xF]]; color++;

		uint32 ts = (uint32)(s->index    ) * LAND_SEG_TILE_COUNT;
		uint32 tf = (uint32)(s->index + 1) * LAND_SEG_TILE_COUNT;

		glBegin(GL_LINES);
		{
			glColor4f(0.5f, 0.5f, 0.5f, 1.0f);
			for (size_t t = ts; t < tf; t++)
			{
				for (int k = 0; k < 2; k++)
				{
					TileVertex *v = &_land.vertex[t * 6 + k * 3];

					glVertex3f(v[0].pos.x, v[0].pos.y, v[0].pos.z);
					glVertex3f(v[1].pos.x, v[1].pos.y, v[1].pos.z);

					glVertex3f(v[1].pos.x, v[1].pos.y, v[1].pos.z);
					glVertex3f(v[2].pos.x, v[2].pos.y, v[2].pos.z);

					glVertex3f(v[2].pos.x, v[2].pos.y, v[2].pos.z);
					glVertex3f(v[0].pos.x, v[0].pos.y, v[0].pos.z);
				}
			}
		}
		glEnd();
	}
/**/
/**/
	for (int i = 0; i < _draw_veh.size(); i++)
	{
		VehicleData *c = _draw_veh[i];
		if (c->linked) continue;

		Vehicle *v = Vehicle::Get(c->index);
		
		glMatrixMode(GL_MODELVIEW);
		glLoadTransposeMatrixf(c->matr);

		glBegin(GL_LINES);
		glColor4f(0.0f, 1.0f, 0.0f, 1.0f);

		float sz_x = 7;
		float sz_y = 3;
		float sz_z = 6;
		switch (v->type)
		{
		case VEH_TRAIN:
			{
				GroundVehicleCache *cache=v->GetGroundVehicleCache();
				sz_x = cache->cached_veh_length;
			}
			break;

		case VEH_ROAD:
			{
				GroundVehicleCache *cache=v->GetGroundVehicleCache();
				sz_x = cache->cached_veh_length;
				sz_y = 3;
				sz_z = 6;
			}
			break;

		case VEH_SHIP:
			sz_x = 32;
			sz_y = 6;
			break;

		case VEH_AIRCRAFT:
			sz_x = 24;
			sz_y = 24;
			sz_z = 5;
			break;
		};

		float bbp[4][2]=
		{
			{ -sz_x / 2.0f, -sz_y / 2.0f },
			{ -sz_x / 2.0f, +sz_y / 2.0f },
			{ +sz_x / 2.0f, +sz_y / 2.0f },
			{ +sz_x / 2.0f, -sz_y / 2.0f },
		};
		for (int j = 0; j < 2; j++)
		{
			for (int i = 0; i < 4; i++)
			{
				int p0 = (i + 0) % 4;
				int p1 = (i + 1) % 4;
				glVertex3f(bbp[p0][0], bbp[p0][1], (sz_z * j) * (TILE_HEIGHT_SCALE));
				glVertex3f(bbp[p1][0], bbp[p1][1], (sz_z * j) * (TILE_HEIGHT_SCALE));
			}
		}
		for (int i = 0; i < 4; i++)
		{
			int p = (i + 0) % 4;
			glVertex3f(bbp[p][0], bbp[p][1], 0.0f);
			glVertex3f(bbp[p][0], bbp[p][1], sz_z * (TILE_HEIGHT_SCALE));
		}
		glEnd();
	}
/**/
	/// debug stuff end

	/* finished all of the 3D drawing, now draw a text overlay */

	glClear(GL_DEPTH_BUFFER_BIT);
	Blitter_OpenGL *blitter = (Blitter_OpenGL*)(BlitterFactory::GetCurrentBlitter());
	{
		bool show_stations = HasBit(_display_opt, DO_SHOW_STATION_NAMES) && _game_mode != GM_MENU;
		bool show_waypoints = HasBit(_display_opt, DO_SHOW_WAYPOINT_NAMES) && _game_mode != GM_MENU;
		bool show_towns = HasBit(_display_opt, DO_SHOW_TOWN_NAMES) && _game_mode != GM_MENU;
		bool show_signs = HasBit(_display_opt, DO_SHOW_SIGNS) && !IsInvisibilitySet(TO_SIGNS);
		bool show_competitors = HasBit(_display_opt, DO_SHOW_COMPETITOR_SIGNS);

		for (size_t i = 0; i < _draw_seg.size(); i++)
		{
			LandSeg *s = _draw_seg[i];
			for (size_t k = 0; k < s->labels.size(); k++)
			{
				LandSign *sg = &s->labels[k];
				switch (sg->type)
				{
				case VPST_STATION:
					{
						if (!show_stations) continue;
						const BaseStation *st = BaseStation::Get(sg->id.station);
						if (!show_competitors && _local_company != st->owner && st->owner != OWNER_NONE) continue;
					}
					break;

				case VPST_WAYPOINT:
					{
						if (!show_waypoints) continue;
						const BaseStation *st = BaseStation::Get(sg->id.station);
						if (!show_competitors && _local_company != st->owner && st->owner != OWNER_NONE) continue;
					}
					break;

				case VPST_TOWN:
					if (!show_towns) continue;
					break;

				default:
					NOT_REACHED();
				}
				DrawSign(vp, sg);
			}
			if (show_signs)
			{
				for (auto id = s->signs.begin(); id != s->signs.end();)
				{
					LandSign *sg = &_signs[*id];
					Sign *si = Sign::GetIfValid(sg->id.sign);
					if (!si)
					{
						id = s->signs.erase(id);
						RemoveSign3D(sg->id.sign);
						continue;
					}
					if (!show_competitors && _local_company != si->owner && si->owner != OWNER_DEITY) continue;
					DrawSign(vp, sg);
					id++;
				}
			}
			for (auto id = s->effects.begin(); id != s->effects.end();)
			{
				LandSign *sg = &_text_effects[*id];
				sg->sign = GetTextEffect(sg->id.effect);
				if (!sg->sign)
				{
					id = s->effects.erase(id);
					RemoveTextEffect3D(sg->id.effect);
					continue;
				}
				DrawSign(vp, sg);
				id++;
			}
		}	
	}
	if (_cursor.sprite_seq[0].sprite == SPR_CURSOR_QUERY) // a small hack to show the tile sprites draw stack
	{
		{
			Point p = MapInvCoords3D(vp, _cursor.pos.x - vp->left, _cursor.pos.y - vp->top, true);

			uint x = Clamp(p.x / TILE_SIZE, 0, MapMaxX());
			uint y = Clamp(p.y / TILE_SIZE, 0, MapMaxY());
			_info_tile = TileXY(x, y);

			float prj[4];
			float pos[4] = { (float)(x * TILE_SIZE), (float)(y * TILE_SIZE), TileHeight(TileXY(x, y)) * TILE_HEIGHT_ACT, 1.0f };
			matrApply44(prj, vp->xyz_to_ogl, pos);
			vectScale3(prj, prj, 1.0f / prj[3]);
			blitter->SetOverlayZ(prj[2]);

			float w2 = vp->width  / 2;
			float h2 = vp->height / 2;

			int left  = (int)(prj[0] * w2 + w2 - 128);
			int right = (int)(prj[0] * w2 + w2 + 128);

			char buf[256];
			char grfid_str[16];
			LandTile *t = _land.tiles[TileXY(x, y)];
			for (int n = 0; n < t->draw[1].size(); n++)
			{
				DrawLink *draw = &t->draw[1][n];
				SpriteID s = draw->sprite & SPRITE_MASK;
				PaletteID p = draw->palette & PALETTE_MASK;
				uint32 grfid = GetOriginGRFID(s);
				uint32 id = GetOriginID(s);

				bool hex = false;
				char *grfid_c = (char*)(&grfid);
				for (int p = 0; p < 4; p++)
				{
					char ch = grfid_c[p];
					if (isalpha(ch) || isdigit(ch)) continue;
					hex = true;
					break;
				}
				if (hex)
				{
					sprintf(grfid_str, "0x%08X", grfid);
				}
				else
				{
					sprintf(grfid_str, "%c%c%c%c", grfid_c[3], grfid_c[2], grfid_c[1], grfid_c[0]);
				}
				sprintf(buf, "%s:%i - %i:%i %c", grfid_str, id, s, p, (draw->sprite & (1 << TRANSPARENT_BIT)) ? 'T' : ' ');

				int top = (int)(-prj[1] * h2 + h2) - n * 14 - 18;
				DrawString(left, right, top, buf, TC_ORANGE, SA_HOR_CENTER, false, FS_NORMAL);
			}
		}

		for (VehicleData *c : _draw_veh)
		{
			Vehicle *v = Vehicle::Get(c->index);

			float prj[4];
			float pos[4] = { c->posf[0], c->posf[1], c->posf[2], 1.0f };
			matrApply44(prj, vp->xyz_to_ogl, pos);
			vectScale3(prj, prj, 1.0f / prj[3]);
			blitter->SetOverlayZ(prj[2]);

			float w2 = vp->width / 2;
			float h2 = vp->height / 2;

			int left  = (int)(prj[0] * w2 + w2 - 128);
			int right = (int)(prj[0] * w2 + w2 + 128);

			if (v->type == VEH_ROAD)
			{
				char frame[256];
				sprintf(frame, "%i %i  %f", ((RoadVehicle*)(v))->frame, c->frame_inc, c->frame);
				int top = (int)(-prj[1] * h2 + h2);
				DrawString(left, right, top, frame, TC_ORANGE, SA_HOR_CENTER, false, FS_NORMAL);
			}
			if (v->type == VEH_TRAIN)
			{
				char frame[256];
				sprintf(frame, "%i", v->progress);
				int top = (int)(-prj[1] * h2 + h2);
				DrawString(left, right, top, frame, TC_ORANGE, SA_HOR_CENTER, false, FS_NORMAL);
			}

			char buf[256];
			char grfid_str[16];
			for (int n = 0; n < c->draw.size(); n++)
			{
				DrawLink *draw = &c->draw[n];
				SpriteID s = c->draw[n].sprite & SPRITE_MASK;
				PaletteID p = c->draw[n].palette & PALETTE_MASK;
				uint32 grfid = GetOriginGRFID(s);
				uint32 id = GetOriginID(s);

				bool hex = false;
				char *grfid_c = (char*)(&grfid);
				for (int p = 0; p < 4; p++)
				{
					char ch = grfid_c[p];
					if (isalpha(ch) || isdigit(ch)) continue;
					hex = true;
					break;
				}
				if (hex)
				{
					sprintf(grfid_str, "0x%08X", grfid);
				}
				else
				{
					sprintf(grfid_str, "%c%c%c%c", grfid_c[3], grfid_c[2], grfid_c[1], grfid_c[0]);
				}
				sprintf(buf, "%s:%i - %i:%i %c", grfid_str, id, s, p, (c->draw[n].sprite & (1 << TRANSPARENT_BIT)) ? 'T' : ' ');

				int top = (int)(-prj[1] * h2 + h2) - n * 14 - 18;
				DrawString(left, right, top, buf, TC_ORANGE, SA_HOR_CENTER, false, FS_NORMAL);
			}
		}
	}
	else
	{
		_info_tile = -1;
	}

	/* clear the drawing lists */
	_draw_veh.clear();
	_draw_seg.clear();
}

/* Called for the viewport data reset */
void ResetViewport3D()
{
	_reset = 1;
}
