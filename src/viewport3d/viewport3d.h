#ifndef VIEWPORT3D_H
#define VIEWPORT3D_H

#include "../window_gui.h"
#include "../texteff.hpp"
#include "../signs_type.h"

extern int _redirect_draw;

extern void SetViewportPosition3D(ViewportData *vp);
extern void ScrollViewport3D(ViewportData *vp);

extern Point MapInvCoords3D(const ViewPort *vp, int x, int y, bool clamp_to_map);
extern Vehicle *CheckClickOnVehicle3D(const ViewPort *vp, int x, int y);
extern bool CheckClickOnViewportSign3D(const ViewPort *vp, int x, int y);
extern void MarkTileDirty3D(TileIndex tile);

extern void AddTextEffect3D(TextEffectID id, int x, int y, int z, StringID string, uint64 params_1, uint64 params_2);
extern void UpdateTextEffect3D(TextEffectID id, StringID string, uint64 params_1, uint64 params_2);
extern void AddSign3D(SignID id);
extern void UpdateSign3D(SignID id);

extern void DrawTileSprite3D(SpriteID sprite, PaletteID pal, int x, int y, int z, bool force = false);
extern void DrawTileSelection3D(SpriteID sprite, PaletteID pal);

extern void DrawPrepare3D();
extern void DrawPrepareViewport3D(const ViewPort *vp);
extern void DrawViewport3D(const ViewPort *vp);
extern void ResetViewport3D();

#endif /* VIEWPORT3D_H */
