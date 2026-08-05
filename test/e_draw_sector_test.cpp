//------------------------------------------------------------------------
//  QUICK SECTOR DRAWING TESTS
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

#include "Document.h"
#include "Instance.h"
#include "LineDef.h"
#include "Sector.h"
#include "SideDef.h"
#include "Vertex.h"
#include "w_rawdef.h"

#include "gtest/gtest.h"

class DrawSectorFixture : public ::testing::Test
{
protected:
	DrawSectorFixture()
	{
		inst.Editor_Init();
		inst.grid.SetSnap(false);  // keep test coordinates exact
		inst.edit.mode = ObjType::sectors;
	}
	~DrawSectorFixture()
	{
		inst.level.clear();
	}

	int addVertex(double x, double y)
	{
		auto vertex = std::make_shared<Vertex>();
		vertex->xf = x;
		vertex->yf = y;
		doc.vertices.push_back(vertex);
		return doc.numVertices() - 1;
	}

	int addSide(int sector)
	{
		auto side = std::make_shared<SideDef>();
		side->sector = sector;
		doc.sidedefs.push_back(side);
		return doc.numSidedefs() - 1;
	}

	int addLine(int start, int end, int rightSector)
	{
		auto line = std::make_shared<LineDef>();
		line->start = start;
		line->end = end;
		line->right = addSide(rightSector);
		line->flags |= MLF_Blocking;
		doc.linedefs.push_back(line);
		return doc.numLinedefs() - 1;
	}

	int addBoxSector(double minX, double minY, double maxX, double maxY)
	{
		auto sector = std::make_shared<Sector>();
		sector->floorh = 0;
		sector->ceilh = 128;
		doc.sectors.push_back(sector);
		const int result = doc.numSectors() - 1;

		const int v0 = addVertex(minX, minY);
		const int v1 = addVertex(minX, maxY);
		const int v2 = addVertex(maxX, maxY);
		const int v3 = addVertex(maxX, minY);
		addLine(v0, v1, result);
		addLine(v1, v2, result);
		addLine(v2, v3, result);
		addLine(v3, v0, result);
		return result;
	}

	int countTwoSidedLines() const
	{
		int result = 0;
		for (const auto &line : doc.linedefs)
			if (line->left >= 0)
				++result;
		return result;
	}

	Instance inst;
	Document &doc = inst.level;
};

TEST_F(DrawSectorFixture, RectCommitInVoidCreatesSector)
{
	M_DrawSectorRectBegin(inst, {0, 0});
	inst.edit.map.xy = {128, 64};
	M_DrawSectorRectCommit(inst);

	ASSERT_EQ(doc.numSectors(), 1);
	EXPECT_EQ(doc.numVertices(), 4);
	EXPECT_EQ(doc.numLinedefs(), 4);
	EXPECT_EQ(doc.numSidedefs(), 4);
	EXPECT_EQ(countTwoSidedLines(), 0);

	// the gesture finished and selected the new sector
	EXPECT_EQ(inst.edit.action, EditorAction::nothing);
	ASSERT_TRUE(inst.edit.Selected.has_value());
	EXPECT_TRUE(inst.edit.Selected->get(0));

	// the whole gesture is a single undo step
	ASSERT_TRUE(doc.basis.undo());
	EXPECT_EQ(doc.numSectors(), 0);
	EXPECT_EQ(doc.numVertices(), 0);
	EXPECT_EQ(doc.numLinedefs(), 0);
	EXPECT_EQ(doc.numSidedefs(), 0);
}

TEST_F(DrawSectorFixture, RectAttachedToExistingSectorSharesEdge)
{
	addBoxSector(0, 0, 64, 64);

	M_DrawSectorRectBegin(inst, {64, 0});
	inst.edit.map.xy = {128, 64};
	M_DrawSectorRectCommit(inst);

	ASSERT_EQ(doc.numSectors(), 2);
	EXPECT_EQ(doc.numVertices(), 6);   // shared edge reuses two vertices
	EXPECT_EQ(doc.numLinedefs(), 7);   // shared edge + 3 new lines
	EXPECT_EQ(countTwoSidedLines(), 1);

	ASSERT_TRUE(doc.basis.undo());
	EXPECT_EQ(doc.numSectors(), 1);
	EXPECT_EQ(doc.numVertices(), 4);
	EXPECT_EQ(doc.numLinedefs(), 4);
	EXPECT_EQ(countTwoSidedLines(), 0);
}

TEST_F(DrawSectorFixture, DegenerateRectIsIgnored)
{
	M_DrawSectorRectBegin(inst, {32, 32});
	inst.edit.map.xy = {33, 63};  // one axis below the minimum size
	M_DrawSectorRectCommit(inst);

	EXPECT_EQ(doc.numSectors(), 0);
	EXPECT_EQ(doc.numVertices(), 0);
	EXPECT_EQ(inst.edit.action, EditorAction::nothing);
}

TEST_F(DrawSectorFixture, PolyCommitWithEnterCreatesSector)
{
	M_DrawSectorPolyBegin(inst);
	EXPECT_EQ(inst.edit.action, EditorAction::drawSectorPoly);

	M_DrawSectorPolyAddPoint(inst, {0, 0}, 0);
	M_DrawSectorPolyAddPoint(inst, {96, 0}, 0);

	// too few points: nothing happens, gesture stays active
	M_DrawSectorPolyCommit(inst);
	EXPECT_EQ(doc.numSectors(), 0);
	EXPECT_EQ(inst.edit.action, EditorAction::drawSectorPoly);

	M_DrawSectorPolyAddPoint(inst, {48, 80}, 0);
	M_DrawSectorPolyCommit(inst);

	ASSERT_EQ(doc.numSectors(), 1);
	EXPECT_EQ(doc.numVertices(), 3);
	EXPECT_EQ(doc.numLinedefs(), 3);
	EXPECT_EQ(inst.edit.action, EditorAction::nothing);

	ASSERT_TRUE(doc.basis.undo());
	EXPECT_EQ(doc.numSectors(), 0);
}

TEST_F(DrawSectorFixture, PolyClickingFirstPointClosesShape)
{
	M_DrawSectorPolyBegin(inst);

	M_DrawSectorPolyAddPoint(inst, {0, 0}, 0);
	M_DrawSectorPolyAddPoint(inst, {128, 0}, 0);
	M_DrawSectorPolyAddPoint(inst, {128, 128}, 0);

	// a duplicate of the last point is ignored, not stored
	M_DrawSectorPolyAddPoint(inst, {128, 128}, 0);
	EXPECT_EQ(inst.edit.drawPolyAnchors.size(), 3u);

	// clicking the first point again closes and commits
	M_DrawSectorPolyAddPoint(inst, {0, 0}, 0);

	ASSERT_EQ(doc.numSectors(), 1);
	EXPECT_EQ(doc.numVertices(), 3);
	EXPECT_EQ(doc.numLinedefs(), 3);
	EXPECT_EQ(inst.edit.action, EditorAction::nothing);
}

TEST_F(DrawSectorFixture, PolyRemoveLastAndCancel)
{
	M_DrawSectorPolyBegin(inst);

	M_DrawSectorPolyAddPoint(inst, {0, 0}, 0);
	M_DrawSectorPolyAddPoint(inst, {64, 0}, 0);
	M_DrawSectorPolyRemoveLast(inst);
	EXPECT_EQ(inst.edit.drawPolyAnchors.size(), 1u);

	// removing all points keeps the gesture alive so the user can
	// restart the outline; right-clicking with no points cancels
	M_DrawSectorPolyRemoveLast(inst);
	EXPECT_EQ(inst.edit.action, EditorAction::drawSectorPoly);
	M_DrawSectorPolyRemoveLast(inst);
	EXPECT_EQ(inst.edit.action, EditorAction::nothing);

	M_DrawSectorPolyBegin(inst);
	M_DrawSectorPolyAddPoint(inst, {0, 0}, 0);
	M_DrawSectorPolyCancel(inst);
	EXPECT_EQ(inst.edit.action, EditorAction::nothing);
	EXPECT_TRUE(inst.edit.drawPolyAnchors.empty());
	EXPECT_EQ(doc.numSectors(), 0);
}

TEST_F(DrawSectorFixture, SnapPointPrefersExistingVertex)
{
	addVertex(37, 41);

	const v2double_t snapped = M_DrawSectorSnapPoint(inst, {37, 41});
	EXPECT_DOUBLE_EQ(snapped.x, 37.0);
	EXPECT_DOUBLE_EQ(snapped.y, 41.0);
}

TEST_F(DrawSectorFixture, RectOutlineRespectsMinimumSize)
{
	EXPECT_TRUE(M_DrawSectorRectOutline(inst, {0, 0}, {1, 64}).empty());
	EXPECT_TRUE(M_DrawSectorRectOutline(inst, {0, 0}, {64, 1}).empty());

	const std::vector<v2double_t> outline =
			M_DrawSectorRectOutline(inst, {0, 0}, {64, 32});
	ASSERT_EQ(outline.size(), 4u);
	EXPECT_DOUBLE_EQ(outline[0].x, 0.0);
	EXPECT_DOUBLE_EQ(outline[0].y, 0.0);
	EXPECT_DOUBLE_EQ(outline[2].x, 64.0);
	EXPECT_DOUBLE_EQ(outline[2].y, 32.0);
}

//--- editor settings ---
// vi:ts=4:sw=4:noexpandtab
