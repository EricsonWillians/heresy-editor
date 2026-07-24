//------------------------------------------------------------------------
//  SMART DOOR TESTS
//------------------------------------------------------------------------

#include "e_door.h"

#include "Document.h"
#include "Instance.h"
#include "im_img.h"
#include "lib_adler.h"
#include "ui_door.h"
#include "w_rawdef.h"

#include "gtest/gtest.h"

#include <stdexcept>

namespace
{

class SmartDoorFixture : public ::testing::Test
{
protected:
	SmartDoorFixture()
	{
		config.default_wall_tex = "DEFAULT";
		config.miscInfo.player_h = 56;
		config.line_types[1].desc = "Door Raise";
		config.line_types[12].desc = "Door Raise";

		DoorPreset doom;
		doom.id = "doom";
		doom.label = "@special";
		doom.special = 1;
		doom.activation = DoorActivation::encoded;
		config.door_presets.push_back(doom);

		DoorPreset use;
		use.id = "use";
		use.label = "Use Door";
		use.special = 12;
		use.activation = DoorActivation::useRepeat;
		use.args = {0, 16, 150, 0, 0};
		config.door_presets.push_back(use);

		buildDoor();
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
		sector->floor_tex = BA_InternaliseString("FLOOR");
		sector->ceil_tex = BA_InternaliseString("CEIL");
		sector->light = 144;
		sector->type = 7;
		sector->tag = 23;
		doc.sectors.push_back(sector);
		return doc.numSectors() - 1;
	}

	int addSide(int sector, const char *upper, const char *middle,
				const char *lower)
	{
		auto side = std::make_shared<SideDef>();
		side->sector = sector;
		side->upper_tex = BA_InternaliseString(upper);
		side->mid_tex = BA_InternaliseString(middle);
		side->lower_tex = BA_InternaliseString(lower);
		side->x_offset = 11;
		side->y_offset = 17;
		doc.sidedefs.push_back(side);
		return doc.numSidedefs() - 1;
	}

	int addLine(int start, int end, int right, int left = -1)
	{
		auto line = std::make_shared<LineDef>();
		line->start = start;
		line->end = end;
		line->right = right;
		line->left = left;
		line->flags = MLF_Mapped | (left >= 0 ? MLF_TwoSided : 0);
		doc.linedefs.push_back(line);
		return doc.numLinedefs() - 1;
	}

	void buildDoor()
	{
		addSector();
		addSector();
		addSector();

		int v0 = addVertex(0, 0);
		int v1 = addVertex(0, 64);
		int v2 = addVertex(16, 64);
		int v3 = addVertex(16, 0);

		int door0 = addSide(0, "-", "-", "DOORLOW");
		int outside0 = addSide(1, "DOORA", "-", "OUTLOW");
		addLine(v0, v1, door0, outside0);

		int track0 = addSide(0, "-", "TRACK", "-");
		addLine(v1, v2, track0);

		int outside1 = addSide(2, "DOORB", "-", "OUTLOW");
		int door1 = addSide(0, "-", "-", "DOORLOW");
		addLine(v2, v3, outside1, door1);

		int track1 = addSide(0, "-", "TRACK", "-");
		addLine(v3, v0, track1);
	}

	DoorOptions optionsFor(MapFormat format) const
	{
		DoorOptions options;
		options.presetId = format == MapFormat::doom ? "doom" : "use";
		return options;
	}

	selection_c selection()
	{
		selection_c result(ObjType::sectors);
		result.set(0);
		return result;
	}

	Instance inst;
	Document &doc = inst.level;
	ConfigData config;
};

TEST_F(SmartDoorFixture, PlannerFindsPortalsTracksFlipsAndDeterministicTextures)
{
	DoorPlan plan = M_PlanSmartDoors(doc, config, nullptr, MapFormat::doom,
									 selection(), optionsFor(MapFormat::doom));

	ASSERT_TRUE(plan.valid());
	EXPECT_EQ(plan.sectors, std::vector<int>({0}));
	EXPECT_EQ(plan.portalLines, std::vector<int>({0, 2}));
	EXPECT_EQ(plan.trackLines, std::vector<int>({1, 3}));
	EXPECT_EQ(plan.requiredFlips, std::vector<int>({0}));
	EXPECT_EQ(plan.inferredFaceTexture, "DOORA");
	EXPECT_EQ(plan.inferredTrackTexture, "TRACK");
	EXPECT_EQ(plan.presetLabel, "Door Raise");
}

TEST_F(SmartDoorFixture, OverridesReplaceAutoTextureChoices)
{
	DoorOptions options = optionsFor(MapFormat::doom);
	options.faceTexture = "CUSTOM";
	options.trackTexture = "METAL";
	DoorPlan plan = M_PlanSmartDoors(doc, config, nullptr, MapFormat::doom,
									 selection(), options);

	ASSERT_TRUE(plan.valid());
	EXPECT_EQ(plan.faceTexture, "CUSTOM");
	EXPECT_EQ(plan.trackTexture, "METAL");
	EXPECT_EQ(plan.inferredFaceTexture, "DOORA");
	EXPECT_EQ(plan.inferredTrackTexture, "TRACK");
}

TEST_F(SmartDoorFixture, AutoTextureActionsClearBothOverrides)
{
	DoorOptions options = optionsFor(MapFormat::doom);
	options.faceTexture = "CUSTOM_FACE";
	options.trackTexture = "CUSTOM_TRACK";

	options.useAutoFaceTexture();
	EXPECT_TRUE(options.faceTexture.empty());
	EXPECT_EQ(options.trackTexture, "CUSTOM_TRACK");

	options.useAutoTrackTexture();
	EXPECT_TRUE(options.trackTexture.empty());
	DoorPlan plan = M_PlanSmartDoors(
			doc, config, nullptr, MapFormat::doom, selection(), options);
	ASSERT_TRUE(plan.valid());
	EXPECT_EQ(plan.faceTexture, "DOORA");
	EXPECT_EQ(plan.trackTexture, "TRACK");
}

TEST_F(SmartDoorFixture, TextureChooserFiltersLoadedTexturesCaseInsensitively)
{
	inst.wad.images.W_AddTexture(
			"BIGDOOR", Img_c(64, 128, true), false);
	inst.wad.images.W_AddTexture(
			"DOORTRAK", Img_c(16, 128, true), false);
	inst.wad.images.W_AddTexture(
			"STONE", Img_c(64, 64, true), false);

	EXPECT_EQ(UI_FilterDoorTextures(inst.wad.images, "door"),
			(std::vector<SString>{"BIGDOOR", "DOORTRAK"}));
	EXPECT_EQ(UI_FilterDoorTextures(inst.wad.images, "TrAk"),
			(std::vector<SString>{"DOORTRAK"}));
	EXPECT_EQ(UI_FilterDoorTextures(inst.wad.images, ""),
			(std::vector<SString>{"BIGDOOR", "DOORTRAK", "STONE"}));
}

TEST_F(SmartDoorFixture, RejectsAdjacentSelectedSectorsWithoutMutation)
{
	selection_c sectors(ObjType::sectors);
	sectors.set(0);
	sectors.set(1);
	int oldCeiling = doc.sectors[0]->ceilh;
	int oldType = doc.linedefs[0]->type;

	DoorPlan plan;
	EXPECT_FALSE(M_ApplySmartDoors(doc, config, nullptr, MapFormat::doom,
								   sectors, optionsFor(MapFormat::doom), &plan));
	EXPECT_FALSE(plan.valid());
	EXPECT_EQ(doc.sectors[0]->ceilh, oldCeiling);
	EXPECT_EQ(doc.linedefs[0]->type, oldType);
	EXPECT_FALSE(doc.basis.undo());
}

TEST_F(SmartDoorFixture, BatchFailsCompletelyWhenOneSectorIsInvalid)
{
	int isolated = addSector();
	selection_c sectors(ObjType::sectors);
	sectors.set(0);
	sectors.set(isolated);

	DoorPlan plan;
	EXPECT_FALSE(M_ApplySmartDoors(doc, config, nullptr, MapFormat::doom,
								   sectors, optionsFor(MapFormat::doom), &plan));
	EXPECT_FALSE(plan.valid());
	EXPECT_EQ(doc.sectors[0]->ceilh, 128);
	EXPECT_EQ(doc.sectors[isolated]->ceilh, 128);
	EXPECT_EQ(doc.linedefs[0]->type, 0);
	EXPECT_FALSE(doc.basis.undo());
}

TEST_F(SmartDoorFixture, AppliesMultipleIndependentDoorsAsOneOperation)
{
	int secondDoor = addSector();
	int neighborA = addSector();
	int neighborB = addSector();
	int baseVertex = doc.numVertices();
	addVertex(128, 0);
	addVertex(128, 64);
	addVertex(144, 64);
	addVertex(144, 0);

	int doorA = addSide(secondDoor, "-", "-", "LOW");
	int outsideA = addSide(neighborA, "DOORA", "-", "LOW");
	addLine(baseVertex, baseVertex + 1, doorA, outsideA);
	int trackA = addSide(secondDoor, "-", "TRACK", "-");
	addLine(baseVertex + 1, baseVertex + 2, trackA);
	int outsideB = addSide(neighborB, "DOORA", "-", "LOW");
	int doorB = addSide(secondDoor, "-", "-", "LOW");
	addLine(baseVertex + 2, baseVertex + 3, outsideB, doorB);
	int trackB = addSide(secondDoor, "-", "TRACK", "-");
	addLine(baseVertex + 3, baseVertex, trackB);

	selection_c sectors(ObjType::sectors);
	sectors.set(0);
	sectors.set(secondDoor);
	DoorPlan plan = M_PlanSmartDoors(doc, config, nullptr, MapFormat::doom,
									 sectors, optionsFor(MapFormat::doom));
	ASSERT_TRUE(plan.valid());
	EXPECT_EQ(plan.sectors.size(), 2);
	EXPECT_EQ(plan.portalLines.size(), 4);
	EXPECT_EQ(plan.trackLines.size(), 4);

	ASSERT_TRUE(M_ApplySmartDoors(doc, config, nullptr, MapFormat::doom,
								  sectors, optionsFor(MapFormat::doom)));
	EXPECT_EQ(doc.sectors[0]->ceilh, 0);
	EXPECT_EQ(doc.sectors[secondDoor]->ceilh, 0);
	ASSERT_TRUE(doc.basis.undo());
	EXPECT_EQ(doc.sectors[0]->ceilh, 128);
	EXPECT_EQ(doc.sectors[secondDoor]->ceilh, 128);
	EXPECT_FALSE(doc.basis.undo());
}

TEST_F(SmartDoorFixture, RejectsSelfReferenceMalformedBoundaryAndInvalidHeight)
{
	doc.sidedefs[doc.linedefs[0]->left]->sector = 0;
	doc.linedefs[1]->left = doc.linedefs[1]->right;
	doc.linedefs[1]->right = -1;
	doc.sectors[0]->ceilh = -8;

	DoorPlan plan = M_PlanSmartDoors(doc, config, nullptr, MapFormat::doom,
									 selection(), optionsFor(MapFormat::doom));
	EXPECT_FALSE(plan.valid());
	int errors = 0;
	for (const DoorIssue &issue : plan.issues)
		if (issue.severity == DoorIssueSeverity::error)
			errors++;
	EXPECT_GE(errors, 3);
}

TEST_F(SmartDoorFixture, WarnsForFloorMismatchClearanceSpecialAndNoTracks)
{
	doc.sectors[1]->floorh = 16;
	doc.sectors[1]->ceilh = 48;
	doc.linedefs[0]->type = 99;
	doc.linedefs[1]->left = addSide(1, "-", "-", "-");
	doc.linedefs[1]->flags |= MLF_TwoSided;
	doc.linedefs[3]->left = addSide(2, "-", "-", "-");
	doc.linedefs[3]->flags |= MLF_TwoSided;

	DoorPlan plan = M_PlanSmartDoors(doc, config, nullptr, MapFormat::doom,
									 selection(), optionsFor(MapFormat::doom));
	ASSERT_TRUE(plan.valid());
	int warnings = 0;
	for (const DoorIssue &issue : plan.issues)
		if (issue.severity == DoorIssueSeverity::warning)
			warnings++;
	EXPECT_GE(warnings, 4);
}

TEST_F(SmartDoorFixture, UnknownLoadedTexturesWarnWithoutBlocking)
{
	DoorPlan plan = M_PlanSmartDoors(doc, config, &inst.wad.images,
									 MapFormat::doom, selection(),
									 optionsFor(MapFormat::doom));
	ASSERT_TRUE(plan.valid());
	int unknownWarnings = 0;
	for (const DoorIssue &issue : plan.issues)
		if (issue.message.find("not currently loaded") != SString::npos)
			unknownWarnings++;
	EXPECT_EQ(unknownWarnings, 2);
}

TEST_F(SmartDoorFixture, PresetAvailabilityIsFormatAndRepresentationAware)
{
	EXPECT_EQ(M_AvailableDoorPresets(config, MapFormat::doom).size(), 1);
	EXPECT_EQ(M_AvailableDoorPresets(config, MapFormat::hexen).size(), 1);
	EXPECT_EQ(M_AvailableDoorPresets(config, MapFormat::udmf).size(), 1);

	config.line_types[200].desc = "Wide arguments";
	DoorPreset wide;
	wide.id = "wide";
	wide.label = "Wide";
	wide.special = 200;
	wide.activation = DoorActivation::useOnce;
	wide.args = {0, 512, 0, 0, 0};
	config.door_presets.push_back(wide);

	EXPECT_EQ(M_AvailableDoorPresets(config, MapFormat::hexen).size(), 1);
	EXPECT_EQ(M_AvailableDoorPresets(config, MapFormat::udmf).size(), 2);
}

TEST_F(SmartDoorFixture, DoomApplyPreservesPropertiesAndUndoRedo)
{
	const int originalStart = doc.linedefs[0]->start;
	const int originalEnd = doc.linedefs[0]->end;
	const StringID originalLower = doc.sidedefs[1]->lower_tex;
	const StringID floorTexture = doc.sectors[0]->floor_tex;
	const StringID ceilingTexture = doc.sectors[0]->ceil_tex;

	doc.linedefs[0]->flags |= static_cast<int>(MLF_SoundBlock) |
							 static_cast<int>(MLF_ZDoom_BlockPlayers);
	crc32_c before;
	doc.getLevelChecksum(before);
	ASSERT_TRUE(M_ApplySmartDoors(doc, config, nullptr, MapFormat::doom,
								  selection(), optionsFor(MapFormat::doom)));

	EXPECT_EQ(doc.sectors[0]->ceilh, doc.sectors[0]->floorh);
	EXPECT_EQ(doc.sectors[0]->floor_tex, floorTexture);
	EXPECT_EQ(doc.sectors[0]->ceil_tex, ceilingTexture);
	EXPECT_EQ(doc.sectors[0]->light, 144);
	EXPECT_EQ(doc.sectors[0]->type, 7);
	EXPECT_EQ(doc.sectors[0]->tag, 23);
	EXPECT_EQ(doc.linedefs[0]->type, 1);
	EXPECT_EQ(doc.linedefs[0]->start, originalEnd);
	EXPECT_EQ(doc.linedefs[0]->end, originalStart);
	EXPECT_EQ(doc.sidedefs[doc.linedefs[0]->right]->UpperTex(), "DOORA");
	EXPECT_TRUE(is_null_tex(doc.sidedefs[doc.linedefs[0]->right]->MidTex()));
	EXPECT_EQ(doc.sidedefs[doc.linedefs[0]->right]->lower_tex, originalLower);
	EXPECT_TRUE(doc.linedefs[0]->flags & MLF_Mapped);
	EXPECT_TRUE(doc.linedefs[0]->flags & MLF_SoundBlock);
	EXPECT_TRUE(doc.linedefs[0]->flags & MLF_ZDoom_BlockPlayers);
	EXPECT_FALSE(doc.linedefs[0]->flags & MLF_Blocking);
	EXPECT_EQ(doc.sidedefs[doc.linedefs[1]->right]->MidTex(), "TRACK");
	EXPECT_TRUE(doc.linedefs[1]->flags & MLF_Blocking);
	EXPECT_TRUE(doc.linedefs[1]->flags & MLF_LowerUnpegged);
	EXPECT_EQ(doc.sidedefs[doc.linedefs[1]->right]->x_offset, 11);
	EXPECT_EQ(doc.sidedefs[doc.linedefs[1]->right]->y_offset, 17);

	ASSERT_TRUE(doc.basis.undo());
	EXPECT_EQ(doc.sectors[0]->ceilh, 128);
	EXPECT_EQ(doc.linedefs[0]->type, 0);
	EXPECT_EQ(doc.linedefs[0]->start, originalStart);
	EXPECT_EQ(doc.linedefs[0]->end, originalEnd);
	EXPECT_EQ(doc.sidedefs[1]->UpperTex(), "DOORA");
	EXPECT_EQ(doc.sidedefs[2]->MidTex(), "TRACK");
	crc32_c restored;
	doc.getLevelChecksum(restored);
	EXPECT_EQ(restored.getPath(), before.getPath());

	ASSERT_TRUE(doc.basis.redo());
	EXPECT_EQ(doc.sectors[0]->ceilh, 0);
	EXPECT_EQ(doc.linedefs[0]->type, 1);
}

TEST_F(SmartDoorFixture, SharedSidedefIsIsolatedSelectively)
{
	int shared = doc.linedefs[0]->left;
	int a = addVertex(128, 0);
	int b = addVertex(128, 64);
	int unrelated = addLine(a, b, shared);

	ASSERT_TRUE(M_ApplySmartDoors(doc, config, nullptr, MapFormat::doom,
								  selection(), optionsFor(MapFormat::doom)));
	EXPECT_EQ(doc.linedefs[unrelated]->right, shared);
	EXPECT_NE(doc.linedefs[0]->right, shared);
	EXPECT_EQ(doc.sidedefs[shared]->UpperTex(), "DOORA");
}

TEST_F(SmartDoorFixture, HexenAndUdmfUseCorrectActivationRepresentations)
{
	for (MapFormat format : {MapFormat::hexen, MapFormat::udmf})
	{
		ASSERT_TRUE(M_ApplySmartDoors(doc, config, nullptr, format,
									  selection(), optionsFor(format)));
		EXPECT_EQ(doc.linedefs[0]->type, 12);
		EXPECT_EQ(doc.linedefs[0]->arg2, 16);
		EXPECT_EQ(doc.linedefs[0]->arg3, 150);
		if (format == MapFormat::hexen)
		{
			EXPECT_EQ(doc.linedefs[0]->flags & MLF_Activation, 0x400);
			EXPECT_TRUE(doc.linedefs[0]->flags & MLF_Repeatable);
		}
		else
		{
			EXPECT_TRUE(doc.linedefs[0]->udmfFlags & MLF_UDMF_playeruse);
			EXPECT_TRUE(doc.linedefs[0]->udmfFlags & MLF_UDMF_repeatspecial);
		}

		ASSERT_TRUE(doc.basis.undo());
	}
}

TEST_F(SmartDoorFixture, UseOnceClearsRepeatForHexenAndUdmf)
{
	config.line_types[11].desc = "Door Open";
	DoorPreset once;
	once.id = "once";
	once.label = "Once";
	once.special = 11;
	once.activation = DoorActivation::useOnce;
	once.args = {0, 16, 0, 0, 0};
	config.door_presets.push_back(once);
	DoorOptions options;
	options.presetId = "once";

	for (MapFormat format : {MapFormat::hexen, MapFormat::udmf})
	{
		doc.linedefs[0]->flags |= MLF_Repeatable | MLF_Activation;
		doc.linedefs[0]->udmfFlags =
				MLF_UDMF_playercross | MLF_UDMF_repeatspecial;
		ASSERT_TRUE(M_ApplySmartDoors(doc, config, nullptr, format,
									  selection(), options));
		if (format == MapFormat::hexen)
		{
			EXPECT_EQ(doc.linedefs[0]->flags & MLF_Activation, 0x400);
			EXPECT_FALSE(doc.linedefs[0]->flags & MLF_Repeatable);
			EXPECT_EQ(doc.linedefs[0]->udmfFlags, 0u);
		}
		else
		{
			EXPECT_TRUE(doc.linedefs[0]->udmfFlags & MLF_UDMF_playeruse);
			EXPECT_FALSE(doc.linedefs[0]->udmfFlags &
						 MLF_UDMF_repeatspecial);
		}
		ASSERT_TRUE(doc.basis.undo());
	}
}

TEST_F(SmartDoorFixture, DialogCancellationLeavesDocumentUndoAndSelectionUntouched)
{
	inst.conf = config;
	inst.loaded.gameName = "doom2";
	inst.loaded.levelFormat = MapFormat::doom;
	inst.edit.mode = ObjType::sectors;
	inst.edit.render3d = false;
	inst.edit.Selected.emplace(ObjType::sectors);
	inst.edit.Selected->set(0);
	inst.edit.designAssistPreview.emplace();
	inst.edit.designAssistPreview->sectors.set(0);

	UI_SmartDoorDialog_Override =
			[](Instance &, const selection_c &chosen, DoorOptions &)
			{
				EXPECT_TRUE(chosen.get(0));
				return false;
			};
	inst.CMD_SEC_MakeDoor();
	UI_SmartDoorDialog_Override = {};

	EXPECT_EQ(doc.sectors[0]->ceilh, 128);
	EXPECT_EQ(doc.linedefs[0]->type, 0);
	EXPECT_TRUE(inst.edit.Selected->get(0));
	EXPECT_FALSE(inst.edit.designAssistPreview);
	EXPECT_FALSE(doc.basis.undo());
}

TEST_F(SmartDoorFixture, DialogAcceptanceAppliesOnceAndKeepsSelection)
{
	inst.conf = config;
	inst.loaded.gameName = "doom2";
	inst.loaded.levelFormat = MapFormat::doom;
	inst.edit.mode = ObjType::sectors;
	inst.edit.render3d = false;
	inst.edit.Selected.emplace(ObjType::sectors);
	inst.edit.Selected->set(0);

	UI_SmartDoorDialog_Override =
			[](Instance &, const selection_c &, DoorOptions &options)
			{
				options.faceTexture = "CHOSEN";
				return true;
			};
	inst.CMD_SEC_MakeDoor();
	UI_SmartDoorDialog_Override = {};

	EXPECT_EQ(doc.sectors[0]->ceilh, 0);
	EXPECT_EQ(doc.sidedefs[doc.linedefs[0]->right]->UpperTex(), "CHOSEN");
	EXPECT_TRUE(inst.edit.Selected->get(0));
	ASSERT_TRUE(doc.basis.undo());
	EXPECT_FALSE(doc.basis.undo());
}

TEST_F(SmartDoorFixture, PreviewOverlayRecomputesRolesAndCleansUp)
{
	DoorPlan initial = M_PlanSmartDoors(doc, config, nullptr, MapFormat::doom,
										selection(), optionsFor(MapFormat::doom));
	UI_SetSmartDoorPreview(inst, initial);
	ASSERT_TRUE(inst.edit.designAssistPreview);
	EXPECT_TRUE(inst.edit.designAssistPreview->sectors.get(0));
	EXPECT_TRUE(inst.edit.designAssistPreview->emphasizeSectors);
	EXPECT_TRUE(inst.edit.designAssistPreview->activatingLines.get(0));
	EXPECT_TRUE(inst.edit.designAssistPreview->trackLines.get(1));

	doc.linedefs.erase(doc.linedefs.begin() + 3);
	doc.linedefs.erase(doc.linedefs.begin() + 1);
	DoorPlan recomputed = M_PlanSmartDoors(doc, config, nullptr, MapFormat::doom,
										   selection(), optionsFor(MapFormat::doom));
	UI_SetSmartDoorPreview(inst, recomputed);
	ASSERT_TRUE(inst.edit.designAssistPreview);
	EXPECT_TRUE(inst.edit.designAssistPreview->activatingLines.get(0));
	EXPECT_TRUE(inst.edit.designAssistPreview->activatingLines.get(1));
	EXPECT_TRUE(inst.edit.designAssistPreview->trackLines.empty());

	UI_ClearDesignAssistPreview(inst);
	EXPECT_FALSE(inst.edit.designAssistPreview);
}

TEST_F(SmartDoorFixture, DialogExceptionAlwaysCleansPreview)
{
	inst.conf = config;
	inst.loaded.gameName = "doom2";
	inst.loaded.levelFormat = MapFormat::doom;
	inst.edit.mode = ObjType::sectors;
	inst.edit.render3d = false;
	inst.edit.Selected.emplace(ObjType::sectors);
	inst.edit.Selected->set(0);

	UI_SmartDoorDialog_Override =
			[](Instance &dialogInstance, const selection_c &,
			   DoorOptions &) -> bool
			{
				dialogInstance.edit.designAssistPreview.emplace();
				throw std::runtime_error("test review failure");
			};
	inst.CMD_SEC_MakeDoor();
	UI_SmartDoorDialog_Override = {};

	EXPECT_FALSE(inst.edit.designAssistPreview);
	EXPECT_EQ(doc.sectors[0]->ceilh, 128);
	EXPECT_TRUE(inst.edit.Selected->get(0));
	EXPECT_FALSE(doc.basis.undo());
}

} // namespace
