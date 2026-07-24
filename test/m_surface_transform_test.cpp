//------------------------------------------------------------------------
//  SURFACE TEXTURE TRANSFORM TESTS
//------------------------------------------------------------------------

#include "gtest/gtest.h"

#include "Document.h"
#include "e_basis.h"
#include "Instance.h"
#include "LineDef.h"
#include "lib_tga.h"
#include "m_surface_transform.h"
#include "Sector.h"
#include "SideDef.h"
#include "ui_surface_transform.h"
#include "Vertex.h"

TEST(SurfaceTransformCapabilities, HonorsFormatAndDeclaredPortFeatures)
{
	ConfigData config;
	config.features.udmf_surface_transforms =
			kSurfaceTransformWallUDMF | kSurfaceTransformPlaneUDMF;

	const auto classic =
			M_SurfaceTransformCapabilities(MapFormat::doom, config);
	EXPECT_TRUE(classic.sharedWallOffsets);
	EXPECT_FALSE(classic.wallPartOffsets);
	EXPECT_FALSE(classic.wallScale);
	EXPECT_FALSE(classic.anyPlaneTransform());

	const auto udmf =
			M_SurfaceTransformCapabilities(MapFormat::udmf, config);
	EXPECT_TRUE(udmf.sharedWallOffsets);
	EXPECT_TRUE(udmf.wallPartOffsets);
	EXPECT_TRUE(udmf.wallScale);
	EXPECT_TRUE(udmf.planeOffsets);
	EXPECT_TRUE(udmf.planeScale);
	EXPECT_TRUE(udmf.planeRotation);

	config.features.udmf_surface_transforms = 0;
	const auto undeclared =
			M_SurfaceTransformCapabilities(MapFormat::udmf, config);
	EXPECT_TRUE(undeclared.sharedWallOffsets);
	EXPECT_FALSE(undeclared.wallScale);
	EXPECT_FALSE(undeclared.anyPlaneTransform());

	SideDef side;
	side.offsetx_mid = 4096.0;
	side.scalex_mid = 32.0;
	Sector sector;
	sector.xpanningfloor = 8192.0;
	sector.rotationfloor = 45.0;
	EXPECT_DOUBLE_EQ(M_EffectiveWallSurfaceTransform(side,
			WallSurfacePart::middle, MapFormat::udmf, config).offsetX, 0.0);
	EXPECT_DOUBLE_EQ(M_EffectiveWallSurfaceTransform(side,
			WallSurfacePart::middle, MapFormat::udmf, config).scaleX, 1.0);
	EXPECT_DOUBLE_EQ(M_EffectivePlaneSurfaceTransform(sector,
			PlaneSurfacePart::floor, MapFormat::udmf, config).rotation, 0.0);
}

TEST(SurfaceTransformValidation, SupportsModernRangesAndRejectsBadValues)
{
	SurfaceTransform transform;
	transform.offsetX = 999999999999.0;
	transform.offsetY = -999999999999.0;
	transform.scaleX = 65536.0;
	transform.scaleY = -1.0 / 65536.0;
	transform.rotation = 725.0;
	EXPECT_TRUE(M_SurfaceTransformValid(transform, true));

	transform = M_NormalizeSurfaceTransform(transform);
	EXPECT_DOUBLE_EQ(transform.rotation, 5.0);

	transform.scaleX = 0.0;
	SString reason;
	EXPECT_FALSE(M_SurfaceTransformValid(transform, true, &reason));
	EXPECT_FALSE(reason.empty());

	transform = {};
	transform.rotation = 45.0;
	EXPECT_FALSE(M_SurfaceTransformValid(transform, false, &reason));
}

TEST(SurfaceTransformEditing, WallAndPlaneChangesUndoAndRedoAtomically)
{
	Instance instance;
	instance.Editor_Init();
	Document document(instance);

	auto side = std::make_shared<SideDef>();
	document.sidedefs.push_back(side);
	auto sector = std::make_shared<Sector>();
	document.sectors.push_back(sector);

	const SurfaceTransform wall{12.5, -4.25, 32.0, -0.5, 0.0};
	const SurfaceTransform plane{8192.0, -4096.0, 0.125, 64.0, 37.5};
	{
		EditOperation operation(document.basis);
		operation.setMessage("transformed 2 surfaces");
		M_ChangeWallSurfaceTransform(operation, 0,
				WallSurfacePart::middle, wall);
		M_ChangePlaneSurfaceTransform(operation, 0,
				PlaneSurfacePart::floor, plane);
	}

	EXPECT_DOUBLE_EQ(side->offsetx_mid, wall.offsetX);
	EXPECT_DOUBLE_EQ(side->offsety_mid, wall.offsetY);
	EXPECT_DOUBLE_EQ(side->scalex_mid, wall.scaleX);
	EXPECT_DOUBLE_EQ(side->scaley_mid, wall.scaleY);
	EXPECT_DOUBLE_EQ(sector->xpanningfloor, plane.offsetX);
	EXPECT_DOUBLE_EQ(sector->ypanningfloor, plane.offsetY);
	EXPECT_DOUBLE_EQ(sector->xscalefloor, plane.scaleX);
	EXPECT_DOUBLE_EQ(sector->yscalefloor, plane.scaleY);
	EXPECT_DOUBLE_EQ(sector->rotationfloor, plane.rotation);

	ASSERT_TRUE(document.basis.undo());
	EXPECT_DOUBLE_EQ(side->offsetx_mid, 0.0);
	EXPECT_DOUBLE_EQ(side->offsety_mid, 0.0);
	EXPECT_DOUBLE_EQ(side->scalex_mid, 1.0);
	EXPECT_DOUBLE_EQ(side->scaley_mid, 1.0);
	EXPECT_DOUBLE_EQ(sector->xpanningfloor, 0.0);
	EXPECT_DOUBLE_EQ(sector->ypanningfloor, 0.0);
	EXPECT_DOUBLE_EQ(sector->xscalefloor, 1.0);
	EXPECT_DOUBLE_EQ(sector->yscalefloor, 1.0);
	EXPECT_DOUBLE_EQ(sector->rotationfloor, 0.0);

	ASSERT_TRUE(document.basis.redo());
	EXPECT_DOUBLE_EQ(side->offsetx_mid, wall.offsetX);
	EXPECT_DOUBLE_EQ(side->scalex_mid, wall.scaleX);
	EXPECT_DOUBLE_EQ(sector->rotationfloor, plane.rotation);
}

TEST(SurfaceTransformPreview, IsTemporaryAndNeverTouchesUndoHistory)
{
	Instance instance;
	instance.Editor_Init();
	Document document(instance);

	auto side = std::make_shared<SideDef>();
	side->x_offset = 7;
	side->y_offset = -3;
	side->mid_tex = BA_InternaliseString("ORIGINAL");
	document.sidedefs.push_back(side);
	auto sector = std::make_shared<Sector>();
	sector->floorh = 16;
	sector->floor_tex = BA_InternaliseString("FLOOR0_1");
	document.sectors.push_back(sector);

	const SideDef originalSide = *side;
	const Sector originalSector = *sector;
	{
		SurfaceTransformPreview preview(document);
		WallSurfacePreviewValue wall;
		wall.sidedef = 0;
		wall.part = WallSurfacePart::middle;
		wall.transform = {12.5, -8.25, 4.0, -0.5, 0.0};
		wall.setPartTransform = true;
		wall.setSharedOffsets = true;
		wall.sharedOffsetX = 200;
		wall.sharedOffsetY = -100;
		PlaneSurfacePreviewValue plane;
		plane.sector = 0;
		plane.part = PlaneSurfacePart::floor;
		plane.transform = {1024.0, -512.0, 2.0, 0.25, 45.0};

		ASSERT_TRUE(preview.apply({wall}, {plane}));
		EXPECT_TRUE(preview.active());
		EXPECT_EQ(side->x_offset, 200);
		EXPECT_DOUBLE_EQ(side->offsetx_mid, 12.5);
		EXPECT_DOUBLE_EQ(side->scaley_mid, -0.5);
		EXPECT_DOUBLE_EQ(sector->xpanningfloor, 1024.0);
		EXPECT_DOUBLE_EQ(sector->rotationfloor, 45.0);
		EXPECT_FALSE(document.hasChanges());
		EXPECT_FALSE(document.basis.undo());

		preview.clear();
		EXPECT_FALSE(preview.active());
	}

	EXPECT_EQ(side->x_offset, originalSide.x_offset);
	EXPECT_EQ(side->y_offset, originalSide.y_offset);
	EXPECT_EQ(side->mid_tex, originalSide.mid_tex);
	EXPECT_DOUBLE_EQ(side->offsetx_mid, originalSide.offsetx_mid);
	EXPECT_EQ(sector->floorh, originalSector.floorh);
	EXPECT_EQ(sector->floor_tex, originalSector.floor_tex);
	EXPECT_DOUBLE_EQ(sector->xpanningfloor,
			originalSector.xpanningfloor);
	EXPECT_FALSE(document.hasChanges());
	EXPECT_FALSE(document.basis.undo());
}

TEST(SurfaceTransformPreview, InvalidBatchRestoresPreviousPreviewAtomically)
{
	Instance instance;
	instance.Editor_Init();
	Document document(instance);
	document.sidedefs.push_back(std::make_shared<SideDef>());

	SurfaceTransformPreview preview(document);
	WallSurfacePreviewValue valid;
	valid.sidedef = 0;
	valid.setSharedOffsets = true;
	valid.sharedOffsetX = 64;
	ASSERT_TRUE(preview.apply({valid}, {}));
	EXPECT_EQ(document.sidedefs[0]->x_offset, 64);

	WallSurfacePreviewValue invalid = valid;
	invalid.sidedef = 99;
	EXPECT_FALSE(preview.apply({invalid}, {}));
	EXPECT_EQ(document.sidedefs[0]->x_offset, 0);
	EXPECT_FALSE(preview.active());
}

TEST(SurfaceTransformPreview, PreservesExistingRedoHistory)
{
	Instance instance;
	instance.Editor_Init();
	Document document(instance);
	document.sidedefs.push_back(std::make_shared<SideDef>());

	{
		EditOperation operation(document.basis);
		operation.setMessage("positioned texture");
		operation.changeSidedef(0, SideDef::F_X_OFFSET, 24);
	}
	ASSERT_TRUE(document.basis.undo());
	EXPECT_EQ(document.sidedefs[0]->x_offset, 0);

	{
		SurfaceTransformPreview preview(document);
		WallSurfacePreviewValue value;
		value.sidedef = 0;
		value.setSharedOffsets = true;
		value.sharedOffsetX = 96;
		ASSERT_TRUE(preview.apply({value}, {}));
		EXPECT_EQ(document.sidedefs[0]->x_offset, 96);
		preview.clear();
	}

	EXPECT_EQ(document.sidedefs[0]->x_offset, 0);
	ASSERT_TRUE(document.basis.redo());
	EXPECT_EQ(document.sidedefs[0]->x_offset, 24);
}

TEST(SurfaceTransformFit, UsesActualWallAndPlaneDimensions)
{
	Instance instance;
	Document &document = instance.level;
	for (const v2double_t point :
			{v2double_t{0, 0}, v2double_t{128, 0},
					v2double_t{128, 96}, v2double_t{0, 96}})
	{
		auto vertex = std::make_shared<Vertex>();
		vertex->SetRawXY(MapFormat::doom, point);
		document.vertices.push_back(vertex);
	}
	auto sector = std::make_shared<Sector>();
	sector->floorh = 0;
	sector->ceilh = 96;
	document.sectors.push_back(sector);
	for (int index = 0; index < 4; ++index)
	{
		auto side = std::make_shared<SideDef>();
		side->sector = 0;
		document.sidedefs.push_back(side);
		auto line = std::make_shared<LineDef>();
		line->start = index;
		line->end = (index + 1) % 4;
		line->right = index;
		document.linedefs.push_back(line);
	}

	const SurfaceDimensions wall = M_WallSurfaceDimensions(
			document, 0, 0, WallSurfacePart::middle);
	EXPECT_DOUBLE_EQ(wall.width, 128.0);
	EXPECT_DOUBLE_EQ(wall.height, 96.0);
	const SurfaceDimensions plane =
			M_PlaneSurfaceDimensions(document, 0);
	EXPECT_DOUBLE_EQ(plane.width, 128.0);
	EXPECT_DOUBLE_EQ(plane.height, 96.0);

	SurfaceTransform current;
	current.scaleX = -2.0;
	current.scaleY = 3.0;
	const SurfaceTransform fitted = M_FitSurfaceTransform(
			current, 512, 384, wall, SurfaceFitMode::fitSurface);
	EXPECT_DOUBLE_EQ(fitted.scaleX, -4.0);
	EXPECT_DOUBLE_EQ(fitted.scaleY, 4.0);

	const SurfaceTransform widthOnly = M_FitSurfaceTransform(
			current, 512, 384, wall, SurfaceFitMode::fitWidth);
	EXPECT_DOUBLE_EQ(widthOnly.scaleX, -4.0);
	EXPECT_DOUBLE_EQ(widthOnly.scaleY, 3.0);
}

TEST(SurfaceTransformFit, BakesMirroredTrueColorTga)
{
	Img_c source(2, 2);
	source.wbuf()[0] = pixelMakeRGB(31, 0, 0);
	source.wbuf()[1] = pixelMakeRGB(0, 31, 0);
	source.wbuf()[2] = pixelMakeRGB(0, 0, 31);
	source.wbuf()[3] = TRANS_PIXEL;
	Palette palette;

	const std::vector<uint8_t> encoded =
			M_BakeSurfaceTextureTGA(source, palette, 2, 2, true, false);
	ASSERT_EQ(encoded.size(), 18u + 2u * 2u * 4u);
	EXPECT_EQ(encoded[2], 2);
	EXPECT_EQ(encoded[12], 2);
	EXPECT_EQ(encoded[14], 2);
	EXPECT_EQ(encoded[16], 32);

	int width = 0;
	int height = 0;
	rgba_color_t *decoded =
			TGA_DecodeImage(encoded.data(), encoded.size(), width, height);
	ASSERT_NE(decoded, nullptr);
	ASSERT_EQ(width, 2);
	ASSERT_EQ(height, 2);
	EXPECT_GT(RGB_GREEN(decoded[0]), 240);
	EXPECT_GT(RGB_RED(decoded[1]), 240);
	EXPECT_LT(RGBA_ALPHA(decoded[2]), 128);
	EXPECT_GT(RGB_BLUE(decoded[3]), 240);
	TGA_FreeImage(decoded);
}

TEST(SurfaceTransformWindow,
		RemainsSeparatedAndAvoidsAttachedModalState)
{
	Instance instance;
	instance.Editor_Init();
	instance.edit.mode = ObjType::linedefs;
	instance.edit.Selected.emplace(ObjType::linedefs, true);
	for (const v2double_t point :
			{v2double_t{0, 0}, v2double_t{128, 0}})
	{
		auto vertex = std::make_shared<Vertex>();
		vertex->SetRawXY(MapFormat::doom, point);
		instance.level.vertices.push_back(vertex);
	}
	auto sector = std::make_shared<Sector>();
	sector->ceilh = 128;
	instance.level.sectors.push_back(sector);
	auto side = std::make_shared<SideDef>();
	side->sector = 0;
	side->mid_tex = BA_InternaliseString("STONE");
	instance.level.sidedefs.push_back(side);
	auto line = std::make_shared<LineDef>();
	line->start = 0;
	line->end = 1;
	line->right = 0;
	instance.level.linedefs.push_back(line);
	instance.edit.Selected->set_ext(0, PART_RT_LOWER);

	SString reason;
	EXPECT_TRUE(UI_VerifySurfaceTransformLayout(
			instance, 720, 700, &reason)) << reason;
	EXPECT_TRUE(UI_VerifySurfaceTransformLayout(
			instance, 1000, 850, &reason)) << reason;
	EXPECT_TRUE(UI_VerifySurfaceTransformWindowPolicy(
			instance, &reason)) << reason;
}

TEST(SurfaceTransformPreview, CanProjectClassicBakedScale)
{
	Instance instance;
	instance.Editor_Init();
	Document document(instance);
	document.sidedefs.push_back(std::make_shared<SideDef>());
	document.sectors.push_back(std::make_shared<Sector>());
	ConfigData config;

	SurfaceTransformPreview preview(document);
	WallSurfacePreviewValue wall;
	wall.sidedef = 0;
	wall.part = WallSurfacePart::middle;
	wall.transform.scaleX = 4.0;
	wall.transform.scaleY = 2.0;
	wall.setPartTransform = true;
	wall.forceUnsupportedPreview = true;
	PlaneSurfacePreviewValue plane;
	plane.sector = 0;
	plane.part = PlaneSurfacePart::floor;
	plane.transform.scaleX = 8.0;
	plane.transform.scaleY = 4.0;
	plane.forceUnsupportedPreview = true;
	ASSERT_TRUE(preview.apply({wall}, {plane}));

	EXPECT_DOUBLE_EQ(M_EffectiveWallSurfaceTransform(
			*document.sidedefs[0], WallSurfacePart::middle,
			MapFormat::doom, config).scaleX, 4.0);
	EXPECT_DOUBLE_EQ(M_EffectivePlaneSurfaceTransform(
			*document.sectors[0], PlaneSurfacePart::floor,
			MapFormat::doom, config).scaleX, 8.0);
	preview.clear();
	EXPECT_FALSE(document.sidedefs[0]->preview_surface_transform);
	EXPECT_FALSE(document.sectors[0]->preview_floor_transform);
	EXPECT_DOUBLE_EQ(M_EffectiveWallSurfaceTransform(
			*document.sidedefs[0], WallSurfacePart::middle,
			MapFormat::doom, config).scaleX, 1.0);
}

TEST(SurfaceTransformAlignment, OneSidedWallsUseTheStoredMiddleTransform)
{
	Instance instance;
	Document &document = instance.level;
	instance.loaded.levelFormat = MapFormat::udmf;
	instance.conf.features.udmf_surface_transforms =
			kSurfaceTransformWallUDMF;
	instance.edit.mode = ObjType::linedefs;
	instance.edit.Selected.emplace(ObjType::linedefs, true);

	for (const v2double_t point :
			{v2double_t{0, 0}, v2double_t{64, 0}})
	{
		auto vertex = std::make_shared<Vertex>();
		vertex->SetRawXY(MapFormat::udmf, point);
		document.vertices.push_back(vertex);
	}

	document.sectors.push_back(std::make_shared<Sector>());
	auto side = std::make_shared<SideDef>();
	side->sector = 0;
	side->x_offset = 17;
	side->offsetx_mid = 5.0;
	side->offsetx_bottom = 123.0;
	side->mid_tex = BA_InternaliseString("STONE");
	document.sidedefs.push_back(side);

	auto line = std::make_shared<LineDef>();
	line->start = 0;
	line->end = 1;
	line->right = 0;
	document.linedefs.push_back(line);

	instance.edit.highlight =
			Objid(ObjType::linedefs, 0, PART_RT_LOWER);
	instance.EXEC_Flags[0] = "/x";
	instance.EXEC_Flags[1] = "/clear";
	instance.CMD_LIN_Align();

	EXPECT_DOUBLE_EQ(side->offsetx_mid, -17.0);
	EXPECT_DOUBLE_EQ(side->offsetx_bottom, 123.0);
	ASSERT_TRUE(document.basis.undo());
	EXPECT_DOUBLE_EQ(side->offsetx_mid, 5.0);
	ASSERT_TRUE(document.basis.redo());
	EXPECT_DOUBLE_EQ(side->offsetx_mid, -17.0);
}

TEST(SurfaceTransformAlignment, ScaledWallPhasesRemainContinuous)
{
	Instance instance;
	Document &document = instance.level;
	instance.loaded.levelFormat = MapFormat::udmf;
	instance.conf.features.udmf_surface_transforms =
			kSurfaceTransformWallUDMF;
	instance.edit.mode = ObjType::linedefs;
	instance.edit.Selected.emplace(ObjType::linedefs, true);

	for (const v2double_t point :
			{v2double_t{0, 0}, v2double_t{64, 0},
					v2double_t{128, 0}})
	{
		auto vertex = std::make_shared<Vertex>();
		vertex->SetRawXY(MapFormat::udmf, point);
		document.vertices.push_back(vertex);
	}

	document.sectors.push_back(std::make_shared<Sector>());
	for (int index = 0; index < 2; index++)
	{
		auto side = std::make_shared<SideDef>();
		side->sector = 0;
		side->x_offset = index == 0 ? 13 : -9;
		side->offsetx_mid = index == 0 ? 7.5 : 4.0;
		side->scalex_mid = index == 0 ? 2.0 : 0.5;
		side->mid_tex = BA_InternaliseString("STONE");
		document.sidedefs.push_back(side);

		auto line = std::make_shared<LineDef>();
		line->start = index;
		line->end = index + 1;
		line->right = index;
		document.linedefs.push_back(line);
		instance.edit.Selected->set_ext(index, PART_RT_LOWER);
	}

	instance.EXEC_Flags[0] = "/x";
	instance.CMD_LIN_Align();

	const SideDef &first = *document.sidedefs[0];
	const SideDef &second = *document.sidedefs[1];
	const double firstAtJoin =
			(64.0 + first.x_offset + first.offsetx_mid) *
			first.scalex_mid;
	const double secondAtJoin =
			(second.x_offset + second.offsetx_mid) *
			second.scalex_mid;
	EXPECT_NEAR(firstAtJoin, secondAtJoin, 1e-9);
}
