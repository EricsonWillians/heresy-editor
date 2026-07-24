//------------------------------------------------------------------------
//
//  Eureka DOOM Editor
//
//  Copyright (C) 2021 Ioan Chera
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

#include "gtest/gtest.h"

#include "Instance.h"
#include "e_door.h"
#include "e_sector_design.h"
#include "m_game.h"
#include "m_loadsave.h"
#include "m_surface_transform.h"

#include <algorithm>

class MGameFixture : public TempDirContext
{
};

namespace
{

class SourceDefinitionPaths
{
public:
	SourceDefinitionPaths() :
		install_(global::install_dir),
		home_(global::home_dir),
		oldHome_(global::old_linux_home_and_cache_dir)
	{
		global::install_dir = fs::path(HERESY_TEST_SOURCE_DIR);
		global::home_dir.clear();
		global::old_linux_home_and_cache_dir.clear();
	}

	~SourceDefinitionPaths()
	{
		global::install_dir = install_;
		global::home_dir = home_;
		global::old_linux_home_and_cache_dir = oldHome_;
	}

private:
	fs::path install_;
	fs::path home_;
	fs::path oldHome_;
};

} // namespace

//=============================================================================
//
// TESTS
//
//=============================================================================


//
// Convenience operator
//
static bool operator == (const thingtype_t &type1, const thingtype_t &type2)
{
	return type1.group == type2.group && type1.flags == type2.flags &&
	type1.radius == type2.radius && type1.scale == type2.scale && type1.desc == type2.desc &&
	type1.sprite == type2.sprite && type1.color == type2.color && type1.args[0] == type2.args[0] &&
	type1.args[1] == type2.args[1] && type1.args[2] == type2.args[2] &&
	type1.args[3] == type2.args[3] && type1.args[4] == type2.args[4];
}

TEST(MGame, ConfigDataGetThingType)
{
	ConfigData config = {};
	thingtype_t type = {};
	type.group = 'a';
	type.flags = THINGDEF_LIT;
	type.radius = 16;
	type.scale = 1.0f;
	type.desc = "Bright spot";
	type.sprite = "BRIG";
	type.color = rgbMake(128, 0, 0);

	config.thing_types[111] = type;

	thingtype_t type2 = {};

	type2.group = 'b';
	type2.flags = 0;
	type2.radius = 16;
	type2.scale = 1.0f;
	type2.desc = "Dark spot";
	type2.sprite = "DARK";
	type2.color = rgbMake(0, 0, 128);

	config.thing_types[222] = type2;

	ASSERT_EQ(config.getThingType(111), type);
	ASSERT_EQ(config.getThingType(222), type2);
	ASSERT_EQ(config.getThingType(1).desc, UNKNOWN_TYPE_STRING);
	ASSERT_EQ(config.getThingType(0).desc, UNKNOWN_TYPE_STRING);
	ASSERT_EQ(config.getThingType(-1).desc, UNKNOWN_TYPE_STRING);
}

TEST_F(MGameFixture, MCollectKnownDefs)
{
	//
	// Helper to make dir and put it to stack
	//
	auto makeDir = [this](const fs::path &path)
	{
		ASSERT_TRUE(FileMakeDir(path));
		mDeleteList.push(path);
	};

	//
	// Helper to create empty file, complete with check
	//
	auto makeFile = [this](const fs::path &path)
	{
		std::ofstream os(path);
		ASSERT_TRUE(os.is_open());
		mDeleteList.push(path);
	};

	// We need the install and home dirs for this test
	const fs::path install_dir = getSubPath("install");
	makeDir(install_dir);

	const fs::path home_dir = getSubPath("home");
	makeDir(home_dir);

	// Now add some other folder inside both of them
	const fs::path folder = "conf";
	const fs::path home = home_dir / folder;
	const fs::path install = install_dir / folder;
	makeDir(install);
	makeDir(home);

	// Now add unrelated folders in each
	fs::path unrelated[2] = {install_dir / "unrel", home_dir / "ated"};
	for(const fs::path &unrel : unrelated)
		makeDir(unrel);

	// Now produce all sorts of files
	makeFile(install / "empty");
	makeFile(install / "hasugh.ugh");
	makeFile(install / "DOOM.ugh");
	makeFile(install / "Config.cfg");
	makeFile(install / "MooD.uGH");
	makeFile(home / "doom.UGH");
	makeFile(home / "Heretic.Ugh");
	makeFile(home / "Junk");
	makeFile(home / "extra.cfg");
	makeFile(home / ".hidden.ugh");	// skip for being hidden
	makeDir(home / "directory.ugh");	// skip for being dir
	makeFile(home / "directory.ugh" / "subfile.ugh");	// skip for being under dir
	makeDir(home / "folder");
	makeFile(home / "folder" / "subfile2.ugh");	// make sure to always skip subdir
	makeFile(home / "Extra.ugh");
	makeFile(unrelated[0] / "bandit.ugh");
	makeFile(unrelated[0] / "brigand.ugh");
	makeFile(unrelated[1] / "jackson.ugh");
	makeFile(unrelated[1] / "jordan.cfg");

	// Requirement: the last entry takes precedence and it's sorted.
	// Hidden and dirs are skipped
	auto expected = std::vector<SString>{"doom", "Extra", "hasugh", "Heretic", "MooD"};
	ASSERT_EQ(M_CollectKnownDefs({install_dir, home_dir}, folder), expected);
}

TEST(MGame, BiasedDoomProfileInheritsZDoomCompatibility)
{
	SourceDefinitionPaths sourceDefinitions;
	const PortInfo_c *profile = M_LoadPortInfo("biaseddoom");

	ASSERT_NE(profile, nullptr);
	EXPECT_TRUE(profile->SupportsGame("doom"));
	EXPECT_TRUE(profile->SupportsGame("doom2"));
	EXPECT_TRUE(profile->SupportsGame("heretic"));
	EXPECT_TRUE(profile->SupportsGame("hexen"));
	EXPECT_TRUE(profile->SupportsGame("strife"));
	const map_format_bitset_t expectedFormats =
			(1 << static_cast<int>(MapFormat::doom)) |
			(1 << static_cast<int>(MapFormat::hexen)) |
			(1 << static_cast<int>(MapFormat::udmf));
	EXPECT_EQ(profile->formats, expectedFormats);
	EXPECT_EQ(profile->udmf_namespace, "ZDoom");
}

TEST(MGame, BiasedDoomProfileCoversProceduralGeneratorIdentifiers)
{
	SourceDefinitionPaths sourceDefinitions;
	LoadingData loading;
	loading.gameName = "doom2";
	loading.portName = "biaseddoom";
	loading.levelFormat = MapFormat::udmf;
	loading.udmfNamespace = "ZDoom";

	auto parseVars = loading.prepareConfigVariables();
	ConfigData config;
	readConfiguration(parseVars, GAMES_DIR, loading.gameName, config);
	readConfiguration(parseVars, PORTS_DIR, loading.portName, config);
	EXPECT_EQ(config.features.udmf_surface_transforms,
			kSurfaceTransformWallUDMF | kSurfaceTransformPlaneUDMF);

	// Every thing the 4.15.7 generator can emit is a stock Doom/Doom II type.
	const int thingTypes[] = {
		1, 5, 6, 8, 9, 13, 15, 16, 17, 20, 35, 41, 43, 44, 45, 46, 48,
		55, 56, 57, 65, 66, 67, 69, 71, 82, 83, 85, 86,
		2001, 2002, 2003, 2004, 2006, 2007, 2008, 2010, 2011, 2012, 2013,
		2014, 2015, 2019, 2022, 2023, 2024, 2026, 2028, 2035, 2045, 2046,
		2047, 2048, 2049, 3001, 3002, 3003, 3004, 3005, 3006
	};
	for (int type : thingTypes)
		EXPECT_NE(config.thing_types.find(type), config.thing_types.end()) << type;

	// The generated doors, lifts, and exit use existing ZDoom specials.
	const int lineSpecials[] = {11, 12, 62, 243};
	for (int special : lineSpecials)
		EXPECT_NE(config.line_types.find(special), config.line_types.end()) << special;

	bool hasSecretFlag = false;
	for (const gensector_t &sector : config.gen_sectors)
		if (sector.options.empty() && sector.value == 1024)
			hasSecretFlag = true;
	EXPECT_TRUE(hasSecretFlag);
}

TEST_F(MGameFixture, ParseDefinitionFileThingFlags)
{
	// Tests both the population, the clearing and the same-position overriding
	std::unordered_map<SString, SString> parseVars;
	ParsePurpose purpose = ParsePurpose::normal;
	fs::path filename = getSubPath("test.ugh");

	std::ofstream stream(filename);
	ASSERT_TRUE(stream.is_open());
	mDeleteList.push(filename);

	stream << "thingflag 0 0 easy    on  0x01" << std::endl;
	stream << "thingflag 1 0 medium  on  0x02" << std::endl;
	stream << "thingflag 2 0 hard    on  0x02" << std::endl;
	stream << "clear thingflags"               << std::endl;
	stream << "thingflag 2 1 ambush  off 0x04" << std::endl;
	stream << "thingflag 3 1 friend  on-opposite 0x08" << std::endl;
	stream << "thingflag 2 1 doppel  on 0x10" << std::endl;

	stream.close();

	ConfigData config = {};

	M_ParseDefinitionFile(parseVars, purpose, &config, filename);

	ASSERT_EQ(config.thing_flags.size(), 2);

	// Also test that the same position gets overwritten
	ASSERT_EQ(config.thing_flags[0].row, 2);
	ASSERT_EQ(config.thing_flags[0].column, 1);
	ASSERT_EQ(config.thing_flags[0].value, 16);
	ASSERT_EQ(config.thing_flags[0].defaultSet, thingflag_t::DefaultMode::on);
	ASSERT_EQ(config.thing_flags[0].label, "doppel");

	ASSERT_EQ(config.thing_flags[1].row, 3);
	ASSERT_EQ(config.thing_flags[1].column, 1);
	ASSERT_EQ(config.thing_flags[1].value, 8);
	ASSERT_EQ(config.thing_flags[1].defaultSet, thingflag_t::DefaultMode::onOpposite);
	ASSERT_EQ(config.thing_flags[1].label, "friend");
}

TEST_F(MGameFixture, DoorPresetsPreserveOrderAndOverrideById)
{
	std::unordered_map<SString, SString> parseVars;
	const fs::path filename = getSubPath("door-presets.ugh");
	std::ofstream stream(filename);
	ASSERT_TRUE(stream.is_open());
	mDeleteList.push(filename);
	stream << "linegroup d \"Doors\"\n";
	stream << "line 1 d \"Inherited door\"\n";
	stream << "door_preset normal @special 1 encoded 0 0 0 0 0\n";
	stream << "door_preset fast \"Fast\" 117 encoded 0 0 0 0 0\n";
	stream << "door_preset NORMAL @special 12 use_repeat 0 64 150 0 0\n";
	stream << "line 12 d \"Final active door\"\n";
	stream.close();

	ConfigData config;
	M_ParseDefinitionFile(parseVars, ParsePurpose::normal, &config, filename);

	ASSERT_EQ(config.door_presets.size(), 2u);
	EXPECT_EQ(config.door_presets[0].id, "NORMAL");
	EXPECT_EQ(config.door_presets[0].label, "@special");
	EXPECT_EQ(config.door_presets[0].special, 12);
	EXPECT_EQ(config.door_presets[0].activation, DoorActivation::useRepeat);
	EXPECT_EQ(config.door_presets[0].args[1], 64);
	EXPECT_EQ(M_DoorPresetLabel(config, config.door_presets[0]),
			  "Final active door");
	EXPECT_EQ(config.door_presets[1].id, "fast");
}

TEST_F(MGameFixture, ClearingLinesAlsoClearsDoorPresets)
{
	std::unordered_map<SString, SString> parseVars;
	const fs::path filename = getSubPath("clear-door-presets.ugh");
	std::ofstream stream(filename);
	ASSERT_TRUE(stream.is_open());
	mDeleteList.push(filename);
	stream << "door_preset old \"Old\" 1 encoded 0 0 0 0 0\n";
	stream << "clear lines\n";
	stream << "door_preset current \"Current\" 12 use_once 0 16 0 0 0\n";
	stream.close();

	ConfigData config;
	M_ParseDefinitionFile(parseVars, ParsePurpose::normal, &config, filename);

	ASSERT_EQ(config.door_presets.size(), 1u);
	EXPECT_EQ(config.door_presets[0].id, "current");
}

TEST_F(MGameFixture, MalformedDoorPresetIsRejected)
{
	std::unordered_map<SString, SString> parseVars;
	const fs::path filename = getSubPath("bad-door-preset.ugh");
	std::ofstream stream(filename);
	ASSERT_TRUE(stream.is_open());
	mDeleteList.push(filename);
	stream << "door_preset bad \"Bad\" twelve use_sometimes 0 0 0 0 0\n";
	stream.close();

	ConfigData config;
	EXPECT_THROW(M_ParseDefinitionFile(parseVars, ParsePurpose::normal,
			&config, filename), ParseException);
}

TEST_F(MGameFixture, SectorActionPresetsPreserveOrderAndTypedTags)
{
	std::unordered_map<SString, SString> parseVars;
	const fs::path filename = getSubPath("sector-action-presets.ugh");
	std::ofstream stream(filename);
	ASSERT_TRUE(stream.is_open());
	mDeleteList.push(filename);
	stream << "sector_action_preset lift normal \"Normal\" 62 use_repeat @tag 16 105 0 0\n";
	stream << "sector_action_preset lift fast \"Fast\" 123 encoded 0 0 0 0 0\n";
	stream << "sector_action_preset lift NORMAL @special 62 use_once @tag 32 35 0 0\n";
	stream.close();

	ConfigData config;
	M_ParseDefinitionFile(parseVars, ParsePurpose::normal, &config, filename);

	ASSERT_EQ(config.sector_action_presets.size(), 2u);
	const SectorActionPreset &normal = config.sector_action_presets[0];
	EXPECT_EQ(normal.id, "NORMAL");
	EXPECT_EQ(normal.label, "@special");
	EXPECT_EQ(normal.activation, ActivationPolicy::useOnce);
	EXPECT_TRUE(normal.args[0].targetTag);
	EXPECT_EQ(normal.args[1].value, 32);
	EXPECT_EQ(config.sector_action_presets[1].id, "fast");
}

TEST_F(MGameFixture, ClearingLinesAlsoClearsSectorActionPresets)
{
	std::unordered_map<SString, SString> parseVars;
	const fs::path filename = getSubPath("clear-sector-actions.ugh");
	std::ofstream stream(filename);
	ASSERT_TRUE(stream.is_open());
	mDeleteList.push(filename);
	stream << "sector_action_preset lift old \"Old\" 21 encoded 0 0 0 0 0\n";
	stream << "clear lines\n";
	stream << "sector_action_preset lift current \"Current\" 62 use_repeat @tag 16 105 0 0\n";
	stream.close();

	ConfigData config;
	M_ParseDefinitionFile(parseVars, ParsePurpose::normal, &config, filename);

	ASSERT_EQ(config.sector_action_presets.size(), 1u);
	EXPECT_EQ(config.sector_action_presets[0].id, "current");
}

TEST_F(MGameFixture, MalformedSectorActionPresetsAreRejected)
{
	std::unordered_map<SString, SString> parseVars;
	const fs::path filename = getSubPath("bad-sector-action.ugh");
	std::ofstream stream(filename);
	ASSERT_TRUE(stream.is_open());
	mDeleteList.push(filename);
	stream << "sector_action_preset lift bad \"Bad\" 62 use_repeat 0 16 105 0 0\n";
	stream.close();

	ConfigData config;
	EXPECT_THROW(M_ParseDefinitionFile(parseVars, ParsePurpose::normal,
			&config, filename), ParseException);
}

TEST(MGame, SupportedConfigurationsDeclareSmartDoorPresets)
{
	SourceDefinitionPaths sourceDefinitions;
	struct Configuration
	{
		const char *game;
		const char *port;
		MapFormat format;
	};
	const Configuration configurations[] = {
		{"doom", "vanilla", MapFormat::doom},
		{"doom2", "vanilla", MapFormat::doom},
		{"freedm", "vanilla", MapFormat::doom},
		{"freedoom1", "vanilla", MapFormat::doom},
		{"freedoom2", "vanilla", MapFormat::doom},
		{"hacx", "vanilla", MapFormat::doom},
		{"harm1", "vanilla", MapFormat::doom},
		{"heretic", "vanilla", MapFormat::doom},
		{"hexen", "vanilla", MapFormat::hexen},
		{"plutonia", "vanilla", MapFormat::doom},
		{"strife1", "vanilla", MapFormat::doom},
		{"tnt", "vanilla", MapFormat::doom},
		{"doom2", "biaseddoom", MapFormat::doom},
		{"doom2", "biaseddoom", MapFormat::hexen},
		{"doom2", "biaseddoom", MapFormat::udmf},
		{"doom2", "eternity", MapFormat::udmf}
	};

	for (const Configuration &configuration : configurations)
	{
		LoadingData loading;
		loading.gameName = configuration.game;
		loading.portName = configuration.port;
		loading.levelFormat = configuration.format;
		auto parseVars = loading.prepareConfigVariables();
		ConfigData config;
		readConfiguration(parseVars, GAMES_DIR, loading.gameName, config);
		readConfiguration(parseVars, PORTS_DIR, loading.portName, config);

		std::vector<DoorPreset> available =
				M_AvailableDoorPresets(config, configuration.format);
		EXPECT_FALSE(available.empty())
				<< configuration.game << "/" << configuration.port << "/"
				<< static_cast<int>(configuration.format);
		EXPECT_TRUE(std::any_of(available.begin(), available.end(),
				[](const DoorPreset &preset)
				{
					return !preset.id.noCaseStartsWith("locked-");
				}))
				<< configuration.game << "/" << configuration.port << "/"
				<< static_cast<int>(configuration.format);
	}
}

TEST(MGame, SupportedConfigurationsDeclareSmartLiftPresets)
{
	SourceDefinitionPaths sourceDefinitions;
	struct Configuration
	{
		const char *game;
		const char *port;
		MapFormat format;
	};
	const Configuration configurations[] = {
		{"doom", "vanilla", MapFormat::doom},
		{"doom2", "vanilla", MapFormat::doom},
		{"freedm", "vanilla", MapFormat::doom},
		{"freedoom1", "vanilla", MapFormat::doom},
		{"freedoom2", "vanilla", MapFormat::doom},
		{"hacx", "vanilla", MapFormat::doom},
		{"harm1", "vanilla", MapFormat::doom},
		{"heretic", "vanilla", MapFormat::doom},
		{"hexen", "vanilla", MapFormat::hexen},
		{"plutonia", "vanilla", MapFormat::doom},
		{"strife1", "vanilla", MapFormat::doom},
		{"tnt", "vanilla", MapFormat::doom},
		{"doom2", "biaseddoom", MapFormat::doom},
		{"doom2", "biaseddoom", MapFormat::hexen},
		{"doom2", "biaseddoom", MapFormat::udmf},
		{"doom2", "eternity", MapFormat::udmf}
	};

	for (const Configuration &configuration : configurations)
	{
		LoadingData loading;
		loading.gameName = configuration.game;
		loading.portName = configuration.port;
		loading.levelFormat = configuration.format;
		auto parseVars = loading.prepareConfigVariables();
		ConfigData config;
		readConfiguration(parseVars, GAMES_DIR, loading.gameName, config);
		readConfiguration(parseVars, PORTS_DIR, loading.portName, config);

		std::vector<SectorActionPreset> available =
				M_AvailableSectorActionPresets(config,
						SectorActionKind::lift, configuration.format);
		EXPECT_FALSE(available.empty())
				<< configuration.game << "/" << configuration.port << "/"
				<< static_cast<int>(configuration.format);
		EXPECT_TRUE(std::any_of(available.begin(), available.end(),
				[](const SectorActionPreset &preset)
				{
					return preset.activation == ActivationPolicy::useRepeat ||
						   preset.id.findNoCase("repeat") != std::string::npos;
				}))
				<< configuration.game << "/" << configuration.port << "/"
				<< static_cast<int>(configuration.format);
	}
}

TEST(MGame, ZDoomFamilyUsesGameSpecificCanonicalDoorLocks)
{
	SourceDefinitionPaths sourceDefinitions;
	struct ExpectedLock
	{
		const char *game;
		const char *preset;
		int lock;
	};
	const ExpectedLock expectedLocks[] = {
		{"doom2", "locked-red", 129},
		{"heretic", "locked-green", 1},
		{"hexen", "locked-castle", 11},
		{"strife1", "locked-mine", 26}
	};

	for (const ExpectedLock &expected : expectedLocks)
	{
		LoadingData loading;
		loading.gameName = expected.game;
		loading.portName = "biaseddoom";
		loading.levelFormat = MapFormat::udmf;
		auto parseVars = loading.prepareConfigVariables();
		ConfigData config;
		readConfiguration(parseVars, GAMES_DIR, loading.gameName, config);
		readConfiguration(parseVars, PORTS_DIR, loading.portName, config);

		std::vector<DoorPreset> available =
				M_AvailableDoorPresets(config, loading.levelFormat);
		auto found = std::find_if(available.begin(), available.end(),
				[&](const DoorPreset &preset)
				{
					return preset.id.noCaseEqual(expected.preset);
				});
		ASSERT_NE(found, available.end()) << expected.game;
		EXPECT_EQ(found->special, 13);
		EXPECT_EQ(found->args[3], expected.lock);
	}
}

//
// Generalized line description tests
//

TEST(MGame, DisabledGeneralizedTypesGivesNothing)
{
	ConfigData config = {};
	ASSERT_TRUE(M_GeneralizedLineDescription(config, 123).empty());
}

TEST(MGame, EmptyGeneralizedInfoFields)
{
	ConfigData config = {
		.features = { .gen_types = 1 },
		.num_gen_linetypes = 1,
		.gen_linetypes = {
			{
				.base = 100,
				.length = 100,
				.fields{}
			}
		}
	};
	ASSERT_TRUE(M_GeneralizedLineDescription(config, 123).empty());
}

TEST(MGame, InsufficientGeneralizedTriggerFields)
{
	ConfigData config = {
		.features = { .gen_types = 1 },
		.num_gen_linetypes = 1,
		.gen_linetypes = {
			{
				.base = 100,
				.length = 100,
				.fields = {
					{ .keywords = {"W1", "WR", "S1", "SR", "G1", "GR", "DR"} }	// D1 missing
				}
			}
		}
	};
	ASSERT_TRUE(M_GeneralizedLineDescription(config, 123).empty());
}

TEST(MGame, GeneralizedHappyPath)
{
	ConfigData config = {
		.features = { .gen_types = 1 },
		.num_gen_linetypes = 1,
		.gen_linetypes = {
			{
				.base = 100,
				.length = 100,
				.name = "SomeType",
				.fields = {
					{ .keywords = {"W1", "WR", "S1", "SR", "G1", "GR", "D1", "DR"} }
				}
			}
		}
	};
	// 0x7b = 0x01111011 => 3 => SR
	ASSERT_EQ(M_GeneralizedLineDescription(config, 123), "SR GENTYPE: SomeType");
}

TEST(MGame, NotAGeneralizedType)
{
	ConfigData config = {
		.features = { .gen_types = 1 },
		.num_gen_linetypes = 1,
		.gen_linetypes = {
			{
				.base = 100,
				.length = 100,
				.name = "SomeType",
				.fields = {
					{ .keywords = {"W1", "WR", "S1", "SR", "G1", "GR", "D1", "DR"} }
				}
			}
		}
	};

	ASSERT_TRUE(M_GeneralizedLineDescription(config, 23).empty());
	ASSERT_TRUE(M_GeneralizedLineDescription(config, 201).empty());
}
