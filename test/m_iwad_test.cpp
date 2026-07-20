//------------------------------------------------------------------------
//  IWAD DISCOVERY TESTS
//------------------------------------------------------------------------
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 2
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
//  GNU General Public License for more details.
//

#include "testUtils/TempDirContext.hpp"

#include "m_iwad.h"

#include <fstream>

class IWADSearchFixture : public TempDirContext
{
protected:
	fs::path makeDirectory(const fs::path &relative)
	{
		fs::path current = mTempDir;
		for(const fs::path &component : relative)
		{
			current /= component;
			if(!fs::exists(current))
			{
				EXPECT_TRUE(fs::create_directory(current));
				mDeleteList.push(current);
			}
		}
		return current;
	}

	fs::path makeWAD(const fs::path &relative, bool isIWAD = true)
	{
		makeDirectory(relative.parent_path());
		const fs::path path = getSubPath(relative);
		std::ofstream stream(path, std::ios::binary);
		EXPECT_TRUE(stream.is_open());
		const char header[12] =
		{
			isIWAD ? 'I' : 'P', 'W', 'A', 'D',
			0, 0, 0, 0, 12, 0, 0, 0
		};
		stream.write(header, sizeof(header));
		stream.close();
		mDeleteList.push(path);
		return path;
	}

	fs::path makeTextFile(const fs::path &relative, const std::string &contents)
	{
		makeDirectory(relative.parent_path());
		const fs::path path = getSubPath(relative);
		std::ofstream stream(path);
		EXPECT_TRUE(stream.is_open());
		stream << contents;
		stream.close();
		mDeleteList.push(path);
		return path;
	}
};


TEST_F(IWADSearchFixture, PreferredDirectoriesAreOrderedAndCaseInsensitive)
{
	const fs::path preferred = makeDirectory("preferred");
	const fs::path secondary = makeDirectory("secondary");
	const fs::path preferredDoom = makeWAD("preferred/DoOm2.WaD");
	makeWAD("secondary/doom2.wad");
	makeWAD("preferred/heretic.wad", false);
	const fs::path validHeretic = makeWAD("secondary/HERETIC.WAD");

	IWADSearchLocations locations;
	locations.preferredDirectories = {preferred, secondary};
	const auto found = M_DiscoverIWADs({"doom2", "heretic"}, locations);

	ASSERT_EQ(found.size(), 2u);
	EXPECT_EQ(found.at("doom2"), preferredDoom);
	EXPECT_EQ(found.at("heretic"), validHeretic);
}


TEST_F(IWADSearchFixture, FindsSteamIWADsInMainAndManifestLibraries)
{
	const fs::path steamRoot = makeDirectory("steam");
	const fs::path secondLibrary = makeDirectory("Second Steam Library");
	const fs::path doom = makeWAD(
			"Second Steam Library/steamapps/common/Ultimate Doom/base/DOOM.WAD");
	const fs::path tnt = makeWAD(
			"Second Steam Library/steamapps/common/Ultimate Doom/base/tnt/TNT.WAD");
	const fs::path doom2 = makeWAD(
			"steam/steamapps/common/DOOM + DOOM II/base/DOOM2.WAD");

	const std::string secondLibraryText = secondLibrary.generic_string();
	makeTextFile("steam/steamapps/libraryfolders.vdf",
			"\"libraryfolders\"\n"
			"{\n"
			"  \"1\"\n"
			"  {\n"
			"    \"path\"    \"" + secondLibraryText + "\"\n"
			"  }\n"
			"}\n");

	IWADSearchLocations locations;
	locations.steamInstallations = {steamRoot};
	const auto found = M_DiscoverIWADs({"doom", "doom2", "tnt"}, locations);

	ASSERT_EQ(found.size(), 3u);
	EXPECT_EQ(found.at("doom"), doom);
	EXPECT_EQ(found.at("doom2"), doom2);
	EXPECT_EQ(found.at("tnt"), tnt);
}


TEST_F(IWADSearchFixture, AcceptsSteamCommonDirectoryAsSearchRoot)
{
	const fs::path common = makeDirectory("steam/steamapps/common");
	const fs::path plutonia = makeWAD(
			"steam/steamapps/common/Ultimate Doom/base/plutonia/PLUTONIA.WAD");

	IWADSearchLocations locations;
	locations.steamInstallations = {common};
	const auto found = M_DiscoverIWADs({"plutonia"}, locations);

	ASSERT_EQ(found.size(), 1u);
	EXPECT_EQ(found.at("plutonia"), plutonia);
}


TEST_F(IWADSearchFixture, ScansKnownGameCollectionsWithoutWalkingUnrelatedGames)
{
	const fs::path collection = makeDirectory("GOG Games");
	const fs::path heretic = makeWAD(
			"GOG Games/Heretic Shadow of the Serpent Riders/data/HERETIC.WAD");
	makeWAD("GOG Games/DOOM Eternal/base/DOOM.WAD");

	IWADSearchLocations locations;
	locations.gameCollections = {collection};
	const auto found = M_DiscoverIWADs({"doom", "heretic"}, locations);

	ASSERT_EQ(found.size(), 1u);
	EXPECT_EQ(found.at("heretic"), heretic);
	EXPECT_EQ(found.find("doom"), found.end());
}
