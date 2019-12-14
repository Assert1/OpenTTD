/*
* This file is part of OpenTTD.
* OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
* OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
* See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
*/

/** @file gfx3d.h Entry for 3D viewports drawing */

#ifndef GFX3D_H
#define GFX3D_H

extern int _draw3d;

extern void MarkRectsDirty(int left, int top, int right, int bottom);
extern void RedrawDirtyRects();

#endif /* VIEWPORT3D_H */
