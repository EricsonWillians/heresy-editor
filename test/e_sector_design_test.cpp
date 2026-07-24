//------------------------------------------------------------------------
//  SMART SECTOR DESIGNER TESTS
//------------------------------------------------------------------------

#include "e_sector_design.h"

#include "Document.h"
#include "Instance.h"
#include "LineDef.h"
#include "m_config.h"
#include "Sector.h"
#include "SideDef.h"
#include "Vertex.h"
#include "ui_door.h"
#include "ui_pic.h"
#include "ui_sector_design.h"
#include "ui_menu.h"
#include "w_rawdef.h"

#include "gtest/gtest.h"

#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <numbers>
#include <set>
#include <sstream>

namespace
{

Fl_Input *FindInputByLabel(Fl_Group &group, const char *label)
{
	for (int index = 0; index < group.children(); ++index)
	{
		Fl_Widget *widget = group.child(index);
		if (auto *input = dynamic_cast<Fl_Input *>(widget);
			input && input->label() &&
			SString(input->label()) == label)
			return input;
		if (auto *childGroup = dynamic_cast<Fl_Group *>(widget))
			if (Fl_Input *input = FindInputByLabel(*childGroup, label))
				return input;
	}
	return nullptr;
}

template<typename Widget>
Widget *FindWidgetByLabel(Fl_Group &group, const char *label)
{
	for (int index = 0; index < group.children(); ++index)
	{
		Fl_Widget *widget = group.child(index);
		if (auto *match = dynamic_cast<Widget *>(widget);
			match && match->label() && SString(match->label()) == label)
			return match;
		if (auto *childGroup = dynamic_cast<Fl_Group *>(widget))
			if (Widget *match =
					FindWidgetByLabel<Widget>(*childGroup, label))
				return match;
	}
	return nullptr;
}

template<typename Widget>
Widget *FindWidgetByType(Fl_Group &group)
{
	for (int index = 0; index < group.children(); ++index)
	{
		Fl_Widget *widget = group.child(index);
		if (auto *match = dynamic_cast<Widget *>(widget))
			return match;
		if (auto *childGroup = dynamic_cast<Fl_Group *>(widget))
			if (Widget *match = FindWidgetByType<Widget>(*childGroup))
				return match;
	}
	return nullptr;
}

template<typename Widget>
int CountWidgetsByType(Fl_Group &group)
{
	int result = 0;
	for (int index = 0; index < group.children(); ++index)
	{
		Fl_Widget *widget = group.child(index);
		result += dynamic_cast<Widget *>(widget) != nullptr;
		if (auto *childGroup = dynamic_cast<Fl_Group *>(widget))
			result += CountWidgetsByType<Widget>(*childGroup);
	}
	return result;
}

DesignPreviewRole ExpectedArchitectureRole(
		SectorArchitectureElement element)
{
	return M_ArchitectureDescriptor(element).role;
}

bool PointInsidePath(const std::vector<v2double_t> &path,
					 const v2double_t &point)
{
	bool inside = false;
	for (size_t current = 0, previous = path.size() - 1;
		 current < path.size(); previous = current++)
	{
		const v2double_t &a = path[previous];
		const v2double_t &b = path[current];
		if ((a.y > point.y) != (b.y > point.y) &&
			point.x < (b.x - a.x) * (point.y - a.y) /
					(b.y - a.y) + a.x)
			inside = !inside;
	}
	return inside;
}

bool SectorContainsPoint(const Document &doc, int sector,
						 const v2double_t &point)
{
	bool inside = false;
	for (const std::shared_ptr<LineDef> &line : doc.linedefs)
	{
		const int right = line->right >= 0 ?
				doc.sidedefs[line->right]->sector : -1;
		const int left = line->left >= 0 ?
				doc.sidedefs[line->left]->sector : -1;
		if ((right == sector) == (left == sector))
			continue;
		const v2double_t a = doc.getStart(*line).xy();
		const v2double_t b = doc.getEnd(*line).xy();
		if ((a.y > point.y) != (b.y > point.y) &&
			point.x < (b.x - a.x) * (point.y - a.y) /
					(b.y - a.y) + a.x)
			inside = !inside;
	}
	return inside;
}

class SmartSectorFixture : public ::testing::Test
{
protected:
	SmartSectorFixture()
	{
		config.default_wall_tex = "WALL";
		config.default_floor_tex = "FLOOR";
		config.default_ceil_tex = "CEIL";
		config.miscInfo.player_h = 56;
	}

	int addVertex(double x, double y)
	{
		auto vertex = std::make_shared<Vertex>();
		vertex->xf = x;
		vertex->yf = y;
		doc.vertices.push_back(vertex);
		return doc.numVertices() - 1;
	}

	int addSector(int floor = 0, int ceiling = 128)
	{
		auto sector = std::make_shared<Sector>();
		sector->floorh = floor;
		sector->ceilh = ceiling;
		sector->floor_tex = BA_InternaliseString("OLD_F");
		sector->ceil_tex = BA_InternaliseString("OLD_C");
		sector->light = 144;
		doc.sectors.push_back(sector);
		return doc.numSectors() - 1;
	}

	int addSide(int sector)
	{
		auto side = std::make_shared<SideDef>();
		side->sector = sector;
		side->mid_tex = BA_InternaliseString("WALL");
		doc.sidedefs.push_back(side);
		return doc.numSidedefs() - 1;
	}

	int addLine(int start, int end, int rightSector, int leftSector = -1)
	{
		auto line = std::make_shared<LineDef>();
		line->start = start;
		line->end = end;
		line->right = addSide(rightSector);
		if (leftSector >= 0)
		{
			line->left = addSide(leftSector);
			line->flags |= MLF_TwoSided;
		}
		else
			line->flags |= MLF_Blocking;
		doc.linedefs.push_back(line);
		return doc.numLinedefs() - 1;
	}

	void addTwoAdjacentSectors()
	{
		addSector(0, 128);
		addSector(0, 128);
		int v0 = addVertex(0, 0);
		int v1 = addVertex(0, 64);
		int v2 = addVertex(64, 64);
		int v3 = addVertex(64, 0);
		int v4 = addVertex(128, 64);
		int v5 = addVertex(128, 0);
		addLine(v0, v1, 0);
		addLine(v1, v2, 0);
		addLine(v2, v3, 0, 1);
		addLine(v3, v0, 0);
		addLine(v2, v4, 1);
		addLine(v4, v5, 1);
		addLine(v5, v3, 1);
	}

	int addBoxSector(double minX, double minY,
					 double maxX, double maxY,
					 int floor = 0, int ceiling = 128)
	{
		const int sector = addSector(floor, ceiling);
		const int v0 = addVertex(minX, minY);
		const int v1 = addVertex(minX, maxY);
		const int v2 = addVertex(maxX, maxY);
		const int v3 = addVertex(maxX, minY);
		addLine(v0, v1, sector);
		addLine(v1, v2, sector);
		addLine(v2, v3, sector);
		addLine(v3, v0, sector);
		return sector;
	}

	int addRingSector(double outerRadius, double holeRadius,
					  int floor = 0, int ceiling = 128)
	{
		const int sector = addBoxSector(
				-outerRadius, -outerRadius,
				outerRadius, outerRadius, floor, ceiling);
		const int v0 = addVertex(-holeRadius, -holeRadius);
		const int v1 = addVertex(holeRadius, -holeRadius);
		const int v2 = addVertex(holeRadius, holeRadius);
		const int v3 = addVertex(-holeRadius, holeRadius);
		// Counter-clockwise hole boundary keeps the ring sector on the right.
		addLine(v0, v1, sector);
		addLine(v1, v2, sector);
		addLine(v2, v3, sector);
		addLine(v3, v0, sector);
		return sector;
	}

	Instance inst;
	Document &doc = inst.level;
	ConfigData config;
};

TEST_F(SmartSectorFixture, RectanglePlannerIsPureAndFormatQuantized)
{
	SectorDesignRequest request;
	request.mode = SectorDesignMode::room;
	request.anchors = {{0.25, 0.75}, {64.75, 32.25}};

	SectorDesignPlan doom = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	ASSERT_TRUE(doom.valid());
	ASSERT_EQ(doom.shapes.size(), 1u);
	EXPECT_EQ(doom.plannedLines, 4);
	EXPECT_EQ(doom.shapes[0].outer[0].x, 0);
	EXPECT_EQ(doom.shapes[0].outer[0].y, 1);
	EXPECT_EQ(doc.numVertices(), 0);
	EXPECT_EQ(doc.numSectors(), 0);
	EXPECT_FALSE(doc.basis.undo());

	SectorDesignPlan udmf = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::udmf, request);
	ASSERT_TRUE(udmf.valid());
	EXPECT_DOUBLE_EQ(udmf.shapes[0].outer[0].x, 0.25);
	EXPECT_DOUBLE_EQ(udmf.shapes[0].outer[0].y, 0.75);
}

TEST_F(SmartSectorFixture, RectangleApplyIsOneUndoableOperation)
{
	SectorDesignRequest request;
	request.mode = SectorDesignMode::room;
	request.anchors = {{0, 0}, {128, 64}};
	request.properties.floorMode = SectorValueMode::absolute;
	request.properties.floorValue = 24;
	request.properties.ceilingMode = SectorValueMode::absolute;
	request.properties.ceilingValue = 160;

	SectorDesignPlan applied;
	ASSERT_TRUE(M_ApplySectorDesign(doc, config, nullptr, MapFormat::doom,
									request, &applied));
	ASSERT_EQ(doc.numSectors(), 1);
	EXPECT_EQ(doc.numVertices(), 4);
	EXPECT_EQ(doc.numLinedefs(), 4);
	EXPECT_EQ(doc.sectors[0]->floorh, 24);
	EXPECT_EQ(doc.sectors[0]->ceilh, 160);
	EXPECT_EQ(applied.createdSectors, std::vector<int>({0}));

	ASSERT_TRUE(doc.basis.undo());
	EXPECT_EQ(doc.numSectors(), 0);
	EXPECT_EQ(doc.numVertices(), 0);
	EXPECT_EQ(doc.numLinedefs(), 0);
	EXPECT_FALSE(doc.basis.undo());
	ASSERT_TRUE(doc.basis.redo());
	EXPECT_EQ(doc.numSectors(), 1);
	EXPECT_EQ(doc.sectors[0]->floorh, 24);
}

TEST_F(SmartSectorFixture, GeneratedSectorsSupportExplicitSpecialAndTag)
{
	config.sector_types[9].desc = "Damage";
	SectorDesignRequest request;
	request.mode = SectorDesignMode::room;
	request.anchors = {{0, 0}, {64, 64}};
	request.properties.sectorType = 9;
	request.properties.sectorTag = 27;

	ASSERT_TRUE(M_ApplySectorDesign(
			doc, config, nullptr, MapFormat::doom, request));
	ASSERT_EQ(doc.numSectors(), 1);
	EXPECT_EQ(doc.sectors[0]->type, 9);
	EXPECT_EQ(doc.sectors[0]->tag, 27);
	ASSERT_TRUE(doc.basis.undo());
	EXPECT_EQ(doc.numSectors(), 0);

	request.properties.sectorType =
			std::numeric_limits<uint16_t>::max();
	EXPECT_TRUE(M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request).valid());
	request.properties.sectorType =
			static_cast<int>(std::numeric_limits<uint16_t>::max()) + 1;
	EXPECT_FALSE(M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request).valid());
	EXPECT_TRUE(M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::udmf, request).valid());
	request.properties.sectorType = 9;
	request.properties.sectorTag = -12;
	EXPECT_TRUE(M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request).valid());
	request.properties.sectorTag =
			std::numeric_limits<int16_t>::max() + 1;
	EXPECT_FALSE(M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::hexen, request).valid());
	EXPECT_TRUE(M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::udmf, request).valid());
}

TEST_F(SmartSectorFixture, FreeformRejectsSelfIntersectionWithoutMutation)
{
	SectorDesignRequest request;
	request.mode = SectorDesignMode::freeform;
	request.anchors = {{0, 0}, {64, 64}, {0, 64}, {64, 0}};

	SectorDesignPlan plan = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	EXPECT_FALSE(plan.valid());
	EXPECT_FALSE(M_ApplySectorDesign(doc, config, nullptr, MapFormat::doom,
									 request));
	EXPECT_EQ(doc.numVertices(), 0);
	EXPECT_FALSE(doc.basis.undo());
}

TEST_F(SmartSectorFixture, RegularPolygonAndRoutedCorridorUseStableShapes)
{
	SectorDesignRequest polygon;
	polygon.mode = SectorDesignMode::polygon;
	polygon.anchors = {{0, 0}, {64, 0}};
	polygon.polygonSides = 8;
	SectorDesignPlan polygonPlan = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, polygon);
	ASSERT_TRUE(polygonPlan.valid());
	EXPECT_EQ(polygonPlan.shapes[0].outer.size(), 8u);

	SectorDesignRequest corridor;
	corridor.mode = SectorDesignMode::corridor;
	corridor.anchors = {{0, 0}, {128, 0}, {128, 128}};
	corridor.width = 48;
	corridor.join = SectorDesignJoin::round;
	SectorDesignPlan corridorPlan = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, corridor);
	ASSERT_TRUE(corridorPlan.valid());
	EXPECT_FALSE(corridorPlan.shapes.empty());
	EXPECT_GT(corridorPlan.shapes[0].outer.size(), 4u);
}

TEST_F(SmartSectorFixture,
	   PolygonProfilesProgressFromSimpleToExtremelyDetailedDeterministically)
{
	const std::array<int, 31> expectedVertices{
		8, 3, 4, 5, 6, 8, 10, 12,
		16, 32, 64, 96,
		10, 16, 24, 32,
		12, 16,
		32, 64, 96, 128,
		36, 48, 60, 96,
		48, 96, 144,
		192, 288
	};
	SectorDesignRequest request;
	request.mode = SectorDesignMode::polygon;
	request.anchors = {{0, 0}, {512, 0}};
	request.polygonSides = 8;
	request.polygonInnerRatio = 0.48;

	for (int profile = 0;
		 profile < static_cast<int>(expectedVertices.size()); ++profile)
	{
		request.polygonProfile =
				static_cast<SectorPolygonProfile>(profile);
		for (MapFormat format :
			 {MapFormat::doom, MapFormat::hexen, MapFormat::udmf})
		{
			const SectorDesignPlan first = M_PlanSectorDesign(
					doc, config, nullptr, format, request);
			const SectorDesignPlan second = M_PlanSectorDesign(
					doc, config, nullptr, format, request);
			ASSERT_TRUE(first.valid())
					<< profile << ", format " << static_cast<int>(format);
			ASSERT_EQ(first.shapes.size(), 1u) << profile;
			const int expected = profile ==
					static_cast<int>(
						SectorPolygonProfile::cathedralTracery48) &&
					format != MapFormat::udmf ?
					272 : expectedVertices[profile];
			EXPECT_EQ(first.shapes.front().outer.size(),
					  static_cast<size_t>(expected))
					<< profile;
			EXPECT_EQ(first.shapes.front().outer,
					  second.shapes.front().outer) << profile;
		}
	}
	EXPECT_EQ(doc.numVertices(), 0);
	EXPECT_EQ(doc.numSectors(), 0);
	EXPECT_FALSE(doc.basis.undo());

	request.polygonProfile = SectorPolygonProfile::customRegular;
	request.polygonSides = 2;
	EXPECT_FALSE(M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request).valid());
	request.polygonProfile = SectorPolygonProfile::triangle;
	EXPECT_TRUE(M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request).valid());
}

TEST_F(SmartSectorFixture,
	   ArchitecturePlansEveryStyleAndLayoutAsClosedHostSectorInserts)
{
	const int host = addBoxSector(-512, -512, 512, 512, 24, 160);
	SectorDesignRequest request;
	request.mode = SectorDesignMode::architecture;
	request.anchors = {{-384, -320}, {384, 320}};
	request.architectureBays = 3;
	request.architectureSize = 32;
	request.architectureMargin = 24;

	for (int style = 0;
		 style <= static_cast<int>(SectorArchitectureStyle::infernal);
		 ++style)
		for (int element = 0;
			 element <= static_cast<int>(
					SectorArchitectureElement::fortifiedKeep);
			 ++element)
		{
			request.architectureStyle =
					static_cast<SectorArchitectureStyle>(style);
			request.architectureElement =
					static_cast<SectorArchitectureElement>(element);
			for (MapFormat format :
				 {MapFormat::doom, MapFormat::hexen, MapFormat::udmf})
			{
				const SectorDesignPlan plan = M_PlanSectorDesign(
						doc, config, nullptr, format, request);
				ASSERT_TRUE(plan.valid())
						<< "style " << style << ", element " << element
						<< ", format " << static_cast<int>(format);
				ASSERT_GT(plan.plannedStructures, 0);
				ASSERT_EQ(plan.shapes.size(),
						  static_cast<size_t>(plan.plannedStructures));
				EXPECT_EQ(plan.retainedSectors,
						  std::vector<int>({host}));
				for (const PlannedSectorShape &shape : plan.shapes)
				{
					EXPECT_EQ(shape.role,
							  DesignPreviewRole::architecture);
					EXPECT_EQ(shape.modelSector, host);
					EXPECT_TRUE(shape.closed);
					EXPECT_GE(shape.outer.size(), 4u);
				}
			}
		}

	EXPECT_EQ(doc.numSectors(), 1);
	EXPECT_EQ(doc.numLinedefs(), 4);
	EXPECT_FALSE(doc.basis.undo());
}

TEST_F(SmartSectorFixture,
	   VolumetricArchitectureBuildsDistinctFloorWallCeilingAndWaterGeometry)
{
	const int host = addBoxSector(-1024, -768, 1024, 768, 24, 192);
	const int bystander =
			addBoxSector(1536, -256, 1792, 256, 8, 160);
	doc.sectors[host]->light = 176;
	doc.sectors[host]->tag = 41;
	doc.sectors[bystander]->light = 112;
	doc.sectors[bystander]->type = 9;
	doc.sectors[bystander]->tag = 73;
	const Sector originalHost = *doc.sectors[host];
	const Sector originalBystander = *doc.sectors[bystander];
	const int originalVertices = doc.numVertices();
	const int originalLines = doc.numLinedefs();
	const int originalSides = doc.numSidedefs();
	const int originalSectors = doc.numSectors();

	SectorDesignRequest request;
	request.mode = SectorDesignMode::architecture;
	request.anchors = {{-640, -480}, {640, 480}};
	request.architectureStyle = SectorArchitectureStyle::gothic;
	request.architectureBays = 4;
	request.architectureSize = 24;
	request.architectureHeight = 16;
	request.architectureMargin = 32;

	for (int element = static_cast<int>(
				SectorArchitectureElement::raisedDais);
		 element <= static_cast<int>(
				SectorArchitectureElement::raisedBridge);
		 ++element)
		for (MapFormat format :
			 {MapFormat::doom, MapFormat::hexen, MapFormat::udmf})
		{
			request.architectureElement =
					static_cast<SectorArchitectureElement>(element);
			const SectorDesignPlan preview = M_PlanSectorDesign(
					doc, config, nullptr, format, request);
			SString issueText;
			for (const SectorDesignIssue &issue : preview.issues)
				issueText += SString::printf(
						"\n- %s", issue.message.c_str());
			ASSERT_TRUE(preview.valid())
					<< "element " << element << ", format "
					<< static_cast<int>(format)
					<< issueText.c_str();
			ASSERT_GT(preview.plannedStructures, 0);
			ASSERT_EQ(preview.shapes.size(),
					  static_cast<size_t>(preview.plannedStructures));
			EXPECT_EQ(preview.retainedSectors,
					  std::vector<int>({host}));
			for (const PlannedSectorShape &shape : preview.shapes)
			{
				EXPECT_EQ(shape.role, ExpectedArchitectureRole(
						request.architectureElement));
				EXPECT_EQ(shape.modelSector, host);
				EXPECT_GE(shape.outer.size(), 4u);
			}

			switch (request.architectureElement)
			{
				case SectorArchitectureElement::raisedDais:
				ASSERT_EQ(preview.shapes.size(), 1u);
				EXPECT_GT(preview.shapes[0].floorDelta, 0);
				EXPECT_FALSE(preview.shapes[0].closed);
				break;
				case SectorArchitectureElement::sunkenCourt:
				ASSERT_EQ(preview.shapes.size(), 1u);
				EXPECT_LT(preview.shapes[0].floorDelta, 0);
				break;
				case SectorArchitectureElement::tieredZiggurat:
				ASSERT_EQ(preview.shapes.size(), 3u);
				EXPECT_EQ(preview.shapes[1].modelShape, 0);
				EXPECT_EQ(preview.shapes[2].modelShape, 1);
				EXPECT_TRUE(std::all_of(
						preview.shapes.begin(), preview.shapes.end(),
						[](const PlannedSectorShape &shape)
						{
							return shape.floorDelta > 0 &&
									!shape.closed;
						}));
				break;
				case SectorArchitectureElement::grandStair:
				ASSERT_EQ(preview.shapes.size(), 4u);
				for (size_t index = 0;
					 index < preview.shapes.size(); ++index)
					EXPECT_EQ(preview.shapes[index].floorDelta,
							  16 * static_cast<int>(index + 1));
				break;
				case SectorArchitectureElement::fountainBasin:
				ASSERT_EQ(preview.shapes.size(), 2u);
				EXPECT_LT(preview.shapes[0].floorDelta, 0);
				EXPECT_TRUE(preview.shapes[1].closed);
				EXPECT_EQ(preview.shapes[1].modelShape, 0);
				break;
				case SectorArchitectureElement::reflectingPool:
				ASSERT_EQ(preview.shapes.size(), 1u);
				EXPECT_LT(preview.shapes[0].floorDelta, 0);
				break;
				case SectorArchitectureElement::balconyGallery:
				ASSERT_EQ(preview.shapes.size(), 2u);
				EXPECT_GT(preview.shapes[0].floorDelta, 0);
				EXPECT_EQ(preview.shapes[1].modelShape, 0);
				EXPECT_LT(preview.shapes[1].floorDelta, 0);
				EXPECT_FALSE(preview.shapes[0].closed);
				EXPECT_FALSE(preview.shapes[1].closed);
				break;
				case SectorArchitectureElement::processionalChannel:
				ASSERT_EQ(preview.shapes.size(), 1u);
				EXPECT_LT(preview.shapes[0].floorDelta, 0);
				break;
				case SectorArchitectureElement::screenWall:
				ASSERT_EQ(preview.shapes.size(), 5u);
				EXPECT_TRUE(std::all_of(
						preview.shapes.begin(), preview.shapes.end(),
						[](const PlannedSectorShape &shape)
						{
							return shape.closed;
						}));
				break;
				case SectorArchitectureElement::cofferedCeiling:
				ASSERT_EQ(preview.shapes.size(), 8u);
				EXPECT_TRUE(std::all_of(
						preview.shapes.begin(), preview.shapes.end(),
						[](const PlannedSectorShape &shape)
						{
							return shape.ceilingDelta > 0 &&
									shape.floorDelta == 0 &&
									!shape.closed;
						}));
				break;
				case SectorArchitectureElement::groinVaults:
				ASSERT_EQ(preview.shapes.size(), 4u);
				EXPECT_TRUE(std::all_of(
						preview.shapes.begin(), preview.shapes.end(),
						[](const PlannedSectorShape &shape)
						{
							return shape.ceilingDelta > 0 &&
									!shape.closed;
						}));
				break;
				case SectorArchitectureElement::raisedBridge:
				ASSERT_EQ(preview.shapes.size(), 1u);
				EXPECT_GT(preview.shapes[0].floorDelta, 0);
				break;
				default:
					FAIL() << "unexpected volumetric element";
			}

			SectorDesignPlan applied;
			ASSERT_TRUE(M_ApplySectorDesign(
					doc, config, nullptr, format, request, &applied))
					<< "element " << element << ", format "
					<< static_cast<int>(format);
			ASSERT_EQ(applied.createdSectors.size(),
					  preview.shapes.size());
			std::map<int, bool> generatedSectorClosed;
			for (size_t shapeIndex = 0;
				 shapeIndex < preview.shapes.size(); ++shapeIndex)
			{
				generatedSectorClosed.emplace(
						applied.createdSectors[shapeIndex],
						preview.shapes[shapeIndex].closed);
			}
			std::vector<int> expectedFloors(
					preview.shapes.size(), originalHost.floorh);
			std::vector<int> expectedCeilings(
					preview.shapes.size(), originalHost.ceilh);
			for (size_t shapeIndex = 0;
				 shapeIndex < preview.shapes.size(); ++shapeIndex)
			{
				const PlannedSectorShape &shape =
						preview.shapes[shapeIndex];
				const int parent = shape.modelShape;
				const int baseFloor = parent >= 0 ?
						expectedFloors[parent] : originalHost.floorh;
				const int baseCeiling = parent >= 0 ?
						expectedCeilings[parent] : originalHost.ceilh;
				expectedFloors[shapeIndex] =
						baseFloor + shape.floorDelta;
				expectedCeilings[shapeIndex] = shape.closed ?
						expectedFloors[shapeIndex] :
						baseCeiling + shape.ceilingDelta;
				const int sector = applied.createdSectors[shapeIndex];
				ASSERT_TRUE(doc.isSector(sector));
				EXPECT_EQ(doc.sectors[sector]->floorh,
						  expectedFloors[shapeIndex]);
				EXPECT_EQ(doc.sectors[sector]->ceilh,
						  expectedCeilings[shapeIndex]);
				int connectedBoundaries = 0;
				for (const std::shared_ptr<LineDef> &line :
						doc.linedefs)
				{
					const int right = line->right >= 0 ?
							doc.sidedefs[line->right]->sector : -1;
					const int left = line->left >= 0 ?
							doc.sidedefs[line->left]->sector : -1;
					if (!((right == sector && left >= 0) ||
						  (left == sector && right >= 0)))
						continue;
					EXPECT_TRUE(line->TwoSided());
					const int neighbor = right == sector ? left : right;
					const auto neighborShape =
							generatedSectorClosed.find(neighbor);
					const bool solidBoundary = shape.closed ||
							(neighborShape != generatedSectorClosed.end() &&
							 neighborShape->second);
					if (solidBoundary)
						EXPECT_NE(
								line->flags & MLF_Blocking, 0u);
					else
						EXPECT_EQ(
								line->flags & MLF_Blocking, 0u);
					connectedBoundaries++;
				}
				EXPECT_GT(connectedBoundaries, 0)
						<< "element " << element << ", format "
						<< static_cast<int>(format)
						<< ", sector " << sector;
			}
			EXPECT_EQ(doc.sectors[host]->floorh, originalHost.floorh);
			EXPECT_EQ(doc.sectors[host]->ceilh, originalHost.ceilh);
			EXPECT_EQ(doc.sectors[host]->light, originalHost.light);
			EXPECT_EQ(doc.sectors[host]->tag, originalHost.tag);
			EXPECT_EQ(doc.sectors[bystander]->floorh,
					  originalBystander.floorh);
			EXPECT_EQ(doc.sectors[bystander]->ceilh,
					  originalBystander.ceilh);
			EXPECT_EQ(doc.sectors[bystander]->floor_tex,
					  originalBystander.floor_tex);
			EXPECT_EQ(doc.sectors[bystander]->ceil_tex,
					  originalBystander.ceil_tex);
			EXPECT_EQ(doc.sectors[bystander]->light,
					  originalBystander.light);
			EXPECT_EQ(doc.sectors[bystander]->type,
					  originalBystander.type);
			EXPECT_EQ(doc.sectors[bystander]->tag,
					  originalBystander.tag);

			ASSERT_TRUE(doc.basis.undo());
			EXPECT_EQ(doc.numVertices(), originalVertices);
			EXPECT_EQ(doc.numLinedefs(), originalLines);
			EXPECT_EQ(doc.numSidedefs(), originalSides);
			EXPECT_EQ(doc.numSectors(), originalSectors);
			EXPECT_FALSE(doc.basis.undo());
	}
}

TEST_F(SmartSectorFixture,
	   ArchitectureCatalogHasFiftyEightStableOrderedDescriptors)
{
	const auto &catalog = M_ArchitectureCatalog();
	ASSERT_EQ(catalog.size(), 58u);
	std::set<SString> ids;
	std::array<int, 6> families{};
	for (size_t index = 0; index < catalog.size(); ++index)
	{
		const SectorArchitectureDescriptor &descriptor = catalog[index];
		EXPECT_EQ(static_cast<size_t>(descriptor.element), index);
		EXPECT_TRUE(ids.insert(descriptor.id).second)
				<< descriptor.id;
		EXPECT_FALSE(SString(descriptor.label).empty());
		EXPECT_FALSE(SString(descriptor.description).empty());
		EXPECT_LE(descriptor.minimumBays, descriptor.defaultBays);
		EXPECT_GE(descriptor.maximumBays, descriptor.defaultBays);
		EXPECT_GE(descriptor.defaultSize, descriptor.minimumSize);
		EXPECT_TRUE(M_ArchitectureSupportsFunction(
				descriptor.element,
				SectorArchitectureFunction::staticGeometry));
		families[static_cast<size_t>(descriptor.family)]++;
		EXPECT_EQ(&M_ArchitectureDescriptor(descriptor.element),
				  &descriptor);
	}
	EXPECT_EQ(families, (std::array<int, 6>{21, 8, 8, 8, 6, 7}));
	EXPECT_STREQ(catalog.front().id, "pillar");
	EXPECT_STREQ(catalog[27].id, "raised_bridge");
	EXPECT_STREQ(catalog[28].id, "cross_core");
	EXPECT_STREQ(catalog.back().id, "beam_lattice");
	EXPECT_TRUE(M_ArchitectureSupportsFunction(
			SectorArchitectureElement::centralPlatform,
			SectorArchitectureFunction::smartLift));
	EXPECT_FALSE(M_ArchitectureSupportsFunction(
			SectorArchitectureElement::raisedDais,
			SectorArchitectureFunction::smartLift));
	EXPECT_TRUE(
			M_ArchitectureDescriptor(
					SectorArchitectureElement::switchbackStair).
					sizeUsesPlayerClearance);
	EXPECT_TRUE(
			M_ArchitectureDescriptor(
					SectorArchitectureElement::bifurcatedStair).
					sizeUsesPlayerClearance);
	EXPECT_TRUE(
			M_ArchitectureDescriptor(
					SectorArchitectureElement::landingCatwalk).
					sizeUsesPlayerClearance);
	EXPECT_TRUE(
			M_ArchitectureDescriptor(
					SectorArchitectureElement::crossingBridges).
					sizeUsesPlayerClearance);
	EXPECT_TRUE(
			M_ArchitectureDescriptor(
					SectorArchitectureElement::gatehousePassage).
					sizeUsesPlayerClearance);
	EXPECT_FALSE(
			M_ArchitectureDescriptor(
					SectorArchitectureElement::spiralStair).
					sizeUsesPlayerClearance);
	EXPECT_FALSE(
			M_ArchitectureDescriptor(
					SectorArchitectureElement::perimeterMoat).
					sizeUsesPlayerClearance);
	EXPECT_FALSE(
			M_ArchitectureDescriptor(
					SectorArchitectureElement::domedCeiling).
					sizeUsesPlayerClearance);
}

TEST_F(SmartSectorFixture,
	   EveryStage25StructurePlansAndAppliesInsideAHostInEveryFormat)
{
	const int host = addBoxSector(
			-2048, -1536, 2048, 1536, 24, 256);
	const Sector originalHost = *doc.sectors[host];
	const int originalVertices = doc.numVertices();
	const int originalLines = doc.numLinedefs();
	const int originalSides = doc.numSidedefs();
	const int originalSectors = doc.numSectors();

	for (const SectorArchitectureDescriptor &descriptor :
			M_ArchitectureCatalog())
	{
		if (static_cast<int>(descriptor.element) <
			static_cast<int>(SectorArchitectureElement::crossCore))
			continue;
		for (MapFormat format :
				{MapFormat::doom, MapFormat::hexen, MapFormat::udmf})
		{
			SectorDesignRequest request;
			request.mode = SectorDesignMode::architecture;
			request.anchors = {{-768, -576}, {768, 576}};
			request.architectureElement = descriptor.element;
			request.architectureStyle =
					SectorArchitectureStyle::functional;
			request.architectureBays = descriptor.defaultBays;
			request.architectureSize =
					std::max(16.0, descriptor.defaultSize);
			request.architectureHeight =
					std::max(8.0, descriptor.defaultHeight);
			request.architectureMargin = 64;

			const SectorDesignPlan first = M_PlanSectorDesign(
					doc, config, nullptr, format, request);
			const SectorDesignPlan second = M_PlanSectorDesign(
					doc, config, nullptr, format, request);
			SString issues;
			for (const SectorDesignIssue &issue : first.issues)
				issues += SString::printf("\n- %s",
						issue.message.c_str());
			ASSERT_TRUE(first.valid())
					<< descriptor.id << ", format "
					<< static_cast<int>(format) << issues.c_str();
			ASSERT_GT(first.plannedStructures, 0)
					<< descriptor.id;
			ASSERT_EQ(first.shapes.size(),
					static_cast<size_t>(first.plannedStructures));
			ASSERT_EQ(first.shapes.size(), second.shapes.size());
			for (size_t shapeIndex = 0;
				 shapeIndex < first.shapes.size(); ++shapeIndex)
			{
				const PlannedSectorShape &shape =
						first.shapes[shapeIndex];
				EXPECT_EQ(shape.outer,
						  second.shapes[shapeIndex].outer)
						<< descriptor.id;
				EXPECT_EQ(shape.role, descriptor.role)
						<< descriptor.id;
				EXPECT_EQ(shape.modelSector, host)
						<< descriptor.id;
			}

			SectorDesignPlan applied;
			const bool appliedOkay = M_ApplySectorDesign(
					doc, config, nullptr, format, request, &applied);
			SString applyIssues;
			for (const SectorDesignIssue &issue : applied.issues)
				applyIssues += SString::printf(
						"\n- %s", issue.message.c_str());
			ASSERT_TRUE(appliedOkay)
					<< descriptor.id << ", format "
					<< static_cast<int>(format)
					<< applyIssues.c_str();
			EXPECT_EQ(doc.sectors[host]->floorh,
					  originalHost.floorh);
			EXPECT_EQ(doc.sectors[host]->ceilh,
					  originalHost.ceilh);
			EXPECT_EQ(doc.sectors[host]->light,
					  originalHost.light);
			if (descriptor.family ==
						SectorArchitectureFamily::structuralSupports ||
				descriptor.family ==
						SectorArchitectureFamily::wallsScreens)
			{
				const std::set<int> generated(
						applied.createdSectors.begin(),
						applied.createdSectors.end());
				int solidBoundaries = 0;
				for (const std::shared_ptr<LineDef> &line :
						doc.linedefs)
				{
					const int right = line->right >= 0 ?
							doc.sidedefs[line->right]->sector : -1;
					const int left = line->left >= 0 ?
							doc.sidedefs[line->left]->sector : -1;
					if (generated.count(right) ==
						generated.count(left))
						continue;
					solidBoundaries++;
					EXPECT_NE(line->flags & MLF_Blocking, 0u)
							<< descriptor.id;
				}
				EXPECT_GT(solidBoundaries, 0)
						<< descriptor.id;
			}
			ASSERT_TRUE(doc.basis.undo()) << descriptor.id;
			EXPECT_EQ(doc.numVertices(), originalVertices);
			EXPECT_EQ(doc.numLinedefs(), originalLines);
			EXPECT_EQ(doc.numSidedefs(), originalSides);
			EXPECT_EQ(doc.numSectors(), originalSectors);
			EXPECT_FALSE(doc.basis.undo());
		}
	}
}

TEST_F(SmartSectorFixture,
	   PostSwitchbackCatalogTailWorksAtPracticalFootprintsInBothAxes)
{
	addBoxSector(-512, -512, 512, 512, 24, 256);
	const int first = static_cast<int>(
			SectorArchitectureElement::bifurcatedStair);
	for (const SectorArchitectureDescriptor &descriptor :
			M_ArchitectureCatalog())
	{
		if (static_cast<int>(descriptor.element) < first)
			continue;
		for (const std::pair<v2double_t, v2double_t> &drag : {
				std::pair<v2double_t, v2double_t>{
						{-128, -96}, {128, 96}},
				std::pair<v2double_t, v2double_t>{
						{-96, -128}, {96, 128}}
			 })
		{
			SectorDesignRequest request;
			request.mode = SectorDesignMode::architecture;
			request.anchors = {drag.first, drag.second};
			request.architectureElement = descriptor.element;
			request.architectureStyle =
					SectorArchitectureStyle::gothic;
			request.architectureBays = descriptor.defaultBays;
			request.architectureSize = std::max(
					descriptor.defaultSize,
					descriptor.minimumSize);
			request.architectureHeight =
					descriptor.defaultHeight;
			request.architectureMargin = 16;

			const SectorDesignPlan preview = M_PlanSectorDesign(
					doc, config, nullptr, MapFormat::doom, request);
			SString issues;
			for (const SectorDesignIssue &issue : preview.issues)
				issues += SString::printf(
						"\n- %s", issue.message.c_str());
			ASSERT_TRUE(preview.valid())
					<< descriptor.id << issues.c_str();
			ASSERT_GT(preview.plannedStructures, 0)
					<< descriptor.id;
		}
	}
}

TEST_F(SmartSectorFixture,
	   WallAndCeilingFamiliesApplyTheirPhysicalEffectsInEveryFormat)
{
	const int host = addBoxSector(
			-512, -512, 512, 512, 24, 192);
	doc.sectors[host]->light = 176;
	doc.sectors[host]->tag = 41;
	const Sector originalHost = *doc.sectors[host];
	const int originalVertices = doc.numVertices();
	const int originalLines = doc.numLinedefs();
	const int originalSides = doc.numSidedefs();
	const int originalSectors = doc.numSectors();

	for (const SectorArchitectureDescriptor &descriptor :
			M_ArchitectureCatalog())
	{
		const bool wall =
				descriptor.family ==
						SectorArchitectureFamily::wallsScreens;
		const bool ceiling =
				descriptor.family ==
						SectorArchitectureFamily::ceilingsVaults;
		if (!wall && !ceiling)
			continue;

		for (MapFormat format :
				{MapFormat::doom, MapFormat::hexen, MapFormat::udmf})
		{
			SectorDesignRequest request;
			request.mode = SectorDesignMode::architecture;
			request.anchors = {{-256, -192}, {256, 192}};
			request.architectureElement = descriptor.element;
			request.architectureStyle =
					SectorArchitectureStyle::gothic;
			request.architectureBays = descriptor.defaultBays;
			request.architectureSize =
					descriptor.defaultSize;
			request.architectureHeight =
					descriptor.defaultHeight;
			request.architectureMargin =
					descriptor.defaultMargin;

			SectorDesignPlan applied;
			const bool didApply = M_ApplySectorDesign(
					doc, config, nullptr, format,
					request, &applied);
			SString applyIssues;
			for (const SectorDesignIssue &issue : applied.issues)
				applyIssues += SString::printf(
						"\n- %s", issue.message.c_str());
			ASSERT_TRUE(didApply)
					<< descriptor.id << ", format "
					<< static_cast<int>(format)
					<< applyIssues.c_str();
			ASSERT_EQ(applied.createdSectors.size(),
					  applied.shapes.size())
					<< descriptor.id;
			const std::set<int> generated(
					applied.createdSectors.begin(),
					applied.createdSectors.end());
			for (int sector : applied.createdSectors)
			{
				ASSERT_TRUE(doc.isSector(sector));
				EXPECT_EQ(doc.sectors[sector]->floorh,
						  originalHost.floorh)
						<< descriptor.id;
				if (wall)
					EXPECT_EQ(doc.sectors[sector]->ceilh,
							  originalHost.floorh)
							<< descriptor.id;
				else
					EXPECT_NE(doc.sectors[sector]->ceilh,
							  originalHost.ceilh)
							<< descriptor.id;
				EXPECT_EQ(doc.sectors[sector]->light,
						  originalHost.light)
						<< descriptor.id;
				EXPECT_EQ(doc.sectors[sector]->tag,
						  originalHost.tag)
						<< descriptor.id;
			}

			int interfaces = 0;
			for (const std::shared_ptr<LineDef> &line :
					doc.linedefs)
			{
				const int right = line->right >= 0 ?
						doc.sidedefs[line->right]->sector : -1;
				const int left = line->left >= 0 ?
						doc.sidedefs[line->left]->sector : -1;
				if (!generated.count(right) &&
					!generated.count(left))
					continue;
				ASSERT_TRUE(line->TwoSided())
						<< descriptor.id;
				if (wall)
					EXPECT_NE(line->flags & MLF_Blocking, 0u)
							<< descriptor.id;
				else
					EXPECT_EQ(line->flags & MLF_Blocking, 0u)
							<< descriptor.id;
				interfaces++;
			}
			EXPECT_GT(interfaces, 0) << descriptor.id;
			EXPECT_EQ(doc.sectors[host]->floorh,
					  originalHost.floorh);
			EXPECT_EQ(doc.sectors[host]->ceilh,
					  originalHost.ceilh);
			EXPECT_EQ(doc.sectors[host]->light,
					  originalHost.light);
			EXPECT_EQ(doc.sectors[host]->tag,
					  originalHost.tag);

			ASSERT_TRUE(doc.basis.undo()) << descriptor.id;
			EXPECT_EQ(doc.numVertices(), originalVertices);
			EXPECT_EQ(doc.numLinedefs(), originalLines);
			EXPECT_EQ(doc.numSidedefs(), originalSides);
			EXPECT_EQ(doc.numSectors(), originalSectors);
			EXPECT_FALSE(doc.basis.undo());
		}
	}
}

TEST_F(SmartSectorFixture,
	   WallAndScreenFootprintsFollowTheGestureLongAxis)
{
	addBoxSector(-512, -512, 512, 512, 24, 192);

	struct Bounds
	{
		double minX = std::numeric_limits<double>::infinity();
		double minY = std::numeric_limits<double>::infinity();
		double maxX = -std::numeric_limits<double>::infinity();
		double maxY = -std::numeric_limits<double>::infinity();
	};
	auto includeShape = [](Bounds &bounds,
						   const PlannedSectorShape &shape)
	{
		for (const v2double_t &point : shape.outer)
		{
			bounds.minX = std::min(bounds.minX, point.x);
			bounds.minY = std::min(bounds.minY, point.y);
			bounds.maxX = std::max(bounds.maxX, point.x);
			bounds.maxY = std::max(bounds.maxY, point.y);
		}
	};

	for (const SectorArchitectureDescriptor &descriptor :
			M_ArchitectureCatalog())
	{
		if (descriptor.family !=
				SectorArchitectureFamily::wallsScreens)
			continue;

		SectorDesignRequest request;
		request.mode = SectorDesignMode::architecture;
		request.anchors = {{-384, -96}, {384, 96}};
		request.architectureElement = descriptor.element;
		request.architectureStyle =
				SectorArchitectureStyle::gothic;
		request.architectureBays = descriptor.defaultBays;
		request.architectureSize = descriptor.defaultSize;
		request.architectureHeight = descriptor.defaultHeight;
		request.architectureMargin = descriptor.defaultMargin;

		const SectorDesignPlan plan = M_PlanSectorDesign(
				doc, config, nullptr, MapFormat::doom, request);
		ASSERT_TRUE(plan.valid()) << descriptor.id;
		ASSERT_FALSE(plan.shapes.empty()) << descriptor.id;
		EXPECT_TRUE(std::all_of(
				plan.shapes.begin(), plan.shapes.end(),
				[](const PlannedSectorShape &shape)
				{
					return shape.closed;
				})) << descriptor.id;
		Bounds bounds;
		for (const PlannedSectorShape &shape : plan.shapes)
			includeShape(bounds, shape);
		EXPECT_GT(bounds.maxX - bounds.minX,
				  (bounds.maxY - bounds.minY) * 2.0)
				<< descriptor.id;

		if (descriptor.element ==
				SectorArchitectureElement::gatehousePassage)
		{
			ASSERT_EQ(plan.shapes.size(), 2u);
			std::array<Bounds, 2> masses;
			includeShape(masses[0], plan.shapes[0]);
			includeShape(masses[1], plan.shapes[1]);
			if (masses[0].minX > masses[1].minX)
				std::swap(masses[0], masses[1]);
			EXPECT_LT(masses[0].maxX, 0.0);
			EXPECT_GT(masses[1].minX, 0.0);
			EXPECT_GE(
					masses[1].minX - masses[0].maxX,
					descriptor.defaultSize - 0.01);
		}
	}
}

TEST_F(SmartSectorFixture,
	   CrossVaultAndBeamLatticeBuildDistinctCeilingTopology)
{
	const int host = addBoxSector(
			-512, -512, 512, 512, 24, 192);
	doc.sectors[host]->light = 176;
	doc.sectors[host]->tag = 41;
	const Sector originalHost = *doc.sectors[host];
	const int originalVertices = doc.numVertices();
	const int originalLines = doc.numLinedefs();
	const int originalSides = doc.numSidedefs();
	const int originalSectors = doc.numSectors();

	const SectorArchitectureDescriptor &vaultDescriptor =
			M_ArchitectureDescriptor(
					SectorArchitectureElement::ribbedCrossVault);
	SectorDesignRequest vault;
	vault.mode = SectorDesignMode::architecture;
	vault.anchors = {{-256, -192}, {256, 192}};
	vault.architectureElement =
			SectorArchitectureElement::ribbedCrossVault;
	vault.architectureBays = vaultDescriptor.defaultBays;
	vault.architectureSize = vaultDescriptor.defaultSize;
	vault.architectureHeight = vaultDescriptor.defaultHeight;
	vault.architectureMargin = vaultDescriptor.defaultMargin;
	const SectorDesignPlan vaultPlan = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, vault);
	ASSERT_TRUE(vaultPlan.valid());
	ASSERT_EQ(vaultPlan.shapes.size(), 5u);
	const int fullRelief = static_cast<int>(
			vaultDescriptor.defaultHeight);
	const int quadrantRelief = std::max(1, fullRelief / 2);
	EXPECT_EQ(std::count_if(
			vaultPlan.shapes.begin(), vaultPlan.shapes.end(),
			[&](const PlannedSectorShape &shape)
			{
				return shape.ceilingDelta == fullRelief;
			}), 1);
	EXPECT_EQ(std::count_if(
			vaultPlan.shapes.begin(), vaultPlan.shapes.end(),
			[&](const PlannedSectorShape &shape)
			{
				return shape.ceilingDelta == quadrantRelief;
			}), 4);
	EXPECT_TRUE(std::all_of(
			vaultPlan.shapes.begin(), vaultPlan.shapes.end(),
			[](const PlannedSectorShape &shape)
			{
				return shape.floorDelta == 0 && !shape.closed &&
						shape.holes.empty();
			}));

	const SectorArchitectureDescriptor &beamDescriptor =
			M_ArchitectureDescriptor(
					SectorArchitectureElement::beamLattice);
	SectorDesignRequest beam;
	beam.mode = SectorDesignMode::architecture;
	beam.anchors = {{-256, -192}, {256, 192}};
	beam.architectureElement =
			SectorArchitectureElement::beamLattice;
	beam.architectureBays = beamDescriptor.defaultBays;
	beam.architectureSize = beamDescriptor.defaultSize;
	beam.architectureHeight = beamDescriptor.defaultHeight;
	beam.architectureMargin = beamDescriptor.defaultMargin;
	const SectorDesignPlan beamPlan = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, beam);
	ASSERT_TRUE(beamPlan.valid());
	ASSERT_EQ(beamPlan.shapes.size(), 1u);
	EXPECT_EQ(beamPlan.shapes.front().holes.size(), 9u);
	EXPECT_EQ(beamPlan.plannedRetainedCells, 9);
	EXPECT_TRUE(beamPlan.shapes.front().holesRetainModel);
	EXPECT_EQ(beamPlan.shapes.front().floorDelta, 0);
	EXPECT_EQ(beamPlan.shapes.front().ceilingDelta,
			  -static_cast<int>(beamDescriptor.defaultHeight));
	EXPECT_GT(beamPlan.shapes.front().outer.size(), 8u);

	SectorDesignPlan applied;
	ASSERT_TRUE(M_ApplySectorDesign(
			doc, config, nullptr, MapFormat::doom,
			beam, &applied));
	ASSERT_EQ(applied.createdSectors.size(), 1u);
	const int beamSector = applied.createdSectors.front();
	ASSERT_TRUE(doc.isSector(beamSector));
	EXPECT_EQ(doc.sectors[beamSector]->floorh,
			  originalHost.floorh);
	EXPECT_EQ(doc.sectors[beamSector]->ceilh,
			  originalHost.ceilh -
					static_cast<int>(
							beamDescriptor.defaultHeight));
	EXPECT_EQ(doc.numSectors(),
			  originalSectors + 1 +
					static_cast<int>(
							beamPlan.shapes.front().holes.size()));
	for (int sector = originalSectors;
		 sector < doc.numSectors(); ++sector)
	{
		if (sector == beamSector)
			continue;
		EXPECT_EQ(doc.sectors[sector]->floorh, originalHost.floorh);
		EXPECT_EQ(doc.sectors[sector]->ceilh, originalHost.ceilh);
		EXPECT_EQ(doc.sectors[sector]->light, originalHost.light);
		EXPECT_EQ(doc.sectors[sector]->tag, originalHost.tag);
	}
	for (const std::shared_ptr<LineDef> &line : doc.linedefs)
	{
		const int right = line->right >= 0 ?
				doc.sidedefs[line->right]->sector : -1;
		const int left = line->left >= 0 ?
				doc.sidedefs[line->left]->sector : -1;
		if (right == beamSector || left == beamSector)
		{
			EXPECT_TRUE(line->TwoSided());
			EXPECT_EQ(line->flags & MLF_Blocking, 0u);
		}
	}

	ASSERT_TRUE(doc.basis.undo());
	EXPECT_EQ(doc.numVertices(), originalVertices);
	EXPECT_EQ(doc.numLinedefs(), originalLines);
	EXPECT_EQ(doc.numSidedefs(), originalSides);
	EXPECT_EQ(doc.numSectors(), originalSectors);
	EXPECT_FALSE(doc.basis.undo());
}

TEST_F(SmartSectorFixture,
	   EveryStage25StructureBuildsANewVoidHallInEveryFormat)
{
	for (const SectorArchitectureDescriptor &descriptor :
			M_ArchitectureCatalog())
	{
		if (static_cast<int>(descriptor.element) <
			static_cast<int>(SectorArchitectureElement::crossCore))
			continue;
		for (MapFormat format :
				{MapFormat::doom, MapFormat::hexen, MapFormat::udmf})
		{
			SectorDesignRequest request;
			request.mode = SectorDesignMode::architecture;
			request.anchors = {{-768, -576}, {768, 576}};
			request.architectureElement = descriptor.element;
			request.architectureStyle =
					SectorArchitectureStyle::functional;
			request.architectureBays = descriptor.defaultBays;
			request.architectureSize =
					std::max(16.0, descriptor.defaultSize);
			request.architectureHeight =
					std::max(8.0, descriptor.defaultHeight);
			request.architectureMargin = 64;
			request.properties.floorMode =
					SectorValueMode::absolute;
			request.properties.floorValue = 24;
			request.properties.ceilingMode =
					SectorValueMode::absolute;
			request.properties.ceilingValue = 256;

			const SectorDesignPlan preview = M_PlanSectorDesign(
					doc, config, nullptr, format, request);
			SString issues;
			for (const SectorDesignIssue &issue : preview.issues)
				issues += SString::printf(
						"\n- %s", issue.message.c_str());
			ASSERT_TRUE(preview.valid())
					<< descriptor.id << ", format "
					<< static_cast<int>(format) << issues.c_str();
			EXPECT_EQ(preview.plannedArchitectureHosts, 1);
			ASSERT_EQ(preview.shapes.size(),
					static_cast<size_t>(
							preview.plannedStructures + 1));
			EXPECT_EQ(preview.shapes.front().role,
					  DesignPreviewRole::proposed);
			for (size_t index = 1; index < preview.shapes.size(); ++index)
			{
				EXPECT_TRUE(preview.shapes[index].inheritModelOnly)
						<< descriptor.id;
				EXPECT_GE(preview.shapes[index].modelShape, 0)
						<< descriptor.id;
			}

			SectorDesignPlan applied;
			ASSERT_TRUE(M_ApplySectorDesign(
					doc, config, nullptr, format, request, &applied))
					<< descriptor.id << ", format "
					<< static_cast<int>(format);
			EXPECT_EQ(applied.createdSectors.size(),
					static_cast<size_t>(
							applied.plannedStructures + 1));
			ASSERT_TRUE(doc.basis.undo());
			EXPECT_EQ(doc.numVertices(), 0);
			EXPECT_EQ(doc.numLinedefs(), 0);
			EXPECT_EQ(doc.numSidedefs(), 0);
			EXPECT_EQ(doc.numSectors(), 0);
			EXPECT_FALSE(doc.basis.undo());
		}
	}
}

TEST_F(SmartSectorFixture,
	   Stage25StyleAwareAndMirroredStructuresChangeTheirOutlines)
{
	addBoxSector(-2048, -1536, 2048, 1536, 24, 256);
	const std::array<SectorArchitectureElement, 8> styled{
		SectorArchitectureElement::crossCore,
		SectorArchitectureElement::hollowTower,
		SectorArchitectureElement::buttressedTower,
		SectorArchitectureElement::steppedMonument,
		SectorArchitectureElement::octagonalPodium,
		SectorArchitectureElement::fountainCourt,
		SectorArchitectureElement::gatehousePassage,
		SectorArchitectureElement::domedCeiling
	};
	for (SectorArchitectureElement element : styled)
	{
		std::vector<std::vector<std::vector<v2double_t>>> outlines;
		for (int style = 0;
			 style <= static_cast<int>(
					SectorArchitectureStyle::infernal);
			 ++style)
		{
			const SectorArchitectureDescriptor &descriptor =
					M_ArchitectureDescriptor(element);
			SectorDesignRequest request;
			request.mode = SectorDesignMode::architecture;
			request.anchors = {{-768, -576}, {768, 576}};
			request.architectureElement = element;
			request.architectureStyle =
					static_cast<SectorArchitectureStyle>(style);
			request.architectureBays = descriptor.defaultBays;
			request.architectureSize = std::max(
					32.0,
					M_MinimumArchitectureSize(
							request.architectureStyle, element));
			request.architectureHeight = 16;
			request.architectureMargin = 64;
			const SectorDesignPlan plan = M_PlanSectorDesign(
					doc, config, nullptr, MapFormat::doom, request);
			ASSERT_TRUE(plan.valid())
					<< descriptor.id << ", style " << style;
			std::vector<std::vector<v2double_t>> signature;
			for (const PlannedSectorShape &shape : plan.shapes)
				signature.push_back(shape.outer);
			outlines.push_back(std::move(signature));
		}
		for (size_t style = 1; style < outlines.size(); ++style)
			EXPECT_NE(outlines[style - 1], outlines[style])
					<< M_ArchitectureDescriptor(element).id
					<< ", style " << style;
	}

	const std::array<SectorArchitectureElement, 5> mirrored{
		SectorArchitectureElement::cornerTerraces,
		SectorArchitectureElement::switchbackStair,
		SectorArchitectureElement::bifurcatedStair,
		SectorArchitectureElement::spiralStair,
		SectorArchitectureElement::staggeredScreen
	};
	for (SectorArchitectureElement element : mirrored)
	{
		const SectorArchitectureDescriptor &descriptor =
				M_ArchitectureDescriptor(element);
		SectorDesignRequest request;
		request.mode = SectorDesignMode::architecture;
		request.anchors = {{-768, -576}, {768, 576}};
		request.architectureElement = element;
		request.architectureBays = descriptor.defaultBays;
		request.architectureSize =
				std::max(24.0, descriptor.defaultSize);
		request.architectureHeight = 8;
		request.architectureMargin = 64;
		const SectorDesignPlan normal = M_PlanSectorDesign(
				doc, config, nullptr, MapFormat::doom, request);
		request.architectureMirrored = true;
		const SectorDesignPlan flipped = M_PlanSectorDesign(
				doc, config, nullptr, MapFormat::doom, request);
		ASSERT_TRUE(normal.valid()) << descriptor.id;
		ASSERT_TRUE(flipped.valid()) << descriptor.id;
		ASSERT_EQ(normal.shapes.size(), flipped.shapes.size());
		bool differs = false;
		for (size_t index = 0; index < normal.shapes.size(); ++index)
			differs = differs ||
					normal.shapes[index].outer !=
							flipped.shapes[index].outer;
		EXPECT_TRUE(differs) << descriptor.id;
	}
}

TEST_F(SmartSectorFixture,
	   Stage25AnchorOrderControlsCascadeAndStairDirection)
{
	addBoxSector(-1024, -768, 1024, 768, 24, 256);
	for (SectorArchitectureElement element :
			{SectorArchitectureElement::steppedCascade,
			 SectorArchitectureElement::splitLevelStage,
			 SectorArchitectureElement::switchbackStair})
	{
		const SectorArchitectureDescriptor &descriptor =
				M_ArchitectureDescriptor(element);
		SectorDesignRequest request;
		request.mode = SectorDesignMode::architecture;
		request.anchors = {{-640, -384}, {640, 384}};
		request.architectureElement = element;
		request.architectureBays = descriptor.defaultBays;
		request.architectureSize =
				std::max(24.0, descriptor.defaultSize);
		request.architectureHeight = 8;
		request.architectureMargin = 48;
		const SectorDesignPlan forward = M_PlanSectorDesign(
				doc, config, nullptr, MapFormat::doom, request);
		std::reverse(request.anchors.begin(), request.anchors.end());
		const SectorDesignPlan reverse = M_PlanSectorDesign(
				doc, config, nullptr, MapFormat::doom, request);
		ASSERT_TRUE(forward.valid()) << descriptor.id;
		ASSERT_TRUE(reverse.valid()) << descriptor.id;
		ASSERT_EQ(forward.shapes.size(), reverse.shapes.size());
		EXPECT_NE(forward.shapes.front().outer,
				  reverse.shapes.front().outer)
				<< descriptor.id;
	}
}

TEST_F(SmartSectorFixture,
	   Stage25PreviewLabelsClearanceAndDescriptorLimitsAreValidated)
{
	addBoxSector(-1024, -768, 1024, 768, 0, 128);
	config.miscInfo.player_h = 56;
	SectorDesignRequest request;
	request.mode = SectorDesignMode::architecture;
	request.anchors = {{-640, -384}, {640, 384}};
	request.architectureElement =
			SectorArchitectureElement::switchbackStair;
	request.architectureBays = 4;
	request.architectureSize = 24;
	request.architectureHeight = 12;
	request.architectureMargin = 48;
	const SectorDesignPlan preview = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	ASSERT_TRUE(preview.valid());
	EXPECT_TRUE(std::any_of(
			preview.previewLabels.begin(),
			preview.previewLabels.end(),
			[](const DesignPreviewLabel &label)
			{
				return label.text.find("forward ->") !=
						SString::npos;
			}));
	EXPECT_TRUE(std::any_of(
			preview.previewLabels.begin(),
			preview.previewLabels.end(),
			[](const DesignPreviewLabel &label)
			{
				return label.text.find("floor +") !=
						SString::npos;
			}));
	EXPECT_TRUE(std::any_of(
			preview.issues.begin(), preview.issues.end(),
			[](const SectorDesignIssue &issue)
			{
				return issue.severity ==
							SectorDesignIssueSeverity::warning &&
						issue.message.find("player height") !=
							SString::npos;
			}));

	config.miscInfo.player_r = 32;
	request.architectureElement =
			SectorArchitectureElement::gatehousePassage;
	request.architectureBays = 1;
	request.architectureSize = 32;
	request.architectureHeight = 16;
	const SectorDesignPlan narrowGate = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	ASSERT_TRUE(narrowGate.valid());
	EXPECT_TRUE(std::any_of(
			narrowGate.issues.begin(), narrowGate.issues.end(),
			[](const SectorDesignIssue &issue)
			{
				return issue.message.find("player diameter") !=
						SString::npos;
			}));

	request.architectureElement =
			SectorArchitectureElement::spiralStair;
	request.architectureBays = 5;
	EXPECT_FALSE(M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request).valid());
	request.architectureBays = 12;
	request.architectureSize = 1;
	EXPECT_FALSE(M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request).valid());
	request.architectureSize = 24;
	request.rotation =
			std::numeric_limits<double>::quiet_NaN();
	EXPECT_FALSE(M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request).valid());
	const int sectors = doc.numSectors();
	EXPECT_FALSE(M_ApplySectorDesign(
			doc, config, nullptr, MapFormat::doom, request));
	EXPECT_EQ(doc.numSectors(), sectors);
	EXPECT_FALSE(doc.basis.undo());
}

TEST_F(SmartSectorFixture,
	   CentralPlatformSmartLiftIsOptionalAndAtomicAcrossFormats)
{
	config.line_types[62].desc = "Encoded lift";
	config.line_types[88].desc = "Tagged lift";
	SectorActionPreset encoded;
	encoded.kind = SectorActionKind::lift;
	encoded.id = "encoded";
	encoded.label = "Encoded lift";
	encoded.special = 62;
	encoded.activation = ActivationPolicy::encoded;
	config.sector_action_presets.push_back(encoded);
	SectorActionPreset tagged;
	tagged.kind = SectorActionKind::lift;
	tagged.id = "tagged";
	tagged.label = "Tagged lift";
	tagged.special = 88;
	tagged.activation = ActivationPolicy::useRepeat;
	tagged.args[0].targetTag = true;
	tagged.args[1].value = 16;
	config.sector_action_presets.push_back(tagged);

	for (MapFormat format :
			{MapFormat::doom, MapFormat::hexen, MapFormat::udmf})
	{
		const int host = addBoxSector(
				-512, -512, 512, 512, 24, 192);
		SectorDesignRequest request;
		request.mode = SectorDesignMode::architecture;
		request.anchors = {{-256, -192}, {256, 192}};
		request.architectureElement =
				SectorArchitectureElement::centralPlatform;
		request.architectureSize = 24;
		request.architectureHeight = 32;
		request.architectureMargin = 32;
		request.architectureFunction =
				SectorArchitectureFunction::smartLift;
		request.actionPresetId =
				format == MapFormat::doom ? "encoded" : "tagged";

		const SectorDesignPlan preview = M_PlanSectorDesign(
				doc, config, nullptr, format, request);
		ASSERT_TRUE(preview.valid());
		ASSERT_EQ(preview.shapes.size(), 1u);
		EXPECT_TRUE(preview.shapes.front().smartLift);
		EXPECT_EQ(preview.shapes.front().role,
				  DesignPreviewRole::lift);
		EXPECT_EQ(preview.plannedLifts, 1);
		ASSERT_TRUE(preview.resolvedActionPreset);
		EXPECT_EQ(preview.resolvedActionPreset->id,
				  request.actionPresetId);

		SectorDesignPlan applied;
		ASSERT_TRUE(M_ApplySectorDesign(
				doc, config, nullptr, format, request, &applied));
		ASSERT_EQ(applied.createdSectors.size(), 1u);
		const int platform = applied.createdSectors.front();
		EXPECT_EQ(doc.sectors[platform]->floorh, 56);
		EXPECT_GT(doc.sectors[platform]->tag, 0);
		int triggers = 0;
		for (const std::shared_ptr<LineDef> &line : doc.linedefs)
			if (line->type ==
				(format == MapFormat::doom ? 62 : 88))
				triggers++;
		EXPECT_GT(triggers, 0);
		ASSERT_TRUE(doc.basis.undo());
		EXPECT_EQ(doc.numSectors(), host + 1);
		EXPECT_FALSE(doc.basis.undo());
		doc.vertices.clear();
		doc.linedefs.clear();
		doc.sidedefs.clear();
		doc.sectors.clear();
	}

	const int host = addBoxSector(
			-512, -512, 512, 512, 24, 192);
	SectorDesignRequest unsupported;
	unsupported.mode = SectorDesignMode::architecture;
	unsupported.anchors = {{-256, -192}, {256, 192}};
	unsupported.architectureElement =
			SectorArchitectureElement::raisedDais;
	unsupported.architectureFunction =
			SectorArchitectureFunction::smartLift;
	const SectorDesignPlan rejected = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, unsupported);
	EXPECT_FALSE(rejected.valid());
	EXPECT_EQ(doc.numSectors(), host + 1);
	EXPECT_FALSE(doc.basis.undo());
}

TEST_F(SmartSectorFixture,
	   ArchitectureFloorConnectsHostAndAdjacentSectorAcrossEitherWinding)
{
	const int host = addSector(24, 192);
	const int neighbor = addSector(24, 192);
	const int v0 = addVertex(-256, -256);
	const int v1 = addVertex(-256, 256);
	const int v2 = addVertex(0, 256);
	const int v3 = addVertex(0, -256);
	const int v4 = addVertex(256, 256);
	const int v5 = addVertex(256, -256);
	addLine(v0, v1, host);
	addLine(v1, v2, host);
	// The host is deliberately on the right side of this downward line.
	// Reversing it below puts the host on the left and certifies both cases.
	const int shared = addLine(v2, v3, host, neighbor);
	addLine(v3, v0, host);
	addLine(v2, v4, neighbor);
	addLine(v4, v5, neighbor);
	addLine(v5, v3, neighbor);

	const int originalVertices = doc.numVertices();
	const int originalLines = doc.numLinedefs();
	const int originalSides = doc.numSidedefs();
	const int originalSectors = doc.numSectors();

	for (MapFormat format :
		 {MapFormat::doom, MapFormat::hexen, MapFormat::udmf})
		for (bool reverseShared : {false, true})
		{
			if (reverseShared)
			{
				std::swap(doc.linedefs[shared]->start,
						  doc.linedefs[shared]->end);
				std::swap(doc.linedefs[shared]->right,
						  doc.linedefs[shared]->left);
			}

			for (SectorArchitectureElement element :
				 {SectorArchitectureElement::raisedDais,
				  SectorArchitectureElement::centralPlatform,
				  SectorArchitectureElement::crossingBridges,
				  SectorArchitectureElement::crossCanal,
				  SectorArchitectureElement::partitionWall})
			{
				SectorDesignRequest request;
				request.mode = SectorDesignMode::architecture;
				// Wall generators follow the gesture's long axis. Give the
				// partition a horizontal footprint so its actual wall mass,
				// rather than merely its layout envelope, reaches the shared
				// boundary being exercised here.
				request.anchors =
						element ==
								SectorArchitectureElement::partitionWall ?
							std::vector<v2double_t>{
									{-192, -96}, {0, 96}} :
							std::vector<v2double_t>{
									{-192, -192}, {0, 192}};
				request.architectureHostSector = host;
				request.architectureElement = element;
				request.architectureSize = 16;
				request.architectureHeight = 16;
				request.architectureMargin = 0;

				SectorDesignPlan preview = M_PlanSectorDesign(
						doc, config, nullptr, format, request);
				ASSERT_TRUE(preview.valid())
						<< static_cast<int>(element) << " format "
						<< static_cast<int>(format) << " reverse "
						<< reverseShared;

				SectorDesignPlan applied;
				ASSERT_TRUE(M_ApplySectorDesign(
						doc, config, nullptr, format,
						request, &applied));
				ASSERT_EQ(applied.createdSectors.size(), 1u);
				const int generated =
						applied.createdSectors.front();
				int hostPortals = 0;
				int neighborPortals = 0;
				for (const std::shared_ptr<LineDef> &line :
						doc.linedefs)
				{
					const int right = line->right >= 0 ?
							doc.sidedefs[line->right]->sector : -1;
					const int left = line->left >= 0 ?
							doc.sidedefs[line->left]->sector : -1;
					const bool generatedHost =
							(right == generated && left == host) ||
							(left == generated && right == host);
					const bool generatedNeighbor =
							(right == generated &&
							 left == neighbor) ||
							(left == generated &&
							 right == neighbor);
					if (!generatedHost && !generatedNeighbor)
						continue;
					EXPECT_TRUE(line->TwoSided());
					if (element ==
						SectorArchitectureElement::partitionWall)
						EXPECT_NE(
								line->flags & MLF_Blocking, 0u);
					else
						EXPECT_EQ(
								line->flags & MLF_Blocking, 0u);
					hostPortals += generatedHost;
					neighborPortals += generatedNeighbor;
				}
				EXPECT_GT(hostPortals, 0);
				EXPECT_GT(neighborPortals, 0);
				if (element ==
					SectorArchitectureElement::crossCanal)
					EXPECT_EQ(doc.sectors[generated]->floorh, 8);
				else if (element ==
						 SectorArchitectureElement::partitionWall)
					EXPECT_EQ(
							doc.sectors[generated]->floorh,
							doc.sectors[generated]->ceilh);
				else
					EXPECT_EQ(doc.sectors[generated]->floorh, 40);

				ASSERT_TRUE(doc.basis.undo());
				EXPECT_EQ(doc.numVertices(), originalVertices);
				EXPECT_EQ(doc.numLinedefs(), originalLines);
				EXPECT_EQ(doc.numSidedefs(), originalSides);
				EXPECT_EQ(doc.numSectors(), originalSectors);
				EXPECT_FALSE(doc.basis.undo());
			}

			if (reverseShared)
			{
				std::swap(doc.linedefs[shared]->start,
						  doc.linedefs[shared]->end);
				std::swap(doc.linedefs[shared]->right,
						  doc.linedefs[shared]->left);
			}
		}
}

TEST_F(SmartSectorFixture,
	   NestedArchitectureUsesTransitivePlanOwnershipInANewVoidHall)
{
	SectorDesignRequest request;
	request.mode = SectorDesignMode::architecture;
	request.anchors = {{-256, -192}, {256, 192}};
	request.architectureElement =
			SectorArchitectureElement::tieredZiggurat;
	request.architectureSize = 32;
	request.architectureHeight = 12;
	request.architectureMargin = 24;
	request.properties.floorMode = SectorValueMode::absolute;
	request.properties.floorValue = 8;
	request.properties.ceilingMode = SectorValueMode::absolute;
	request.properties.ceilingValue = 192;

	const SectorDesignPlan preview = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	ASSERT_TRUE(preview.valid());
	ASSERT_EQ(preview.plannedArchitectureHosts, 1);
	ASSERT_EQ(preview.plannedStructures, 3);
	ASSERT_EQ(preview.shapes.size(), 4u);
	EXPECT_EQ(preview.shapes[1].modelShape, 0);
	EXPECT_EQ(preview.shapes[2].modelShape, 1);
	EXPECT_EQ(preview.shapes[3].modelShape, 2);

	SectorDesignPlan applied;
	ASSERT_TRUE(M_ApplySectorDesign(
			doc, config, nullptr, MapFormat::doom, request, &applied));
	ASSERT_EQ(applied.createdSectors.size(), 4u);
	EXPECT_EQ(doc.sectors[applied.createdSectors[0]]->floorh, 8);
	EXPECT_EQ(doc.sectors[applied.createdSectors[1]]->floorh, 20);
	EXPECT_EQ(doc.sectors[applied.createdSectors[2]]->floorh, 32);
	EXPECT_EQ(doc.sectors[applied.createdSectors[3]]->floorh, 44);
	ASSERT_TRUE(doc.basis.undo());
	EXPECT_EQ(doc.numVertices(), 0);
	EXPECT_EQ(doc.numLinedefs(), 0);
	EXPECT_EQ(doc.numSidedefs(), 0);
	EXPECT_EQ(doc.numSectors(), 0);
	EXPECT_FALSE(doc.basis.undo());
}

TEST_F(SmartSectorFixture,
	   ArchitecturalStructuresProtectHolesAndGrandStairsFollowTheGesture)
{
	const int host = addRingSector(320, 64, 24, 192);
	SectorDesignRequest request;
	request.mode = SectorDesignMode::architecture;
	request.anchors = {{-240, -240}, {240, 240}};
	request.architectureElement =
			SectorArchitectureElement::raisedDais;
	request.architectureSize = 24;
	request.architectureHeight = 16;
	request.architectureMargin = 80;

	const SectorDesignPlan blocked = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	EXPECT_FALSE(blocked.valid());
	EXPECT_TRUE(std::any_of(
			blocked.issues.begin(), blocked.issues.end(),
			[](const SectorDesignIssue &issue)
			{
				return issue.message.find("foreign cell") !=
						SString::npos;
			}));
	EXPECT_EQ(blocked.retainedSectors, std::vector<int>({host}));

	// The directional property is pure planning geometry, so use a void hall
	// to avoid coupling this check to fixture construction details.
	SectorDesignRequest stair;
	stair.mode = SectorDesignMode::architecture;
	stair.anchors = {{256, -128}, {-256, 128}};
	stair.architectureElement =
			SectorArchitectureElement::grandStair;
	stair.architectureBays = 4;
	stair.architectureSize = 48;
	stair.architectureHeight = 8;
	stair.architectureMargin = 16;
	Instance emptyInstance;
	const SectorDesignPlan stairs = M_PlanSectorDesign(
			emptyInstance.level, config, nullptr, MapFormat::doom, stair);
	ASSERT_TRUE(stairs.valid());
	ASSERT_EQ(stairs.shapes.size(), 5u);
	// Shape zero is the generated hall. The first, lowest tread lies at the
	// gesture start (positive X); the last, highest tread lies at negative X.
	auto centroidX = [](const PlannedSectorShape &shape)
	{
		double result = 0;
		for (const v2double_t &point : shape.outer)
			result += point.x;
		return result / shape.outer.size();
	};
	EXPECT_GT(centroidX(stairs.shapes[1]),
			  centroidX(stairs.shapes[4]));
	EXPECT_EQ(stairs.shapes[1].floorDelta, 8);
	EXPECT_EQ(stairs.shapes[4].floorDelta, 32);
}

TEST_F(SmartSectorFixture,
	   EveryPolygonProfileAppliesAsOneSectorInEveryFormat)
{
	SectorDesignRequest request;
	request.mode = SectorDesignMode::polygon;
	request.anchors = {{0, 0}, {512, 0}};
	request.polygonSides = 8;
	request.polygonInnerRatio = 0.52;
	request.rotation = std::numbers::pi / 17.0;

	for (int profile = 0;
		 profile <= static_cast<int>(
				SectorPolygonProfile::cathedralTracery48);
		 ++profile)
		for (MapFormat format :
			 {MapFormat::doom, MapFormat::hexen, MapFormat::udmf})
		{
			request.polygonProfile =
					static_cast<SectorPolygonProfile>(profile);
			SectorDesignPlan applied;
			ASSERT_TRUE(M_ApplySectorDesign(
					doc, config, nullptr, format, request, &applied))
					<< "profile " << profile << ", format "
					<< static_cast<int>(format);
			EXPECT_EQ(applied.createdSectors.size(), 1u)
					<< "profile " << profile << ", format "
					<< static_cast<int>(format);
			ASSERT_TRUE(doc.basis.undo());
			EXPECT_EQ(doc.numVertices(), 0);
			EXPECT_EQ(doc.numLinedefs(), 0);
			EXPECT_EQ(doc.numSidedefs(), 0);
			EXPECT_EQ(doc.numSectors(), 0);
			EXPECT_FALSE(doc.basis.undo());
		}
}

TEST_F(SmartSectorFixture,
	   EveryArchitectureLayoutBuildsItsOwnHallInEveryFormat)
{
	SectorDesignRequest request;
	request.mode = SectorDesignMode::architecture;
	request.anchors = {{-512, -384}, {512, 384}};
	request.architectureStyle = SectorArchitectureStyle::functional;
	request.architectureBays = 3;
	request.architectureSize = 16;
	request.architectureMargin = 32;

	for (int element = 0;
		 element <= static_cast<int>(
				SectorArchitectureElement::fortifiedKeep);
		 ++element)
		for (MapFormat format :
			 {MapFormat::doom, MapFormat::hexen, MapFormat::udmf})
		{
			request.architectureElement =
					static_cast<SectorArchitectureElement>(element);
			const SectorDesignPlan preview = M_PlanSectorDesign(
					doc, config, nullptr, format, request);
			ASSERT_TRUE(preview.valid())
					<< "element " << element << ", format "
					<< static_cast<int>(format);
			ASSERT_EQ(preview.plannedArchitectureHosts, 1);
			ASSERT_GT(preview.plannedStructures, 0);

			SectorDesignPlan applied;
			ASSERT_TRUE(M_ApplySectorDesign(
					doc, config, nullptr, format, request, &applied))
					<< "element " << element << ", format "
					<< static_cast<int>(format);
			EXPECT_EQ(applied.createdSectors.size(),
					  static_cast<size_t>(
						applied.plannedStructures + 1))
					<< "element " << element << ", format "
					<< static_cast<int>(format);
			ASSERT_TRUE(doc.basis.undo());
			EXPECT_EQ(doc.numVertices(), 0);
			EXPECT_EQ(doc.numLinedefs(), 0);
			EXPECT_EQ(doc.numSidedefs(), 0);
			EXPECT_EQ(doc.numSectors(), 0);
			EXPECT_FALSE(doc.basis.undo());
		}
}

TEST_F(SmartSectorFixture,
	   GothicNaveAppliesAtomicallyAndLeavesItsHostPropertiesIntact)
{
	const int host = addBoxSector(-512, -512, 512, 512, 24, 160);
	doc.sectors[host]->light = 176;
	doc.sectors[host]->type = 7;
	doc.sectors[host]->tag = 91;
	const Sector originalHost = *doc.sectors[host];
	const int originalVertices = doc.numVertices();
	const int originalLines = doc.numLinedefs();
	const int originalSides = doc.numSidedefs();
	const int originalSectors = doc.numSectors();

	SectorDesignRequest request;
	request.mode = SectorDesignMode::architecture;
	request.anchors = {{-384, -320}, {384, 320}};
	request.architectureStyle = SectorArchitectureStyle::gothic;
	request.architectureElement = SectorArchitectureElement::nave;
	request.architectureBays = 3;
	request.architectureSize = 16;
	request.architectureMargin = 24;

	SectorDesignPlan applied;
	ASSERT_TRUE(M_ApplySectorDesign(
			doc, config, nullptr, MapFormat::doom, request, &applied));
	ASSERT_EQ(applied.plannedStructures, 16);
	ASSERT_EQ(applied.createdSectors.size(), 16u);
	EXPECT_EQ(doc.sectors[host]->floorh, originalHost.floorh);
	EXPECT_EQ(doc.sectors[host]->ceilh, originalHost.ceilh);
	EXPECT_EQ(doc.sectors[host]->light, originalHost.light);
	EXPECT_EQ(doc.sectors[host]->type, originalHost.type);
	EXPECT_EQ(doc.sectors[host]->tag, originalHost.tag);
	for (int sector : applied.createdSectors)
	{
		ASSERT_TRUE(doc.isSector(sector));
		EXPECT_EQ(doc.sectors[sector]->floorh,
				  doc.sectors[sector]->ceilh);
		EXPECT_EQ(doc.sectors[sector]->floorh, originalHost.floorh);
	}

	ASSERT_TRUE(doc.basis.undo());
	EXPECT_EQ(doc.numVertices(), originalVertices);
	EXPECT_EQ(doc.numLinedefs(), originalLines);
	EXPECT_EQ(doc.numSidedefs(), originalSides);
	EXPECT_EQ(doc.numSectors(), originalSectors);
	EXPECT_FALSE(doc.basis.undo());
	ASSERT_TRUE(doc.basis.redo());
	EXPECT_EQ(doc.numSectors(), originalSectors + 16);
}

TEST_F(SmartSectorFixture,
	   ArchitectureBuildsAHostHallInClearVoidAsOneUndoableOperation)
{
	SectorDesignRequest request;
	request.mode = SectorDesignMode::architecture;
	request.anchors = {{-128, -96}, {128, 96}};
	request.architectureSize = 16;
	request.architectureMargin = 24;
	request.properties.floorMode = SectorValueMode::absolute;
	request.properties.floorValue = 16;
	request.properties.ceilingMode = SectorValueMode::absolute;
	request.properties.ceilingValue = 192;

	const SectorDesignPlan preview = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	ASSERT_TRUE(preview.valid());
	ASSERT_EQ(preview.plannedArchitectureHosts, 1);
	ASSERT_EQ(preview.plannedStructures, 1);
	ASSERT_EQ(preview.shapes.size(), 2u);
	EXPECT_FALSE(preview.shapes.front().closed);
	EXPECT_EQ(preview.shapes.front().modelShape, -1);
	EXPECT_TRUE(preview.shapes.back().closed);
	EXPECT_EQ(preview.shapes.back().modelShape, 0);
	EXPECT_TRUE(preview.shapes.back().inheritModelOnly);
	EXPECT_EQ(doc.numSectors(), 0);
	EXPECT_FALSE(doc.basis.undo());

	SectorDesignPlan applied;
	ASSERT_TRUE(M_ApplySectorDesign(
			doc, config, nullptr, MapFormat::doom, request, &applied));
	ASSERT_EQ(applied.createdSectors.size(), 2u);
	const int hall = applied.createdSectors.front();
	const int support = applied.createdSectors.back();
	EXPECT_EQ(doc.sectors[hall]->floorh, 16);
	EXPECT_EQ(doc.sectors[hall]->ceilh, 192);
	EXPECT_EQ(doc.sectors[support]->floorh, 16);
	EXPECT_EQ(doc.sectors[support]->ceilh, 16);
	ASSERT_TRUE(doc.basis.undo());
	EXPECT_EQ(doc.numVertices(), 0);
	EXPECT_EQ(doc.numLinedefs(), 0);
	EXPECT_EQ(doc.numSidedefs(), 0);
	EXPECT_EQ(doc.numSectors(), 0);
	EXPECT_FALSE(doc.basis.undo());
	ASSERT_TRUE(doc.basis.redo());
	EXPECT_EQ(doc.numSectors(), 2);
}

TEST_F(SmartSectorFixture,
	   ArchitectureLocksTheGestureStartHostWhenItsCenterIsAHole)
{
	const int host = addRingSector(256, 64, 24, 160);
	SectorDesignRequest request;
	request.mode = SectorDesignMode::architecture;
	request.anchors = {{-192, -192}, {192, 192}};
	request.architectureStyle = SectorArchitectureStyle::functional;
	request.architectureElement = SectorArchitectureElement::rotunda;
	request.architectureBays = 4;
	request.architectureSize = 16;
	request.architectureMargin = 24;

	const SectorDesignPlan preview = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	ASSERT_TRUE(preview.valid());
	EXPECT_EQ(preview.plannedArchitectureHosts, 0);
	EXPECT_EQ(preview.retainedSectors, std::vector<int>({host}));
	ASSERT_EQ(preview.plannedStructures, 8);
	ASSERT_EQ(preview.shapes.size(), 8u);
	for (const PlannedSectorShape &shape : preview.shapes)
	{
		EXPECT_EQ(shape.modelSector, host);
		EXPECT_EQ(shape.role, DesignPreviewRole::architecture);
	}

	const int originalSectors = doc.numSectors();
	SectorDesignPlan applied;
	ASSERT_TRUE(M_ApplySectorDesign(
			doc, config, nullptr, MapFormat::doom, request, &applied));
	EXPECT_EQ(applied.createdSectors.size(), 8u);
	EXPECT_EQ(doc.numSectors(), originalSectors + 8);
	ASSERT_TRUE(doc.basis.undo());
	EXPECT_EQ(doc.numSectors(), originalSectors);
	EXPECT_FALSE(doc.basis.undo());

	request.architectureHostSector = 99;
	const SectorDesignPlan stale = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	EXPECT_FALSE(stale.valid());
	EXPECT_TRUE(std::any_of(
			stale.previewPaths.begin(), stale.previewPaths.end(),
			[](const DesignPreviewPath &path)
			{
				return path.role == DesignPreviewRole::conflict;
			}));
	EXPECT_FALSE(M_ApplySectorDesign(
			doc, config, nullptr, MapFormat::doom, request));
	EXPECT_EQ(doc.numSectors(), originalSectors);
	EXPECT_FALSE(doc.basis.undo());
}

TEST_F(SmartSectorFixture,
	   ArchitectureRejectsExistingGeometryContactAndOverlappingSupports)
{
	addBoxSector(0, -64, 128, 64);
	SectorDesignRequest request;
	request.mode = SectorDesignMode::architecture;
	request.anchors = {{96, -32}, {224, 32}};
	request.architectureStyle = SectorArchitectureStyle::functional;
	request.architectureSize = 8;
	request.architectureMargin = 8;
	const SectorDesignPlan crossing = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	EXPECT_FALSE(crossing.valid());
	EXPECT_TRUE(std::any_of(
			crossing.issues.begin(), crossing.issues.end(),
			[](const SectorDesignIssue &issue)
			{
				return issue.message.find("leave") !=
						SString::npos;
			}));
	EXPECT_TRUE(std::any_of(
			crossing.shapes.begin(), crossing.shapes.end(),
			[](const PlannedSectorShape &shape)
			{
				return shape.role == DesignPreviewRole::conflict;
			}));

	request.anchors = {{224, -32}, {96, 32}};
	const SectorDesignPlan voidCollision = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	EXPECT_FALSE(voidCollision.valid());
	auto lineIssue = std::find_if(
			voidCollision.issues.begin(), voidCollision.issues.end(),
			[](const SectorDesignIssue &issue)
			{
				return issue.line >= 0 &&
						issue.message.find("must not touch") !=
							SString::npos;
			});
	ASSERT_NE(lineIssue, voidCollision.issues.end());
	UI_SetSectorDesignPreview(inst, voidCollision,
							  voidCollision.retainedSectors);
	ASSERT_TRUE(inst.edit.designAssistPreview);
	EXPECT_TRUE(std::any_of(
			inst.edit.designAssistPreview->paths.begin(),
			inst.edit.designAssistPreview->paths.end(),
			[&](const DesignPreviewPath &path)
			{
				if (path.role != DesignPreviewRole::conflict ||
					path.points.size() != 2)
					return false;
				const LineDef &line = *doc.linedefs[lineIssue->line];
				return path.points.front() == doc.getStart(line).xy() &&
						path.points.back() == doc.getEnd(line).xy();
			}));
	inst.edit.designAssistPreview.reset();

	request.anchors = {{8, -56}, {120, 56}};
	request.architectureElement = SectorArchitectureElement::sanctuary;
	request.architectureBays = 32;
	request.architectureSize = 64;
	request.architectureMargin = 0;
	EXPECT_FALSE(M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request).valid());
	EXPECT_EQ(doc.numSectors(), 1);
	EXPECT_FALSE(doc.basis.undo());
}

TEST_F(SmartSectorFixture,
	   MinimumArchitectureSizesNeverCommitFragmentedSupports)
{
	addBoxSector(-512, -512, 512, 512);
	SectorDesignRequest request;
	request.mode = SectorDesignMode::architecture;
	request.anchors = {{-128, -128}, {128, 128}};
	request.architectureElement = SectorArchitectureElement::pillar;
	request.architectureMargin = 16;

	for (int style = 0;
		 style <= static_cast<int>(SectorArchitectureStyle::infernal);
		 ++style)
		for (double size : {4.0, 8.0, 16.0, 24.0, 32.0, 48.0, 64.0})
			for (MapFormat format :
				 {MapFormat::doom, MapFormat::hexen, MapFormat::udmf})
			for (double rotation :
				 {0.0, std::numbers::pi / 16.0})
			{
				request.architectureStyle =
						static_cast<SectorArchitectureStyle>(style);
				request.architectureSize = size;
				request.rotation = rotation;
				SectorDesignPlan preview = M_PlanSectorDesign(
						doc, config, nullptr, format, request);
				const double minimum = M_MinimumArchitectureSize(
						request.architectureStyle);
				if (size < minimum)
				{
					EXPECT_FALSE(preview.valid());
					continue;
				}
				ASSERT_TRUE(preview.valid())
						<< "style " << style << ", size " << size
						<< ", format " << static_cast<int>(format)
						<< ", rotation " << rotation;
				const int beforeSectors = doc.numSectors();
				SectorDesignPlan applied;
				ASSERT_TRUE(M_ApplySectorDesign(
						doc, config, nullptr, format, request, &applied))
						<< "style " << style << ", size " << size
						<< ", format " << static_cast<int>(format)
						<< ", rotation " << rotation;
				EXPECT_EQ(applied.createdSectors.size(), 1u)
						<< "style " << style << ", size " << size
						<< ", format " << static_cast<int>(format)
						<< ", rotation " << rotation;
				ASSERT_TRUE(doc.basis.undo());
				EXPECT_EQ(doc.numSectors(), beforeSectors);
			}
}

TEST_F(SmartSectorFixture,
	   ArchitectureRequiresExplicitReplacementForASelectedProtectedHost)
{
	const int host = addBoxSector(-256, -256, 256, 256);
	doc.sectors[host]->tag = 12;
	config.line_types[62].desc = "Lift";
	SectorActionPreset lift;
	lift.kind = SectorActionKind::lift;
	lift.id = "lift";
	lift.label = "Lift";
	lift.special = 62;
	lift.activation = ActivationPolicy::encoded;
	config.sector_action_presets.push_back(lift);
	doc.linedefs[0]->type = 62;
	doc.linedefs[0]->arg1 = 12;

	SectorDesignRequest request;
	request.mode = SectorDesignMode::architecture;
	request.anchors = {{-128, -128}, {128, 128}};
	// Merely having the functional host selected must not act as the inspector's
	// explicit protected-geometry replacement permission.
	request.targetSectors = {host};
	SectorDesignPlan blocked = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	EXPECT_FALSE(blocked.valid());
	EXPECT_TRUE(std::any_of(
			blocked.issues.begin(), blocked.issues.end(),
			[](const SectorDesignIssue &issue)
			{
				return issue.severity ==
						SectorDesignIssueSeverity::error &&
					issue.message.find("protected door or lift") !=
						SString::npos;
			}));

	request.replaceAffectedSectors = true;
	const SectorDesignPlan explicitPlan = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	EXPECT_TRUE(explicitPlan.valid());
	EXPECT_TRUE(std::any_of(
			explicitPlan.issues.begin(), explicitPlan.issues.end(),
			[](const SectorDesignIssue &issue)
			{
				return issue.severity ==
						SectorDesignIssueSeverity::warning &&
					issue.message.find("protected door or lift") !=
						SString::npos;
			}));
	EXPECT_FALSE(doc.basis.undo());
}

TEST_F(SmartSectorFixture,
	   CorridorConnectionEditsOnlyThePostSplitSharedPortal)
{
	const int room = addSector();
	const int v0 = addVertex(-128, -128);
	const int v1 = addVertex(-128, 128);
	const int v2 = addVertex(0, 128);
	const int v3 = addVertex(0, -128);
	addLine(v0, v1, room);
	const int unrelated = addLine(v1, v2, room);
	const int source = addLine(v2, v3, room);
	addLine(v3, v0, room);
	const int sharedSide = doc.linedefs[source]->right;
	doc.linedefs[unrelated]->right = sharedSide;
	doc.sidedefs[sharedSide]->mid_tex =
			BA_InternaliseString("HOSTWALL");

	SectorDesignRequest request;
	request.mode = SectorDesignMode::corridor;
	request.anchors = {{0, 0}, {128, 0}};
	request.anchorLines = {source, -1};
	request.width = 64;
	request.startConnection = SectorConnection::open;
	request.endConnection = SectorConnection::open;

	SectorDesignPlan applied;
	ASSERT_TRUE(M_ApplySectorDesign(
			doc, config, nullptr, MapFormat::doom, request, &applied));
	ASSERT_EQ(applied.createdSectors.size(), 1u);
	const int corridor = applied.createdSectors.front();
	EXPECT_EQ(doc.linedefs[unrelated]->right, sharedSide);
	EXPECT_EQ(doc.sidedefs[sharedSide]->MidTex(), "HOSTWALL");

	int portalCount = 0;
	int retainedWallCount = 0;
	for (int line = 0; line < doc.numLinedefs(); ++line)
	{
		const LineDef &candidate = *doc.linedefs[line];
		const v2double_t start = doc.getStart(candidate).xy();
		const v2double_t end = doc.getEnd(candidate).xy();
		if (std::abs(start.x) > 0.001 ||
			std::abs(end.x) > 0.001)
			continue;

		const int right = candidate.right >= 0 ?
				doc.sidedefs[candidate.right]->sector : -1;
		const int left = candidate.left >= 0 ?
				doc.sidedefs[candidate.left]->sector : -1;
		if ((right == room && left == corridor) ||
			(right == corridor && left == room))
		{
			portalCount++;
			ASSERT_TRUE(candidate.TwoSided());
			EXPECT_EQ(candidate.flags & MLF_Blocking, 0u);
			for (int side : {candidate.right, candidate.left})
			{
				ASSERT_TRUE(doc.isSidedef(side));
				EXPECT_EQ(doc.sidedefs[side]->MidTex(), "-");
				EXPECT_EQ(doc.sidedefs[side]->UpperTex(), "HOSTWALL");
				EXPECT_EQ(doc.sidedefs[side]->LowerTex(), "HOSTWALL");
			}
		}
		else if ((right == room || left == room) &&
				 !candidate.TwoSided())
		{
			retainedWallCount++;
			const int side = right == room ?
					candidate.right : candidate.left;
			ASSERT_TRUE(doc.isSidedef(side));
			EXPECT_EQ(doc.sidedefs[side]->MidTex(), "HOSTWALL");
		}
	}
	EXPECT_EQ(portalCount, 1);
	EXPECT_EQ(retainedWallCount, 2);

	ASSERT_TRUE(doc.basis.undo());
	EXPECT_EQ(doc.numSectors(), 1);
	EXPECT_EQ(doc.numLinedefs(), 4);
	EXPECT_EQ(doc.linedefs[source]->right, sharedSide);
	EXPECT_EQ(doc.linedefs[unrelated]->right, sharedSide);
	EXPECT_EQ(doc.sidedefs[sharedSide]->MidTex(), "HOSTWALL");
	EXPECT_FALSE(doc.basis.undo());

	request.startConnection = SectorConnection::wall;
	request.properties.wallTexture = "PORTALW";
	ASSERT_TRUE(M_ApplySectorDesign(
			doc, config, nullptr, MapFormat::doom, request, &applied));
	const int wallCorridor = applied.createdSectors.front();
	EXPECT_EQ(doc.linedefs[unrelated]->right, sharedSide);
	EXPECT_EQ(doc.sidedefs[sharedSide]->MidTex(), "HOSTWALL");
	portalCount = 0;
	retainedWallCount = 0;
	for (const std::shared_ptr<LineDef> &candidate : doc.linedefs)
	{
		const v2double_t start = doc.getStart(*candidate).xy();
		const v2double_t end = doc.getEnd(*candidate).xy();
		if (std::abs(start.x) > 0.001 ||
			std::abs(end.x) > 0.001)
			continue;
		const int right = candidate->right >= 0 ?
				doc.sidedefs[candidate->right]->sector : -1;
		const int left = candidate->left >= 0 ?
				doc.sidedefs[candidate->left]->sector : -1;
		if ((right == room && left == wallCorridor) ||
			(right == wallCorridor && left == room))
		{
			portalCount++;
			EXPECT_NE(candidate->flags & MLF_Blocking, 0u);
			for (int side : {candidate->right, candidate->left})
				EXPECT_EQ(doc.sidedefs[side]->MidTex(), "PORTALW");
		}
		else if ((right == room || left == room) &&
				 !candidate->TwoSided())
		{
			retainedWallCount++;
			const int side = right == room ?
					candidate->right : candidate->left;
			EXPECT_EQ(doc.sidedefs[side]->MidTex(), "HOSTWALL");
		}
	}
	EXPECT_EQ(portalCount, 1);
	EXPECT_EQ(retainedWallCount, 2);
	ASSERT_TRUE(doc.basis.undo());
	EXPECT_FALSE(doc.basis.undo());
}

TEST_F(SmartSectorFixture,
	   SameSectorUCorridorKeepsItsEnclosedMiddleEmpty)
{
	const int room = addSector();
	const int v0 = addVertex(-128, -128);
	const int v1 = addVertex(-128, 128);
	const int v2 = addVertex(0, 128);
	const int v3 = addVertex(0, -128);
	addLine(v0, v1, room);
	addLine(v1, v2, room);
	const int source = addLine(v2, v3, room);
	addLine(v3, v0, room);

	SectorDesignRequest request;
	request.mode = SectorDesignMode::corridor;
	request.anchors = {
		{0, -64}, {192, -64}, {192, 64}, {0, 64}
	};
	request.anchorLines = {source, -1, -1, source};
	request.width = 32;
	request.startConnection = SectorConnection::open;
	request.endConnection = SectorConnection::open;
	request.properties.floorMode = SectorValueMode::absolute;
	request.properties.floorValue = 24;

	const v2double_t enclosedMiddle{96, 0};
	const SectorDesignPlan preview = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	ASSERT_TRUE(preview.valid());
	ASSERT_EQ(preview.shapes.size(), 1u);
	EXPECT_FALSE(PointInsidePath(
			preview.shapes.front().outer, enclosedMiddle));

	SectorDesignPlan applied;
	ASSERT_TRUE(M_ApplySectorDesign(
			doc, config, nullptr, MapFormat::doom, request, &applied));
	ASSERT_FALSE(applied.createdSectors.empty());
	for (int sector : applied.createdSectors)
	{
		EXPECT_FALSE(SectorContainsPoint(doc, sector, enclosedMiddle))
				<< "created sector " << sector << " of "
				<< applied.createdSectors.size() << " (map has "
				<< doc.numSectors() << " sectors and "
				<< doc.numLinedefs() << " lines)";
		EXPECT_EQ(doc.sectors[sector]->floorh, 24);
	}
	for (const std::shared_ptr<LineDef> &line : doc.linedefs)
		if (line->OneSided())
		{
			EXPECT_GE(line->right, 0);
			EXPECT_LT(line->left, 0);
			EXPECT_NE(line->flags & MLF_Blocking, 0u);
			EXPECT_EQ(line->flags & MLF_TwoSided, 0u);
			ASSERT_TRUE(doc.isSidedef(line->right));
			EXPECT_NE(doc.sidedefs[line->right]->MidTex(), "-");
		}
	EXPECT_TRUE(SectorContainsPoint(doc, room, {-64, 0}));
	ASSERT_TRUE(doc.basis.undo());
	EXPECT_EQ(doc.numSectors(), 1);
	EXPECT_EQ(doc.numLinedefs(), 4);
	EXPECT_FALSE(doc.basis.undo());
}

TEST_F(SmartSectorFixture, RoutedToolsRejectStaleEndpointLinesAtomically)
{
	for (SectorDesignMode mode :
		 {SectorDesignMode::corridor, SectorDesignMode::stairs})
	{
		SectorDesignRequest request;
		request.mode = mode;
		request.anchors = {{0, 0}, {128, 0}};
		request.anchorLines = {-1, 99};
		request.width = 64;
		request.stairCount = 4;

		const SectorDesignPlan plan = M_PlanSectorDesign(
				doc, config, nullptr, MapFormat::doom, request);
		EXPECT_FALSE(plan.valid());
		EXPECT_TRUE(std::any_of(
				plan.issues.begin(), plan.issues.end(),
				[](const SectorDesignIssue &issue)
				{
					return issue.line == 99 &&
							issue.message.find("no longer exists") !=
								std::string::npos;
				}));
		EXPECT_FALSE(M_ApplySectorDesign(
				doc, config, nullptr, MapFormat::doom, request));
		EXPECT_EQ(doc.numVertices(), 0);
		EXPECT_EQ(doc.numLinedefs(), 0);
		EXPECT_EQ(doc.numSectors(), 0);
		EXPECT_FALSE(doc.basis.undo());
	}
}

TEST_F(SmartSectorFixture, GeneratedStairsPreserveHeadroom)
{
	SectorDesignRequest request;
	request.mode = SectorDesignMode::stairs;
	request.anchors = {{0, 0}, {128, 0}};
	request.width = 64;
	request.stairCount = 4;
	request.stairRise = 8;
	request.preserveHeadroom = true;

	SectorDesignPlan plan = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	ASSERT_TRUE(plan.valid());
	EXPECT_EQ(plan.plannedSteps, 4);
	ASSERT_EQ(plan.shapes.size(), 4u);
	EXPECT_EQ(plan.shapes[3].floorDelta, 24);
	EXPECT_EQ(plan.shapes[3].ceilingDelta, 24);
}

TEST_F(SmartSectorFixture, RoutedStairsFollowWaypointsAndFitTarget)
{
	SectorDesignRequest request;
	request.mode = SectorDesignMode::stairs;
	request.anchors = {{0, 0}, {96, 0}, {96, 96}};
	request.width = 32;
	request.stairCount = 5;
	request.stairFitTarget = true;
	request.stairTargetFloor = 30;
	request.join = SectorDesignJoin::bevel;

	const SectorDesignPlan plan = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	ASSERT_TRUE(plan.valid());
	EXPECT_EQ(plan.plannedSteps, 5);
	ASSERT_EQ(plan.shapes.size(), 5u);
	EXPECT_EQ(plan.shapes.front().floorDelta, 0);
	EXPECT_EQ(plan.shapes.back().floorDelta, 30);
	EXPECT_TRUE(std::any_of(plan.issues.begin(), plan.issues.end(),
			[](const SectorDesignIssue &issue)
			{
				return issue.message.find("uneven") != std::string::npos;
			}));
	EXPECT_TRUE(std::any_of(plan.shapes.begin(), plan.shapes.end(),
			[](const PlannedSectorShape &shape)
			{
				return shape.outer.size() > 4;
	}));
}

TEST_F(SmartSectorFixture, StairEndpointDoorsShareOneAtomicOperation)
{
	const int startSector = addSector();
	int a0 = addVertex(-64, -16);
	int a1 = addVertex(0, -16);
	int a2 = addVertex(0, 16);
	int a3 = addVertex(-64, 16);
	addLine(a0, a1, startSector);
	const int startPortal = addLine(a1, a2, startSector);
	addLine(a2, a3, startSector);
	addLine(a3, a0, startSector);

	const int endSector = addSector();
	int b0 = addVertex(160, -16);
	int b1 = addVertex(224, -16);
	int b2 = addVertex(224, 16);
	int b3 = addVertex(160, 16);
	addLine(b0, b1, endSector);
	addLine(b1, b2, endSector);
	addLine(b2, b3, endSector);
	const int endPortal = addLine(b3, b0, endSector);

	config.line_types[1].desc = "Door";
	DoorPreset door;
	door.id = "normal";
	door.label = "Normal";
	door.special = 1;
	door.activation = ActivationPolicy::encoded;
	config.door_presets.push_back(door);

	SectorDesignRequest request;
	request.mode = SectorDesignMode::stairs;
	request.anchors = {{0, 0}, {160, 0}};
	request.anchorLines = {startPortal, endPortal};
	request.width = 32;
	request.doorDepth = 16;
	request.stairCount = 4;
	request.startConnection = SectorConnection::door;
	request.endConnection = SectorConnection::door;
	request.doorOptions.presetId = "normal";

	const SectorDesignPlan plan = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	ASSERT_TRUE(plan.valid());
	EXPECT_EQ(plan.plannedDoors, 2);
	EXPECT_EQ(plan.plannedSteps, 4);

	ASSERT_TRUE(M_ApplySectorDesign(
			doc, config, nullptr, MapFormat::doom, request));
	EXPECT_EQ(doc.numSectors(), 8);
	EXPECT_EQ(std::count_if(doc.sectors.begin(), doc.sectors.end(),
			[](const std::shared_ptr<Sector> &sector)
			{
				return sector->floorh == sector->ceilh;
			}), 2);
	ASSERT_TRUE(doc.basis.undo());
	EXPECT_EQ(doc.numSectors(), 2);
	EXPECT_EQ(doc.numLinedefs(), 8);
	EXPECT_FALSE(doc.basis.undo());
}

TEST_F(SmartSectorFixture, ExistingStairChainIsOrderedAndAtomic)
{
	addTwoAdjacentSectors();
	SectorDesignRequest request;
	request.mode = SectorDesignMode::stairs;
	request.targetSectors = {0, 1};
	request.startSector = 0;
	request.stairRise = 16;

	SectorDesignPlan plan = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	ASSERT_TRUE(plan.valid());
	ASSERT_EQ(plan.sectorChanges.size(), 2u);
	EXPECT_EQ(*plan.sectorChanges[0].floor, 0);
	EXPECT_EQ(*plan.sectorChanges[1].floor, 16);

	ASSERT_TRUE(M_ApplySectorDesign(doc, config, nullptr, MapFormat::doom,
									request));
	EXPECT_EQ(doc.sectors[1]->floorh, 16);
	EXPECT_EQ(doc.sectors[1]->ceilh, 144);
	ASSERT_TRUE(doc.basis.undo());
	EXPECT_EQ(doc.sectors[1]->floorh, 0);
	EXPECT_EQ(doc.sectors[1]->ceilh, 128);
}

TEST_F(SmartSectorFixture, LiftPlannerUsesTypedTagAndFormatActivation)
{
	addTwoAdjacentSectors();
	doc.sectors[0]->floorh = 64;
	config.line_types[62].desc = "Lift";
	SectorActionPreset lift;
	lift.kind = SectorActionKind::lift;
	lift.id = "repeat";
	lift.label = "@special";
	lift.special = 62;
	lift.activation = ActivationPolicy::useRepeat;
	lift.args[0].targetTag = true;
	lift.args[1].value = 16;
	lift.args[2].value = 105;
	config.sector_action_presets.push_back(lift);

	SectorDesignRequest request;
	request.mode = SectorDesignMode::lift;
	request.targetSectors = {0};
	request.actionPresetId = "repeat";

	SectorDesignPlan plan = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::hexen, request);
	ASSERT_TRUE(plan.valid());
	ASSERT_EQ(plan.lifts.size(), 1u);
	EXPECT_EQ(plan.lifts[0].triggerLines, std::vector<int>({2}));
	EXPECT_GT(plan.lifts[0].tag, 0);
	EXPECT_EQ(plan.lifts[0].lowerStop, 0);
	EXPECT_EQ(plan.lifts[0].travel, 64);
	EXPECT_EQ(M_SectorActionPresetLabel(config, plan.lifts[0].preset),
			  "Lift");

	ASSERT_TRUE(M_ApplySectorDesign(doc, config, nullptr,
									MapFormat::hexen, request));
	EXPECT_EQ(doc.sectors[0]->tag, plan.lifts[0].tag);
	EXPECT_EQ(doc.linedefs[2]->type, 62);
	EXPECT_EQ(doc.linedefs[2]->arg1, plan.lifts[0].tag);
	EXPECT_EQ(doc.linedefs[2]->arg2, 16);
	EXPECT_EQ(doc.linedefs[2]->flags & MLF_Activation, 0x400);
	EXPECT_TRUE(doc.linedefs[2]->flags & MLF_Repeatable);
	ASSERT_TRUE(doc.basis.undo());
	EXPECT_EQ(doc.sectors[0]->tag, 0);
	EXPECT_EQ(doc.linedefs[2]->type, 0);
}

TEST_F(SmartSectorFixture, DoomLiftUsesEncodedSpecialAndPlatformTag)
{
	addTwoAdjacentSectors();
	doc.sectors[0]->floorh = 64;
	config.line_types[62].desc = "Lift";
	SectorActionPreset lift;
	lift.id = "repeat";
	lift.special = 62;
	lift.activation = ActivationPolicy::encoded;
	config.sector_action_presets.push_back(lift);

	SectorDesignRequest request;
	request.mode = SectorDesignMode::lift;
	request.targetSectors = {0};
	request.actionPresetId = "repeat";
	ASSERT_TRUE(M_ApplySectorDesign(
			doc, config, nullptr, MapFormat::doom, request));
	EXPECT_GT(doc.sectors[0]->tag, 0);
	EXPECT_EQ(doc.linedefs[2]->type, 62);
	EXPECT_EQ(doc.linedefs[2]->arg1, doc.sectors[0]->tag);
	EXPECT_EQ(doc.linedefs[2]->flags & MLF_Activation, 0);
	ASSERT_TRUE(doc.basis.undo());
	EXPECT_EQ(doc.sectors[0]->tag, 0);
	EXPECT_EQ(doc.linedefs[2]->type, 0);
}

TEST_F(SmartSectorFixture, UdmfLiftUsesActivationPropertiesAndTypedTag)
{
	addTwoAdjacentSectors();
	doc.sectors[0]->floorh = 64;
	config.line_types[62].desc = "Lift";
	SectorActionPreset lift;
	lift.id = "repeat";
	lift.special = 62;
	lift.activation = ActivationPolicy::useRepeat;
	lift.args[0].targetTag = true;
	lift.args[1].value = 32;
	lift.args[2].value = 105;
	config.sector_action_presets.push_back(lift);

	SectorDesignRequest request;
	request.mode = SectorDesignMode::lift;
	request.targetSectors = {0};
	request.actionPresetId = "repeat";
	ASSERT_TRUE(M_ApplySectorDesign(
			doc, config, nullptr, MapFormat::udmf, request));
	EXPECT_GT(doc.sectors[0]->tag, 0);
	EXPECT_EQ(doc.linedefs[2]->arg1, doc.sectors[0]->tag);
	EXPECT_EQ(doc.linedefs[2]->arg2, 32);
	EXPECT_TRUE(doc.linedefs[2]->udmfFlags & MLF_UDMF_playeruse);
	EXPECT_TRUE(doc.linedefs[2]->udmfFlags & MLF_UDMF_repeatspecial);
	ASSERT_TRUE(doc.basis.undo());
	EXPECT_EQ(doc.linedefs[2]->udmfFlags, 0u);
}

TEST_F(SmartSectorFixture, LiftPresetAvailabilityFiltersRepresentations)
{
	config.line_types[21].desc = "Doom Lift";
	config.line_types[62].desc = "Hexen Lift";
	SectorActionPreset doom;
	doom.id = "doom";
	doom.special = 21;
	doom.activation = ActivationPolicy::encoded;
	config.sector_action_presets.push_back(doom);
	SectorActionPreset hexen;
	hexen.id = "hexen";
	hexen.special = 62;
	hexen.activation = ActivationPolicy::useOnce;
	hexen.args[0].targetTag = true;
	config.sector_action_presets.push_back(hexen);

	EXPECT_EQ(M_AvailableSectorActionPresets(config,
			SectorActionKind::lift, MapFormat::doom).size(), 1u);
	EXPECT_EQ(M_AvailableSectorActionPresets(config,
			SectorActionKind::lift, MapFormat::hexen).size(), 1u);
	EXPECT_EQ(M_AvailableSectorActionPresets(config,
			SectorActionKind::lift, MapFormat::udmf).size(), 1u);
}

TEST_F(SmartSectorFixture, CorridorRouteIndexProducesStableAlternatives)
{
	SectorDesignRequest request;
	request.mode = SectorDesignMode::corridor;
	request.anchors = {{0, 0}, {128, 96}};
	request.width = 32;

	std::vector<std::vector<v2double_t>> outlines;
	for (int route = 0; route < 4; route++)
	{
		request.routeIndex = route;
		SectorDesignPlan plan = M_PlanSectorDesign(
				doc, config, nullptr, MapFormat::doom, request);
		ASSERT_TRUE(plan.valid()) << route;
		ASSERT_FALSE(plan.shapes.empty());
		outlines.push_back(plan.shapes.front().outer);
	}
	EXPECT_NE(outlines[0], outlines[1]);
	EXPECT_NE(outlines[1], outlines[2]);
	EXPECT_NE(outlines[2], outlines[3]);
}

TEST_F(SmartSectorFixture, AdjacentInsetSelectionIsRejectedAtomically)
{
	addTwoAdjacentSectors();
	SectorDesignRequest request;
	request.mode = SectorDesignMode::inset;
	request.targetSectors = {0, 1};
	request.offset = 8;

	SectorDesignPlan plan = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	EXPECT_FALSE(plan.valid());
	EXPECT_FALSE(M_ApplySectorDesign(
			doc, config, nullptr, MapFormat::doom, request));
	EXPECT_EQ(doc.numSectors(), 2);
	EXPECT_FALSE(doc.basis.undo());
}

TEST_F(SmartSectorFixture, ProtectedActionInsideGeneratedRoomNeedsOptIn)
{
	addTwoAdjacentSectors();
	doc.linedefs[0]->type = 17;
	SectorDesignRequest request;
	request.mode = SectorDesignMode::room;
	request.anchors = {{-16, -16}, {32, 80}};

	SectorDesignPlan protectedPlan = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	EXPECT_FALSE(protectedPlan.valid());
	EXPECT_TRUE(std::any_of(protectedPlan.issues.begin(),
			protectedPlan.issues.end(),
			[](const SectorDesignIssue &issue)
			{
				return issue.message.find("protected action line") !=
						std::string::npos;
			}));

	request.replaceAffectedSectors = true;
	SectorDesignPlan replacementPlan = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	EXPECT_TRUE(replacementPlan.valid());
	EXPECT_TRUE(std::any_of(replacementPlan.issues.begin(),
			replacementPlan.issues.end(),
			[](const SectorDesignIssue &issue)
			{
				return issue.severity ==
						SectorDesignIssueSeverity::warning &&
						issue.message.find("protected action line") !=
							std::string::npos;
	}));
}

TEST_F(SmartSectorFixture,
	   ExtrudeRetainsActionOnSourceSeamWithoutFalseConsumptionError)
{
	addSector();
	const int v0 = addVertex(0, 0);
	const int v1 = addVertex(0, 64);
	const int v2 = addVertex(64, 64);
	const int v3 = addVertex(64, 0);
	const int source = addLine(v0, v1, 0);
	addLine(v1, v2, 0);
	addLine(v2, v3, 0);
	addLine(v3, v0, 0);
	doc.linedefs[source]->type = 17;
	doc.linedefs[source]->arg1 = 23;

	SectorDesignRequest request;
	request.mode = SectorDesignMode::extrude;
	request.anchorLines = {source};
	request.extrudeUseDragDepth = true;
	request.extrudeReferenceLine = source;
	request.anchors = {{-32, 32}};
	request.startConnection = SectorConnection::open;

	const SectorDesignPlan preview = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	SString issues;
	for (const SectorDesignIssue &issue : preview.issues)
		issues += issue.message + "\n";
	ASSERT_TRUE(preview.valid()) << issues.c_str();
	EXPECT_TRUE(std::any_of(preview.issues.begin(), preview.issues.end(),
			[source](const SectorDesignIssue &issue)
			{
				return issue.severity ==
							SectorDesignIssueSeverity::warning &&
						issue.line == source &&
						issue.message.find("retained on this connection") !=
							std::string::npos;
			}));
	EXPECT_FALSE(std::any_of(preview.issues.begin(), preview.issues.end(),
			[](const SectorDesignIssue &issue)
			{
				return issue.message.find("consume a protected action") !=
						std::string::npos;
			}));

	SectorDesignPlan applied;
	ASSERT_TRUE(M_ApplySectorDesign(doc, config, nullptr, MapFormat::doom,
									request, &applied));
	EXPECT_EQ(doc.linedefs[source]->type, 17);
	EXPECT_EQ(doc.linedefs[source]->arg1, 23);
	ASSERT_TRUE(doc.basis.undo());
	EXPECT_EQ(doc.numSectors(), 1);
	EXPECT_EQ(doc.numLinedefs(), 4);
	EXPECT_EQ(doc.linedefs[source]->type, 17);
	EXPECT_EQ(doc.linedefs[source]->arg1, 23);
}

TEST_F(SmartSectorFixture,
	   TaggedLiftProtectsPlatformButNotItsOrdinaryNeighbor)
{
	addTwoAdjacentSectors();
	doc.sectors[0]->tag = 9;
	doc.linedefs[2]->type = 62;
	doc.linedefs[2]->arg1 = 9;

	SectorActionPreset lift;
	lift.kind = SectorActionKind::lift;
	lift.id = "local";
	lift.special = 62;
	lift.activation = ActivationPolicy::encoded;
	config.sector_action_presets.push_back(lift);

	SectorDesignRequest request;
	request.mode = SectorDesignMode::room;
	request.anchors = {{72, 8}, {120, 56}};
	SectorDesignPlan neighbor = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	SString neighborIssues;
	for (const SectorDesignIssue &issue : neighbor.issues)
		neighborIssues += issue.message + "\n";
	EXPECT_TRUE(neighbor.valid()) << neighborIssues.c_str();

	request.anchors = {{8, 8}, {56, 56}};
	SectorDesignPlan platform = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	EXPECT_FALSE(platform.valid());
	EXPECT_TRUE(std::any_of(platform.issues.begin(), platform.issues.end(),
			[](const SectorDesignIssue &issue)
			{
				return issue.message.find("protected door or lift") !=
						std::string::npos;
			}));
}

TEST_F(SmartSectorFixture, LiftPortalInclusionIsValidatedAndSelective)
{
	addTwoAdjacentSectors();
	config.line_types[62].desc = "Lift";
	SectorActionPreset lift;
	lift.id = "repeat";
	lift.special = 62;
	lift.activation = ActivationPolicy::useRepeat;
	lift.args[0].targetTag = true;
	config.sector_action_presets.push_back(lift);

	SectorDesignRequest request;
	request.mode = SectorDesignMode::lift;
	request.targetSectors = {0};
	request.actionPresetId = "repeat";
	request.liftTriggerLines = {2};
	SectorDesignPlan chosen = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::hexen, request);
	ASSERT_TRUE(chosen.valid());
	ASSERT_EQ(chosen.lifts.size(), 1u);
	EXPECT_EQ(chosen.lifts[0].triggerLines, std::vector<int>({2}));

	request.liftTriggerLines = {0};
	SectorDesignPlan invalid = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::hexen, request);
	EXPECT_FALSE(invalid.valid());
}

TEST_F(SmartSectorFixture, BranchedExistingStairsRequireAnchoredPath)
{
	for (int index = 0; index < 4; index++)
		addSector();
	for (int index = 0; index < 6; index++)
		addVertex(index * 16, 0);
	addLine(0, 1, 0, 1);
	addLine(2, 3, 1, 2);
	addLine(4, 5, 1, 3);

	SectorDesignRequest request;
	request.mode = SectorDesignMode::stairs;
	request.targetSectors = {0, 1, 2, 3};
	SectorDesignPlan ambiguous = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	EXPECT_FALSE(ambiguous.valid());

	request.startSector = 0;
	request.endSector = 2;
	SectorDesignPlan anchored = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	ASSERT_TRUE(anchored.valid());
	ASSERT_EQ(anchored.sectorChanges.size(), 3u);
	EXPECT_EQ(anchored.sectorChanges[0].sector, 0);
	EXPECT_EQ(anchored.sectorChanges[1].sector, 1);
	EXPECT_EQ(anchored.sectorChanges[2].sector, 2);
}

TEST_F(SmartSectorFixture, InsetHasIndependentRingAndInnerProperties)
{
	addSector();
	int v0 = addVertex(0, 0);
	int v1 = addVertex(0, 128);
	int v2 = addVertex(128, 128);
	int v3 = addVertex(128, 0);
	addLine(v0, v1, 0);
	addLine(v1, v2, 0);
	addLine(v2, v3, 0);
	addLine(v3, v0, 0);

	SectorDesignRequest request;
	request.mode = SectorDesignMode::inset;
	request.targetSectors = {0};
	request.offset = 16;
	SectorPropertyOptions ring;
	ring.floorMode = SectorValueMode::relative;
	ring.floorValue = 8;
	ring.floorTexture = "RING";
	request.ringProperties = ring;
	SectorPropertyOptions inner;
	inner.floorMode = SectorValueMode::absolute;
	inner.floorValue = 24;
	inner.floorTexture = "INNER";
	request.innerProperties = inner;

	SectorDesignPlan plan = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	ASSERT_TRUE(plan.valid());
	ASSERT_EQ(plan.shapes.size(), 1u);
	ASSERT_TRUE(plan.shapes.front().properties);
	EXPECT_EQ(plan.shapes.front().properties->floorValue, 24);
	ASSERT_EQ(plan.sectorChanges.size(), 1u);
	EXPECT_EQ(*plan.sectorChanges.front().floor, 8);
	EXPECT_EQ(*plan.sectorChanges.front().floorTexture, "RING");

	SectorDesignPlan applied;
	ASSERT_TRUE(M_ApplySectorDesign(
			doc, config, nullptr, MapFormat::doom, request, &applied));
	ASSERT_EQ(applied.createdSectors.size(), 1u);
	EXPECT_EQ(doc.sectors[0]->floorh, 8);
	EXPECT_EQ(doc.sectors[0]->FloorTex(), "RING");
	const int innerSector = applied.createdSectors.front();
	EXPECT_EQ(doc.sectors[innerSector]->floorh, 24);
	EXPECT_EQ(doc.sectors[innerSector]->FloorTex(), "INNER");
	ASSERT_TRUE(doc.basis.undo());
	EXPECT_EQ(doc.numSectors(), 1);
	EXPECT_EQ(doc.sectors[0]->floorh, 0);
	EXPECT_EQ(doc.sectors[0]->FloorTex(), "OLD_F");
}

TEST_F(SmartSectorFixture, ExtrudeAcceptsNonbranchingBoundaryChain)
{
	addSector();
	int v0 = addVertex(0, 0);
	int v1 = addVertex(0, 64);
	int v2 = addVertex(64, 64);
	int v3 = addVertex(64, 0);
	addLine(v0, v1, 0);
	addLine(v1, v2, 0);
	addLine(v2, v3, 0);
	addLine(v3, v0, 0);

	SectorDesignRequest request;
	request.mode = SectorDesignMode::extrude;
	request.anchorLines = {0, 1};
	request.depth = 32;
	request.join = SectorDesignJoin::bevel;
	SectorDesignPlan plan = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	ASSERT_TRUE(plan.valid());
	ASSERT_EQ(plan.shapes.size(), 1u);
	EXPECT_GT(plan.shapes.front().outer.size(), 4u);

	int branchVertex = addVertex(-32, 96);
	int branchLine = addLine(v1, branchVertex, 0);
	request.anchorLines.push_back(branchLine);
	SectorDesignPlan branching = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	EXPECT_FALSE(branching.valid());
}

TEST_F(SmartSectorFixture, ExtrudeDragDepthUsesClosestChainSegment)
{
	addSector();
	const int v0 = addVertex(0, 0);
	const int v1 = addVertex(0, 64);
	const int v2 = addVertex(64, 64);
	const int v3 = addVertex(64, 0);
	addLine(v0, v1, 0);
	addLine(v1, v2, 0);
	addLine(v2, v3, 0);
	addLine(v3, v0, 0);

	SectorDesignRequest request;
	request.mode = SectorDesignMode::extrude;
	request.anchorLines = {0, 1};
	request.anchors = {{16, 96}};
	request.extrudeUseDragDepth = true;
	const SectorDesignPlan dragged = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	ASSERT_TRUE(dragged.valid());
	ASSERT_EQ(dragged.shapes.size(), 1u);
	double draggedMaxY = -std::numeric_limits<double>::max();
	for (const v2double_t &point : dragged.shapes.front().outer)
		draggedMaxY = std::max(draggedMaxY, point.y);
	EXPECT_DOUBLE_EQ(draggedMaxY, 96);

	request.extrudeUseDragDepth = false;
	request.depth = 48;
	const SectorDesignPlan exact = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	ASSERT_TRUE(exact.valid());
	double exactMaxY = -std::numeric_limits<double>::max();
	for (const v2double_t &point : exact.shapes.front().outer)
		exactMaxY = std::max(exactMaxY, point.y);
	EXPECT_DOUBLE_EQ(exactMaxY, 112);
}

TEST_F(SmartSectorFixture, ExtrudeDragAlwaysBuildsTowardPointer)
{
	addSector();
	const int lower = addVertex(0, 0);
	const int upper = addVertex(0, 64);
	const int source = addLine(lower, upper, 0);

	SectorDesignRequest request;
	request.mode = SectorDesignMode::extrude;
	request.anchorLines = {source};
	request.extrudeUseDragDepth = true;
	request.anchors = {{-48, 32}};

	const SectorDesignPlan left = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	ASSERT_TRUE(left.valid());
	ASSERT_EQ(left.shapes.size(), 1u);
	EXPECT_EQ(left.extrudeReferenceLine, source);
	EXPECT_DOUBLE_EQ(left.resolvedExtrudeDepth, 48);
	double minimumX = std::numeric_limits<double>::max();
	double maximumX = -std::numeric_limits<double>::max();
	for (const v2double_t &point : left.shapes.front().outer)
	{
		minimumX = std::min(minimumX, point.x);
		maximumX = std::max(maximumX, point.x);
	}
	EXPECT_DOUBLE_EQ(minimumX, -48);
	EXPECT_DOUBLE_EQ(maximumX, 0);
	for (MapFormat format : {MapFormat::hexen, MapFormat::udmf})
	{
		const SectorDesignPlan formatPlan = M_PlanSectorDesign(
				doc, config, nullptr, format, request);
		ASSERT_TRUE(formatPlan.valid());
		double formatMinimumX = std::numeric_limits<double>::max();
		double formatMaximumX =
				-std::numeric_limits<double>::max();
		for (const v2double_t &point :
				formatPlan.shapes.front().outer)
		{
			formatMinimumX = std::min(formatMinimumX, point.x);
			formatMaximumX = std::max(formatMaximumX, point.x);
		}
		EXPECT_DOUBLE_EQ(formatMinimumX, -48);
		EXPECT_DOUBLE_EQ(formatMaximumX, 0);
	}

	// Reversing the geometric traversal must not invert pointer intent.
	doc.vertices[lower]->yf = 64;
	doc.vertices[upper]->yf = 0;
	const SectorDesignPlan reversed = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	ASSERT_TRUE(reversed.valid());
	minimumX = std::numeric_limits<double>::max();
	maximumX = -std::numeric_limits<double>::max();
	for (const v2double_t &point : reversed.shapes.front().outer)
	{
		minimumX = std::min(minimumX, point.x);
		maximumX = std::max(maximumX, point.x);
	}
	EXPECT_DOUBLE_EQ(minimumX, -48);
	EXPECT_DOUBLE_EQ(maximumX, 0);

	request.extrudeOpposite = true;
	const SectorDesignPlan opposite = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	ASSERT_TRUE(opposite.valid());
	EXPECT_TRUE(opposite.extrudeOpposite);
	minimumX = std::numeric_limits<double>::max();
	maximumX = -std::numeric_limits<double>::max();
	for (const v2double_t &point : opposite.shapes.front().outer)
	{
		minimumX = std::min(minimumX, point.x);
		maximumX = std::max(maximumX, point.x);
	}
	EXPECT_DOUBLE_EQ(minimumX, 0);
	EXPECT_DOUBLE_EQ(maximumX, 48);
}

TEST_F(SmartSectorFixture, ExtrudeFillsExactGapBetweenExistingSectors)
{
	addSector();
	addSector();
	const int a0 = addVertex(-256, -256);
	const int a1 = addVertex(-256, 256);
	const int a2 = addVertex(256, 256);
	const int a3 = addVertex(256, -256);
	const int b0 = addVertex(272, 256);
	const int b1 = addVertex(272, -256);
	const int b2 = addVertex(383, 256);
	const int b3 = addVertex(383, -256);
	addLine(a0, a1, 0);
	addLine(a1, a2, 0);
	const int source = addLine(a2, a3, 0);
	addLine(a3, a0, 0);
	addLine(b1, b0, 1);
	addLine(b0, b2, 1);
	addLine(b2, b3, 1);
	addLine(b3, b1, 1);

	SectorDesignRequest request;
	request.mode = SectorDesignMode::extrude;
	request.anchorLines = {source};
	request.anchors = {{272, 0}};
	request.extrudeUseDragDepth = true;
	request.extrudeReferenceLine = source;

	const SectorDesignPlan preview = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	ASSERT_TRUE(preview.valid());
	ASSERT_EQ(preview.shapes.size(), 1u);

	SectorDesignPlan applied;
	ASSERT_TRUE(M_ApplySectorDesign(
			doc, config, nullptr, MapFormat::doom, request, &applied));
	EXPECT_EQ(doc.numSectors(), 3);
	ASSERT_TRUE(doc.basis.undo());
	EXPECT_EQ(doc.numSectors(), 2);
	EXPECT_EQ(doc.numLinedefs(), 8);
}

TEST_F(SmartSectorFixture, EveryValidStraightBoundaryExtrudeAppliesAndUndoes)
{
	addSector();
	const int v0 = addVertex(-256, -256);
	const int v1 = addVertex(-256, 256);
	const int v2 = addVertex(256, 256);
	const int v3 = addVertex(256, -256);
	addLine(v0, v1, 0);
	addLine(v1, v2, 0);
	addLine(v2, v3, 0);
	addLine(v3, v0, 0);

	const int originalVertices = doc.numVertices();
	const int originalLinedefs = doc.numLinedefs();
	const int originalSidedefs = doc.numSidedefs();
	const int originalSectors = doc.numSectors();
	for (int line = 0; line < originalLinedefs; ++line)
	for (double direction : {-1.0, 1.0})
	for (double depth : {8.0, 16.0, 64.0, 511.0, 512.0, 513.0,
						 1024.0})
	{
		const v2double_t start = doc.getStart(*doc.linedefs[line]).xy();
		const v2double_t end = doc.getEnd(*doc.linedefs[line]).xy();
		const v2double_t delta = end - start;
		const v2double_t midpoint = (start + end) * 0.5;
		const v2double_t normal =
				v2double_t{-delta.y, delta.x} *
				(direction * depth / delta.hypot());

		SectorDesignRequest request;
		request.mode = SectorDesignMode::extrude;
		request.anchorLines = {line};
		request.anchors = {midpoint + normal};
		request.extrudeUseDragDepth = true;
		request.extrudeReferenceLine = line;
		const SectorDesignPlan preview = M_PlanSectorDesign(
				doc, config, nullptr, MapFormat::doom, request);
		if (!preview.valid())
			continue;

		SectorDesignPlan applied;
		const bool appliedSuccessfully = M_ApplySectorDesign(
				doc, config, nullptr, MapFormat::doom, request, &applied);
		SString appliedIssues;
		for (const SectorDesignIssue &issue : applied.issues)
			appliedIssues += issue.message + "\n";
		EXPECT_TRUE(appliedSuccessfully)
				<< "line " << line << ", direction " << direction
				<< ", depth " << depth << "\n"
				<< appliedIssues.c_str();
		if (!appliedSuccessfully)
			continue;
		ASSERT_TRUE(doc.basis.undo());
		EXPECT_EQ(doc.numVertices(), originalVertices);
		EXPECT_EQ(doc.numLinedefs(), originalLinedefs);
		EXPECT_EQ(doc.numSidedefs(), originalSidedefs);
		EXPECT_EQ(doc.numSectors(), originalSectors);
	}
}

TEST_F(SmartSectorFixture, ExactHoleBoundaryIsRejectedBeforeApply)
{
	addSector();
	const int o0 = addVertex(0, 0);
	const int o1 = addVertex(0, 128);
	const int o2 = addVertex(128, 128);
	const int o3 = addVertex(128, 0);
	addLine(o0, o1, 0);
	addLine(o1, o2, 0);
	addLine(o2, o3, 0);
	addLine(o3, o0, 0);

	// Counter-clockwise inner boundary: sector 0 remains on its right and
	// the center is void.
	const int i0 = addVertex(32, 32);
	const int i1 = addVertex(96, 32);
	const int i2 = addVertex(96, 96);
	const int i3 = addVertex(32, 96);
	addLine(i0, i1, 0);
	addLine(i1, i2, 0);
	addLine(i2, i3, 0);
	addLine(i3, i0, 0);

	SectorDesignRequest request;
	request.mode = SectorDesignMode::room;
	request.anchors = {{32, 32}, {96, 96}};
	const SectorDesignPlan preview = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	ASSERT_FALSE(preview.valid());
	EXPECT_FALSE(M_ApplySectorDesign(
			doc, config, nullptr, MapFormat::doom, request));
	EXPECT_EQ(doc.numSectors(), 1);
	EXPECT_EQ(doc.numLinedefs(), 8);
	EXPECT_FALSE(doc.basis.undo());
}

TEST_F(SmartSectorFixture, ExtrudeCanLockDirectionToOneBentChainSeam)
{
	addSector();
	const int v0 = addVertex(0, 0);
	const int v1 = addVertex(0, 64);
	const int v2 = addVertex(64, 64);
	const int vertical = addLine(v0, v1, 0);
	const int horizontal = addLine(v1, v2, 0);

	SectorDesignRequest request;
	request.mode = SectorDesignMode::extrude;
	request.anchorLines = {vertical, horizontal};
	request.extrudeUseDragDepth = true;
	request.anchors = {{4, 65}};

	const SectorDesignPlan nearest = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	ASSERT_TRUE(nearest.valid());
	EXPECT_EQ(nearest.extrudeReferenceLine, horizontal);
	EXPECT_DOUBLE_EQ(nearest.resolvedExtrudeDepth, 1);

	request.extrudeReferenceLine = vertical;
	const SectorDesignPlan locked = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	ASSERT_TRUE(locked.valid());
	EXPECT_EQ(locked.extrudeReferenceLine, vertical);
	EXPECT_DOUBLE_EQ(locked.resolvedExtrudeDepth, 4);
	ASSERT_FALSE(locked.previewLabels.empty());
	EXPECT_EQ(locked.previewLabels.front().text, "4.0");
	EXPECT_TRUE(std::any_of(
			locked.previewPaths.begin(), locked.previewPaths.end(),
			[](const DesignPreviewPath &path)
			{
				return path.role == DesignPreviewRole::anchor &&
						!path.closed && path.points.size() == 2;
			}));

	request.extrudeReferenceLine = 99;
	const SectorDesignPlan stale = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	EXPECT_FALSE(stale.valid());
}

TEST_F(SmartSectorFixture, ExtrudeOppositePreviewAndApplyUseIdenticalSide)
{
	addSector();
	const int v0 = addVertex(0, 0);
	const int v1 = addVertex(0, 64);
	const int v2 = addVertex(64, 64);
	const int v3 = addVertex(64, 0);
	const int source = addLine(v0, v1, 0);
	addLine(v1, v2, 0);
	addLine(v2, v3, 0);
	addLine(v3, v0, 0);

	SectorDesignRequest request;
	request.mode = SectorDesignMode::extrude;
	request.anchorLines = {source};
	request.anchors = {{64, 32}}; // Pointer is inside the existing room.
	request.extrudeUseDragDepth = true;
	request.extrudeReferenceLine = source;
	request.extrudeOpposite = true; // Deliberately create outside it.

	const SectorDesignPlan preview = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	ASSERT_TRUE(preview.valid());
	ASSERT_EQ(preview.shapes.size(), 1u);
	double previewMinimumX = std::numeric_limits<double>::max();
	for (const v2double_t &point : preview.shapes.front().outer)
		previewMinimumX = std::min(previewMinimumX, point.x);
	EXPECT_DOUBLE_EQ(previewMinimumX, -64);

	SectorDesignPlan applied;
	ASSERT_TRUE(M_ApplySectorDesign(
			doc, config, nullptr, MapFormat::doom, request, &applied));
	ASSERT_EQ(applied.createdSectors.size(), 1u);
	EXPECT_TRUE(applied.extrudeOpposite);
	EXPECT_DOUBLE_EQ(applied.resolvedExtrudeDepth, 64);
	double appliedMinimumX = std::numeric_limits<double>::max();
	for (const std::shared_ptr<Vertex> &vertex : doc.vertices)
		appliedMinimumX = std::min(appliedMinimumX, vertex->x());
	EXPECT_DOUBLE_EQ(appliedMinimumX, previewMinimumX);
	ASSERT_TRUE(doc.basis.undo());
	EXPECT_EQ(doc.numSectors(), 1);
	EXPECT_EQ(doc.numLinedefs(), 4);
}

TEST_F(SmartSectorFixture, ExtrudeDoorCanBeCenteredSizedAndOffset)
{
	addSector();
	const int v0 = addVertex(0, 0);
	const int v1 = addVertex(0, 128);
	const int v2 = addVertex(64, 128);
	const int v3 = addVertex(64, 0);
	const int source = addLine(v0, v1, 0);
	addLine(v1, v2, 0);
	addLine(v2, v3, 0);
	addLine(v3, v0, 0);

	config.line_types[1].desc = "Door";
	DoorPreset doorPreset;
	doorPreset.id = "normal";
	doorPreset.label = "Normal";
	doorPreset.special = 1;
	doorPreset.activation = ActivationPolicy::encoded;
	config.door_presets.push_back(doorPreset);

	SectorDesignRequest request;
	request.mode = SectorDesignMode::extrude;
	request.anchorLines = {source};
	request.extrudeUseDragDepth = false;
	request.depth = 64;
	request.startConnection = SectorConnection::door;
	request.doorDepth = 16;
	request.doorWidth = 32;
	request.doorOffset = 16;
	request.doorOptions.presetId = "normal";

	const SectorDesignPlan plan = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	ASSERT_TRUE(plan.valid());
	EXPECT_EQ(plan.doorSourceLines, std::vector<int>({source}));
	EXPECT_EQ(plan.plannedDoors, 1);
	ASSERT_GE(plan.shapes.size(), 2u);
	const PlannedSectorShape &door = plan.shapes.front();
	EXPECT_TRUE(door.smartDoor);
	double minimumY = std::numeric_limits<double>::max();
	double maximumY = -std::numeric_limits<double>::max();
	for (const v2double_t &point : door.outer)
	{
		minimumY = std::min(minimumY, point.y);
		maximumY = std::max(maximumY, point.y);
	}
	EXPECT_DOUBLE_EQ(minimumY, 64);
	EXPECT_DOUBLE_EQ(maximumY, 96);

	ASSERT_TRUE(M_ApplySectorDesign(
			doc, config, nullptr, MapFormat::doom, request));
	EXPECT_TRUE(std::any_of(doc.sectors.begin(), doc.sectors.end(),
			[](const std::shared_ptr<Sector> &sector)
			{
				return sector->floorh == sector->ceilh;
			}));
	ASSERT_TRUE(doc.basis.undo());
	EXPECT_EQ(doc.numSectors(), 1);
	EXPECT_EQ(doc.numLinedefs(), 4);
}

TEST_F(SmartSectorFixture, ExtrudeDoorAutoAndExplicitSegmentsAreDeterministic)
{
	addSector();
	const int v0 = addVertex(0, 0);
	const int v1 = addVertex(0, 64);
	const int v2 = addVertex(128, 64);
	const int v3 = addVertex(128, 0);
	const int shortLine = addLine(v0, v1, 0);
	const int longLine = addLine(v1, v2, 0);
	addLine(v2, v3, 0);
	addLine(v3, v0, 0);
	config.line_types[1].desc = "Door";
	DoorPreset door;
	door.id = "normal";
	door.special = 1;
	door.activation = ActivationPolicy::encoded;
	config.door_presets.push_back(door);

	SectorDesignRequest request;
	request.mode = SectorDesignMode::extrude;
	request.anchorLines = {shortLine, longLine};
	request.extrudeUseDragDepth = false;
	request.depth = 64;
	request.startConnection = SectorConnection::door;
	request.doorDepth = 16;
	request.doorWidth = 24;
	request.doorOptions.presetId = "normal";

	const SectorDesignPlan automatic = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	ASSERT_TRUE(automatic.valid());
	EXPECT_EQ(automatic.doorSourceLines,
			  std::vector<int>({longLine}));

	request.doorWidth = 0;
	const SectorDesignPlan fullWidth = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	ASSERT_TRUE(fullWidth.valid());
	ASSERT_FALSE(fullWidth.shapes.empty());
	EXPECT_DOUBLE_EQ(fullWidth.shapes.front().outer.front().x, 8);
	SectorDesignPlan automaticApplied;
	ASSERT_TRUE(M_ApplySectorDesign(
			doc, config, nullptr, MapFormat::doom, request,
			&automaticApplied));
	EXPECT_EQ(std::count_if(doc.sectors.begin(), doc.sectors.end(),
			[](const std::shared_ptr<Sector> &sector)
			{
				return sector->floorh == sector->ceilh;
			}), 1);
	ASSERT_TRUE(doc.basis.undo());
	EXPECT_EQ(doc.numSectors(), 1);
	EXPECT_EQ(doc.numLinedefs(), 4);

	request.doorWidth = 24;
	request.autoDoorLines = false;
	request.doorLines = {longLine, shortLine};
	const SectorDesignPlan explicitPlan = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	ASSERT_TRUE(explicitPlan.valid());
	EXPECT_EQ(explicitPlan.plannedDoors, 2);
	EXPECT_EQ(explicitPlan.doorSourceLines,
			  std::vector<int>({shortLine, longLine}));
	request.doorLines = {shortLine, longLine};
	const SectorDesignPlan reorderedPlan = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	ASSERT_TRUE(reorderedPlan.valid());
	EXPECT_EQ(reorderedPlan.doorSourceLines,
			  explicitPlan.doorSourceLines);
	ASSERT_EQ(reorderedPlan.shapes.size(), explicitPlan.shapes.size());
	for (size_t index = 0; index < reorderedPlan.shapes.size(); ++index)
		EXPECT_EQ(reorderedPlan.shapes[index].outer,
				  explicitPlan.shapes[index].outer);

	SectorDesignPlan applied;
	ASSERT_TRUE(M_ApplySectorDesign(
			doc, config, nullptr, MapFormat::doom, request, &applied));
	EXPECT_EQ(applied.plannedDoors, 2);
	EXPECT_EQ(std::count_if(doc.sectors.begin(), doc.sectors.end(),
			[](const std::shared_ptr<Sector> &sector)
			{
				return sector->floorh == sector->ceilh;
			}), 2);
	ASSERT_TRUE(doc.basis.undo());
	EXPECT_EQ(doc.numSectors(), 1);
	EXPECT_EQ(doc.numLinedefs(), 4);
}

TEST_F(SmartSectorFixture, ExtrudeDoorRequiresUsableDepthAndSelection)
{
	addSector();
	const int v0 = addVertex(0, 0);
	const int v1 = addVertex(0, 64);
	const int source = addLine(v0, v1, 0);
	config.line_types[1].desc = "Door";
	DoorPreset door;
	door.id = "normal";
	door.special = 1;
	door.activation = ActivationPolicy::encoded;
	config.door_presets.push_back(door);

	SectorDesignRequest request;
	request.mode = SectorDesignMode::extrude;
	request.anchorLines = {source};
	request.extrudeUseDragDepth = false;
	request.depth = 16;
	request.startConnection = SectorConnection::door;
	request.doorDepth = 16;
	request.doorOptions.presetId = "normal";
	EXPECT_FALSE(M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request).valid());

	request.depth = 64;
	request.autoDoorLines = false;
	request.doorLines.clear();
	EXPECT_FALSE(M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request).valid());
	EXPECT_FALSE(M_ApplySectorDesign(
			doc, config, nullptr, MapFormat::doom, request));
	EXPECT_EQ(doc.numSectors(), 1);
	EXPECT_FALSE(doc.basis.undo());

	request.autoDoorLines = true;
	request.doorWidth = -16;
	EXPECT_FALSE(M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request).valid());
}

TEST_F(SmartSectorFixture, WallConnectionIsolatesOnlyChangedSharedSidedef)
{
	addSector();
	int v0 = addVertex(0, 0);
	int v1 = addVertex(0, 64);
	int v2 = addVertex(64, 64);
	int v3 = addVertex(64, 0);
	const int source = addLine(v0, v1, 0);
	const int unrelated = addLine(v1, v2, 0);
	addLine(v2, v3, 0);
	addLine(v3, v0, 0);
	const int sharedSide = doc.linedefs[source]->right;
	doc.linedefs[unrelated]->right = sharedSide;
	const int originalSides = doc.numSidedefs();

	SectorDesignRequest request;
	request.mode = SectorDesignMode::extrude;
	request.anchorLines = {source};
	request.depth = 32;
	request.startConnection = SectorConnection::wall;
	request.properties.wallTexture = "NEW_WALL";

	ASSERT_TRUE(M_ApplySectorDesign(
			doc, config, nullptr, MapFormat::doom, request));
	EXPECT_NE(doc.linedefs[source]->right,
			  doc.linedefs[unrelated]->right);
	EXPECT_EQ(doc.sidedefs[doc.linedefs[source]->right]->MidTex(),
			  "NEW_WALL");
	EXPECT_EQ(doc.sidedefs[doc.linedefs[unrelated]->right]->MidTex(),
			  "WALL");
	EXPECT_GT(doc.numSidedefs(), originalSides);

	ASSERT_TRUE(doc.basis.undo());
	EXPECT_EQ(doc.linedefs[source]->right, sharedSide);
	EXPECT_EQ(doc.linedefs[unrelated]->right, sharedSide);
	EXPECT_EQ(doc.numSidedefs(), originalSides);
}

TEST_F(SmartSectorFixture, ExtrudedDoorUsesSmartDoorInSameUndoStep)
{
	addSector();
	int v0 = addVertex(0, 0);
	int v1 = addVertex(0, 64);
	int v2 = addVertex(64, 64);
	int v3 = addVertex(64, 0);
	addLine(v0, v1, 0);
	addLine(v1, v2, 0);
	addLine(v2, v3, 0);
	addLine(v3, v0, 0);

	config.line_types[1].desc = "Door";
	DoorPreset door;
	door.id = "normal";
	door.label = "Normal";
	door.special = 1;
	door.activation = ActivationPolicy::encoded;
	config.door_presets.push_back(door);

	SectorDesignRequest request;
	request.mode = SectorDesignMode::extrude;
	request.anchorLines = {0};
	request.depth = 64;
	request.doorDepth = 16;
	request.startConnection = SectorConnection::door;
	request.doorOptions.presetId = "normal";

	SectorDesignPlan plan = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	ASSERT_TRUE(plan.valid());
	EXPECT_EQ(plan.plannedDoors, 1);

	SectorDesignPlan applied;
	ASSERT_TRUE(M_ApplySectorDesign(
			doc, config, nullptr, MapFormat::doom, request, &applied));
	ASSERT_EQ(applied.createdSectors.size(), 2u);
	const int doorSector = applied.createdSectors.front();
	EXPECT_EQ(doc.sectors[doorSector]->ceilh,
			  doc.sectors[doorSector]->floorh);
	EXPECT_TRUE(std::any_of(doc.linedefs.begin(), doc.linedefs.end(),
			[](const std::shared_ptr<LineDef> &line)
			{
				return line->type == 1;
			}));
	ASSERT_TRUE(doc.basis.undo());
	EXPECT_EQ(doc.numSectors(), 1);
	EXPECT_EQ(doc.numLinedefs(), 4);
	EXPECT_FALSE(doc.basis.undo());
}

TEST_F(SmartSectorFixture, ExtrudedDoorAutoTexturesClearOverridesAndInfer)
{
	addSector();
	const int v0 = addVertex(0, 0);
	const int v1 = addVertex(0, 64);
	const int v2 = addVertex(64, 64);
	const int v3 = addVertex(64, 0);
	const int source = addLine(v0, v1, 0);
	addLine(v1, v2, 0);
	addLine(v2, v3, 0);
	addLine(v3, v0, 0);
	doc.sidedefs[doc.linedefs[source]->right]->mid_tex =
			BA_InternaliseString("BRICK");

	config.line_types[1].desc = "Door";
	DoorPreset door;
	door.id = "normal";
	door.label = "Normal";
	door.special = 1;
	door.activation = ActivationPolicy::encoded;
	config.door_presets.push_back(door);

	SectorDesignRequest request;
	request.mode = SectorDesignMode::extrude;
	request.anchorLines = {source};
	request.extrudeUseDragDepth = false;
	request.depth = 64;
	request.doorDepth = 16;
	request.startConnection = SectorConnection::door;
	request.doorOptions.presetId = "normal";
	request.doorOptions.faceTexture = "WRONGFACE";
	request.doorOptions.trackTexture = "WRONGTRK";
	request.doorOptions.useAutoFaceTexture();
	request.doorOptions.useAutoTrackTexture();

	const SectorDesignPlan plan = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	ASSERT_TRUE(plan.valid());
	EXPECT_TRUE(request.doorOptions.faceTexture.empty());
	EXPECT_TRUE(request.doorOptions.trackTexture.empty());
	EXPECT_EQ(plan.inferredDoorFaceTexture, "BRICK");
	EXPECT_EQ(plan.inferredDoorTrackTexture, "BRICK");
	EXPECT_EQ(plan.doorFaceTexture, "BRICK");
	EXPECT_EQ(plan.doorTrackTexture, "BRICK");
	EXPECT_EQ(plan.resolvedDoorOptions.faceTexture, "BRICK");
	EXPECT_EQ(plan.resolvedDoorOptions.trackTexture, "BRICK");

	SectorDesignPlan applied;
	ASSERT_TRUE(M_ApplySectorDesign(
			doc, config, nullptr, MapFormat::doom, request, &applied));
	ASSERT_FALSE(applied.createdSectors.empty());
	const int doorSector = applied.createdSectors.front();
	bool foundPortal = false;
	bool foundTrack = false;
	for (const std::shared_ptr<LineDef> &line : doc.linedefs)
	{
		if (line->type == 1 && line->right >= 0)
		{
			foundPortal = true;
			EXPECT_EQ(doc.sidedefs[line->right]->UpperTex(), "BRICK");
		}
		if (line->left < 0 && line->right >= 0 &&
			doc.sidedefs[line->right]->sector == doorSector &&
			doc.sidedefs[line->right]->MidTex() == "BRICK")
			foundTrack = true;
	}
	EXPECT_TRUE(foundPortal);
	EXPECT_TRUE(foundTrack);
	ASSERT_TRUE(doc.basis.undo());
	EXPECT_EQ(doc.numSectors(), 1);
}

TEST_F(SmartSectorFixture,
	   ExtrudedDoorDoesNotMisclassifyNeighborOfExistingDoorAsProtected)
{
	const int sourceRoom = addSector(0, 128);
	const int room = addSector(0, 128);
	const int existingDoor = addSector(0, 0);
	const int farRoom = addSector(0, 128);

	const int a0 = addVertex(0, 0);
	const int a1 = addVertex(0, 128);
	const int a2 = addVertex(128, 128);
	const int a3 = addVertex(128, 0);
	const int b0 = addVertex(256, 128);
	const int b1 = addVertex(256, 96);
	const int b2 = addVertex(256, 32);
	const int b3 = addVertex(256, 0);
	const int d0 = addVertex(272, 96);
	const int d1 = addVertex(272, 32);
	const int f0 = addVertex(384, 96);
	const int f1 = addVertex(384, 32);

	addLine(a0, a1, sourceRoom);
	addLine(a1, a2, sourceRoom);
	const int source = addLine(a3, a2, room, sourceRoom);
	addLine(a3, a0, sourceRoom);
	addLine(a2, b0, room);
	addLine(b0, b1, room);
	const int nearDoorPortal = addLine(
			b1, b2, room, existingDoor);
	addLine(b2, b3, room);
	addLine(b3, a3, room);
	addLine(b1, d0, existingDoor);
	const int farDoorPortal = addLine(
			d0, d1, existingDoor, farRoom);
	addLine(d1, b2, existingDoor);
	addLine(d0, f0, farRoom);
	addLine(f0, f1, farRoom);
	addLine(f1, d1, farRoom);

	config.line_types[1].desc = "Door";
	DoorPreset door;
	door.id = "normal";
	door.label = "Normal";
	door.special = 1;
	door.activation = ActivationPolicy::encoded;
	config.door_presets.push_back(door);
	doc.linedefs[nearDoorPortal]->type = 1;
	doc.linedefs[farDoorPortal]->type = 1;

	SectorDesignRequest request;
	request.mode = SectorDesignMode::extrude;
	request.anchorLines = {source};
	request.extrudeUseDragDepth = false;
	request.depth = -64;  // Into the ordinary neighbor on the source line.
	request.doorDepth = 16;
	request.startConnection = SectorConnection::door;
	request.doorOptions.presetId = "normal";

	const SectorDesignPlan preview = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	SString issues;
	for (const SectorDesignIssue &issue : preview.issues)
		issues += issue.message + "\n";
	ASSERT_TRUE(preview.valid()) << issues.c_str();
	EXPECT_EQ(preview.plannedDoors, 1);
	EXPECT_EQ(std::count_if(preview.issues.begin(), preview.issues.end(),
			[](const SectorDesignIssue &issue)
			{
				return issue.message.find("protected door or lift") !=
						std::string::npos;
			}), 0);

	const int originalSectors = doc.numSectors();
	const int originalLines = doc.numLinedefs();
	SectorDesignPlan applied;
	const bool appliedSuccessfully = M_ApplySectorDesign(
			doc, config, nullptr, MapFormat::doom, request, &applied);
	SString appliedIssues;
	for (const SectorDesignIssue &issue : applied.issues)
		appliedIssues += issue.message + "\n";
	ASSERT_TRUE(appliedSuccessfully) << appliedIssues.c_str();
	EXPECT_EQ(doc.sectors[existingDoor]->floorh,
			  doc.sectors[existingDoor]->ceilh);
	EXPECT_GT(doc.numSectors(), originalSectors);
	EXPECT_GE(std::count_if(doc.linedefs.begin(), doc.linedefs.end(),
			[](const std::shared_ptr<LineDef> &line)
			{
				return line->type == 1;
			}), 4);
	ASSERT_TRUE(doc.basis.undo());
	EXPECT_EQ(doc.numSectors(), originalSectors);
	EXPECT_EQ(doc.numLinedefs(), originalLines);
	EXPECT_EQ(doc.linedefs[nearDoorPortal]->type, 1);
	EXPECT_EQ(doc.linedefs[farDoorPortal]->type, 1);
}

TEST_F(SmartSectorFixture, ExtrudePanelAutoButtonsClearTypedDoorTextures)
{
	inst.conf = config;
	inst.loaded.gameName = "doom2";
	inst.loaded.levelFormat = MapFormat::doom;
	inst.edit.mode = ObjType::sectors;
	inst.edit.render3d = false;
	inst.edit.Selected.emplace(ObjType::sectors);

	DoorPreset door;
	door.id = "normal";
	door.label = "Normal";
	door.special = 1;
	door.activation = ActivationPolicy::encoded;
	inst.conf.line_types[1].desc = "Door";
	inst.conf.door_presets.push_back(door);

	UI_SectorDesigner panel(inst, 0, 0, 308, 760);
	panel.Open(SectorDesignMode::extrude);
	Fl_Choice *base =
			FindWidgetByLabel<Fl_Choice>(panel, "Base seam:");
	ASSERT_NE(base, nullptr);
	base->value(static_cast<int>(SectorConnection::door));
	base->do_callback();

	Fl_Input *face = FindInputByLabel(panel, "Door face:");
	Fl_Input *track = FindInputByLabel(panel, "Track wall:");
	Fl_Toggle_Button *faceAuto =
			FindWidgetByLabel<Fl_Toggle_Button>(panel, "Face Auto");
	Fl_Toggle_Button *trackAuto =
			FindWidgetByLabel<Fl_Toggle_Button>(panel, "Track Auto");
	ASSERT_NE(face, nullptr);
	ASSERT_NE(track, nullptr);
	ASSERT_NE(faceAuto, nullptr);
	ASSERT_NE(trackAuto, nullptr);

	face->value("CUSTOM_FACE");
	face->do_callback();
	track->value("CUSTOM_TRACK");
	track->do_callback();
	EXPECT_EQ(faceAuto->value(), 0);
	EXPECT_EQ(trackAuto->value(), 0);

	faceAuto->do_callback();
	trackAuto->do_callback();
	EXPECT_STREQ(face->value(), "");
	EXPECT_STREQ(track->value(), "");
	EXPECT_EQ(faceAuto->value(), 1);
	EXPECT_EQ(trackAuto->value(), 1);
	panel.Close();
}

TEST_F(SmartSectorFixture,
	   SectorPropertyTextureSelectorsCoverFlatsWallsAndInsetModels)
{
	struct ChooserReset
	{
		~ChooserReset()
		{
			UI_LoadedImageChooser_Override = {};
		}
	} resetChooser;

	inst.conf = config;
	inst.loaded.gameName = "doom2";
	inst.loaded.levelFormat = MapFormat::doom;
	inst.edit.mode = ObjType::sectors;
	inst.edit.render3d = false;
	inst.edit.Selected.emplace(ObjType::sectors);

	std::vector<UI_ImageSelectionKind> kinds;
	int floorChoices = 0;
	UI_LoadedImageChooser_Override =
			[&](Instance &, UI_ImageSelectionKind kind,
				const SString &purpose, const SString &inferred,
				SString &selected)
			{
				kinds.push_back(kind);
				EXPECT_FALSE(inferred.empty());
				if (purpose.find("Floor") != SString::npos)
					selected = floorChoices++ == 0 ?
							"RINGFLAT" : "INNERFLT";
				else if (purpose.find("Ceiling") != SString::npos)
					selected = "CEILPICK";
				else
					selected = "WALLPICK";
				return true;
			};

	UI_SectorDesigner panel(inst, 0, 0, 308, 760);
	panel.Open(SectorDesignMode::inset);
	Fl_Input *floor = FindInputByLabel(panel, "Floor flat:");
	Fl_Input *ceiling = FindInputByLabel(panel, "Ceil flat:");
	Fl_Input *wall = FindInputByLabel(panel, "Wall tex:");
	Fl_Button *chooseFloor =
			FindWidgetByLabel<Fl_Button>(panel, "Choose Floor...");
	Fl_Button *chooseCeiling =
			FindWidgetByLabel<Fl_Button>(panel, "Choose Ceiling...");
	Fl_Button *chooseWall =
			FindWidgetByLabel<Fl_Button>(panel, "Choose Wall...");
	Fl_Toggle_Button *floorAuto =
			FindWidgetByLabel<Fl_Toggle_Button>(panel, "Floor Auto");
	Fl_Toggle_Button *ceilingAuto =
			FindWidgetByLabel<Fl_Toggle_Button>(panel, "Ceil Auto");
	Fl_Choice *target =
			FindWidgetByLabel<Fl_Choice>(panel, "Edit:");
	ASSERT_NE(floor, nullptr);
	ASSERT_NE(ceiling, nullptr);
	ASSERT_NE(wall, nullptr);
	ASSERT_NE(chooseFloor, nullptr);
	ASSERT_NE(chooseCeiling, nullptr);
	ASSERT_NE(chooseWall, nullptr);
	ASSERT_NE(floorAuto, nullptr);
	ASSERT_NE(ceilingAuto, nullptr);
	ASSERT_NE(target, nullptr);
	EXPECT_EQ(CountWidgetsByType<UI_Pic>(panel), 5);
	EXPECT_EQ(floorAuto->value(), 1);
	EXPECT_GE(chooseFloor->w(), 136);
	EXPECT_GE(chooseCeiling->w(), 136);
	EXPECT_LE(chooseFloor->labelsize(), 11);
	EXPECT_LE(chooseCeiling->labelsize(), 11);
	EXPECT_LE(chooseFloor->x() + chooseFloor->w() + 6,
			  floorAuto->x());
	EXPECT_LE(chooseCeiling->x() + chooseCeiling->w() + 6,
			  ceilingAuto->x());
	EXPECT_LT(chooseFloor->y() + chooseFloor->h(), ceiling->y());
	EXPECT_LT(chooseCeiling->y() + chooseCeiling->h(), wall->y());

	chooseFloor->do_callback();
	EXPECT_STREQ(floor->value(), "RINGFLAT");
	EXPECT_EQ(floorAuto->value(), 0);

	target->value(1);
	target->do_callback();
	EXPECT_STREQ(floor->value(), "");
	chooseFloor->do_callback();
	EXPECT_STREQ(floor->value(), "INNERFLT");

	target->value(0);
	target->do_callback();
	EXPECT_STREQ(floor->value(), "RINGFLAT");
	chooseCeiling->do_callback();
	chooseWall->do_callback();
	EXPECT_STREQ(ceiling->value(), "CEILPICK");
	EXPECT_STREQ(wall->value(), "WALLPICK");
	ASSERT_EQ(kinds.size(), 4u);
	EXPECT_EQ(kinds[0], UI_ImageSelectionKind::flat);
	EXPECT_EQ(kinds[1], UI_ImageSelectionKind::flat);
	EXPECT_EQ(kinds[2], UI_ImageSelectionKind::flat);
	EXPECT_EQ(kinds[3], UI_ImageSelectionKind::wallTexture);

	floorAuto->do_callback();
	EXPECT_STREQ(floor->value(), "");
	EXPECT_EQ(floorAuto->value(), 1);
	target->value(1);
	target->do_callback();
	EXPECT_STREQ(floor->value(), "INNERFLT");
	panel.Close();
}

TEST_F(SmartSectorFixture,
	   SectorSpecialSupportsManualValuesAndConfiguredBrowserChoices)
{
	inst.conf = config;
	inst.conf.sector_types[9].desc = "Damage";
	inst.conf.sector_types[13].desc = "Blinking light";
	inst.loaded.gameName = "sector_special_chooser";
	inst.loaded.levelFormat = MapFormat::doom;
	inst.edit.mode = ObjType::sectors;
	inst.edit.render3d = false;
	inst.edit.Selected.emplace(ObjType::sectors);

	UI_SectorDesigner panel(inst, 0, 0, 308, 760);
	panel.Open(SectorDesignMode::room);
	Fl_Input *special = FindInputByLabel(panel, "Special:");
	Fl_Output *meaning =
			FindWidgetByLabel<Fl_Output>(panel, "Meaning:");
	Fl_Button *choose =
			FindWidgetByLabel<Fl_Button>(panel, "Choose Special...");
	ASSERT_NE(special, nullptr);
	ASSERT_NE(meaning, nullptr);
	ASSERT_NE(choose, nullptr);
	EXPECT_NE(SString(meaning->value()).find("Auto"), SString::npos);

	special->value("9");
	special->do_callback();
	EXPECT_STREQ(special->value(), "9");
	EXPECT_NE(SString(meaning->value()).find("Damage"), SString::npos);

	panel.BrowsedItem(BrowserMode::sectorTypes, 13, "", 0);
	EXPECT_STREQ(special->value(), "13");
	EXPECT_NE(
			SString(meaning->value()).find("Blinking light"),
			SString::npos);

	special->value("777");
	special->do_callback();
	EXPECT_NE(
			SString(meaning->value()).find("Custom"),
			SString::npos);
	EXPECT_TRUE(panel.Plan().valid() ||
				panel.ReviewText().find("WAITING FOR GESTURE") !=
						SString::npos);
	panel.Close();
}

TEST_F(SmartSectorFixture,
	   SectorSpecialButtonOpensSearchableBrowserAndRoutesSelection)
{
	inst.conf = config;
	inst.conf.sector_types[42].desc = "Secret effect";
	inst.loaded.gameName = "sector_special_browser";
	inst.loaded.levelFormat = MapFormat::doom;
	inst.edit.mode = ObjType::sectors;
	inst.edit.render3d = false;
	inst.edit.Selected.emplace(ObjType::sectors);

	UI_MainWindow window(inst);
	inst.main_win = &window;
	window.ShowSectorDesigner(SectorDesignMode::room);
	Fl_Button *choose = FindWidgetByLabel<Fl_Button>(
			*window.sector_design_box, "Choose Special...");
	Fl_Input *special = FindInputByLabel(
			*window.sector_design_box, "Special:");
	ASSERT_NE(choose, nullptr);
	ASSERT_NE(special, nullptr);

	choose->do_callback();
	EXPECT_TRUE(window.browser->visible());
	EXPECT_EQ(window.browser->GetMode(), BrowserMode::sectorTypes);
	window.BrowsedItem(BrowserMode::sectorTypes, 42, "", 0);
	EXPECT_STREQ(special->value(), "42");

	window.HideSectorDesigner();
	inst.main_win = nullptr;
}

TEST_F(SmartSectorFixture,
	   CorridorCanvasWheelUsesGridStepAndLeavesOtherModesToZoom)
{
	inst.conf = config;
	inst.loaded.gameName = "corridor_wheel_width";
	inst.loaded.levelFormat = MapFormat::doom;
	inst.edit.mode = ObjType::sectors;
	inst.edit.render3d = false;
	inst.edit.Selected.emplace(ObjType::sectors);
	inst.grid.ForceStep(16);
	inst.grid.SetSnap(true);

	UI_SectorDesigner panel(inst, 0, 0, 308, 760);
	panel.Open(SectorDesignMode::corridor);
	Fl_Input *width = FindInputByLabel(panel, "Width:");
	ASSERT_NE(width, nullptr);
	width->value("64");
	width->do_callback();
	panel.CanvasClick({0, 0}, 0);
	panel.CanvasClick({128, 0}, 0);
	ASSERT_TRUE(panel.Plan().valid());
	const std::vector<v2double_t> initial =
			panel.Plan().shapes.front().outer;

	EXPECT_TRUE(panel.CanvasWheel(-1));
	EXPECT_STREQ(width->value(), "80");
	ASSERT_TRUE(panel.Plan().valid());
	EXPECT_NE(panel.Plan().shapes.front().outer, initial);

	EXPECT_TRUE(panel.CanvasWheel(2));
	EXPECT_STREQ(width->value(), "48");
	EXPECT_TRUE(panel.CanvasWheel(20));
	EXPECT_STREQ(width->value(), "16");
	EXPECT_FALSE(doc.basis.undo());

	Fl_Choice *mode = FindWidgetByLabel<Fl_Choice>(panel, "Mode:");
	ASSERT_NE(mode, nullptr);
	mode->value(static_cast<int>(SectorDesignMode::room));
	mode->do_callback();
	EXPECT_FALSE(panel.CanvasWheel(-1));
	EXPECT_STREQ(width->value(), "16");
	panel.Close();
}

TEST_F(SmartSectorFixture,
	   InsetCanvasWheelUsesGridStepAndPreservesInwardOrOutwardDirection)
{
	addBoxSector(0, 0, 256, 256);
	inst.conf = config;
	inst.loaded.gameName = "inset_wheel_thickness";
	inst.loaded.levelFormat = MapFormat::doom;
	inst.edit.mode = ObjType::sectors;
	inst.edit.render3d = false;
	inst.edit.Selected.emplace(ObjType::sectors);
	inst.grid.ForceStep(16);
	inst.grid.SetSnap(true);

	UI_SectorDesigner panel(inst, 0, 0, 308, 760);
	panel.Open(SectorDesignMode::inset);
	Fl_Input *offset = FindInputByLabel(panel, "Offset:");
	ASSERT_NE(offset, nullptr);
	offset->value("16");
	offset->do_callback();
	panel.CanvasClick({128, 128}, 0);
	ASSERT_TRUE(panel.Plan().valid());

	EXPECT_TRUE(panel.CanvasWheel(-1));
	EXPECT_STREQ(offset->value(), "32");
	ASSERT_TRUE(panel.Plan().valid());
	EXPECT_TRUE(panel.CanvasWheel(2));
	EXPECT_STREQ(offset->value(), "16");

	offset->value("-16");
	offset->do_callback();
	EXPECT_TRUE(panel.CanvasWheel(-2));
	EXPECT_STREQ(offset->value(), "-48");
	EXPECT_TRUE(panel.CanvasWheel(20));
	EXPECT_STREQ(offset->value(), "-16");
	offset->value("8");
	offset->do_callback();
	EXPECT_TRUE(panel.CanvasWheel(1));
	EXPECT_STREQ(offset->value(), "8");
	EXPECT_TRUE(panel.CanvasWheel(-1));
	EXPECT_STREQ(offset->value(), "24");
	EXPECT_TRUE(panel.CanvasWheel(1));
	EXPECT_STREQ(offset->value(), "8");
	EXPECT_FALSE(doc.basis.undo());

	Fl_Choice *mode = FindWidgetByLabel<Fl_Choice>(panel, "Mode:");
	ASSERT_NE(mode, nullptr);
	mode->value(static_cast<int>(SectorDesignMode::polygon));
	mode->do_callback();
	EXPECT_FALSE(panel.CanvasWheel(-1));
	EXPECT_STREQ(offset->value(), "8");
	panel.Close();
}

TEST_F(SmartSectorFixture,
	   ArchitectureCanvasWheelScalesByQuarterGridAndRespectsStyleMinimum)
{
	inst.conf = config;
	inst.loaded.gameName = "architecture_wheel_size";
	inst.loaded.levelFormat = MapFormat::doom;
	inst.edit.mode = ObjType::sectors;
	inst.edit.render3d = false;
	inst.edit.Selected.emplace(ObjType::sectors);
	inst.grid.ForceStep(64);
	inst.grid.SetSnap(false);

	UI_SectorDesigner panel(inst, 0, 0, 340, 760);
	panel.Open(SectorDesignMode::architecture);
	Fl_Input *size = FindInputByLabel(panel, "Diameter:");
	Fl_Choice *style = FindWidgetByLabel<Fl_Choice>(panel, "Style:");
	ASSERT_NE(size, nullptr);
	ASSERT_NE(style, nullptr);
	EXPECT_STREQ(size->value(), "24");

	panel.CanvasClick({-128, -96}, 0);
	panel.CanvasMove({128, 96}, 0, true);
	panel.CanvasRelease({128, 96}, 0);
	ASSERT_TRUE(panel.Plan().valid());
	EXPECT_EQ(panel.Plan().plannedArchitectureHosts, 1);

	EXPECT_TRUE(panel.CanvasWheel(-1));
	EXPECT_STREQ(size->value(), "40");
	ASSERT_TRUE(panel.Plan().valid());
	EXPECT_TRUE(panel.CanvasWheel(2));
	EXPECT_STREQ(size->value(), "16");
	EXPECT_TRUE(panel.CanvasWheel(20));
	EXPECT_STREQ(size->value(), "16");

	size->value("4");
	size->do_callback();
	style->value(static_cast<int>(SectorArchitectureStyle::infernal));
	style->do_callback();
	EXPECT_STREQ(size->value(), "32");
	ASSERT_TRUE(panel.Plan().valid());

	style->value(static_cast<int>(SectorArchitectureStyle::functional));
	style->do_callback();
	EXPECT_TRUE(panel.CanvasWheel(2));
	EXPECT_STREQ(size->value(), "4");
	ASSERT_TRUE(panel.Plan().valid());
	EXPECT_TRUE(panel.CanvasWheel(-1));
	EXPECT_STREQ(size->value(), "20");
	EXPECT_FALSE(doc.basis.undo());
	panel.Close();
}

TEST_F(SmartSectorFixture,
	   ArchitecturePanelBuildsInVoidSelectsResultsAndKeepsDesignerActive)
{
	inst.conf = config;
	inst.loaded.gameName = "architecture_void_commit";
	inst.loaded.levelFormat = MapFormat::doom;
	inst.edit.mode = ObjType::sectors;
	inst.edit.render3d = false;
	inst.edit.Selected.emplace(ObjType::sectors);
	inst.grid.SetSnap(false);

	UI_SectorDesigner panel(inst, 0, 0, 340, 760);
	panel.Open(SectorDesignMode::architecture);
	panel.CanvasClick({-128, -96}, 0);
	panel.CanvasMove({128, 96}, 0, true);
	panel.CanvasRelease({128, 96}, 0);
	ASSERT_TRUE(panel.Plan().valid());
	ASSERT_EQ(panel.Plan().plannedArchitectureHosts, 1);
	ASSERT_EQ(panel.Plan().plannedStructures, 1);
	ASSERT_TRUE(inst.edit.designAssistPreview);

	panel.Commit();
	EXPECT_TRUE(panel.Active());
	EXPECT_EQ(doc.numSectors(), 2);
	ASSERT_TRUE(inst.edit.Selected);
	EXPECT_EQ(inst.edit.Selected->count_obj(), 2);
	EXPECT_TRUE(inst.edit.Selected->get(0));
	EXPECT_TRUE(inst.edit.Selected->get(1));
	EXPECT_FALSE(inst.edit.designAssistPreview);
	EXPECT_NE(panel.ReviewText().find(
			"Status: WAITING FOR GESTURE"), SString::npos);

	ASSERT_TRUE(doc.basis.undo());
	EXPECT_EQ(doc.numVertices(), 0);
	EXPECT_EQ(doc.numLinedefs(), 0);
	EXPECT_EQ(doc.numSidedefs(), 0);
	EXPECT_EQ(doc.numSectors(), 0);
	EXPECT_FALSE(doc.basis.undo());
	ASSERT_TRUE(doc.basis.redo());
	EXPECT_EQ(doc.numSectors(), 2);
	panel.Close();
}

TEST_F(SmartSectorFixture,
	   ArchitecturePanelPreviewsAndBuildsInsideAnExistingSector)
{
	const int host = addBoxSector(-256, -256, 256, 256, 24, 160);
	doc.sectors[host]->light = 176;
	doc.sectors[host]->type = 7;
	doc.sectors[host]->tag = 91;
	const Sector originalHost = *doc.sectors[host];

	inst.conf = config;
	inst.loaded.gameName = "architecture_existing_host";
	inst.loaded.levelFormat = MapFormat::doom;
	inst.edit.mode = ObjType::sectors;
	inst.edit.render3d = false;
	inst.edit.Selected.emplace(ObjType::sectors);
	inst.grid.SetSnap(false);

	UI_SectorDesigner panel(inst, 0, 0, 340, 760);
	panel.Open(SectorDesignMode::architecture);
	panel.CanvasClick({-128, -128}, 0);
	panel.CanvasMove({128, 128}, 0, true);
	panel.CanvasRelease({128, 128}, 0);
	ASSERT_TRUE(panel.Plan().valid());
	EXPECT_EQ(panel.Plan().plannedArchitectureHosts, 0);
	EXPECT_EQ(panel.Plan().retainedSectors, std::vector<int>({host}));
	ASSERT_EQ(panel.Plan().plannedStructures, 1);
	ASSERT_EQ(panel.Plan().shapes.size(), 1u);
	EXPECT_EQ(panel.Plan().shapes.front().role,
			  DesignPreviewRole::architecture);
	EXPECT_NE(panel.ReviewText().find(
			"Host sector #0 is locked from the gesture start"),
			SString::npos);
	ASSERT_TRUE(inst.edit.designAssistPreview);
	EXPECT_TRUE(std::any_of(
			inst.edit.designAssistPreview->paths.begin(),
			inst.edit.designAssistPreview->paths.end(),
			[](const DesignPreviewPath &path)
			{
				return path.role == DesignPreviewRole::architecture &&
						path.filled && path.closed;
			}));

	panel.Commit();
	EXPECT_EQ(doc.numSectors(), 2);
	EXPECT_EQ(doc.sectors[host]->floorh, originalHost.floorh);
	EXPECT_EQ(doc.sectors[host]->ceilh, originalHost.ceilh);
	EXPECT_EQ(doc.sectors[host]->light, originalHost.light);
	EXPECT_EQ(doc.sectors[host]->type, originalHost.type);
	EXPECT_EQ(doc.sectors[host]->tag, originalHost.tag);
	ASSERT_TRUE(inst.edit.Selected);
	EXPECT_EQ(inst.edit.Selected->count_obj(), 1);
	EXPECT_TRUE(inst.edit.Selected->get(1));
	ASSERT_TRUE(doc.basis.undo());
	EXPECT_EQ(doc.numSectors(), 1);
	EXPECT_FALSE(doc.basis.undo());
	panel.Close();
}

TEST_F(SmartSectorFixture,
	   ArchitecturePanelBuildsPurposeBuiltCeilingGeometry)
{
	const int host = addBoxSector(-384, -320, 384, 320, 24, 160);
	const Sector originalHost = *doc.sectors[host];
	inst.conf = config;
	inst.loaded.gameName = "architecture_coffered_ceiling";
	inst.loaded.levelFormat = MapFormat::doom;
	inst.edit.mode = ObjType::sectors;
	inst.edit.render3d = false;
	inst.edit.Selected.emplace(ObjType::sectors);
	inst.grid.SetSnap(false);

	UI_SectorDesigner panel(inst, 0, 0, 360, 760);
	panel.Open(SectorDesignMode::architecture);
	Fl_Choice *family =
			FindWidgetByLabel<Fl_Choice>(panel, "Family:");
	Fl_Choice *structure =
			FindWidgetByLabel<Fl_Choice>(panel, "Structure:");
	Fl_Input *elevation =
			FindInputByLabel(panel, "Elevation:");
	ASSERT_NE(family, nullptr);
	ASSERT_NE(structure, nullptr);
	ASSERT_NE(elevation, nullptr);
	family->value(5);
	family->do_callback();
	structure->value(0);
	structure->do_callback();
	ASSERT_TRUE(elevation->active());
	elevation->value("24");
	elevation->do_callback();

	panel.CanvasClick({-288, -224}, 0);
	panel.CanvasMove({288, 224}, 0, true);
	panel.CanvasRelease({288, 224}, 0);
	ASSERT_TRUE(panel.Plan().valid());
	ASSERT_EQ(panel.Plan().plannedStructures, 8);
	EXPECT_TRUE(std::all_of(
			panel.Plan().shapes.begin(), panel.Plan().shapes.end(),
			[](const PlannedSectorShape &shape)
			{
				return shape.role ==
							DesignPreviewRole::architectureCeiling &&
						!shape.closed &&
						shape.floorDelta == 0 &&
						shape.ceilingDelta == 24;
			}));
	EXPECT_NE(panel.ReviewText().find(
			"Architecture family: Ceilings and vaults. "
			"Structure: Coffered ceiling"), SString::npos);
	EXPECT_NE(panel.ReviewText().find(
			"24.0-unit elevation/depth"), SString::npos);

	panel.Commit();
	EXPECT_EQ(doc.numSectors(), 9);
	EXPECT_EQ(doc.sectors[host]->floorh, originalHost.floorh);
	EXPECT_EQ(doc.sectors[host]->ceilh, originalHost.ceilh);
	ASSERT_TRUE(inst.edit.Selected);
	EXPECT_EQ(inst.edit.Selected->count_obj(), 8);
	for (int sector = 1; sector < doc.numSectors(); ++sector)
	{
		EXPECT_EQ(doc.sectors[sector]->floorh, originalHost.floorh);
		EXPECT_EQ(doc.sectors[sector]->ceilh,
				  originalHost.ceilh + 24);
	}
	ASSERT_TRUE(doc.basis.undo());
	EXPECT_EQ(doc.numSectors(), 1);
	EXPECT_FALSE(doc.basis.undo());
	panel.Close();
}

TEST_F(SmartSectorFixture,
	   ArchitectureFamiliesDragPreviewAndBuildInsideAnExistingSector)
{
	const int host = addBoxSector(-640, -512, 640, 512, 24, 192);
	const Sector originalHost = *doc.sectors[host];
	inst.conf = config;
	inst.loaded.gameName = "architecture_family_existing_host";
	inst.loaded.levelFormat = MapFormat::doom;
	inst.edit.mode = ObjType::sectors;
	inst.edit.render3d = false;
	inst.edit.Selected.emplace(ObjType::sectors);
	inst.grid.SetSnap(false);

	UI_SectorDesigner panel(inst, 0, 0, 360, 800);
	panel.Open(SectorDesignMode::architecture);
	Fl_Choice *family =
			FindWidgetByLabel<Fl_Choice>(panel, "Family:");
	Fl_Choice *structure =
			FindWidgetByLabel<Fl_Choice>(panel, "Structure:");
	ASSERT_NE(family, nullptr);
	ASSERT_NE(structure, nullptr);

	struct Scenario
	{
		int family;
		int structure;
		const char *familyName;
		DesignPreviewRole role;
	};
	const Scenario scenarios[] =
	{
		{1, 0, "Floors and terraces",
		 DesignPreviewRole::architectureFloor},
		{2, 0, "Circulation",
		 DesignPreviewRole::architectureCirculation},
		{3, 1, "Waterworks",
		 DesignPreviewRole::architectureWater},
		{4, 0, "Walls and screens",
		 DesignPreviewRole::architectureWall},
		{5, 0, "Ceilings and vaults",
		 DesignPreviewRole::architectureCeiling}
	};

	for (const Scenario &scenario : scenarios)
	{
		family->value(scenario.family);
		family->do_callback();
		structure->value(scenario.structure);
		structure->do_callback();

		panel.CanvasClick({-384, -288}, 0);
		panel.CanvasMove({384, 288}, 0, true);
		panel.CanvasRelease({384, 288}, 0);
		ASSERT_TRUE(panel.Plan().valid()) << scenario.familyName;
		ASSERT_GT(panel.Plan().plannedStructures, 0);
		EXPECT_TRUE(std::all_of(
				panel.Plan().shapes.begin(), panel.Plan().shapes.end(),
				[&](const PlannedSectorShape &shape)
				{
					return shape.role == scenario.role &&
							shape.modelSector == host;
				}));
		ASSERT_TRUE(inst.edit.designAssistPreview);
		EXPECT_TRUE(std::any_of(
				inst.edit.designAssistPreview->paths.begin(),
				inst.edit.designAssistPreview->paths.end(),
				[&](const DesignPreviewPath &path)
				{
					return path.role == scenario.role &&
							path.filled && path.closed;
				}));
		const SString review = panel.ReviewText();
		EXPECT_NE(review.find(SString::printf(
				"Architecture family: %s", scenario.familyName)),
				SString::npos);
		EXPECT_NE(review.find("Effect:"), SString::npos);
		EXPECT_NE(review.find("preview)"), SString::npos);

		panel.Commit();
		ASSERT_GT(doc.numSectors(), 1);
		EXPECT_EQ(doc.sectors[host]->floorh, originalHost.floorh);
		EXPECT_EQ(doc.sectors[host]->ceilh, originalHost.ceilh);
		ASSERT_TRUE(inst.edit.Selected);
		ASSERT_GT(inst.edit.Selected->count_obj(), 0);

		const Sector &created = *doc.sectors[1];
		switch (scenario.role)
		{
			case DesignPreviewRole::architectureFloor:
			case DesignPreviewRole::architectureCirculation:
				EXPECT_GT(created.floorh, originalHost.floorh);
				break;
			case DesignPreviewRole::architectureWater:
				EXPECT_LT(created.floorh, originalHost.floorh);
				break;
			case DesignPreviewRole::architectureWall:
				EXPECT_EQ(created.floorh, created.ceilh);
				break;
			case DesignPreviewRole::architectureCeiling:
				EXPECT_GT(created.ceilh, originalHost.ceilh);
				break;
			default:
				FAIL() << "Unexpected architecture preview role";
		}

		ASSERT_TRUE(doc.basis.undo());
		EXPECT_EQ(doc.numSectors(), 1);
		EXPECT_FALSE(doc.basis.undo());
	}
	panel.Close();
}

TEST_F(SmartSectorFixture,
	   EveryPurposeBuiltStructureUsesRealCanvasDragBesideANeighbor)
{
	const int host = addSector(24, 192);
	const int neighbor = addSector(8, 160);
	doc.sectors[neighbor]->light = 112;
	doc.sectors[neighbor]->tag = 77;
	const int v0 = addVertex(-384, -384);
	const int v1 = addVertex(-384, 384);
	const int v2 = addVertex(0, 384);
	const int v3 = addVertex(0, -384);
	const int v4 = addVertex(384, 384);
	const int v5 = addVertex(384, -384);
	addLine(v0, v1, host);
	addLine(v1, v2, host);
	addLine(v2, v3, host, neighbor);
	addLine(v3, v0, host);
	addLine(v2, v4, neighbor);
	addLine(v4, v5, neighbor);
	addLine(v5, v3, neighbor);
	const Sector originalHost = *doc.sectors[host];
	const Sector originalNeighbor = *doc.sectors[neighbor];
	inst.conf = config;
	inst.loaded.gameName = "architecture_real_canvas_events";
	inst.loaded.levelFormat = MapFormat::doom;
	inst.edit.mode = ObjType::sectors;
	inst.edit.render3d = false;
	inst.edit.Selected.emplace(ObjType::sectors);
	inst.grid.SetSnap(false);

	UI_MainWindow window(inst);
	inst.main_win = &window;
	window.ShowSectorDesigner(SectorDesignMode::architecture);
	UI_SectorDesigner &panel = *window.sector_design_box;
	Fl_Choice *family =
			FindWidgetByLabel<Fl_Choice>(panel, "Family:");
	Fl_Choice *structure =
			FindWidgetByLabel<Fl_Choice>(panel, "Structure:");
	Fl_Button *make =
			FindWidgetByLabel<Fl_Button>(panel, "Make Sectors");
	ASSERT_NE(family, nullptr);
	ASSERT_NE(structure, nullptr);
	ASSERT_NE(make, nullptr);

	struct Scenario
	{
		int family;
		int structure;
		SectorArchitectureElement element;
	};
	std::vector<Scenario> scenarios;
	for (const SectorArchitectureDescriptor &descriptor :
			M_ArchitectureCatalog())
	{
		if (static_cast<int>(descriptor.element) <
			static_cast<int>(SectorArchitectureElement::crossCore))
			continue;
		int structureIndex = 0;
		for (const SectorArchitectureDescriptor &candidate :
				M_ArchitectureCatalog())
		{
			if (candidate.element == descriptor.element)
				break;
			if (candidate.family == descriptor.family)
				structureIndex++;
		}
		scenarios.push_back({
			static_cast<int>(descriptor.family),
			structureIndex, descriptor.element
		});
	}
	ASSERT_EQ(scenarios.size(), 30u);

	const bool oldFocus = global::app_has_focus;
	global::app_has_focus = true;
	auto sendPointerEvent = [&](int event, const v2double_t &point,
								bool buttonDown)
	{
		const v2double_t origin = inst.grid.getOrig();
		const double scale = inst.grid.getScale();
		Fl::e_x_root = window.x_root() + window.canvas->x() +
				window.canvas->w() / 2 +
				iround((point.x - origin.x) * scale);
		Fl::e_y_root = window.y_root() + window.canvas->y() +
				window.canvas->h() / 2 +
				iround((origin.y - point.y) * scale);
		Fl::e_x = Fl::e_x_root - window.x_root();
		Fl::e_y = Fl::e_y_root - window.y_root();
		Fl::e_keysym = FL_Button + 1;
		Fl::e_state = buttonDown ? FL_BUTTON1 : 0;
		return window.canvas->handle(event);
	};

	int scenarioIndex = 0;
	for (const Scenario &scenario : scenarios)
	{
		SCOPED_TRACE(SString::printf(
				"architecture element %d",
				static_cast<int>(scenario.element)).c_str());
		family->value(scenario.family);
		family->do_callback();
		structure->value(scenario.structure);
		structure->do_callback();

		EXPECT_EQ(sendPointerEvent(FL_PUSH, {-320, -224}, true), 1);
		// Alternate the backend-reported button state. The event is still an
		// FL_DRAG and must remain a drag even on compositors which omit the
		// intermediate FL_BUTTON1 state bit.
		EXPECT_EQ(sendPointerEvent(
				FL_DRAG, {-32, 224}, scenarioIndex % 2 == 0), 1);
		EXPECT_EQ(sendPointerEvent(FL_RELEASE, {-32, 224}, false), 1);
		SString shapeBounds;
		for (const PlannedSectorShape &shape : panel.Plan().shapes)
		{
			double lowX = std::numeric_limits<double>::infinity();
			double lowY = std::numeric_limits<double>::infinity();
			double highX = -std::numeric_limits<double>::infinity();
			double highY = -std::numeric_limits<double>::infinity();
			for (const v2double_t &point : shape.outer)
			{
				lowX = std::min(lowX, point.x);
				lowY = std::min(lowY, point.y);
				highX = std::max(highX, point.x);
				highY = std::max(highY, point.y);
			}
			shapeBounds += SString::printf(
					"\nrole %d bounds %.1f %.1f .. %.1f %.1f",
					static_cast<int>(shape.role),
					lowX, lowY, highX, highY);
		}
		for (const SectorDesignIssue &issue : panel.Plan().issues)
			if (issue.position)
				shapeBounds += SString::printf(
						"\nissue position %.1f %.1f",
						issue.position->x, issue.position->y);
		ASSERT_TRUE(panel.Plan().valid())
				<< static_cast<int>(scenario.element) << "\n"
				<< panel.ReviewText().c_str()
				<< shapeBounds.c_str();
		ASSERT_GT(panel.Plan().shapes.size(), 0u)
				<< panel.ReviewText().c_str();
		EXPECT_NE(panel.ReviewText().find(
				M_ArchitectureDescriptor(
						scenario.element).label), SString::npos)
				<< panel.ReviewText().c_str();
		EXPECT_TRUE(std::all_of(
				panel.Plan().shapes.begin(), panel.Plan().shapes.end(),
				[&](const PlannedSectorShape &shape)
				{
					return shape.role ==
							ExpectedArchitectureRole(scenario.element);
				}));

		EXPECT_EQ(doc.numSectors(), 2);
		EXPECT_EQ(doc.sectors[host]->floorh, originalHost.floorh);
		EXPECT_EQ(doc.sectors[host]->ceilh, originalHost.ceilh);
		EXPECT_EQ(doc.sectors[neighbor]->floorh,
				  originalNeighbor.floorh);
		EXPECT_EQ(doc.sectors[neighbor]->ceilh,
				  originalNeighbor.ceilh);
		ASSERT_TRUE(make->active())
				<< panel.ReviewText().c_str();
		make->do_callback();
		EXPECT_GT(doc.numSectors(), 2);
		EXPECT_EQ(doc.sectors[host]->floorh, originalHost.floorh);
		EXPECT_EQ(doc.sectors[host]->ceilh, originalHost.ceilh);
		EXPECT_EQ(doc.sectors[neighbor]->floorh,
				  originalNeighbor.floorh);
		EXPECT_EQ(doc.sectors[neighbor]->ceilh,
				  originalNeighbor.ceilh);
		EXPECT_EQ(doc.sectors[neighbor]->light,
				  originalNeighbor.light);
		EXPECT_EQ(doc.sectors[neighbor]->tag,
				  originalNeighbor.tag);
		ASSERT_TRUE(doc.basis.undo());
		EXPECT_EQ(doc.numSectors(), 2);
		EXPECT_FALSE(doc.basis.undo());
		panel.Refresh();
		scenarioIndex++;
	}

	// A compositor may coalesce a short drag into press + displaced release.
	// The release coordinate itself must still complete the architecture
	// gesture instead of silently leaving one anchor.
	family->value(1);
	family->do_callback();
	structure->value(0);
	structure->do_callback();
	EXPECT_EQ(sendPointerEvent(FL_PUSH, {-320, -192}, true), 1);
	EXPECT_EQ(sendPointerEvent(FL_RELEASE, {-64, 192}, false), 1);
	ASSERT_TRUE(panel.Plan().valid());
	ASSERT_EQ(panel.Plan().shapes.size(), 1u);
	panel.Escape();

	global::app_has_focus = oldFocus;
	window.HideSectorDesigner();
	inst.main_win = nullptr;
	EXPECT_FALSE(doc.basis.undo());
}

TEST_F(SmartSectorFixture,
	   RealCanvasFloorDragBuildsInsideEditorCreatedInitialSector)
{
	inst.conf = config;
	inst.loaded.gameName = "architecture_editor_initial_sector";
	inst.loaded.levelFormat = MapFormat::doom;
	inst.edit.mode = ObjType::sectors;
	inst.edit.render3d = false;
	inst.edit.Selected.emplace(ObjType::sectors);
	inst.grid.SetSnap(false);
	inst.edit.map.x = -256;
	inst.edit.map.y = -256;

	const int oldSectorSize = config::new_sector_size;
	config::new_sector_size = 512;
	inst.CMD_ObjectInsert();
	config::new_sector_size = oldSectorSize;
	ASSERT_EQ(doc.numSectors(), 1);
	const int host = 0;
	const Sector originalHost = *doc.sectors[host];

	UI_MainWindow window(inst);
	inst.main_win = &window;
	window.ShowSectorDesigner(SectorDesignMode::architecture);
	UI_SectorDesigner &panel = *window.sector_design_box;
	Fl_Choice *family =
			FindWidgetByLabel<Fl_Choice>(panel, "Family:");
	Fl_Choice *structure =
			FindWidgetByLabel<Fl_Choice>(panel, "Structure:");
	Fl_Button *make =
			FindWidgetByLabel<Fl_Button>(panel, "Make Sectors");
	ASSERT_NE(family, nullptr);
	ASSERT_NE(structure, nullptr);
	ASSERT_NE(make, nullptr);
	family->value(1);
	family->do_callback();
	structure->value(0);
	structure->do_callback();

	const bool oldFocus = global::app_has_focus;
	global::app_has_focus = true;
	auto sendPointerEvent = [&](int event, const v2double_t &point,
								bool buttonDown)
	{
		const v2double_t origin = inst.grid.getOrig();
		const double scale = inst.grid.getScale();
		Fl::e_x_root = window.x_root() + window.canvas->x() +
				window.canvas->w() / 2 +
				iround((point.x - origin.x) * scale);
		Fl::e_y_root = window.y_root() + window.canvas->y() +
				window.canvas->h() / 2 +
				iround((origin.y - point.y) * scale);
		Fl::e_x = Fl::e_x_root - window.x_root();
		Fl::e_y = Fl::e_y_root - window.y_root();
		Fl::e_keysym = FL_Button + 1;
		Fl::e_state = buttonDown ? FL_BUTTON1 : 0;
		return window.canvas->handle(event);
	};

	EXPECT_EQ(sendPointerEvent(FL_PUSH, {-192, -160}, true), 1);
	EXPECT_EQ(sendPointerEvent(FL_DRAG, {192, 160}, true), 1);
	EXPECT_EQ(sendPointerEvent(FL_RELEASE, {192, 160}, false), 1);
	ASSERT_TRUE(panel.Plan().valid()) << panel.ReviewText().c_str();
	ASSERT_EQ(panel.Plan().shapes.size(), 1u);
	EXPECT_EQ(panel.Plan().shapes.front().role,
			  DesignPreviewRole::architectureFloor);

	make->do_callback();
	ASSERT_EQ(doc.numSectors(), 2);
	const int floor = 1;
	EXPECT_EQ(doc.sectors[host]->floorh, originalHost.floorh);
	EXPECT_GT(doc.sectors[floor]->floorh, originalHost.floorh);
	int portals = 0;
	for (const std::shared_ptr<LineDef> &line : doc.linedefs)
	{
		const int right = line->right >= 0 ?
				doc.sidedefs[line->right]->sector : -1;
		const int left = line->left >= 0 ?
				doc.sidedefs[line->left]->sector : -1;
		if (!((right == host && left == floor) ||
			  (left == host && right == floor)))
			continue;
		EXPECT_TRUE(line->TwoSided());
		EXPECT_EQ(line->flags & MLF_Blocking, 0u);
		portals++;
	}
	EXPECT_GT(portals, 0);

	inst.CMD_Undo();
	EXPECT_EQ(doc.numSectors(), 1);
	global::app_has_focus = oldFocus;
	window.HideSectorDesigner();
	inst.main_win = nullptr;
	ASSERT_TRUE(doc.basis.undo());
	EXPECT_EQ(doc.numSectors(), 0);
	EXPECT_FALSE(doc.basis.undo());
}

TEST_F(SmartSectorFixture,
	   ArchitectureMirrorKeyAndPerStructureMemoryAreContextual)
{
	addBoxSector(-1024, -768, 1024, 768, 24, 256);
	inst.conf = config;
	inst.loaded.gameName = "stage25_structure_memory";
	inst.loaded.levelFormat = MapFormat::doom;
	inst.edit.mode = ObjType::sectors;
	inst.edit.render3d = false;
	inst.edit.Selected.emplace(ObjType::sectors);
	inst.grid.SetSnap(false);
	UI_SectorDesigner panel(inst, 0, 0, 380, 860);
	panel.Open(SectorDesignMode::architecture);
	Fl_Choice *family =
			FindWidgetByLabel<Fl_Choice>(panel, "Family:");
	Fl_Choice *structure =
			FindWidgetByLabel<Fl_Choice>(panel, "Structure:");
	Fl_Check_Button *mirror =
			FindWidgetByLabel<Fl_Check_Button>(
					panel, "Mirrored (F)");
	ASSERT_NE(family, nullptr);
	ASSERT_NE(structure, nullptr);
	ASSERT_NE(mirror, nullptr);

	family->value(
			static_cast<int>(
					SectorArchitectureFamily::circulation));
	family->do_callback();
	structure->value(3);
	structure->do_callback();
	Fl_Input *bays =
			FindInputByLabel(panel, "Steps/flight:");
	ASSERT_NE(bays, nullptr);
	bays->value("6");
	bays->do_callback();
	panel.CanvasClick({-640, -384}, 0);
	panel.CanvasMove({640, 384}, 0, true);
	panel.CanvasRelease({640, 384}, 0);
	ASSERT_TRUE(panel.Plan().valid());
	std::vector<std::vector<v2double_t>> normal;
	for (const PlannedSectorShape &shape : panel.Plan().shapes)
		normal.push_back(shape.outer);
	EXPECT_TRUE(panel.CanvasKey('f'));
	EXPECT_EQ(mirror->value(), 1);
	ASSERT_TRUE(panel.Plan().valid());
	ASSERT_EQ(panel.Plan().shapes.size(), normal.size());
	bool changed = false;
	for (size_t index = 0; index < normal.size(); ++index)
		changed = changed ||
				normal[index] != panel.Plan().shapes[index].outer;
	EXPECT_TRUE(changed);

	structure->value(5);
	structure->do_callback();
	EXPECT_STREQ(bays->label(), "Steps:");
	bays->value("10");
	bays->do_callback();
	EXPECT_EQ(mirror->value(), 0);
	structure->value(3);
	structure->do_callback();
	EXPECT_STREQ(bays->label(), "Steps/flight:");
	EXPECT_STREQ(bays->value(), "6");
	EXPECT_EQ(mirror->value(), 1);

	panel.Close();
	panel.Open(SectorDesignMode::architecture);
	family = FindWidgetByLabel<Fl_Choice>(panel, "Family:");
	structure = FindWidgetByLabel<Fl_Choice>(panel, "Structure:");
	ASSERT_NE(family, nullptr);
	ASSERT_NE(structure, nullptr);
	EXPECT_EQ(family->value(),
			  static_cast<int>(
					SectorArchitectureFamily::circulation));
	EXPECT_EQ(structure->value(), 3);
	bays = FindInputByLabel(panel, "Steps/flight:");
	ASSERT_NE(bays, nullptr);
	EXPECT_STREQ(bays->value(), "6");
	EXPECT_EQ(mirror->value(), 1);
	panel.Close();
}

TEST_F(SmartSectorFixture,
	   ArchitecturePanelBuildsARotundaAroundAnExistingSectorHole)
{
	const int host = addRingSector(256, 64, 24, 160);
	inst.conf = config;
	inst.loaded.gameName = "architecture_ring_host";
	inst.loaded.levelFormat = MapFormat::doom;
	inst.edit.mode = ObjType::sectors;
	inst.edit.render3d = false;
	inst.edit.Selected.emplace(ObjType::sectors);
	inst.grid.SetSnap(false);

	UI_SectorDesigner panel(inst, 0, 0, 340, 760);
	panel.Open(SectorDesignMode::architecture);
	Fl_Choice *element =
			FindWidgetByLabel<Fl_Choice>(panel, "Structure:");
	ASSERT_NE(element, nullptr);
	element->value(static_cast<int>(
			SectorArchitectureElement::rotunda));
	element->do_callback();
	panel.CanvasClick({-192, -192}, 0);
	panel.CanvasMove({192, 192}, 0, true);
	panel.CanvasRelease({192, 192}, 0);
	ASSERT_TRUE(panel.Plan().valid());
	EXPECT_EQ(panel.Plan().plannedArchitectureHosts, 0);
	EXPECT_EQ(panel.Plan().retainedSectors, std::vector<int>({host}));
	EXPECT_EQ(panel.Plan().plannedStructures, 8);
	EXPECT_EQ(panel.Plan().shapes.size(), 8u);
	ASSERT_TRUE(inst.edit.designAssistPreview);
	EXPECT_EQ(std::count_if(
			inst.edit.designAssistPreview->paths.begin(),
			inst.edit.designAssistPreview->paths.end(),
			[](const DesignPreviewPath &path)
			{
				return path.role == DesignPreviewRole::architecture;
			}), 8);

	panel.Commit();
	EXPECT_EQ(doc.numSectors(), 9);
	ASSERT_TRUE(inst.edit.Selected);
	EXPECT_EQ(inst.edit.Selected->count_obj(), 8);
	ASSERT_TRUE(doc.basis.undo());
	EXPECT_EQ(doc.numSectors(), 1);
	EXPECT_FALSE(doc.basis.undo());
	panel.Close();
}

TEST_F(SmartSectorFixture,
	   BlockedArchitectureKeepsItsIntentAndValidationVisible)
{
	addBoxSector(-128, -128, 128, 128);
	inst.conf = config;
	inst.loaded.gameName = "architecture_visible_validation";
	inst.loaded.levelFormat = MapFormat::doom;
	inst.edit.mode = ObjType::sectors;
	inst.edit.render3d = false;
	inst.edit.Selected.emplace(ObjType::sectors);
	inst.grid.SetSnap(false);

	UI_SectorDesigner panel(inst, 0, 0, 340, 760);
	panel.Open(SectorDesignMode::architecture);
	panel.CanvasClick({-16, -16}, 0);
	panel.CanvasMove({16, 16}, 0, true);
	panel.CanvasRelease({16, 16}, 0);
	ASSERT_FALSE(panel.Plan().valid());
	ASSERT_FALSE(panel.Plan().shapes.empty());
	EXPECT_EQ(panel.Plan().shapes.front().role,
			  DesignPreviewRole::conflict);
	EXPECT_NE(panel.ReviewText().find("footprint is too small"),
			  SString::npos);
	EXPECT_NE(panel.ReviewText().find("needs at least 56.0x56.0"),
			  SString::npos);
	ASSERT_TRUE(inst.edit.designAssistPreview);
	EXPECT_TRUE(std::any_of(
			inst.edit.designAssistPreview->paths.begin(),
			inst.edit.designAssistPreview->paths.end(),
			[](const DesignPreviewPath &path)
			{
				return path.role == DesignPreviewRole::conflict;
			}));
	EXPECT_TRUE(std::any_of(
			inst.edit.designAssistPreview->points.begin(),
			inst.edit.designAssistPreview->points.end(),
			[](const DesignPreviewPoint &point)
			{
				return point.role == DesignPreviewRole::conflict;
			}));

	panel.Commit();
	EXPECT_EQ(doc.numSectors(), 1);
	EXPECT_FALSE(doc.basis.undo());
	panel.Close();
}

TEST_F(SmartSectorFixture,
	   SmartSectorSidebarWidthIsAdjustableAndWorkspaceBounded)
{
	inst.conf = config;
	inst.loaded.gameName = "doom2";
	inst.loaded.levelFormat = MapFormat::doom;
	inst.edit.mode = ObjType::sectors;
	inst.edit.render3d = false;
	inst.edit.Selected.emplace(ObjType::sectors);

	UI_MainWindow window(inst);
	inst.main_win = &window;
	window.ShowSectorDesigner(SectorDesignMode::room);
	ASSERT_TRUE(window.sector_design_box->visible());
	Fl_Widget *divider = nullptr;
	for (int index = 0; index < window.children(); ++index)
	{
		Fl_Widget *candidate = window.child(index);
		if (candidate->tooltip() &&
			SString(candidate->tooltip()).find("Drag horizontally") !=
					SString::npos)
			divider = candidate;
	}
	ASSERT_NE(divider, nullptr);
	EXPECT_TRUE(divider->visible());
	EXPECT_EQ(divider->w(), 7);
	Fl_Input *floor =
			FindInputByLabel(*window.sector_design_box, "Floor flat:");
	Fl_Choice *mode =
			FindWidgetByLabel<Fl_Choice>(
					*window.sector_design_box, "Mode:");
	ASSERT_NE(floor, nullptr);
	ASSERT_NE(mode, nullptr);
	const int originalInputWidth = floor->w();
	const int originalModeWidth = mode->w();

	window.SetSectorDesignerWidth(400);
	EXPECT_EQ(window.SectorDesignerWidth(), 400);
	EXPECT_EQ(window.sector_design_box->w(), 400);
	EXPECT_EQ(window.sector_design_box->x(), window.w() - 400);
	EXPECT_EQ(window.tile->w(), window.w() - 400);
	EXPECT_GT(floor->w(), originalInputWidth);
	EXPECT_GT(mode->w(), originalModeWidth);

	window.SetSectorDesignerWidth(100);
	EXPECT_EQ(window.SectorDesignerWidth(), PANEL_WIDTH);
	EXPECT_EQ(window.tile->w(), window.w() - PANEL_WIDTH);
	window.SetSectorDesignerWidth(window.w());
	EXPECT_GE(window.tile->w(), 480);

	window.HideSectorDesigner();
	EXPECT_FALSE(divider->visible());
	EXPECT_EQ(window.sector_design_box->w(), PANEL_WIDTH);
	EXPECT_EQ(window.tile->w(), window.w() - PANEL_WIDTH);
	inst.main_win = nullptr;
}

TEST_F(SmartSectorFixture,
	   ModeChoiceIsFlatAndEverySwitchUsesTheVisibleModeImmediately)
{
	inst.conf = config;
	inst.loaded.gameName = "doom2";
	inst.loaded.levelFormat = MapFormat::doom;
	inst.edit.mode = ObjType::sectors;
	inst.edit.render3d = false;
	inst.edit.Selected.emplace(ObjType::sectors);
	inst.grid.SetSnap(false);

	UI_SectorDesigner panel(inst, 0, 0, 308, 760);
	panel.Open(SectorDesignMode::room);
	Fl_Choice *mode = FindWidgetByLabel<Fl_Choice>(panel, "Mode:");
	ASSERT_NE(mode, nullptr);
	// Nine flat entries plus FLTK's terminator. In particular, '/' must not
	// create a hidden Inset submenu that shifts Corridor and every later mode.
	EXPECT_EQ(mode->size(), 10);
	ASSERT_NE(mode->text(4), nullptr);
	EXPECT_STREQ(mode->text(4), "Inset - Ring");

	const std::array<const char *, 9> reviewNames{
		"Room", "Polygon", "Freeform", "Extrude",
		"Inset / Ring", "Corridor", "Stairs", "Lift", "Architecture"
	};
	for (int index = 0; index < static_cast<int>(reviewNames.size()); ++index)
	{
		mode->value(index);
		mode->do_callback();
		EXPECT_EQ(mode->value(), index);
		EXPECT_NE(panel.ReviewText().find(SString::printf(
				"Mode: %s", reviewNames[index])), SString::npos) << index;
	}

	mode->value(static_cast<int>(SectorDesignMode::corridor));
	mode->do_callback();
	EXPECT_NE(panel.ReviewText().find(
			"Status: WAITING FOR GESTURE"), SString::npos);
	panel.CanvasClick({0, 0}, 0);
	panel.CanvasClick({128, 0}, 0);
	ASSERT_TRUE(panel.Plan().valid());
	ASSERT_FALSE(panel.Plan().shapes.empty());
	EXPECT_NE(panel.ReviewText().find("Mode: Corridor"), SString::npos);

	// Switching clears the complete corridor gesture. Inset cannot continue
	// drawing it under the wrong visible label.
	mode->value(static_cast<int>(SectorDesignMode::inset));
	mode->do_callback();
	EXPECT_TRUE(panel.Plan().shapes.empty());
	EXPECT_NE(panel.ReviewText().find("Mode: Inset / Ring"), SString::npos);
	EXPECT_NE(panel.ReviewText().find(
			"Status: WAITING FOR GESTURE"), SString::npos);
	panel.CanvasClick({64, 64}, 0);
	EXPECT_TRUE(panel.Plan().shapes.empty());

	// The first click after switching back belongs to Corridor immediately;
	// no extra selection change is needed to make the mode catch up.
	mode->value(static_cast<int>(SectorDesignMode::corridor));
	mode->do_callback();
	panel.CanvasClick({0, 64}, 0);
	EXPECT_NE(panel.ReviewText().find(
			"Place the corridor end."), SString::npos);
	panel.CanvasClick({128, 64}, 0);
	EXPECT_TRUE(panel.Plan().valid());
	EXPECT_FALSE(panel.Plan().shapes.empty());
	EXPECT_NE(panel.ReviewText().find("Mode: Corridor"), SString::npos);
	panel.Close();
}

TEST_F(SmartSectorFixture,
	   PolygonAndArchitectureControlsExposeOrderedPurposeSpecificChoices)
{
	inst.conf = config;
	inst.loaded.gameName = "designer_profile_controls";
	inst.loaded.levelFormat = MapFormat::doom;
	inst.edit.mode = ObjType::sectors;
	inst.edit.render3d = false;
	inst.edit.Selected.emplace(ObjType::sectors);

	UI_SectorDesigner panel(inst, 0, 0, 360, 760);
	panel.Open(SectorDesignMode::polygon);
	Fl_Choice *mode = FindWidgetByLabel<Fl_Choice>(panel, "Mode:");
	Fl_Choice *profile = FindWidgetByLabel<Fl_Choice>(panel, "Profile:");
	Fl_Input *sides = FindInputByLabel(panel, "Sides:");
	Fl_Input *inner = FindInputByLabel(panel, "Inner %:");
	Fl_Choice *style = FindWidgetByLabel<Fl_Choice>(panel, "Style:");
	Fl_Choice *family = FindWidgetByLabel<Fl_Choice>(panel, "Family:");
	Fl_Choice *element = FindWidgetByLabel<Fl_Choice>(panel, "Structure:");
	Fl_Input *bays = FindInputByLabel(panel, "Bays:");
	Fl_Input *elevation = FindInputByLabel(panel, "Elevation:");
	Fl_Check_Button *mirror =
			FindWidgetByLabel<Fl_Check_Button>(
					panel, "Mirrored (F)");
	Fl_Choice *function =
			FindWidgetByLabel<Fl_Choice>(panel, "Function:");
	ASSERT_NE(mode, nullptr);
	ASSERT_NE(profile, nullptr);
	ASSERT_NE(sides, nullptr);
	ASSERT_NE(inner, nullptr);
	ASSERT_NE(style, nullptr);
	ASSERT_NE(family, nullptr);
	ASSERT_NE(element, nullptr);
	ASSERT_NE(bays, nullptr);
	ASSERT_NE(elevation, nullptr);
	ASSERT_NE(mirror, nullptr);
	ASSERT_NE(function, nullptr);

	EXPECT_EQ(profile->size(), 32);
	EXPECT_STREQ(profile->text(0), "Regular - Custom (3-64)");
	EXPECT_STREQ(profile->text(1), "Primitive - Triangle (3)");
	EXPECT_STREQ(profile->text(16), "Cross - Greek (12)");
	EXPECT_STREQ(profile->text(30),
				 "Tracery - Grand 48 lobes (288)");
	EXPECT_TRUE(sides->active());
	EXPECT_FALSE(inner->active());

	profile->value(static_cast<int>(SectorPolygonProfile::triangle));
	profile->do_callback();
	EXPECT_FALSE(sides->active());
	EXPECT_FALSE(inner->active());
	profile->value(static_cast<int>(SectorPolygonProfile::gear24));
	profile->do_callback();
	EXPECT_FALSE(sides->active());
	EXPECT_TRUE(inner->active());

	mode->value(static_cast<int>(SectorDesignMode::architecture));
	mode->do_callback();
	EXPECT_TRUE(style->active());
	EXPECT_TRUE(family->active());
	EXPECT_TRUE(element->active());
	EXPECT_EQ(style->size(), 8);
	EXPECT_EQ(family->size(), 7);
	EXPECT_EQ(element->size(), 22);
	EXPECT_STREQ(style->text(
			static_cast<int>(SectorArchitectureStyle::gothic)), "Gothic");
	EXPECT_STREQ(family->text(0), "Structural supports");
	EXPECT_STREQ(family->text(1), "Floors and terraces");
	EXPECT_STREQ(family->text(2), "Circulation");
	EXPECT_STREQ(family->text(3), "Waterworks");
	EXPECT_STREQ(family->text(4), "Walls and screens");
	EXPECT_STREQ(family->text(5), "Ceilings and vaults");
	EXPECT_STREQ(element->text(
			static_cast<int>(SectorArchitectureElement::sanctuary)),
			"Complex sanctuary");
	EXPECT_STREQ(element->text(
			static_cast<int>(SectorArchitectureElement::fortifiedKeep)),
			"Fortified keep");
	element->value(static_cast<int>(
			SectorArchitectureElement::pillar));
	element->do_callback();
	EXPECT_FALSE(bays->active());
	element->value(static_cast<int>(
			SectorArchitectureElement::colonnade));
	element->do_callback();
	EXPECT_TRUE(bays->active());
	element->value(static_cast<int>(
			SectorArchitectureElement::fortifiedKeep));
	element->do_callback();
	EXPECT_FALSE(bays->active());
	EXPECT_FALSE(mirror->active());
	EXPECT_FALSE(function->active());
	EXPECT_STREQ(element->text(16), "Cross-shaped structural core");
	EXPECT_STREQ(element->text(20), "Stepped monument plinth");

	family->value(1);
	family->do_callback();
	EXPECT_EQ(element->size(), 9);
	EXPECT_STREQ(element->text(0), "Raised dais");
	EXPECT_STREQ(element->text(1), "Sunken court");
	EXPECT_STREQ(element->text(2), "Tiered ziggurat");
	EXPECT_STREQ(element->text(3), "Central platform - lift");
	EXPECT_STREQ(element->text(7), "Horseshoe amphitheater");
	element->do_callback();
	EXPECT_FALSE(style->active());
	EXPECT_FALSE(bays->active());
	EXPECT_TRUE(elevation->active());
	element->value(3);
	element->do_callback();
	EXPECT_TRUE(function->active());

	family->value(4);
	family->do_callback();
	EXPECT_EQ(element->size(), 7);
	EXPECT_STREQ(element->text(0), "Perforated screen wall");
	EXPECT_STREQ(element->text(1), "Solid partition wall");
	EXPECT_STREQ(element->text(5), "Gatehouse with open passage");
	EXPECT_FALSE(style->active());
	EXPECT_TRUE(bays->active());
	EXPECT_FALSE(elevation->active());

	family->value(3);
	family->do_callback();
	EXPECT_EQ(element->size(), 9);
	EXPECT_STREQ(element->text(0),
				 "Fountain basin and centerpiece");
	EXPECT_STREQ(element->text(3), "Perimeter moat");
	EXPECT_STREQ(element->text(7), "Four-basin fountain court");
	EXPECT_TRUE(style->active());
	EXPECT_FALSE(bays->active());
	EXPECT_TRUE(elevation->active());

	family->value(5);
	family->do_callback();
	EXPECT_EQ(element->size(), 8);
	EXPECT_STREQ(element->text(0), "Coffered ceiling");
	EXPECT_STREQ(element->text(1), "Groin-vault bays");
	EXPECT_STREQ(element->text(2), "Recessed tray ceiling");
	EXPECT_STREQ(element->text(6), "Downstand beam lattice");

	family->value(2);
	family->do_callback();
	EXPECT_EQ(element->size(), 9);
	EXPECT_STREQ(element->text(0), "Grand staircase");
	EXPECT_STREQ(element->text(1), "Perimeter balcony gallery");
	EXPECT_STREQ(element->text(2), "Raised bridge");
	EXPECT_STREQ(element->text(3), "Switchback stairs");
	EXPECT_STREQ(element->text(7), "Crossing bridges");
	element->value(3);
	element->do_callback();
	EXPECT_TRUE(mirror->active());
	panel.Close();
}

TEST_F(SmartSectorFixture,
	   PostSwitchbackChoicesRecoverEvenWhenAPlatformCallbackIsMissed)
{
	addBoxSector(-512, -512, 512, 512, 24, 256);
	inst.conf = config;
	inst.loaded.gameName =
			"architecture_post_switchback_choice_resync";
	inst.loaded.levelFormat = MapFormat::doom;
	inst.edit.mode = ObjType::sectors;
	inst.edit.render3d = false;
	inst.edit.Selected.emplace(ObjType::sectors);
	inst.grid.SetSnap(false);

	UI_SectorDesigner panel(inst, 0, 0, 360, 800);
	panel.Open(SectorDesignMode::architecture);
	Fl_Choice *family =
			FindWidgetByLabel<Fl_Choice>(panel, "Family:");
	Fl_Choice *structure =
			FindWidgetByLabel<Fl_Choice>(panel, "Structure:");
	ASSERT_NE(family, nullptr);
	ASSERT_NE(structure, nullptr);

	const int first = static_cast<int>(
			SectorArchitectureElement::bifurcatedStair);
	for (const SectorArchitectureDescriptor &descriptor :
			M_ArchitectureCatalog())
	{
		if (static_cast<int>(descriptor.element) < first)
			continue;

		const int familyIndex =
				static_cast<int>(descriptor.family);
		int structureIndex = 0;
		for (const SectorArchitectureDescriptor &candidate :
				M_ArchitectureCatalog())
		{
			if (candidate.element == descriptor.element)
				break;
			if (candidate.family == descriptor.family)
				structureIndex++;
		}

		// Simulate a backend which updates a choice value but does not deliver
		// the widget callback. Any canvas refresh or gesture must still adopt
		// the visible family, rebuild its index map, and load safe defaults.
		family->value(familyIndex);
		panel.Refresh();
		structure->value(structureIndex);
		panel.Refresh();
		EXPECT_NE(panel.ReviewText().find(descriptor.label),
				  SString::npos)
				<< descriptor.id;

		panel.CanvasClick({-128, -96}, 0);
		panel.CanvasClick({128, 96}, 0);
		SString issues;
		for (const SectorDesignIssue &issue : panel.Plan().issues)
			issues += SString::printf(
					"\n- %s", issue.message.c_str());
		ASSERT_TRUE(panel.Plan().valid())
				<< descriptor.id << issues.c_str();
		ASSERT_GT(panel.Plan().plannedStructures, 0)
				<< descriptor.id;
		panel.Escape();
	}
	panel.Close();
}

TEST_F(SmartSectorFixture,
	   LiftPanelExplainsStopsTagsBehaviorsAndSelectableTriggerPortals)
{
	addTwoAdjacentSectors();
	doc.sectors[0]->floorh = 64;
	config.line_types[62].desc = "Repeatable local lift";
	SectorActionPreset lift;
	lift.kind = SectorActionKind::lift;
	lift.id = "repeat";
	lift.label = "Repeatable local lift";
	lift.special = 62;
	lift.activation = ActivationPolicy::encoded;
	config.sector_action_presets.push_back(lift);

	inst.conf = config;
	inst.loaded.gameName = "lift_guidance";
	inst.loaded.levelFormat = MapFormat::doom;
	inst.edit.mode = ObjType::sectors;
	inst.edit.render3d = false;
	inst.edit.Selected.emplace(ObjType::sectors);
	inst.grid.SetSnap(false);

	UI_SectorDesigner panel(inst, 0, 0, 360, 760);
	panel.Open(SectorDesignMode::lift);
	Fl_Choice *behavior =
			FindWidgetByLabel<Fl_Choice>(panel, "Lift behavior:");
	Fl_Multi_Browser *triggers = FindWidgetByLabel<Fl_Multi_Browser>(
			panel, "Trigger portals:");
	Fl_Output *status =
			FindWidgetByLabel<Fl_Output>(panel, "Lift plan:");
	Fl_Box *guide = FindWidgetByLabel<Fl_Box>(
			panel,
			"HOW LIFT WORKS\n"
			"Existing: click the platform; its lowest neighbor is the stop.\n"
			"New: drag outward from a wall to make an alcove.\n"
			"Choose behavior/trigger portals; Enter assigns a fresh tag "
			"and actions in one Undo.");
	ASSERT_NE(behavior, nullptr);
	ASSERT_NE(triggers, nullptr);
	ASSERT_NE(status, nullptr);
	ASSERT_NE(guide, nullptr);
	EXPECT_TRUE(guide->visible());
	EXPECT_EQ(behavior->size(), 2);

	panel.CanvasClick({32, 32}, 0);
	ASSERT_TRUE(panel.Plan().valid());
	ASSERT_EQ(panel.Plan().lifts.size(), 1u);
	EXPECT_GT(triggers->size(), 0);
	EXPECT_NE(SString(status->value()).find("platform"), SString::npos);
	EXPECT_NE(SString(status->value()).find("travel"), SString::npos);
	const SString review = panel.ReviewText();
	EXPECT_NE(review.find("fresh tag"), SString::npos);
	EXPECT_NE(review.find("lower stop"), SString::npos);
	EXPECT_NE(review.find("trigger portals"), SString::npos);
	EXPECT_NE(review.find("Repeatable local lift"), SString::npos);
	panel.Close();
}

TEST_F(SmartSectorFixture,
	   EveryVisibleModeAcceptsItsAdvertisedFirstGesture)
{
	addTwoAdjacentSectors();
	doc.sectors[0]->floorh = 64;
	const int leftWall = 0;
	config.line_types[62].desc = "Lift";
	SectorActionPreset lift;
	lift.kind = SectorActionKind::lift;
	lift.id = "repeat";
	lift.label = "Lift";
	lift.special = 62;
	lift.activation = ActivationPolicy::encoded;
	config.sector_action_presets.push_back(lift);

	inst.conf = config;
	inst.loaded.gameName = "all_mode_gestures";
	inst.loaded.levelFormat = MapFormat::doom;
	inst.edit.mode = ObjType::sectors;
	inst.edit.render3d = false;
	inst.edit.Selected.emplace(ObjType::sectors);
	inst.grid.SetSnap(false);

	UI_SectorDesigner panel(inst, 0, 0, 308, 760);
	panel.Open(SectorDesignMode::room);
	Fl_Choice *mode = FindWidgetByLabel<Fl_Choice>(panel, "Mode:");
	ASSERT_NE(mode, nullptr);
	auto switchMode = [&](SectorDesignMode selected, const char *label)
	{
		mode->value(static_cast<int>(selected));
		mode->do_callback();
		EXPECT_NE(panel.ReviewText().find(
				SString::printf("Mode: %s", label)), SString::npos);
		EXPECT_NE(panel.ReviewText().find(
				"Status: WAITING FOR GESTURE"), SString::npos);
	};
	auto drag = [&](v2double_t start, v2double_t end)
	{
		panel.CanvasClick(start, 0);
		panel.CanvasMove(end, 0, true);
		panel.CanvasRelease(end, 0);
	};

	drag({-320, -192}, {-256, -128});
	EXPECT_TRUE(panel.Plan().valid());

	switchMode(SectorDesignMode::polygon, "Polygon");
	drag({-224, -192}, {-160, -192});
	EXPECT_TRUE(panel.Plan().valid());

	switchMode(SectorDesignMode::freeform, "Freeform");
	panel.CanvasClick({-128, -192}, 0);
	panel.CanvasClick({-64, -192}, 0);
	panel.CanvasClick({-96, -128}, 0);
	EXPECT_TRUE(panel.Plan().valid());

	switchMode(SectorDesignMode::extrude, "Extrude");
	drag({0, 32}, {-64, 32});
	ASSERT_TRUE(panel.Plan().valid());
	EXPECT_EQ(panel.Plan().extrudeReferenceLine, leftWall);

	switchMode(SectorDesignMode::inset, "Inset / Ring");
	panel.CanvasClick({32, 32}, 0);
	SString insetIssues;
	for (const SectorDesignIssue &issue : panel.Plan().issues)
		insetIssues += issue.message + "\n";
	EXPECT_TRUE(panel.Plan().valid()) << insetIssues.c_str();

	switchMode(SectorDesignMode::corridor, "Corridor");
	drag({256, 0}, {384, 0});
	EXPECT_TRUE(panel.Plan().valid());

	switchMode(SectorDesignMode::stairs, "Stairs");
	drag({256, 128}, {384, 128});
	EXPECT_TRUE(panel.Plan().valid());
	EXPECT_GT(panel.Plan().plannedSteps, 0);

	switchMode(SectorDesignMode::lift, "Lift");
	drag({0, 32}, {-64, 32});
	EXPECT_TRUE(panel.Plan().valid());
	EXPECT_EQ(panel.Plan().plannedLifts, 1);
	EXPECT_EQ(panel.Plan().extrudeReferenceLine, leftWall);

	switchMode(SectorDesignMode::architecture, "Architecture");
	drag({0, 0}, {64, 64});
	EXPECT_TRUE(panel.Plan().valid());
	EXPECT_EQ(panel.Plan().plannedStructures, 1);
	ASSERT_EQ(panel.Plan().shapes.size(), 1u);
	EXPECT_EQ(panel.Plan().shapes.front().role,
			  DesignPreviewRole::architecture);
	panel.Close();
}

TEST_F(SmartSectorFixture,
	   SelectionToolsSupportCanvasTargetsAndExplicitStairEndpoints)
{
	addTwoAdjacentSectors();
	doc.sectors[0]->floorh = 64;
	config.line_types[62].desc = "Lift";
	SectorActionPreset lift;
	lift.kind = SectorActionKind::lift;
	lift.id = "repeat";
	lift.label = "Lift";
	lift.special = 62;
	lift.activation = ActivationPolicy::encoded;
	config.sector_action_presets.push_back(lift);

	inst.conf = config;
	inst.loaded.gameName = "target_gestures";
	inst.loaded.levelFormat = MapFormat::doom;
	inst.edit.mode = ObjType::sectors;
	inst.edit.render3d = false;
	inst.edit.Selected.emplace(ObjType::sectors);
	inst.grid.SetSnap(false);

	UI_SectorDesigner panel(inst, 0, 0, 308, 760);
	panel.Open(SectorDesignMode::inset);
	Fl_Choice *mode = FindWidgetByLabel<Fl_Choice>(panel, "Mode:");
	ASSERT_NE(mode, nullptr);

	panel.CanvasClick({32, 32}, 0);
	SString insetIssues;
	for (const SectorDesignIssue &issue : panel.Plan().issues)
		insetIssues += issue.message + "\n";
	ASSERT_TRUE(panel.Plan().valid()) << insetIssues.c_str();
	EXPECT_FALSE(panel.Plan().shapes.empty());
	panel.CanvasClick({32, 32}, EMOD_SHIFT);
	EXPECT_NE(panel.ReviewText().find(
			"Status: WAITING FOR GESTURE"), SString::npos);

	mode->value(static_cast<int>(SectorDesignMode::stairs));
	mode->do_callback();
	panel.CanvasClick({32, 32}, EMOD_SHIFT);
	panel.CanvasClick({96, 32}, EMOD_SHIFT);
	ASSERT_TRUE(panel.Plan().valid());
	EXPECT_EQ(panel.Plan().plannedSteps, 2);
	// Plain clicks inside the selected path choose direction. This is also
	// the required escape hatch for branched selections.
	panel.CanvasClick({32, 32}, 0);
	panel.CanvasClick({96, 32}, 0);
	ASSERT_TRUE(panel.Plan().valid());
	EXPECT_EQ(panel.Plan().plannedSteps, 2);
	ASSERT_EQ(panel.Plan().previewLabels.size(), 2u);
	EXPECT_EQ(panel.Plan().previewLabels.front().text, "1");
	EXPECT_EQ(panel.Plan().previewLabels.back().text, "2");

	mode->value(static_cast<int>(SectorDesignMode::lift));
	mode->do_callback();
	panel.CanvasClick({32, 32}, 0);
	ASSERT_TRUE(panel.Plan().valid());
	ASSERT_EQ(panel.Plan().lifts.size(), 1u);
	EXPECT_EQ(panel.Plan().lifts.front().sectors,
			std::vector<int>({0}));
	panel.CanvasClick({96, 32}, 0);
	ASSERT_TRUE(panel.Plan().valid());
	ASSERT_EQ(panel.Plan().lifts.size(), 1u);
	EXPECT_EQ(panel.Plan().lifts.front().sectors,
			std::vector<int>({1}));
	panel.Close();
}

TEST_F(SmartSectorFixture,
	   CorridorEndpointLinesStayAttachedToTheirActualEnds)
{
	addSector();
	const int v0 = addVertex(0, 0);
	const int v1 = addVertex(0, 64);
	const int v2 = addVertex(64, 64);
	const int v3 = addVertex(64, 0);
	const int leftWall = addLine(v0, v1, 0);
	addLine(v1, v2, 0);
	addLine(v2, v3, 0);
	addLine(v3, v0, 0);

	inst.conf = config;
	inst.loaded.gameName = "endpoint_ownership";
	inst.loaded.levelFormat = MapFormat::doom;
	inst.edit.mode = ObjType::sectors;
	inst.edit.render3d = false;
	inst.edit.Selected.emplace(ObjType::sectors);
	inst.grid.SetSnap(false);

	UI_SectorDesigner panel(inst, 0, 0, 308, 760);
	panel.Open(SectorDesignMode::corridor);
	Fl_Choice *end =
			FindWidgetByLabel<Fl_Choice>(panel, "End:");
	ASSERT_NE(end, nullptr);
	end->value(static_cast<int>(SectorConnection::wall));
	end->do_callback();

	panel.CanvasClick({-128, 32}, 0);
	panel.CanvasRelease({-128, 32}, 0);
	panel.CanvasClick({0, 32}, 0);
	panel.CanvasRelease({0, 32}, 0);
	ASSERT_TRUE(panel.Plan().valid());
	ASSERT_EQ(panel.Plan().connections.size(), 1u);
	EXPECT_EQ(panel.Plan().connections.front().line, leftWall);
	EXPECT_EQ(panel.Plan().connections.front().connection,
			SectorConnection::wall);
	panel.Close();
}

TEST_F(SmartSectorFixture,
	   SelectionBasedModesWaitForANewTargetAfterCommit)
{
	inst.conf = config;
	inst.loaded.gameName = "one_shot_selection_modes";
	inst.loaded.levelFormat = MapFormat::doom;
	inst.edit.mode = ObjType::sectors;
	inst.edit.render3d = false;
	inst.edit.Selected.emplace(ObjType::sectors);
	inst.grid.SetSnap(false);

	UI_SectorDesigner panel(inst, 0, 0, 308, 760);
	panel.Open(SectorDesignMode::stairs);
	panel.CanvasClick({0, 0}, 0);
	panel.CanvasMove({128, 0}, 0, true);
	panel.CanvasRelease({128, 0}, 0);
	ASSERT_TRUE(panel.Plan().valid());
	panel.Commit();
	const int sectorsAfterCommit = doc.numSectors();
	ASSERT_GT(sectorsAfterCommit, 0);
	EXPECT_NE(panel.ReviewText().find(
			"Status: WAITING FOR GESTURE"), SString::npos);

	panel.Commit();
	EXPECT_EQ(doc.numSectors(), sectorsAfterCommit);
	ASSERT_TRUE(doc.basis.undo());
	EXPECT_EQ(doc.numSectors(), 0);
	EXPECT_FALSE(doc.basis.undo());
	panel.Close();
}

TEST_F(SmartSectorFixture, ExtrudePanelDragLocksPointerDirectionAndShowsFlipState)
{
	addSector();
	const int v0 = addVertex(0, 0);
	const int v1 = addVertex(0, 64);
	const int v2 = addVertex(64, 64);
	const int v3 = addVertex(64, 0);
	const int source = addLine(v0, v1, 0);
	addLine(v1, v2, 0);
	addLine(v2, v3, 0);
	addLine(v3, v0, 0);

	inst.conf = config;
	inst.loaded.gameName = "doom2";
	inst.loaded.levelFormat = MapFormat::doom;
	inst.edit.mode = ObjType::sectors;
	inst.edit.render3d = false;
	inst.edit.Selected.emplace(ObjType::sectors);
	inst.grid.SetSnap(false);

	UI_SectorDesigner panel(inst, 0, 0, 308, 760);
	panel.Open(SectorDesignMode::extrude);
	Fl_Choice *base =
			FindWidgetByLabel<Fl_Choice>(panel, "Base seam:");
	ASSERT_NE(base, nullptr);
	base->value(static_cast<int>(SectorConnection::open));
	base->do_callback();
	Fl_Check_Button *opposite =
			FindWidgetByLabel<Fl_Check_Button>(
					panel, "Opposite side (F)");
	ASSERT_NE(opposite, nullptr);
	EXPECT_EQ(opposite->value(), 0);

	// A real press-drag-release gesture locks the depth on the same side as
	// the pointer, without needing a second click.
	panel.CanvasClick({0, 32}, 0);
	panel.CanvasMove({-64, 32}, 0, true);
	EXPECT_EQ(panel.Plan().extrudeReferenceLine, source);
	EXPECT_DOUBLE_EQ(panel.Plan().resolvedExtrudeDepth, 64);
	panel.CanvasRelease({-64, 32}, 0);
	ASSERT_TRUE(panel.Plan().valid());
	double minimumX = std::numeric_limits<double>::max();
	double maximumX = -std::numeric_limits<double>::max();
	for (const v2double_t &point : panel.Plan().shapes.front().outer)
	{
		minimumX = std::min(minimumX, point.x);
		maximumX = std::max(maximumX, point.x);
	}
	EXPECT_DOUBLE_EQ(minimumX, -64);
	EXPECT_DOUBLE_EQ(maximumX, 0);

	EXPECT_TRUE(panel.CanvasKey('f'));
	EXPECT_EQ(opposite->value(), 1);
	EXPECT_TRUE(panel.CanvasKey('f'));
	EXPECT_EQ(opposite->value(), 0);
	EXPECT_FALSE(panel.CanvasKey(EMOD_SHIFT | 'g'));
	EXPECT_TRUE(panel.CanvasKey(EMOD_SHIFT | 'f'));
	EXPECT_EQ(opposite->value(), 1);
	EXPECT_TRUE(panel.Plan().extrudeOpposite);
	minimumX = std::numeric_limits<double>::max();
	maximumX = -std::numeric_limits<double>::max();
	for (const v2double_t &point : panel.Plan().shapes.front().outer)
	{
		minimumX = std::min(minimumX, point.x);
		maximumX = std::max(maximumX, point.x);
	}
	EXPECT_DOUBLE_EQ(minimumX, 0);
	EXPECT_DOUBLE_EQ(maximumX, 64);

	EXPECT_TRUE(panel.CanvasKey(EMOD_SHIFT | 'f'));
	ASSERT_TRUE(panel.Plan().valid());
	EXPECT_TRUE(panel.CanvasKey(FL_Enter));
	EXPECT_EQ(doc.numSectors(), 2);
	ASSERT_TRUE(doc.basis.undo());
	EXPECT_EQ(doc.numSectors(), 1);
	panel.Close();
}

TEST_F(SmartSectorFixture,
	   ExtrudePanelDirectSectorDragNeedsNoPriorSelection)
{
	addSector(24, 152);
	const int v0 = addVertex(0, 0);
	const int v1 = addVertex(0, 64);
	const int v2 = addVertex(64, 64);
	const int v3 = addVertex(64, 0);
	addLine(v0, v1, 0);
	addLine(v1, v2, 0);
	const int rightBoundary = addLine(v2, v3, 0);
	addLine(v3, v0, 0);
	doc.linedefs[rightBoundary]->type = 17;
	doc.linedefs[rightBoundary]->arg1 = 23;

	inst.conf = config;
	inst.loaded.gameName = "doom2";
	inst.loaded.levelFormat = MapFormat::doom;
	inst.edit.mode = ObjType::sectors;
	inst.edit.render3d = false;
	inst.edit.Selected.reset();
	inst.grid.SetSnap(false);

	UI_SectorDesigner panel(inst, 0, 0, 308, 760);
	panel.Open(SectorDesignMode::extrude);
	Fl_Button *makeSectors =
			FindWidgetByLabel<Fl_Button>(panel, "Make Sectors");
	ASSERT_NE(makeSectors, nullptr);
	EXPECT_FALSE(makeSectors->active());
	ASSERT_NE(makeSectors->tooltip(), nullptr);
	EXPECT_NE(SString(makeSectors->tooltip()).find(
			"Ready for the next extrusion"), SString::npos);

	// This starts inside the room but within the editor's broad ordinary line
	// hover radius of the left wall. Extrude must still follow the drag and
	// choose the right wall it crosses, instead of producing an overlapping
	// strip from the nearby wall.
	panel.CanvasClick({16, 32}, 0);
	panel.CanvasMove({96, 32}, 0, true);
	EXPECT_EQ(panel.Plan().extrudeReferenceLine, rightBoundary);
	EXPECT_DOUBLE_EQ(panel.Plan().resolvedExtrudeDepth, 32);
	panel.CanvasRelease({96, 32}, 0);
	ASSERT_TRUE(panel.Plan().valid());
	EXPECT_TRUE(makeSectors->active());
	const SString review = panel.ReviewText();
	EXPECT_NE(review.find("Status: READY"), SString::npos);
	EXPECT_NE(review.find("WARNING — line #2"), SString::npos);
	EXPECT_NE(review.find(
			"The existing action is retained on this connection."),
			SString::npos);
	EXPECT_NE(FindWidgetByLabel<Fl_Button>(
			panel, "Copy Review"), nullptr);
	EXPECT_NE(FindWidgetByLabel<Fl_Button>(
			panel, "Expand Review..."), nullptr);

	EXPECT_TRUE(panel.CanvasKey(FL_Enter));
	ASSERT_EQ(doc.numSectors(), 2);
	EXPECT_EQ(doc.linedefs[rightBoundary]->type, 17);
	EXPECT_EQ(doc.linedefs[rightBoundary]->arg1, 23);
	ASSERT_TRUE(inst.edit.Selected.has_value());
	EXPECT_EQ(inst.edit.Selected->count_obj(), 1);
	const int created = inst.edit.Selected->find_first();
	ASSERT_TRUE(doc.isSector(created));
	EXPECT_NE(created, 0);
	EXPECT_EQ(doc.sectors[created]->floorh, 24);
	EXPECT_EQ(doc.sectors[created]->ceilh, 152);
	const SString waitingReview = panel.ReviewText();
	EXPECT_NE(waitingReview.find(
			"Result: Ready for next extrusion"), SString::npos);
	EXPECT_NE(waitingReview.find(
			"Status: WAITING FOR GESTURE (0 errors, 0 warnings)"),
			SString::npos);
	EXPECT_EQ(waitingReview.find(
			"Extrude needs an existing boundary"), SString::npos);
	EXPECT_EQ(waitingReview.find("ERROR"), SString::npos);

	// Repeat mode is deliberately idle after a successful commit. A repeated
	// Enter must prompt for the next drag, not report a failed operation or
	// create another edit.
	EXPECT_TRUE(panel.CanvasKey(FL_Enter));
	EXPECT_EQ(doc.numSectors(), 2);

	ASSERT_TRUE(doc.basis.undo());
	EXPECT_EQ(doc.numSectors(), 1);
	EXPECT_EQ(doc.linedefs[rightBoundary]->type, 17);
	EXPECT_EQ(doc.linedefs[rightBoundary]->arg1, 23);
	EXPECT_FALSE(doc.basis.undo());
	panel.Close();
}

TEST_F(SmartSectorFixture, ExtrudePanelCanDragFromAnySelectedChainSeam)
{
	addSector();
	const int v0 = addVertex(0, 0);
	const int v1 = addVertex(0, 64);
	const int v2 = addVertex(64, 64);
	const int v3 = addVertex(64, 0);
	const int vertical = addLine(v0, v1, 0);
	const int horizontal = addLine(v1, v2, 0);
	addLine(v2, v3, 0);
	addLine(v3, v0, 0);

	inst.conf = config;
	inst.loaded.gameName = "doom2";
	inst.loaded.levelFormat = MapFormat::doom;
	inst.edit.mode = ObjType::sectors;
	inst.edit.render3d = false;
	inst.edit.Selected.emplace(ObjType::sectors);
	inst.grid.SetSnap(false);

	UI_SectorDesigner panel(inst, 0, 0, 308, 760);
	panel.Open(SectorDesignMode::extrude);
	Fl_Choice *base =
			FindWidgetByLabel<Fl_Choice>(panel, "Base seam:");
	ASSERT_NE(base, nullptr);
	base->value(static_cast<int>(SectorConnection::open));
	base->do_callback();

	// Select a chain with ordinary and Shift clicks, then begin the actual
	// drag from its horizontal member instead of the first line.
	panel.CanvasClick({0, 32}, 0);
	panel.CanvasRelease({0, 32}, 0);
	panel.CanvasClick({32, 64}, EMOD_SHIFT);
	panel.CanvasRelease({32, 64}, EMOD_SHIFT);
	panel.CanvasClick({32, 64}, 0);
	panel.CanvasMove({32, 96}, 0, true);
	EXPECT_EQ(panel.Plan().extrudeReferenceLine, horizontal);
	EXPECT_DOUBLE_EQ(panel.Plan().resolvedExtrudeDepth, 32);
	panel.CanvasRelease({32, 96}, 0);
	ASSERT_TRUE(panel.Plan().valid());
	EXPECT_EQ(panel.Plan().extrudeReferenceLine, horizontal);
	EXPECT_NE(panel.Plan().extrudeReferenceLine, vertical);

	double maximumY = -std::numeric_limits<double>::max();
	for (const v2double_t &point : panel.Plan().shapes.front().outer)
		maximumY = std::max(maximumY, point.y);
	EXPECT_DOUBLE_EQ(maximumY, 96);
	panel.Close();
}

TEST_F(SmartSectorFixture,
	   ExtrudePanelRejectsDuplicateSectorAndRecoversAfterRepeatedCommit)
{
	addSector();
	const int v0 = addVertex(-256, -256);
	const int v1 = addVertex(-256, 256);
	const int v2 = addVertex(256, 256);
	const int v3 = addVertex(256, -256);
	const int source = addLine(v0, v1, 0);
	addLine(v1, v2, 0);
	addLine(v2, v3, 0);
	addLine(v3, v0, 0);

	inst.conf = config;
	inst.loaded.gameName = "doom2";
	inst.loaded.levelFormat = MapFormat::doom;
	inst.edit.mode = ObjType::sectors;
	inst.edit.render3d = false;
	inst.edit.Selected.emplace(ObjType::sectors);
	inst.grid.SetSnap(false);

	UI_SectorDesigner panel(inst, 0, 0, 308, 760);
	panel.Open(SectorDesignMode::extrude);
	panel.CanvasClick({-256, 0}, 0);
	panel.CanvasMove({256, 0}, 0, true);
	panel.CanvasRelease({256, 0}, 0);
	EXPECT_EQ(panel.Plan().extrudeReferenceLine, source);
	ASSERT_FALSE(panel.Plan().valid());
	EXPECT_TRUE(std::any_of(panel.Plan().issues.begin(),
			panel.Plan().issues.end(),
			[](const SectorDesignIssue &issue)
			{
				return issue.message.find("exactly repeats") !=
						std::string::npos;
				}));
	const SString blockedReview = panel.ReviewText();
	EXPECT_NE(blockedReview.find("Status: BLOCKED"), SString::npos);
	EXPECT_NE(blockedReview.find(
			"The generated outline exactly repeats an existing sector "
			"boundary and would not create anything."),
			SString::npos);
	Fl_Button *expandReview = FindWidgetByLabel<Fl_Button>(
			panel, "Expand Review...");
	ASSERT_NE(expandReview, nullptr);
	expandReview->do_callback();
	Fl_Double_Window *details = panel.ReviewDetailsWindow();
	ASSERT_NE(details, nullptr);
	ASSERT_NE(details->label(), nullptr);
	EXPECT_STREQ(details->label(), "Smart Sector Review");
	Fl_Text_Display *reviewDisplay =
			FindWidgetByType<Fl_Text_Display>(*details);
	ASSERT_NE(reviewDisplay, nullptr);
	ASSERT_NE(reviewDisplay->buffer(), nullptr);
	char *detailsText = reviewDisplay->buffer()->text();
	ASSERT_NE(detailsText, nullptr);
	EXPECT_STREQ(detailsText, panel.ReviewText().c_str());
	std::free(detailsText);
	const int displayWidth = reviewDisplay->w();
	const int displayHeight = reviewDisplay->h();
	details->resize(details->x(), details->y(),
					details->w() + 160, details->h() + 100);
	EXPECT_GT(reviewDisplay->w(), displayWidth);
	EXPECT_GT(reviewDisplay->h(), displayHeight);

	const int originalVertices = doc.numVertices();
	const int originalLines = doc.numLinedefs();
	const int originalSides = doc.numSidedefs();
	const int originalSectors = doc.numSectors();
	EXPECT_TRUE(panel.CanvasKey(FL_Enter));
	EXPECT_TRUE(panel.CanvasKey(FL_Enter));
	EXPECT_EQ(doc.numVertices(), originalVertices);
	EXPECT_EQ(doc.numLinedefs(), originalLines);
	EXPECT_EQ(doc.numSidedefs(), originalSides);
	EXPECT_EQ(doc.numSectors(), originalSectors);
	EXPECT_FALSE(doc.basis.undo());

	// The same panel remains usable: clear the rejected gesture and create a
	// smaller inward extrusion, then undo it in one step.
	panel.Escape();
	panel.CanvasClick({-256, 0}, 0);
	panel.CanvasMove({-192, 0}, 0, true);
	panel.CanvasRelease({-192, 0}, 0);
	ASSERT_TRUE(panel.Plan().valid());
	EXPECT_TRUE(panel.CanvasKey(FL_Enter));
	EXPECT_EQ(doc.numSectors(), originalSectors + 1);
	ASSERT_TRUE(doc.basis.undo());
	EXPECT_EQ(doc.numVertices(), originalVertices);
	EXPECT_EQ(doc.numLinedefs(), originalLines);
	EXPECT_EQ(doc.numSidedefs(), originalSides);
	EXPECT_EQ(doc.numSectors(), originalSectors);
	panel.Close();
}

TEST_F(SmartSectorFixture, ExtrudePanelCommitsSmartDoorInOneUndoStep)
{
	addSector();
	const int v0 = addVertex(0, 0);
	const int v1 = addVertex(0, 64);
	const int v2 = addVertex(64, 64);
	const int v3 = addVertex(64, 0);
	addLine(v0, v1, 0);
	addLine(v1, v2, 0);
	addLine(v2, v3, 0);
	addLine(v3, v0, 0);

	inst.conf = config;
	inst.loaded.gameName = "doom2";
	inst.loaded.levelFormat = MapFormat::doom;
	inst.edit.mode = ObjType::sectors;
	inst.edit.render3d = false;
	inst.edit.Selected.reset();
	inst.grid.SetSnap(false);
	DoorPreset door;
	door.id = "normal";
	door.label = "Normal";
	door.special = 1;
	door.activation = ActivationPolicy::encoded;
	inst.conf.line_types[1].desc = "Door";
	inst.conf.door_presets.push_back(door);

	UI_SectorDesigner panel(inst, 0, 0, 308, 760);
	panel.Open(SectorDesignMode::extrude);
	Fl_Choice *base =
			FindWidgetByLabel<Fl_Choice>(panel, "Base seam:");
	ASSERT_NE(base, nullptr);
	base->value(static_cast<int>(SectorConnection::door));
	base->do_callback();
	Fl_Input *doorDepth = FindInputByLabel(panel, "Door depth:");
	ASSERT_NE(doorDepth, nullptr);
	doorDepth->value("16");
	doorDepth->do_callback();

	panel.CanvasClick({16, 32}, 0);
	panel.CanvasMove({96, 32}, 0, true);
	panel.CanvasRelease({96, 32}, 0);
	SString issueText;
	for (const SectorDesignIssue &issue : panel.Plan().issues)
		issueText += issue.message + "\n";
	ASSERT_TRUE(panel.Plan().valid()) << issueText.c_str();
	EXPECT_EQ(panel.Plan().plannedDoors, 1);
	Fl_Button *expandReview = FindWidgetByLabel<Fl_Button>(
			panel, "Expand Review...");
	ASSERT_NE(expandReview, nullptr);
	expandReview->do_callback();
	ASSERT_NE(panel.ReviewDetailsWindow(), nullptr);
	// Mark the lazily-created test window visible without opening a native
	// display surface. A successful apply must close any stale diagnostics.
	panel.ReviewDetailsWindow()->set_visible();
	ASSERT_TRUE(panel.ReviewDetailsWindow()->visible());
	panel.Commit();
	EXPECT_FALSE(panel.ReviewDetailsWindow()->visible());

	const int committedSectors = doc.numSectors();
	const int committedLines = doc.numLinedefs();
	EXPECT_EQ(std::count_if(doc.sectors.begin(), doc.sectors.end(),
			[](const std::shared_ptr<Sector> &sector)
			{
				return sector->floorh == sector->ceilh;
			}), 1);
	EXPECT_TRUE(std::any_of(doc.linedefs.begin(), doc.linedefs.end(),
			[](const std::shared_ptr<LineDef> &line)
			{
				return line->type == 1;
			}));
	const SString waitingReview = panel.ReviewText();
	EXPECT_NE(waitingReview.find(
			"Status: WAITING FOR GESTURE (0 errors, 0 warnings)"),
			SString::npos);
	EXPECT_EQ(waitingReview.find(
			"Extrude needs an existing boundary"), SString::npos);
	EXPECT_TRUE(panel.CanvasKey(FL_Enter));
	EXPECT_EQ(doc.numSectors(), committedSectors);
	EXPECT_EQ(doc.numLinedefs(), committedLines);
	ASSERT_TRUE(doc.basis.undo());
	EXPECT_EQ(doc.numSectors(), 1);
	EXPECT_EQ(doc.numLinedefs(), 4);
	EXPECT_FALSE(doc.basis.undo());
	panel.Close();
}

TEST_F(SmartSectorFixture, CommandDispatchAndGenericPreviewAreNonMutating)
{
	inst.conf = config;
	inst.loaded.gameName = "doom2";
	inst.loaded.levelFormat = MapFormat::doom;
	inst.edit.mode = ObjType::sectors;
	inst.edit.render3d = false;
	inst.edit.Selected.emplace(ObjType::sectors);

	bool opened = false;
	SectorDesignMode openedMode = SectorDesignMode::room;
	UI_SmartSectorOpen_Override =
			[&](Instance &chosen, SectorDesignMode mode)
			{
				EXPECT_EQ(&chosen, &inst);
				opened = true;
				openedMode = mode;
			};
	inst.EXEC_Param[0] = "corridor";
	inst.CMD_SEC_SmartSector();
	EXPECT_TRUE(opened);
	EXPECT_EQ(openedMode, SectorDesignMode::corridor);
	opened = false;
	inst.EXEC_Param[0] = "architecture";
	inst.CMD_SEC_SmartSector();
	inst.EXEC_Param[0].clear();
	UI_SmartSectorOpen_Override = {};
	EXPECT_TRUE(opened);
	EXPECT_EQ(openedMode, SectorDesignMode::architecture);
	EXPECT_EQ(doc.numSectors(), 0);
	EXPECT_FALSE(doc.basis.undo());

	SectorDesignRequest request;
	request.anchors = {{0, 0}, {64, 64}};
	SectorDesignPlan plan = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	UI_SetSectorDesignPreview(inst, plan);
	ASSERT_TRUE(inst.edit.designAssistPreview);
	EXPECT_EQ(inst.edit.designAssistPreview->paths.size(),
			  plan.previewPaths.size());
	EXPECT_EQ(inst.edit.designAssistPreview->points.size(),
			  plan.previewPoints.size());
	EXPECT_EQ(inst.edit.designAssistPreview->labels.size(),
			  plan.previewLabels.size());
	inst.edit.designAssistPreview.reset();
}

TEST_F(SmartSectorFixture, PanelCloseCancelsGestureAndCleansPreview)
{
	inst.conf = config;
	inst.loaded.gameName = "doom2";
	inst.loaded.levelFormat = MapFormat::doom;
	inst.edit.mode = ObjType::sectors;
	inst.edit.render3d = false;
	inst.edit.Selected.emplace(ObjType::sectors);

	UI_SectorDesigner panel(inst, 0, 0, 308, 700);
	panel.Open(SectorDesignMode::room);
	ASSERT_TRUE(panel.Active());
	panel.CanvasClick({0, 0}, 0);
	panel.CanvasMove({96, 64}, 0);
	ASSERT_TRUE(inst.edit.designAssistPreview);
	ASSERT_TRUE(panel.Plan().valid());

	panel.Close();
	EXPECT_FALSE(inst.edit.designAssistPreview);
	EXPECT_EQ(inst.edit.action, EditorAction::nothing);
	EXPECT_EQ(doc.numSectors(), 0);
	EXPECT_TRUE(inst.edit.Selected->empty());
	EXPECT_FALSE(doc.basis.undo());
}

TEST_F(SmartSectorFixture, InvalidSpecialTextCannotCommitThroughKeyboard)
{
	inst.conf = config;
	inst.loaded.gameName = "doom2";
	inst.loaded.levelFormat = MapFormat::doom;
	inst.edit.mode = ObjType::sectors;
	inst.edit.render3d = false;
	inst.edit.Selected.emplace(ObjType::sectors);

	UI_SectorDesigner panel(inst, 0, 0, 308, 700);
	panel.Open(SectorDesignMode::room);
	panel.CanvasClick({0, 0}, 0);
	panel.CanvasClick({64, 64}, 0);
	ASSERT_TRUE(panel.Plan().valid());

	Fl_Input *special = FindInputByLabel(panel, "Special:");
	ASSERT_NE(special, nullptr);
	special->value("not-a-number");
	special->do_callback();
	EXPECT_FALSE(panel.Plan().valid());
	panel.Commit();
	EXPECT_EQ(doc.numSectors(), 0);
	EXPECT_FALSE(doc.basis.undo());
	panel.Close();
}

TEST_F(SmartSectorFixture, SmartSectorGesturesHonorSharedGridSnap)
{
	inst.conf = config;
	inst.loaded.gameName = "doom2";
	inst.loaded.levelFormat = MapFormat::doom;
	inst.edit.mode = ObjType::sectors;
	inst.edit.render3d = false;
	inst.edit.Selected.emplace(ObjType::sectors);
	inst.grid.ForceStep(16);
	inst.grid.SetSnap(true);

	UI_SectorDesigner panel(inst, 0, 0, 308, 700);
	panel.Open(SectorDesignMode::room);
	panel.CanvasClick({3.2, 5.2}, 0);
	panel.CanvasClick({35.2, 37.2}, 0);
	ASSERT_TRUE(panel.Plan().valid());
	ASSERT_EQ(panel.Plan().shapes.size(), 1u);
	EXPECT_EQ(panel.Plan().shapes.front().outer[0],
			  v2double_t(0, 0));
	EXPECT_EQ(panel.Plan().shapes.front().outer[2],
			  v2double_t(32, 32));

	panel.Escape();
	inst.grid.SetSnap(false);
	panel.CanvasClick({3.2, 5.2}, 0);
	panel.CanvasClick({35.2, 37.2}, 0);
	ASSERT_TRUE(panel.Plan().valid());
	EXPECT_EQ(panel.Plan().shapes.front().outer[0],
			  v2double_t(3, 5));
	EXPECT_EQ(panel.Plan().shapes.front().outer[2],
			  v2double_t(35, 37));
	panel.Close();
}

TEST_F(SmartSectorFixture, TopMenuSnapToggleUsesEditorGridState)
{
	inst.grid.ForceStep(16);
	inst.grid.SetSnap(false);
	Fl_Sys_Menu_Bar *bar = menu::create(0, 0, 640, 30, &inst);
	ASSERT_NE(bar, nullptr);
	const Fl_Menu_Item *snapItem = nullptr;
	for (int index = 0; index < bar->size(); ++index)
		if (bar->menu()[index].text &&
			SString(bar->menu()[index].text).find("Snap to") !=
				SString::npos)
		{
			snapItem = &bar->menu()[index];
			break;
		}
	ASSERT_NE(snapItem, nullptr);
	EXPECT_TRUE(snapItem->checkbox());
	EXPECT_FALSE(snapItem->value());
	ASSERT_NE(snapItem->callback(), nullptr);
	EXPECT_EQ(snapItem->user_data(), &inst);

	bar->picked(snapItem);
	EXPECT_TRUE(inst.grid.snaps());
	EXPECT_EQ(inst.grid.Snap({3.2, 5.2}), v2double_t(0, 0));
	EXPECT_TRUE(snapItem->value());
	menu::setSnapToGrid(bar, false);
	EXPECT_FALSE(snapItem->value());
	delete bar;
}

TEST_F(SmartSectorFixture, PanelSupportsRepeatedSelectedCommits)
{
	inst.conf = config;
	inst.loaded.gameName = "doom2";
	inst.loaded.levelFormat = MapFormat::doom;
	inst.edit.mode = ObjType::sectors;
	inst.edit.render3d = false;
	inst.edit.Selected.emplace(ObjType::sectors);

	UI_SectorDesigner panel(inst, 0, 0, 308, 700);
	panel.Open(SectorDesignMode::room);
	panel.CanvasClick({0, 0}, 0);
	panel.CanvasClick({64, 64}, 0);
	ASSERT_TRUE(panel.Plan().valid());
	panel.Commit();
	ASSERT_EQ(doc.numSectors(), 1);
	EXPECT_TRUE(panel.Active());
	EXPECT_TRUE(inst.edit.Selected->get(0));

	panel.CanvasClick({128, 0}, 0);
	panel.CanvasClick({192, 64}, 0);
	ASSERT_TRUE(panel.Plan().valid());
	panel.Commit();
	ASSERT_EQ(doc.numSectors(), 2);
	EXPECT_FALSE(inst.edit.Selected->get(0));
	EXPECT_TRUE(inst.edit.Selected->get(1));

	ASSERT_TRUE(doc.basis.undo());
	EXPECT_EQ(doc.numSectors(), 1);
	ASSERT_TRUE(doc.basis.undo());
	EXPECT_EQ(doc.numSectors(), 0);
	EXPECT_FALSE(doc.basis.undo());
	panel.Close();
	EXPECT_FALSE(inst.edit.designAssistPreview);
}

TEST_F(SmartSectorFixture, PlanningIsDeterministicForConcaveGeometry)
{
	SectorDesignRequest request;
	request.mode = SectorDesignMode::freeform;
	request.anchors = {
		{0, 0}, {128, 0}, {128, 48}, {64, 48},
		{64, 112}, {0, 112}
	};

	const SectorDesignPlan first = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	const SectorDesignPlan second = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	ASSERT_TRUE(first.valid());
	ASSERT_EQ(first.shapes.size(), 1u);
	EXPECT_EQ(first.shapes.front().outer, second.shapes.front().outer);
	ASSERT_EQ(first.previewPaths.size(), second.previewPaths.size());
	EXPECT_EQ(first.previewPaths.front().points,
			  second.previewPaths.front().points);
	EXPECT_EQ(doc.numVertices(), 0);
	EXPECT_FALSE(doc.basis.undo());
}

TEST_F(SmartSectorFixture, FormatCoordinateLimitsAreRejectedBeforeClipping)
{
	SectorDesignRequest request;
	request.mode = SectorDesignMode::room;
	request.anchors = {{32600.4, -32767.6}, {32767.4, -32600}};
	EXPECT_TRUE(M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request).valid());

	request.anchors = {{32768, 0}, {32780, 32}};
	EXPECT_FALSE(M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request).valid());
	EXPECT_FALSE(M_ApplySectorDesign(
			doc, config, nullptr, MapFormat::doom, request));

	request.anchors = {{1.0e14, 0}, {1.0e14 + 64, 64}};
	EXPECT_FALSE(M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::udmf, request).valid());
	request.anchors = {
		{std::numeric_limits<double>::infinity(), 0}, {64, 64}
	};
	EXPECT_FALSE(M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::udmf, request).valid());
	EXPECT_EQ(doc.numVertices(), 0);
	EXPECT_FALSE(doc.basis.undo());
}

TEST_F(SmartSectorFixture, UdmfCoordinatesUseExactSixteenSixteenGrid)
{
	const double unit = 1.0 / 65536.0;
	SectorDesignRequest request;
	request.mode = SectorDesignMode::room;
	request.anchors = {
		{1.0 + unit * 0.4, 2.0 + unit * 0.6},
		{65.0 + unit * 0.6, 34.0 + unit * 0.4}
	};
	const SectorDesignPlan plan = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::udmf, request);
	ASSERT_TRUE(plan.valid());
	ASSERT_EQ(plan.shapes.size(), 1u);
	EXPECT_DOUBLE_EQ(plan.shapes.front().outer[0].x, 1.0);
	EXPECT_DOUBLE_EQ(plan.shapes.front().outer[0].y, 2.0 + unit);
	EXPECT_DOUBLE_EQ(plan.shapes.front().outer[2].x, 65.0 + unit);
	EXPECT_DOUBLE_EQ(plan.shapes.front().outer[2].y, 34.0);
}

TEST_F(SmartSectorFixture, OversizedInsetCollapseIsAtomic)
{
	addSector();
	const int v0 = addVertex(0, 0);
	const int v1 = addVertex(0, 64);
	const int v2 = addVertex(64, 64);
	const int v3 = addVertex(64, 0);
	addLine(v0, v1, 0);
	addLine(v1, v2, 0);
	addLine(v2, v3, 0);
	addLine(v3, v0, 0);

	SectorDesignRequest request;
	request.mode = SectorDesignMode::inset;
	request.targetSectors = {0};
	request.offset = 40;
	const SectorDesignPlan plan = M_PlanSectorDesign(
			doc, config, nullptr, MapFormat::doom, request);
	EXPECT_FALSE(plan.valid());
	EXPECT_FALSE(M_ApplySectorDesign(
			doc, config, nullptr, MapFormat::doom, request));
	EXPECT_EQ(doc.numSectors(), 1);
	EXPECT_EQ(doc.numLinedefs(), 4);
	EXPECT_FALSE(doc.basis.undo());
}

TEST_F(SmartSectorFixture, LargeMapPreviewBenchmarkStaysPure)
{
	constexpr int gridSize = 24;
	for (int y = 0; y < gridSize; ++y)
		for (int x = 0; x < gridSize; ++x)
		{
			const int sector = addSector();
			const double ox = x * 96.0;
			const double oy = y * 96.0;
			const int v0 = addVertex(ox, oy);
			const int v1 = addVertex(ox, oy + 64);
			const int v2 = addVertex(ox + 64, oy + 64);
			const int v3 = addVertex(ox + 64, oy);
			addLine(v0, v1, sector);
			addLine(v1, v2, sector);
			addLine(v2, v3, sector);
			addLine(v3, v0, sector);
		}
	const int originalVertices = doc.numVertices();
	const int originalLines = doc.numLinedefs();
	const int originalSectors = doc.numSectors();

	SectorDesignRequest request;
	request.mode = SectorDesignMode::freeform;
	request.anchors = {
		{10000, 10000}, {10256, 10000}, {10256, 10128},
		{10128, 10128}, {10128, 10256}, {10000, 10256}
	};
	const auto started = std::chrono::steady_clock::now();
	for (int iteration = 0; iteration < 32; ++iteration)
	{
		const SectorDesignPlan plan = M_PlanSectorDesign(
				doc, config, nullptr, MapFormat::doom, request);
		ASSERT_TRUE(plan.valid());
	}
	const auto elapsed = std::chrono::duration_cast<
			std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - started).count();
	RecordProperty("map_sectors", originalSectors);
	RecordProperty("planning_iterations", 32);
	RecordProperty("elapsed_ms", elapsed);
	EXPECT_LT(elapsed, 5000);
	EXPECT_EQ(doc.numVertices(), originalVertices);
	EXPECT_EQ(doc.numLinedefs(), originalLines);
	EXPECT_EQ(doc.numSectors(), originalSectors);
	EXPECT_FALSE(doc.basis.undo());
}

TEST(SmartSectorDefaults, DesignerExtrudeAndSnapHaveUnambiguousShortcuts)
{
	auto readSourceFile = [](const char *name)
	{
		std::ifstream stream(fs::path(HERESY_TEST_SOURCE_DIR) / name);
		EXPECT_TRUE(stream.is_open()) << name;
		std::ostringstream contents;
		contents << stream.rdbuf();
		return contents.str();
	};

	const std::string bindings = readSourceFile("bindings.cfg");
	EXPECT_NE(bindings.find(
			"sector\tCMD-S\tSEC_SmartSector\troom"),
			std::string::npos);
	EXPECT_NE(bindings.find(
			"sector\tx\tSEC_SmartSector\textrude"),
			std::string::npos);
	EXPECT_NE(bindings.find(
			"general\tG\tToggle\tsnap"),
			std::string::npos);
	EXPECT_EQ(bindings.find(
			"general\tf\tToggle\tsnap"),
			std::string::npos);

	const std::string operations = readSourceFile("operations.cfg");
	EXPECT_NE(operations.find(
			"x\t\"Smart extrude...\"\t\tSEC_SmartSector\textrude"),
			std::string::npos);
	EXPECT_NE(operations.find(
			"a\t\"Smart architecture...\"\t\tSEC_SmartSector\tarchitecture"),
			std::string::npos);

	const std::string commands =
			readSourceFile("misc/command-doc.toml");
	EXPECT_NE(commands.find(
			"SEC_SmartSector [room|polygon|freeform|extrude|inset|"
			"corridor|stairs|lift|architecture]"),
			std::string::npos);
}

} // namespace
