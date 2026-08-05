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

#include "e_draw_sector.h"

#include <cmath>

#include "Document.h"
#include "e_basis.h"
#include "e_main.h"
#include "e_objects.h"
#include "Instance.h"
#include "r_grid.h"
#include "Vertex.h"


// a drag smaller than this (in map units, on either axis) is treated
// as a mere click, not a rectangle
static constexpr double MIN_RECT_SIDE = 4.0;

// squared-distance epsilon for "same point" tests on snapped coordinates
static constexpr double SAME_POINT_DIST = 0.5;


static bool SamePoint(const v2double_t &a, const v2double_t &b)
{
	return std::hypot(a.x - b.x, a.y - b.y) < SAME_POINT_DIST;
}


// constrain the segment origin->point to a multiple of 45 degrees
static v2double_t Constrain45(const v2double_t &origin, const v2double_t &point)
{
	const v2double_t delta = point - origin;
	const double length = delta.hypot();
	if (length <= 0.000001)
		return point;
	constexpr double eighthTurn = 3.14159265358979323846 / 4.0;
	const double angle = std::round(delta.atan2() / eighthTurn) * eighthTurn;
	return origin + v2double_t(std::cos(angle), std::sin(angle)) * length;
}


static bool CommitOutline(Instance &inst, std::vector<v2double_t> &outline)
{
	Document &doc = inst.level;

	std::vector<int> newSectors;
	{
		EditOperation op(doc.basis);
		op.setMessage("drew sector");

		newSectors = doc.objects.insertSectorPolygon(
				op, outline, inst.loaded.levelFormat);

		if (newSectors.empty())
			op.setAbort(false /* keepChanges */);
	}

	if (newSectors.empty())
	{
		inst.Beep("Could not create a sector from that shape");
		return false;
	}

	// select the new sector(s)
	inst.Selection_Clear();
	for (int sec : newSectors)
		inst.edit.Selected->set(sec);

	inst.Editor_ClearAction();
	inst.RedrawMap();

	inst.Status_Set("Drew sector #%d (Shift-drag for a selection box, "
					"F for freeform drawing)", newSectors.back());

	return true;
}


//------------------------------------------------------------------------
//  RECTANGLE GESTURE
//------------------------------------------------------------------------

std::vector<v2double_t> M_DrawSectorRectOutline(const Instance &inst,
		const v2double_t &from, const v2double_t &to)
{
	const v2double_t first  = inst.grid.Snap(from);
	const v2double_t second = inst.grid.Snap(to);

	const grid::Basis basis = inst.grid.getBasis();

	v2double_t sideA;
	v2double_t sideB;

	if (std::abs(basis.determinant) > 1e-9)
	{
		// express the drag delta in grid-basis coordinates, so a rotated
		// "mathematical grid" produces a rotated rectangle
		const v2double_t delta = second - first;
		const double u = (delta.x * basis.secondary.y -
						  delta.y * basis.secondary.x) / basis.determinant;
		const double v = (basis.primary.x * delta.y -
						  basis.primary.y * delta.x) / basis.determinant;

		sideA = basis.primary * u;
		sideB = basis.secondary * v;
	}
	else
	{
		// degenerate basis: fall back to an axis-aligned box
		sideA = { second.x - first.x, 0.0 };
		sideB = { 0.0, second.y - first.y };
	}

	if (sideA.hypot() < MIN_RECT_SIDE || sideB.hypot() < MIN_RECT_SIDE)
		return {};

	return { first, first + sideA, second, first + sideB };
}


void M_DrawSectorRectBegin(Instance &inst, const v2double_t &start)
{
	inst.edit.drawRectFrom = start;

	inst.Editor_SetAction(EditorAction::drawSectorRect);

	M_DrawSectorRectUpdate(inst);

	inst.Status_Set("%s", "Drag to size the new sector, release to create "
					"it (hold SHIFT for a selection box)");
}


void M_DrawSectorRectUpdate(Instance &inst)
{
	const std::vector<v2double_t> outline = M_DrawSectorRectOutline(
			inst, inst.edit.drawRectFrom, inst.edit.map.xy);

	DesignAssistPreview preview;

	if (! outline.empty())
	{
		DesignPreviewPath path;
		path.points = outline;
		path.closed = true;
		path.role   = DesignPreviewRole::proposed;
		preview.paths.push_back(std::move(path));

		DesignPreviewPoint anchor;
		anchor.position = inst.grid.Snap(inst.edit.drawRectFrom);
		anchor.role     = DesignPreviewRole::anchor;
		preview.points.push_back(anchor);
	}

	inst.edit.designAssistPreview = std::move(preview);
}


void M_DrawSectorRectCommit(Instance &inst)
{
	std::vector<v2double_t> outline = M_DrawSectorRectOutline(
			inst, inst.edit.drawRectFrom, inst.edit.map.xy);

	if (outline.empty())
	{
		// a mere click and release on empty space: behave like the
		// selection box did and unselect everything
		inst.Editor_ClearAction();
		inst.ExecuteCommand("UnselectAll");
		return;
	}

	// the drag is over no matter what, never leave the action dangling
	if (! CommitOutline(inst, outline) &&
		inst.edit.action == EditorAction::drawSectorRect)
	{
		inst.Editor_ClearAction();
		inst.RedrawMap();
	}
}


//------------------------------------------------------------------------
//  FREEFORM POLYGON GESTURE
//------------------------------------------------------------------------

v2double_t M_DrawSectorSnapPoint(const Instance &inst, const v2double_t &point)
{
	// prefer an existing vertex, so outlines can attach exactly to
	// existing geometry even when it sits off the grid
	const Objid vert = inst.getNearbyObject(ObjType::vertices, point);
	if (vert.valid())
		return inst.level.vertices[vert.num]->xy();

	return inst.grid.Snap(point);
}


void M_DrawSectorPolyBegin(Instance &inst)
{
	inst.edit.drawPolyAnchors.clear();

	inst.Editor_SetAction(EditorAction::drawSectorPoly);

	M_DrawSectorPolyUpdatePreview(inst);

	inst.Status_Set("%s", "Draw sector: click to add points, click the "
					"first point or press ENTER to close, right-click "
					"removes the last point, ESC cancels");
}


void M_DrawSectorPolyAddPoint(Instance &inst, const v2double_t &point,
							  keycode_t modifiers)
{
	std::vector<v2double_t> &anchors = inst.edit.drawPolyAnchors;

	v2double_t placed = M_DrawSectorSnapPoint(inst, point);

	if ((modifiers & EMOD_SHIFT) && ! anchors.empty())
		placed = Constrain45(anchors.back(), placed);

	// clicking the first point closes the shape
	if (anchors.size() >= 3 && SamePoint(placed, anchors.front()))
	{
		M_DrawSectorPolyCommit(inst);
		return;
	}

	// ignore accidental double-clicks on the same spot
	if (! anchors.empty() && SamePoint(placed, anchors.back()))
		return;

	anchors.push_back(placed);

	M_DrawSectorPolyUpdatePreview(inst);
}


void M_DrawSectorPolyRemoveLast(Instance &inst)
{
	std::vector<v2double_t> &anchors = inst.edit.drawPolyAnchors;

	if (anchors.empty())
	{
		M_DrawSectorPolyCancel(inst);
		return;
	}

	anchors.pop_back();

	M_DrawSectorPolyUpdatePreview(inst);
}


void M_DrawSectorPolyCommit(Instance &inst)
{
	std::vector<v2double_t> outline = inst.edit.drawPolyAnchors;

	if (outline.size() < 3)
	{
		inst.Beep("Need at least 3 points to make a sector");
		return;
	}

	CommitOutline(inst, outline);
}


void M_DrawSectorPolyCancel(Instance &inst)
{
	inst.Editor_ClearAction();
	inst.Status_Clear();
	inst.RedrawMap();
}


void M_DrawSectorPolyUpdatePreview(Instance &inst)
{
	const std::vector<v2double_t> &anchors = inst.edit.drawPolyAnchors;

	DesignAssistPreview preview;

	DesignPreviewPath path;
	path.points = anchors;
	path.role   = DesignPreviewRole::proposed;

	if (inst.edit.pointer_in_window)
		path.points.push_back(M_DrawSectorSnapPoint(inst, inst.edit.map.xy));

	// once the shape can close, show the would-be polygon
	path.closed = anchors.size() >= 2;

	if (! path.points.empty())
		preview.paths.push_back(std::move(path));

	for (const v2double_t &anchorPos : anchors)
	{
		DesignPreviewPoint point;
		point.position = anchorPos;
		point.role     = DesignPreviewRole::anchor;
		preview.points.push_back(point);
	}

	inst.edit.designAssistPreview = std::move(preview);
}

//--- editor settings ---
// vi:ts=4:sw=4:noexpandtab
