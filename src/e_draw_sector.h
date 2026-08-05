//------------------------------------------------------------------------
//  QUICK SECTOR DRAWING
//------------------------------------------------------------------------
//
//  Eureka DOOM Editor
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 2
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//------------------------------------------------------------------------
//
//  Direct mouse gestures for creating sectors without opening a
//  dialog: drag out a rectangle on empty space in sectors mode, or
//  trace a freeform polygon point by point.  Both gestures commit
//  through ObjectsModule::insertSectorPolygon, so vertex reuse, line
//  splitting and closed-loop sector creation behave exactly like
//  drawing the lines by hand.
//
//------------------------------------------------------------------------

#ifndef __EUREKA_E_DRAW_SECTOR_H__
#define __EUREKA_E_DRAW_SECTOR_H__

#include <vector>

#include "m_keys.h"
#include "m_vector.h"

class Instance;

// rectangle gesture (EditorAction::drawSectorRect)
void M_DrawSectorRectBegin(Instance &inst, const v2double_t &start);
void M_DrawSectorRectUpdate(Instance &inst);
void M_DrawSectorRectCommit(Instance &inst);

// freeform polygon gesture (EditorAction::drawSectorPoly)
void M_DrawSectorPolyBegin(Instance &inst);
void M_DrawSectorPolyAddPoint(Instance &inst, const v2double_t &point,
							  keycode_t modifiers);
void M_DrawSectorPolyRemoveLast(Instance &inst);
void M_DrawSectorPolyCommit(Instance &inst);
void M_DrawSectorPolyCancel(Instance &inst);
void M_DrawSectorPolyUpdatePreview(Instance &inst);

// helpers (exposed for unit tests)
std::vector<v2double_t> M_DrawSectorRectOutline(const Instance &inst,
		const v2double_t &from, const v2double_t &to);
v2double_t M_DrawSectorSnapPoint(const Instance &inst,
		const v2double_t &point);

#endif  /* __EUREKA_E_DRAW_SECTOR_H__ */

//--- editor settings ---
// vi:ts=4:sw=4:noexpandtab
