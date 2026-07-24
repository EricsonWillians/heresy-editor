//------------------------------------------------------------------------
//
//  Eureka DOOM Editor
//
//  Copyright (C) 2024 Ioan Chera
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

#include "testUtils/TempDirContext.hpp"

#include "Instance.h"
#include "m_config.h"
#include "m_files.h"
#include "m_mapinfo.h"
#include "m_package.h"
#include "m_session.h"
#include "m_testmap.h"
#include "ui_mapinfo.h"
#include "w_rawdef.h"
#include "w_texture.h"

#include "gtest/gtest.h"

#ifdef _WIN32
#else
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <chrono>
#include <fstream>
#include <thread>

class TestMapFixture : public TempDirContext
{
protected:
	void SetUp() override;
	void TearDown() override;
	void setPortName(const char* name);
	void addIWAD();
	void addResources();
	void addPWAD();
	void addPK3();
	void configureRuntimeProject();
	void addManagedRuntimeProject(bool stale);
	std::vector<std::string> getResultLines() const;

	std::vector<std::string> testMapAndGetLines();

	Instance inst;
	fs::path outputPath;
	fs::path finishMarkPath;	// empty file added by test script to indicate it's done
	fs::path portPath;
	fs::path gameWadPath;
	fs::path res1Path;
	fs::path res2Path;
	fs::path editWadPath;

#ifdef __APPLE__
	fs::path macPath;
#endif

private:
#ifndef _WIN32
	void writeShellScript(const fs::path &path);
#endif
};

#ifndef _WIN32
void TestMapFixture::writeShellScript(const fs::path &path)
{
	std::ofstream stream(path);
	ASSERT_TRUE(stream.is_open());
	mDeleteList.push(path);
	stream << "#!/bin/bash" << std::endl;
	stream << "echo running script" << std::endl;
	stream << "echo \"$0\" > " << SString(outputPath.u8string()).spaceEscape(true) << std::endl;
	stream << "for var in \"$@\"" << std::endl;
	stream << "do" << std::endl;
	stream << "echo \"$var\" >> " << SString(outputPath.u8string()).spaceEscape(true) <<
		std::endl;
	stream << "done" << std::endl;
	stream << "echo done > " << SString(finishMarkPath.u8string()).spaceEscape(true) << std::endl;
	stream.close();
	int r = chmod(path.string().c_str(), S_IRWXU);
	ASSERT_FALSE(r);
}
#endif

void TestMapFixture::SetUp()
{
	TempDirContext::SetUp();
	outputPath = getSubPath("output.list");
	finishMarkPath = getSubPath("finish.mark");

	// Setup the program
#ifdef _WIN32
	portPath = getSubPath("port.bat");
	std::ofstream stream(portPath);
	ASSERT_TRUE(stream.is_open());
	mDeleteList.push(portPath);
	stream << "@echo off" << std::endl;
	stream << "echo running script" << std::endl;
	stream << "echo %0 > " << SString(outputPath.u8string()).spaceEscape(false) << std::endl;
	stream << "for %%x in (%*) do (" << std::endl;
	stream << "echo %%x >> " << SString(outputPath.u8string()).spaceEscape(false) << std::endl;
	stream << ")" << std::endl;
	stream << "echo done > " << SString(finishMarkPath.u8string()).spaceEscape(false) << std::endl;
	stream.close();
#else
	portPath = getSubPath("port-script");
	writeShellScript(portPath);
#endif
#ifdef __APPLE__
	macPath = getSubPath("port.app");
	bool result;
	result = FileMakeDir(macPath);
	ASSERT_TRUE(result);
	mDeleteList.push(macPath);
	fs::path contdir = macPath / "Contents";
	result = FileMakeDir(contdir);
	ASSERT_TRUE(result);
	mDeleteList.push(contdir);
	fs::path macosdir = contdir / "MacOS";
	result = FileMakeDir(macosdir);
	ASSERT_TRUE(result);
	mDeleteList.push(macosdir);
	fs::path execpath = macosdir / "portentry";
	writeShellScript(execpath);
	// Now the info plist file
	fs::path infopath = contdir / "Info.plist";
	std::ofstream infostream(infopath);
	ASSERT_TRUE(infostream.is_open());
	mDeleteList.push(infopath);
	infostream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" << std::endl;
	infostream << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">" << std::endl;
	infostream << "<plist version=\"1.0\">" << std::endl;
	infostream << "<dict>" << std::endl;
	infostream << "<key>CFBundleExecutable</key>" << std::endl;
	infostream << "<string>" << execpath.filename().u8string() << "</string>" << std::endl;
	infostream << "<key>CFBundleIdentifier</key>" << std::endl;
	infostream << "<string>com.eureka.testmaptest</string>" << std::endl;
	infostream << "</dict>" << std::endl;
	infostream << "</plist>" << std::endl;
	infostream.close();
#endif
}

void TestMapFixture::TearDown()
{
	DLG_Confirm_Override = nullptr;
	UI_RuntimeMapInfoPreview_Override = nullptr;
	inst.wad.master.MasterDir_CloseAll();
	TempDirContext::TearDown();
}


void TestMapFixture::setPortName(const char* name)
{
	// Prepare the conditions
	global::recent.setPortPath(!strcmp(name, "vanilla") ? "vanilla_doom2" : name, portPath);

	// Populate for GrabWadNamesArgs
	inst.loaded.portName = name;
}

void TestMapFixture::addIWAD()
{
	gameWadPath = getSubPath("ga me.wad");
	std::shared_ptr<Wad_file> gameWad = Wad_file::Open(gameWadPath, WadOpenMode::write);
	inst.wad.master.setGameWad(gameWad);
}

void TestMapFixture::addResources()
{
	std::vector<std::shared_ptr<Wad_file>> resources;
	res1Path = getSubPath("re s1.wad");
	res2Path = getSubPath("re s2.wad");
	resources.push_back(Wad_file::Open(res1Path, WadOpenMode::write));
	resources.push_back(Wad_file::Open(res2Path, WadOpenMode::write));
	inst.wad.master.setResources(resources);
}

void TestMapFixture::addPWAD()
{
	editWadPath = getSubPath("ed it.wad");
	std::shared_ptr<Wad_file> editWad = Wad_file::Open(editWadPath, WadOpenMode::write);
	inst.wad.master.ReplaceEditWad(editWad);
	mDeleteList.push(editWadPath);
}

void TestMapFixture::addPK3()
{
	editWadPath = getSubPath("ed it.pk3");
	const std::shared_ptr<PackageBackend> backend = M_CreatePackageBackend(
			editWadPath, ProjectPackage::pk3);
	ASSERT_TRUE(backend);
	std::shared_ptr<Wad_file> editWad = backend->openEditable();
	ASSERT_TRUE(editWad);
	editWad->writeToDisk();
	inst.wad.master.ReplaceEditWad(editWad);
	mDeleteList.push(editWadPath);
}

void TestMapFixture::configureRuntimeProject()
{
	inst.loaded.gameName = "doom2";
	inst.loaded.levelName = "MAP07";
	inst.loaded.project.version = ProjectMetadata::CURRENT_VERSION;
	inst.loaded.project.name = "Runtime Test";
	inst.loaded.project.package = M_ProjectPackageForPath(editWadPath);
	inst.loaded.project.campaign = CampaignMode::custom;
	inst.loaded.project.mapSlots = { "MAP07" };
	CampaignMapDefinition definition;
	definition.mapName = "MAP07";
	definition.title = "Original title";
	inst.loaded.project.mapDefinitions = { definition };
}

void TestMapFixture::addManagedRuntimeProject(bool stale)
{
	std::shared_ptr<Wad_file> editWad = inst.wad.master.editWad();
	ASSERT_TRUE(editWad);
	configureRuntimeProject();

	const auto generated = M_GenerateRuntimeMapInfo(inst.loaded.project,
			"biaseddoom", "doom2");
	ASSERT_TRUE(generated);
	M_StoreManagedRuntimeMapInfo(*editWad, generated->text);
	editWad->writeToDisk();
	mDeleteList.push(M_ProjectSessionPath(editWadPath));

	if (stale)
	{
		inst.loaded.project.mapDefinition("MAP07")->title = "Updated title";
		inst.Project_MarkMetadataDirty();
	}
}

std::vector<std::string> TestMapFixture::getResultLines() const
{
	// Try until it's open
	for (int i = 0; i < 20; ++i)
	{
		if (fs::exists(finishMarkPath))
			break;
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	std::ifstream input;
	input.open(outputPath);
	if(!input.is_open())
	{
		// Wait more if it fails at first, just in case
		std::this_thread::sleep_for(std::chrono::seconds(2));
		input.open(outputPath);
	}
	EXPECT_TRUE(input.is_open());

	std::vector<std::string> result;
	while (input && !input.eof())
	{
		std::string line;
		std::getline(input, line);
		if (line.empty() && input.eof())
			return result;
		// Windows may add quotes on arguments with spaces
#ifdef _WIN32
		SString changedLine = SString(line);
		changedLine.trimTrailingSpaces();
		line = changedLine.get();
		if (line.length() >= 2 && line[0] == '"' && line.back() == '"')
			line = line.substr(1, line.length() - 2);
#endif
		result.push_back(line);
	}
	return result;
}

std::vector<std::string> TestMapFixture::testMapAndGetLines()
{
	// Now run
    try
    {
        inst.CMD_TestMap();
    }
    catch(const std::exception &e)
    {
        // Add some delay to make sure we pass Windows failures
        if(strstr(e.what(), "being used by another process"))
            std::this_thread::sleep_for(std::chrono::seconds(2));
        // Retry
        inst.CMD_TestMap();
    }
	mDeleteList.push(outputPath);
	mDeleteList.push(finishMarkPath);

	return getResultLines();
}


TEST_F(TestMapFixture, TestMapVanillaWithResources)
{
	setPortName("vanilla");

	addIWAD();

	addResources();

	addPWAD();

	inst.loaded.levelName = "MAP14";

	// Now run

	std::vector<std::string> lines = testMapAndGetLines();
	std::vector<std::string> expected = {portPath.string(), "-iwad", gameWadPath.string(), "-merge",
		res1Path.string(), res2Path.string(), "-file", editWadPath.string(), "-warp", "14"};
	ASSERT_EQ(lines, expected);
}

TEST_F(TestMapFixture, TestMapPortWithResources)
{
	setPortName("boom");

	addIWAD();

	addResources();

	addPWAD();

	inst.loaded.levelName = "MAP14";

	std::vector<std::string> lines = testMapAndGetLines();
	std::vector<std::string> expected = {portPath.string(), "-iwad", gameWadPath.string(), "-file",
		res1Path.string(), res2Path.string(), editWadPath.string(), "-warp", "14"};
	ASSERT_EQ(lines, expected);
}

TEST_F(TestMapFixture, TestMapBiasedDoomProfileArguments)
{
	setPortName("biaseddoom");
	addIWAD();
	addResources();
	addPWAD();
	inst.loaded.levelName = "MAP07";

	std::vector<std::string> lines = testMapAndGetLines();
	std::vector<std::string> expected = {portPath.string(), "-iwad", gameWadPath.string(),
		"-file", res1Path.string(), res2Path.string(), editWadPath.string(), "-warp", "7"};
	ASSERT_EQ(lines, expected);
}

TEST_F(TestMapFixture, SaveForTestPreservesUndoAndAccidentalUndoRecovery)
{
	struct SaveSettingsRestore
	{
		fs::path home = global::home_dir;
		int backupFiles = config::backup_max_files;
		~SaveSettingsRestore()
		{
			global::home_dir = home;
			config::backup_max_files = backupFiles;
		}
	} restoreSaveSettings;
	global::home_dir = mTempDir;
	config::backup_max_files = 0;
	mDeleteList.push(mTempDir / "misc.cfg");

	setPortName("boom");
	addIWAD();
	addPWAD();
	inst.loaded.levelName = "MAP14";
	inst.loaded.levelFormat = MapFormat::doom;
	inst.level.headerData = {1};
	{
		EditOperation operation(inst.level.basis);
		operation.changeLump(LumpType::header, std::vector<byte>{2});
		operation.setMessage("changed the test revision");
	}
	ASSERT_TRUE(inst.level.hasChanges());

	DLG_Confirm_Override =
			[](const std::vector<SString> &, const char *, va_list)
			{
				return 1; // Save before launching.
			};
	const std::vector<std::string> lines = testMapAndGetLines();
	DLG_Confirm_Override = nullptr;
	ASSERT_FALSE(lines.empty());
	EXPECT_FALSE(inst.level.hasChanges());
	EXPECT_EQ(inst.level.headerData, (std::vector<byte>{2}));

	// This is the user's post-test Ctrl/Cmd+Z, followed by recovery from
	// an accidental Undo via Ctrl/Cmd+Shift+Z, Ctrl/Cmd+Y, or Edit / Redo.
	inst.CMD_Undo();
	EXPECT_EQ(inst.level.headerData, (std::vector<byte>{1}));
	EXPECT_TRUE(inst.level.hasChanges());
	inst.CMD_Redo();
	EXPECT_EQ(inst.level.headerData, (std::vector<byte>{2}));
	EXPECT_FALSE(inst.level.hasChanges());
}

TEST_F(TestMapFixture, CurrentManagedRuntimeMapInfoLaunchesWithoutPrompt)
{
	setPortName("biaseddoom");
	addIWAD();
	addPWAD();
	addManagedRuntimeProject(false);
	bool prompted = false;
	DLG_Confirm_Override = [&prompted](const std::vector<SString> &,
			const char *, va_list)
	{
		prompted = true;
		return 0;
	};

	const std::vector<std::string> lines = testMapAndGetLines();
	EXPECT_FALSE(prompted);
	EXPECT_EQ(lines, (std::vector<std::string>{ portPath.string(), "-iwad",
			gameWadPath.string(), "-file", editWadPath.string(), "-warp", "7" }));
}

TEST_F(TestMapFixture, MissingRuntimeMapInfoRemainsOptionalAndDoesNotPrompt)
{
	setPortName("biaseddoom");
	addIWAD();
	addPWAD();
	configureRuntimeProject();
	inst.wad.master.editWad()->writeToDisk();
	bool prompted = false;
	DLG_Confirm_Override = [&prompted](const std::vector<SString> &,
			const char *, va_list)
	{
		prompted = true;
		return 0;
	};

	const std::vector<std::string> lines = testMapAndGetLines();
	EXPECT_FALSE(prompted);
	EXPECT_EQ(lines, (std::vector<std::string>{ portPath.string(), "-iwad",
			gameWadPath.string(), "-file", editWadPath.string(), "-warp", "7" }));
}

TEST_F(TestMapFixture, UserAuthoredRuntimeMapInfoDoesNotPrompt)
{
	setPortName("biaseddoom");
	addIWAD();
	addPWAD();
	configureRuntimeProject();
	inst.wad.master.editWad()->AddLump("MAPINFO").Printf(
			"map MAP07 \"User-owned title\" {}\n");
	inst.wad.master.editWad()->writeToDisk();
	bool prompted = false;
	DLG_Confirm_Override = [&prompted](const std::vector<SString> &,
			const char *, va_list)
	{
		prompted = true;
		return 0;
	};

	const std::vector<std::string> lines = testMapAndGetLines();
	EXPECT_FALSE(prompted);
	EXPECT_EQ(lines, (std::vector<std::string>{ portPath.string(), "-iwad",
			gameWadPath.string(), "-file", editWadPath.string(), "-warp", "7" }));
}

TEST_F(TestMapFixture, StaleManagedRuntimeMapInfoCanLaunchUnchanged)
{
	setPortName("biaseddoom");
	addIWAD();
	addPWAD();
	addManagedRuntimeProject(true);
	bool prompted = false;
	DLG_Confirm_Override = [&prompted](const std::vector<SString> &buttons,
			const char *message, va_list)
	{
		prompted = true;
		EXPECT_EQ(buttons.size(), 3u);
		EXPECT_NE(std::string(message).find("out of date"), std::string::npos);
		return 1; // Launch Anyway.
	};

	const std::vector<std::string> lines = testMapAndGetLines();
	EXPECT_TRUE(prompted);
	EXPECT_EQ(lines, (std::vector<std::string>{ portPath.string(), "-iwad",
			gameWadPath.string(), "-file", editWadPath.string(), "-warp", "7" }));
	const RuntimeMapInfoInspection inspection = M_InspectRuntimeMapInfo(editWadPath,
			ProjectPackage::wad, *inst.wad.master.editWad());
	const auto expected = M_GenerateRuntimeMapInfo(inst.loaded.project,
			"biaseddoom", "doom2");
	ASSERT_TRUE(expected);
	EXPECT_EQ(M_RuntimeMapInfoFreshness(inspection, expected->text),
			RuntimeMapInfoFreshness::stale);
}

TEST_F(TestMapFixture, StaleManagedRuntimeMapInfoCanCancelLaunch)
{
	setPortName("biaseddoom");
	addIWAD();
	addPWAD();
	addManagedRuntimeProject(true);
	DLG_Confirm_Override = [](const std::vector<SString> &, const char *, va_list)
	{
		return 0;
	};

	inst.CMD_TestMap();
	EXPECT_FALSE(fs::exists(outputPath));
	EXPECT_FALSE(fs::exists(finishMarkPath));
}

TEST_F(TestMapFixture, StaleManagedRuntimeMapInfoPreflightCoversPk3Projects)
{
	setPortName("biaseddoom");
	addIWAD();
	addPK3();
	addManagedRuntimeProject(true);
	bool prompted = false;
	DLG_Confirm_Override = [&prompted](const std::vector<SString> &,
			const char *, va_list)
	{
		prompted = true;
		return 1;
	};

	const std::vector<std::string> lines = testMapAndGetLines();
	EXPECT_TRUE(prompted);
	EXPECT_EQ(lines, (std::vector<std::string>{ portPath.string(), "-iwad",
			gameWadPath.string(), "-file", editWadPath.string(), "-warp", "7" }));
	const RuntimeMapInfoInspection inspection = M_InspectRuntimeMapInfo(editWadPath,
			ProjectPackage::pk3, *inst.wad.master.editWad());
	const auto expected = M_GenerateRuntimeMapInfo(inst.loaded.project,
			"biaseddoom", "doom2");
	ASSERT_TRUE(expected);
	EXPECT_EQ(M_RuntimeMapInfoFreshness(inspection, expected->text),
			RuntimeMapInfoFreshness::stale);
}

TEST_F(TestMapFixture, StaleManagedRuntimeMapInfoCanReviewUpdateAndLaunch)
{
	struct BackupSettingRestore
	{
		int previous = config::backup_max_files;
		~BackupSettingRestore()
		{
			config::backup_max_files = previous;
		}
	} restoreBackupSetting;
	config::backup_max_files = 0;
	setPortName("biaseddoom");
	addIWAD();
	addPWAD();
	addManagedRuntimeProject(true);
	DLG_Confirm_Override = [](const std::vector<SString> &buttons,
			const char *, va_list)
	{
		EXPECT_EQ(buttons.size(), 3u);
		return 2; // Review and Update.
	};
	bool previewed = false;
	UI_RuntimeMapInfoPreview_Override = [&previewed](
			const GeneratedRuntimeMapInfo &generated,
			const RuntimeMapInfoInspection &inspection, const fs::path &,
			size_t mapCount, bool projectModified)
	{
		previewed = true;
		EXPECT_EQ(inspection.state, RuntimeMapInfoState::managed);
		EXPECT_NE(generated.text.find("Updated title"), SString::npos);
		EXPECT_EQ(mapCount, 1u);
		EXPECT_TRUE(projectModified);
		return true;
	};

	const std::vector<std::string> lines = testMapAndGetLines();
	EXPECT_TRUE(previewed);
	EXPECT_EQ(lines, (std::vector<std::string>{ portPath.string(), "-iwad",
			gameWadPath.string(), "-file", editWadPath.string(), "-warp", "7" }));
	const RuntimeMapInfoInspection inspection = M_InspectRuntimeMapInfo(editWadPath,
			ProjectPackage::wad, *inst.wad.master.editWad());
	const auto expected = M_GenerateRuntimeMapInfo(inst.loaded.project,
			"biaseddoom", "doom2");
	ASSERT_TRUE(expected);
	EXPECT_EQ(M_RuntimeMapInfoFreshness(inspection, expected->text),
			RuntimeMapInfoFreshness::current);
}

class BiasedDoomFormatLaunchFixture : public TestMapFixture,
		public ::testing::WithParamInterface<MapFormat>
{
protected:
	fs::path previousHome;
	bool previousBspOnSave = false;
	int previousBackupFiles = 0;

	void SetUp() override
	{
		TestMapFixture::SetUp();
		previousHome = global::home_dir;
		previousBspOnSave = config::bsp_on_save;
		previousBackupFiles = config::backup_max_files;
		global::home_dir = getSubPath("home");
		ASSERT_TRUE(FileMakeDir(global::home_dir));
		mDeleteList.push(global::home_dir);
		mDeleteList.push(global::home_dir / "misc.cfg");
		config::bsp_on_save = true;
		config::backup_max_files = 0;
		inst.Editor_Init();
	}

	void TearDown() override
	{
		DLG_Confirm_Override = nullptr;
		inst.wad.master.MasterDir_CloseAll();
		config::bsp_on_save = previousBspOnSave;
		config::backup_max_files = previousBackupFiles;
		global::home_dir = previousHome;
		TempDirContext::TearDown();
	}

	static std::string textOf(const Lump_c &lump)
	{
		const std::vector<byte> &data = lump.getData();
		return std::string(reinterpret_cast<const char *>(data.data()), data.size());
	}
};

TEST_P(BiasedDoomFormatLaunchFixture, SavesBuildsAndLaunchesDirtyMap)
{
	const MapFormat format = GetParam();
	setPortName("biaseddoom");
	addIWAD();
	addResources();
	addPWAD();

	inst.loaded.gameName = "doom2";
	inst.loaded.levelName = "MAP07";
	inst.loaded.levelFormat = format;
	inst.loaded.udmfNamespace = "ZDoom";
	inst.conf.default_wall_tex = "STARTAN3";
	inst.conf.default_floor_tex = "FLOOR0_1";
	inst.conf.default_ceil_tex = "CEIL1_1";
	auto sector = std::make_shared<Sector>();
	sector->floorh = 0;
	sector->ceilh = 128;
	sector->floor_tex = BA_InternaliseString("FLOOR0_1");
	sector->ceil_tex = BA_InternaliseString("CEIL1_1");
	sector->light = 160;
	inst.level.sectors.push_back(std::move(sector));
	for (int index = 0; index < 4; ++index)
	{
		auto vertex = std::make_shared<Vertex>();
		vertex->SetRawXY(format, {
				(index >= 2) ? 256.0 : -256.0,
				(index == 1 || index == 2) ? 256.0 : -256.0});
		inst.level.vertices.push_back(std::move(vertex));

		auto side = std::make_shared<SideDef>();
		side->sector = 0;
		side->upper_tex = BA_InternaliseString("STARTAN3");
		side->mid_tex = BA_InternaliseString("STARTAN3");
		side->lower_tex = BA_InternaliseString("STARTAN3");
		inst.level.sidedefs.push_back(std::move(side));

		auto line = std::make_shared<LineDef>();
		line->start = index;
		line->end = (index + 1) % 4;
		line->right = index;
		line->flags = MLF_Blocking;
		inst.level.linedefs.push_back(std::move(line));

		auto thing = std::make_shared<Thing>();
		thing->type = index + 1;
		thing->angle = 90;
		thing->SetRawXY(format, {index * 32.0 - 48.0, 0.0});
		inst.level.things.push_back(std::move(thing));
	}
	inst.level.CalculateLevelBounds();
	if (format == MapFormat::udmf)
	{
		inst.level.linedefs[0]->udmf_properties.push_back({"locknumber", "3"});
		inst.level.sidedefs[0]->udmf_properties.push_back({"scalex_mid", "1.25"});
		inst.level.sectors[0]->udmf_properties.push_back({"lightcolor", "16755200"});
	}
	inst.level.markRecovered();

	bool savePromptSeen = false;
	DLG_Confirm_Override = [&savePromptSeen](const std::vector<SString> &,
			const char *message, va_list)
	{
		savePromptSeen = true;
		EXPECT_NE(std::string(message).find("unsaved changes"), std::string::npos);
		return 1; // Save and continue to the engine launch.
	};

	const std::vector<std::string> lines = testMapAndGetLines();
	const std::vector<std::string> expected = {
		portPath.string(), "-iwad", gameWadPath.string(), "-file",
		res1Path.string(), res2Path.string(), editWadPath.string(), "-warp", "7"
	};
	EXPECT_EQ(lines, expected);
	EXPECT_TRUE(savePromptSeen);
	EXPECT_FALSE(inst.level.hasChanges());

	const std::shared_ptr<Wad_file> savedWad = inst.wad.master.editWad();
	ASSERT_TRUE(savedWad);
	const int level = savedWad->LevelFind("MAP07");
	ASSERT_GE(level, 0);
	EXPECT_EQ(savedWad->LevelFormat(level), format);
	auto lump = [&](const char *name) -> const Lump_c *
	{
		const int index = savedWad->LevelLookupLump(level, name);
		return index < 0 ? nullptr : savedWad->GetLump(index);
	};

	if (format == MapFormat::doom)
	{
		const Lump_c *things = lump("THINGS");
		const Lump_c *linedefs = lump("LINEDEFS");
		ASSERT_NE(things, nullptr);
		ASSERT_NE(linedefs, nullptr);
		EXPECT_EQ(things->Length(), 4 * static_cast<int>(sizeof(raw_thing_t)));
		EXPECT_EQ(linedefs->Length(), 4 * static_cast<int>(sizeof(raw_linedef_t)));
		EXPECT_EQ(lump("BEHAVIOR"), nullptr);
		EXPECT_EQ(lump("TEXTMAP"), nullptr);
		ASSERT_NE(lump("NODES"), nullptr);
		ASSERT_NE(lump("SEGS"), nullptr);
		ASSERT_NE(lump("SSECTORS"), nullptr);
		EXPECT_GT(lump("SEGS")->Length(), 0);
		EXPECT_GT(lump("SSECTORS")->Length(), 0);
	}
	else if (format == MapFormat::hexen)
	{
		const Lump_c *things = lump("THINGS");
		const Lump_c *linedefs = lump("LINEDEFS");
		ASSERT_NE(things, nullptr);
		ASSERT_NE(linedefs, nullptr);
		EXPECT_EQ(things->Length(), 4 * static_cast<int>(sizeof(raw_hexen_thing_t)));
		EXPECT_EQ(linedefs->Length(), 4 * static_cast<int>(sizeof(raw_hexen_linedef_t)));
		ASSERT_NE(lump("BEHAVIOR"), nullptr);
		EXPECT_EQ(lump("TEXTMAP"), nullptr);
		ASSERT_NE(lump("NODES"), nullptr);
		ASSERT_NE(lump("SEGS"), nullptr);
		ASSERT_NE(lump("SSECTORS"), nullptr);
		EXPECT_GT(lump("SEGS")->Length(), 0);
		EXPECT_GT(lump("SSECTORS")->Length(), 0);
	}
	else
	{
		const Lump_c *textmap = lump("TEXTMAP");
		ASSERT_NE(textmap, nullptr);
		const std::string text = textOf(*textmap);
		EXPECT_NE(text.find("namespace = \"ZDoom\";"), std::string::npos);
		EXPECT_NE(text.find("locknumber = 3;"), std::string::npos);
		EXPECT_NE(text.find("scalex_mid = 1.25;"), std::string::npos);
		EXPECT_NE(text.find("lightcolor = 16755200;"), std::string::npos);
		ASSERT_NE(lump("ENDMAP"), nullptr);
		ASSERT_NE(lump("ZNODES"), nullptr);
		EXPECT_GT(lump("ZNODES")->Length(), 0);
	}
}

INSTANTIATE_TEST_SUITE_P(AllMapFormats, BiasedDoomFormatLaunchFixture,
		::testing::Values(MapFormat::doom, MapFormat::hexen, MapFormat::udmf),
		[](const ::testing::TestParamInfo<MapFormat> &info)
		{
			switch (info.param)
			{
				case MapFormat::doom:  return std::string("Doom");
				case MapFormat::hexen: return std::string("Hexen");
				case MapFormat::udmf:  return std::string("UDMF");
				default:               return std::string("Unknown");
			}
		});

TEST_F(TestMapFixture, TestMapPortWithoutResources)
{
	setPortName("boom");

	addIWAD();

	addPWAD();

	inst.loaded.levelName = "MAP14";

	std::vector<std::string> lines = testMapAndGetLines();
	std::vector<std::string> expected = {portPath.string(), "-iwad", gameWadPath.string(), "-file", editWadPath.string(), "-warp", "14"};
	ASSERT_EQ(lines, expected);
}

TEST_F(TestMapFixture, TestMapPortWithoutResourcesDoom1Map)
{
	setPortName("boom");

	addIWAD();

	addPWAD();

	inst.loaded.levelName = "E6M9";

	std::vector<std::string> lines = testMapAndGetLines();
	std::vector<std::string> expected = {portPath.string(), "-iwad", gameWadPath.string(), "-file", editWadPath.string(), "-warp", "6", "9"};
	ASSERT_EQ(lines, expected);
}

TEST_F(TestMapFixture, TestMapPortWithoutResourcesNonstandardMap)
{
	setPortName("boom");

	addIWAD();

	addPWAD();

	inst.loaded.levelName = "ZOMFG65";

	std::vector<std::string> lines = testMapAndGetLines();
	std::vector<std::string> expected = {portPath.string(), "-iwad", gameWadPath.string(), "-file", editWadPath.string(), "-warp", "65"};
	ASSERT_EQ(lines, expected);
}

TEST_F(TestMapFixture, TestMapPortWithoutResourcesBadMap)
{
	setPortName("boom");

	addIWAD();

	addPWAD();

	inst.loaded.levelName = "NOTHING";

	std::vector<std::string> lines = testMapAndGetLines();
	std::vector<std::string> expected = {portPath.string(), "-iwad", gameWadPath.string(), "-file", editWadPath.string()};
	ASSERT_EQ(lines, expected);
}

// TODO: add mac app bundle test

class PortExecutableDiscoveryFixture : public TempDirContext
{
protected:
	fs::path makeDirectory(const fs::path &name)
	{
		const fs::path directory = getSubPath(name);
		EXPECT_TRUE(FileMakeDir(directory));
		mDeleteList.push(directory);
		return directory;
	}

	fs::path makeRegularFile(const fs::path &directory, const char *name)
	{
		const fs::path file = directory / name;
		std::ofstream stream(file);
		EXPECT_TRUE(stream.is_open());
		stream << "test file" << std::endl;
		stream.close();
		mDeleteList.push(file);
		return file;
	}

	fs::path makeExecutable(const fs::path &directory, const char *name)
	{
		const fs::path executable = makeRegularFile(directory, name);
#ifndef _WIN32
		EXPECT_EQ(chmod(executable.string().c_str(), S_IRWXU), 0);
#endif
		return executable;
	}

	const char *biasedDoomExecutableName() const
	{
#ifdef _WIN32
		return "biaseddoom.exe";
#else
		return "biaseddoom";
#endif
	}

	const char *gzDoomExecutableName() const
	{
#ifdef _WIN32
		return "gzdoom.exe";
#else
		return "gzdoom";
#endif
	}
};

TEST_F(PortExecutableDiscoveryFixture, AutoDiscoverySupportMatchesKnownProfiles)
{
	EXPECT_TRUE(M_PortExecutableAutoDiscoverySupported("biaseddoom"));
	EXPECT_TRUE(M_PortExecutableAutoDiscoverySupported("GZDoom"));
	EXPECT_TRUE(M_PortExecutableAutoDiscoverySupported("ZDOOM"));
	EXPECT_FALSE(M_PortExecutableAutoDiscoverySupported("boom"));
	EXPECT_FALSE(M_PortExecutableAutoDiscoverySupported("vanilla"));
}

TEST_F(PortExecutableDiscoveryFixture, ValidationExplainsEmptyMissingAndDirectoryPaths)
{
	const PortExecutablePathValidation empty = M_ValidatePortExecutablePath({});
	EXPECT_FALSE(empty.valid());
	EXPECT_EQ(empty.issue, PortExecutablePathIssue::empty);
	EXPECT_FALSE(empty.message.empty());

	const PortExecutablePathValidation missing =
			M_ValidatePortExecutablePath(getSubPath("missing-engine"));
	EXPECT_FALSE(missing.valid());
	EXPECT_EQ(missing.issue, PortExecutablePathIssue::notFound);
	EXPECT_FALSE(missing.message.empty());

	const PortExecutablePathValidation directory =
			M_ValidatePortExecutablePath(makeDirectory("engine-directory"));
	EXPECT_FALSE(directory.valid());
	EXPECT_EQ(directory.issue, PortExecutablePathIssue::directory);
	EXPECT_FALSE(directory.message.empty());
}

TEST_F(PortExecutableDiscoveryFixture, ValidationAcceptsExecutableWithoutExeSuffix)
{
	const fs::path executable = makeExecutable(makeDirectory("engine"), "biaseddoom");
	const PortExecutablePathValidation validation =
			M_ValidatePortExecutablePath(executable);

	EXPECT_TRUE(validation.valid());
	EXPECT_EQ(validation.issue, PortExecutablePathIssue::none);
	EXPECT_FALSE(validation.message.empty());
}

#ifndef _WIN32
TEST_F(PortExecutableDiscoveryFixture, ValidationRejectsFileWithoutExecutePermission)
{
	const fs::path file = makeRegularFile(makeDirectory("engine"), "biaseddoom");
	ASSERT_EQ(chmod(file.string().c_str(), S_IRUSR | S_IWUSR), 0);
	const PortExecutablePathValidation validation =
			M_ValidatePortExecutablePath(file);

	EXPECT_FALSE(validation.valid());
	EXPECT_EQ(validation.issue, PortExecutablePathIssue::notExecutable);
	EXPECT_FALSE(validation.message.empty());
}
#endif

TEST_F(PortExecutableDiscoveryFixture, ConfiguredPathHasHighestPriority)
{
	const fs::path configured = makeExecutable(makeDirectory("configured"), "custom-engine");
	const fs::path pathCandidate = makeExecutable(makeDirectory("path"),
			biasedDoomExecutableName());
	const fs::path fallback = makeExecutable(makeDirectory("fallback"),
			biasedDoomExecutableName());
	PortExecutableSearchLocations locations = {
		.configuredPaths = {configured},
		.searchDirectories = {pathCandidate.parent_path()},
		.fallbackCandidates = {fallback}
	};

	ASSERT_EQ(M_FindPortExecutable("biaseddoom", locations), fs::absolute(configured));
}

TEST_F(PortExecutableDiscoveryFixture, TriesBothConfiguredEnginePaths)
{
	const fs::path configured = makeExecutable(makeDirectory("configured"), "custom-gzdoom");
	PortExecutableSearchLocations locations = {
		.configuredPaths = {getSubPath("missing-biaseddoom"), configured}
	};

	ASSERT_EQ(M_FindPortExecutable("biaseddoom", locations), fs::absolute(configured));
}

TEST_F(PortExecutableDiscoveryFixture, SearchesPathBeforeFallbackCandidates)
{
	const fs::path pathCandidate = makeExecutable(makeDirectory("path"),
			biasedDoomExecutableName());
	const fs::path fallback = makeExecutable(makeDirectory("fallback"),
			biasedDoomExecutableName());
	PortExecutableSearchLocations locations = {
		.configuredPaths = {getSubPath("missing")},
		.searchDirectories = {getSubPath("also-missing"), pathCandidate.parent_path()},
		.fallbackCandidates = {fallback},
		.recursiveRoots = {fallback.parent_path()}
	};

	ASSERT_EQ(M_FindPortExecutable("BIASEDDOOM", locations), fs::absolute(pathCandidate));
}

TEST_F(PortExecutableDiscoveryFixture, SkipsInvalidCandidates)
{
	const fs::path invalidDirectory = makeDirectory("not-an-executable");
	const fs::path fallback = makeExecutable(makeDirectory("fallback"),
			biasedDoomExecutableName());
	PortExecutableSearchLocations locations = {
		.configuredPaths = {invalidDirectory, getSubPath("missing")},
		.searchDirectories = {},
		.fallbackCandidates = {getSubPath("missing"), fallback}
	};

	ASSERT_EQ(M_FindPortExecutable("biaseddoom", locations), fs::absolute(fallback));
}

TEST_F(PortExecutableDiscoveryFixture, RecursivelyFindsBiasedDoomWithoutAssumingExeSuffix)
{
	const fs::path home = makeDirectory("home");
	makeDirectory(fs::path("home") / "code");
	makeDirectory(fs::path("home") / "code" / "ports");
	makeDirectory(fs::path("home") / "code" / "ports" / "BiasedDoom");
	makeDirectory(fs::path("home") / "code" / "ports" / "BiasedDoom" / "out");
	makeDirectory(fs::path("home") / "code" / "ports" / "BiasedDoom" / "out" / "linux");
	const fs::path executable = makeExecutable(
			getSubPath(fs::path("home") / "code" / "ports" / "BiasedDoom" / "out" / "linux"),
			biasedDoomExecutableName());
	PortExecutableSearchLocations locations = {.recursiveRoots = {home}};

#ifndef _WIN32
	EXPECT_TRUE(executable.extension().empty());
#endif
	ASSERT_EQ(M_FindPortExecutable("biaseddoom", locations), fs::absolute(executable));
}

TEST_F(PortExecutableDiscoveryFixture, BiasedDoomProfileFallsBackToRecursiveGZDoom)
{
	const fs::path home = makeDirectory("home");
	makeDirectory(fs::path("home") / "engines");
	makeDirectory(fs::path("home") / "engines" / "custom-build");
	const fs::path executable = makeExecutable(
			getSubPath(fs::path("home") / "engines" / "custom-build"),
			gzDoomExecutableName());
	PortExecutableSearchLocations locations = {.recursiveRoots = {home}};

	ASSERT_EQ(M_FindPortExecutable("biaseddoom", locations), fs::absolute(executable));
}

TEST_F(PortExecutableDiscoveryFixture, RecursiveSearchReportsProgressUntilFound)
{
	const fs::path root = makeDirectory("root");
	makeDirectory(fs::path("root") / "engines");
	makeDirectory(fs::path("root") / "engines" / "build");
	const fs::path executable = makeExecutable(
			getSubPath(fs::path("root") / "engines" / "build"),
			biasedDoomExecutableName());
	PortExecutableSearchLocations locations = {
		.configuredPaths = {getSubPath("missing-configured")},
		.fallbackCandidates = {getSubPath("missing-fallback")},
		.recursiveRoots = {root}
	};
	std::vector<PortExecutableSearchProgress> updates;
	PortExecutableSearchControl control;
	control.progress = [&](const PortExecutableSearchProgress &progress)
	{
		updates.push_back(progress);
	};

	EXPECT_EQ(M_FindPortExecutable("biaseddoom", locations, &control),
			fs::absolute(executable));
	ASSERT_FALSE(updates.empty());
	EXPECT_EQ(updates.front().phase,
			PortExecutableSearchPhase::configuredPath);
	EXPECT_TRUE(std::any_of(updates.begin(), updates.end(),
			[](const PortExecutableSearchProgress &progress)
			{
				return progress.phase ==
						PortExecutableSearchPhase::recursiveDirectory &&
						progress.directoriesScanned >= 3;
			}));
	for(size_t index = 1; index < updates.size(); ++index)
	{
		EXPECT_GE(updates[index].candidatesChecked,
				updates[index - 1].candidatesChecked);
		EXPECT_GE(updates[index].directoriesScanned,
				updates[index - 1].directoriesScanned);
	}
}

TEST_F(PortExecutableDiscoveryFixture, CancellationStopsRecursiveSearchPromptly)
{
	const fs::path root = makeDirectory("root");
	makeDirectory(fs::path("root") / "first");
	makeDirectory(fs::path("root") / "second");
	makeExecutable(getSubPath(fs::path("root") / "second"),
			biasedDoomExecutableName());
	PortExecutableSearchLocations locations = {.recursiveRoots = {root}};
	bool cancelled = false;
	size_t reportedDirectories = 0;
	PortExecutableSearchControl control;
	control.cancelled = [&]() { return cancelled; };
	control.progress = [&](const PortExecutableSearchProgress &progress)
	{
		if(progress.phase == PortExecutableSearchPhase::recursiveDirectory &&
				progress.directoriesScanned > 0)
		{
			reportedDirectories = progress.directoriesScanned;
			cancelled = true;
		}
	};

	EXPECT_FALSE(M_FindPortExecutable("biaseddoom", locations, &control));
	EXPECT_TRUE(cancelled);
	EXPECT_EQ(reportedDirectories, 1u);
}

TEST_F(PortExecutableDiscoveryFixture, ZDoomProfileAutomaticallyFindsGZDoom)
{
	const fs::path directory = makeDirectory("path");
	const fs::path executable = makeExecutable(directory, gzDoomExecutableName());
	PortExecutableSearchLocations locations = {.searchDirectories = {directory}};

	ASSERT_EQ(M_FindPortExecutable("zdoom", locations), fs::absolute(executable));
}

TEST_F(PortExecutableDiscoveryFixture, ProfileEnginePreferenceIsStable)
{
	const fs::path directory = makeDirectory("path");
	const fs::path biasedDoom = makeExecutable(directory, biasedDoomExecutableName());
	const fs::path gzDoom = makeExecutable(directory, gzDoomExecutableName());
	PortExecutableSearchLocations locations = {.searchDirectories = {directory}};

	EXPECT_EQ(M_FindPortExecutable("biaseddoom", locations), fs::absolute(biasedDoom));
	EXPECT_EQ(M_FindPortExecutable("zdoom", locations), fs::absolute(gzDoom));
}

#ifndef _WIN32
TEST_F(PortExecutableDiscoveryFixture, RecursiveSearchSkipsFilesWithoutExecutePermission)
{
	const fs::path root = makeDirectory("root");
	const fs::path invalid = makeExecutable(root, biasedDoomExecutableName());
	ASSERT_EQ(chmod(invalid.string().c_str(), S_IRUSR | S_IWUSR), 0);
	makeDirectory(fs::path("root") / "valid");
	const fs::path fallback = makeExecutable(getSubPath(fs::path("root") / "valid"),
			gzDoomExecutableName());
	PortExecutableSearchLocations locations = {.recursiveRoots = {root}};

	ASSERT_EQ(M_FindPortExecutable("biaseddoom", locations), fs::absolute(fallback));
}
#endif

TEST_F(PortExecutableDiscoveryFixture, DoesNotGuessExecutablesForUnrelatedProfiles)
{
	const fs::path configured = makeExecutable(makeDirectory("configured"), "custom-engine");
	const fs::path root = makeDirectory("root");
	makeExecutable(root, biasedDoomExecutableName());
	PortExecutableSearchLocations locations = {
		.configuredPaths = {configured},
		.recursiveRoots = {root}
	};

	ASSERT_FALSE(M_FindPortExecutable("boom", locations));
}

TEST_F(PortExecutableDiscoveryFixture, SystemLocationsRecursivelyIncludeHomeAndInstallDirectory)
{
	const fs::path installDirectory = makeDirectory("engine-install-root");
	const fs::path previousInstallDirectory = global::install_dir;
	global::install_dir = installDirectory;
	const PortExecutableSearchLocations locations =
			M_SystemPortExecutableSearchLocations("biaseddoom");
	global::install_dir = previousInstallDirectory;

	EXPECT_NE(std::find(locations.recursiveRoots.begin(), locations.recursiveRoots.end(),
			fs::absolute(installDirectory)), locations.recursiveRoots.end());

	const char *homeText = UTF8_getenv("HOME");
#ifdef _WIN32
	if(!homeText || !*homeText)
		homeText = UTF8_getenv("USERPROFILE");
#endif
	ASSERT_TRUE(homeText && *homeText);
	const fs::path home(reinterpret_cast<const char8_t *>(homeText));
	EXPECT_NE(std::find(locations.recursiveRoots.begin(), locations.recursiveRoots.end(), home),
			locations.recursiveRoots.end());
}
