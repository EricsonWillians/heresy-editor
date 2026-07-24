//------------------------------------------------------------------------
//  PROJECT MODEL TESTS
//------------------------------------------------------------------------

#include "m_project.h"

#include "lib_file.h"
#include "m_loadsave.h"
#include "m_parse.h"
#include "m_streams.h"
#include "main.h"
#include "testUtils/TempDirContext.hpp"
#include "w_wad.h"

#include "gtest/gtest.h"

#include <algorithm>

class ProjectTest : public TempDirContext
{
protected:
	static void AddDoomMap(Wad_file &wad, const SString &name)
	{
		wad.AddLevel(name);
		wad.AddLump("THINGS");
		wad.AddLump("LINEDEFS");
		wad.AddLump("SIDEDEFS");
		wad.AddLump("VERTEXES");
		wad.AddLump("SECTORS");
	}
};

TEST_F(ProjectTest, FullIwadMetadataUsesCanonicalSlots)
{
	auto iwad = Wad_file::Open(getSubPath("doom2.wad"), WadOpenMode::write);
	ASSERT_TRUE(iwad);
	AddDoomMap(*iwad, "MAP01");
	AddDoomMap(*iwad, "MAP02");
	AddDoomMap(*iwad, "MAP03");

	ProjectMetadata project = M_NewWadProjectMetadata(
			getSubPath("My Campaign.wad"), CampaignMode::fullIwad, *iwad);

	ASSERT_TRUE(project.isExplicit());
	EXPECT_EQ(project.name, "My Campaign");
	EXPECT_EQ(project.package, ProjectPackage::wad);
	EXPECT_EQ(project.campaign, CampaignMode::fullIwad);
	ASSERT_EQ(project.mapSlots.size(), 3u);
	EXPECT_EQ(project.mapSlots[0], "MAP01");
	EXPECT_EQ(project.mapSlots[1], "MAP02");
	EXPECT_EQ(project.mapSlots[2], "MAP03");

	EXPECT_EQ(M_NextProjectMap(project, "map01"),
			std::optional<SString>("MAP02"));
	EXPECT_EQ(M_NextProjectMap(project, "MAP02"),
			std::optional<SString>("MAP03"));
	EXPECT_FALSE(M_NextProjectMap(project, "MAP03"));
	EXPECT_FALSE(M_NextProjectMap(project, "E1M1"));
}

TEST_F(ProjectTest, Pk3MetadataUsesTheSelectedBackend)
{
	auto iwad = Wad_file::Open(getSubPath("doom2.wad"), WadOpenMode::write);
	ASSERT_TRUE(iwad);
	AddDoomMap(*iwad, "MAP01");

	ProjectMetadata project = M_NewProjectMetadata(getSubPath("heresy.pk3"),
			ProjectPackage::pk3, CampaignMode::fullIwad, *iwad);
	EXPECT_TRUE(project.isExplicit());
	EXPECT_EQ(project.name, "heresy");
	EXPECT_EQ(project.package, ProjectPackage::pk3);
	ASSERT_EQ(project.mapSlots.size(), 1u);
	EXPECT_EQ(project.mapSlots.front(), "MAP01");
}

TEST_F(ProjectTest, MetadataFieldsRoundTrip)
{
	ProjectMetadata source;
	source.version = ProjectMetadata::CURRENT_VERSION;
	source.name = "A Heretical Campaign";
	source.package = ProjectPackage::wad;
	source.campaign = CampaignMode::custom;
	source.mapSlots = { "E1M1", "E1M2", "E1M9" };
	CampaignMapDefinition first;
	first.mapName = "E1M1";
	first.title = "The First Heresy";
	first.episode = "Ashes";
	first.normalExit = "E1M2";
	first.secretExit = "E1M9";
	CampaignMapDefinition second;
	second.mapName = "E1M2";
	second.title = "No Way Out";
	second.episode = "Ashes";
	second.normalExit = SString{};
	second.entryPoint = true;
	source.mapDefinitions = { first, second };

	ProjectMetadata parsed;
	for (const auto &field : source.serializedFields())
		EXPECT_TRUE(parsed.parseField(field.first, field.second));
	M_ReconcileCampaignMetadata(parsed);

	EXPECT_TRUE(parsed.isExplicit());
	EXPECT_EQ(parsed.version, source.version);
	EXPECT_EQ(parsed.name, source.name);
	EXPECT_EQ(parsed.package, source.package);
	EXPECT_EQ(parsed.campaign, source.campaign);
	EXPECT_EQ(parsed.mapSlots, source.mapSlots);
	ASSERT_EQ(parsed.mapDefinitions.size(), 2u);
	EXPECT_EQ(parsed.mapDefinitions[0].title, "The First Heresy");
	EXPECT_EQ(parsed.mapDefinitions[0].episode, "Ashes");
	EXPECT_EQ(parsed.mapDefinitions[0].normalExit,
			std::optional<SString>("E1M2"));
	EXPECT_EQ(parsed.mapDefinitions[0].secretExit,
			std::optional<SString>("E1M9"));
	ASSERT_TRUE(parsed.mapDefinitions[1].normalExit.has_value());
	EXPECT_TRUE(parsed.mapDefinitions[1].normalExit->empty());
	EXPECT_TRUE(parsed.mapDefinitions[1].entryPoint);
	EXPECT_EQ(M_CampaignEntryMaps(parsed),
			(std::vector<SString>{ "E1M1", "E1M2" }));
	EXPECT_FALSE(parsed.parseField("future_setting", "value"));
}


TEST(ProjectModel, ParsesAndValidatesCustomCampaignOrder)
{
	SString error;
	std::optional<std::vector<SString>> slots = M_ParseCustomMapSlots(
			"map01, MAP02  e1m9;secret_1", &error);
	ASSERT_TRUE(slots) << error.c_str();
	EXPECT_EQ(*slots, (std::vector<SString>{
			"MAP01", "MAP02", "E1M9", "SECRET_1" }));
	EXPECT_EQ(M_FormatCustomMapSlots(*slots),
			"MAP01, MAP02, E1M9, SECRET_1");

	EXPECT_FALSE(M_ParseCustomMapSlots("", &error));
	EXPECT_FALSE(M_ParseCustomMapSlots("MAP01 map01", &error));
	EXPECT_FALSE(M_ParseCustomMapSlots("TOO_LONG_1", &error));
	EXPECT_FALSE(M_ParseCustomMapSlots("MAP-01", &error));
	EXPECT_TRUE(M_IsValidProjectMapName("E1M1"));
	EXPECT_TRUE(M_IsValidProjectMapName("SECRET_1"));
	EXPECT_FALSE(M_IsValidProjectMapName("SECRET_01"));
}

TEST(ProjectModel, VersionTwoCampaignsKeepTheirImplicitEntryUntilOptIn)
{
	ProjectMetadata project;
	project.version = 2;
	project.package = ProjectPackage::wad;
	project.campaign = CampaignMode::custom;
	project.mapSlots = { "E1M1", "E1M2", "E2M1" };
	project.mapDefinitions = {
		{ "E1M1", "Start", "Episode One", std::nullopt, std::nullopt },
		{ "E1M2", "End", "Episode One", SString{}, std::nullopt }
	};
	M_ReconcileCampaignMetadata(project);
	EXPECT_EQ(project.version, 2);
	EXPECT_EQ(M_CampaignEntryMaps(project),
			(std::vector<SString>{ "E1M1" }));
	ASSERT_FALSE(project.serializedFields().empty());
	EXPECT_EQ(project.serializedFields().front(),
			(std::pair<SString, SString>{ "project_version", "2" }));
	CampaignMapDefinition redundantFirst = *project.mapDefinition("E1M1");
	redundantFirst.entryPoint = true;
	SString error;
	ASSERT_TRUE(M_SetCampaignMapDefinition(project, redundantFirst, &error))
			<< error.c_str();
	EXPECT_EQ(project.version, 2);
	EXPECT_FALSE(project.mapDefinition("E1M1")->entryPoint);

	CampaignMapDefinition secondEpisode;
	secondEpisode.mapName = "E2M1";
	secondEpisode.episode = "Episode Two";
	secondEpisode.entryPoint = true;
	ASSERT_TRUE(M_SetCampaignMapDefinition(project, secondEpisode, &error))
			<< error.c_str();
	EXPECT_EQ(project.version, ProjectMetadata::CURRENT_VERSION);
	EXPECT_EQ(M_CampaignEntryMaps(project),
			(std::vector<SString>{ "E1M1", "E2M1" }));

	secondEpisode.entryPoint = false;
	ASSERT_TRUE(M_SetCampaignMapDefinition(project, secondEpisode, &error))
			<< error.c_str();
	EXPECT_EQ(project.version, ProjectMetadata::CURRENT_VERSION);
	EXPECT_EQ(M_CampaignEntryMaps(project),
			(std::vector<SString>{ "E1M1" }));
	const auto fields = project.serializedFields();
	EXPECT_EQ(std::count_if(fields.begin(), fields.end(), [](const auto &field)
			{
				return field.first.startsWith("map_entry_");
			}), 0);
}

TEST(ProjectModel, RichCampaignMetadataControlsRoutesAndFollowsRenames)
{
	ProjectMetadata project;
	project.version = 1;
	project.package = ProjectPackage::wad;
	project.campaign = CampaignMode::custom;
	project.mapSlots = { "MAP01", "MAP02", "MAP03", "MAP31" };

	CampaignMapDefinition first;
	first.mapName = "map01";
	first.title = "Arrival";
	first.episode = "Episode One";
	first.normalExit = "map03";
	first.secretExit = "map31";
	SString error;
	ASSERT_TRUE(M_SetCampaignMapDefinition(project, first, &error))
			<< error.c_str();
	EXPECT_EQ(project.version, 2);
	EXPECT_EQ(M_NextProjectMap(project, "MAP01"),
			std::optional<SString>("MAP03"));
	EXPECT_EQ(M_ProjectExitTarget(project, "MAP01", CampaignExit::secret),
			std::optional<SString>("MAP31"));
	EXPECT_EQ(M_NextProjectMap(project, "MAP02"),
			std::optional<SString>("MAP03"));

	CampaignMapDefinition terminal;
	terminal.mapName = "MAP03";
	terminal.title = "Finale";
	terminal.normalExit = SString{};
	terminal.entryPoint = true;
	ASSERT_TRUE(M_SetCampaignMapDefinition(project, terminal, &error))
			<< error.c_str();
	EXPECT_EQ(project.version, ProjectMetadata::CURRENT_VERSION);
	EXPECT_FALSE(M_NextProjectMap(project, "MAP03"));

	ASSERT_TRUE(M_RenameProjectMapMetadata(project, "MAP03", "MAP30"));
	EXPECT_EQ(project.mapSlots,
			(std::vector<SString>{ "MAP01", "MAP02", "MAP30", "MAP31" }));
	EXPECT_EQ(M_NextProjectMap(project, "MAP01"),
			std::optional<SString>("MAP30"));
	const CampaignMapDefinition *renamed = project.mapDefinition("MAP30");
	ASSERT_TRUE(renamed);
	EXPECT_EQ(renamed->title, "Finale");
	ASSERT_TRUE(renamed->normalExit.has_value());
	EXPECT_TRUE(renamed->normalExit->empty());
	EXPECT_TRUE(renamed->entryPoint);
	EXPECT_EQ(M_CampaignEntryMaps(project),
			(std::vector<SString>{ "MAP01", "MAP30" }));

	const ProjectMetadata beforeCollision = project;
	EXPECT_FALSE(M_RenameProjectMapMetadata(project, "MAP30", "MAP31"));
	EXPECT_EQ(project.serializedFields(), beforeCollision.serializedFields());
}

TEST(ProjectModel, RejectsInvalidCampaignDefinitionsAndPrunesOrphans)
{
	ProjectMetadata project;
	project.version = ProjectMetadata::CURRENT_VERSION;
	project.package = ProjectPackage::wad;
	project.campaign = CampaignMode::custom;
	project.mapSlots = { "MAP01", "MAP02" };

	CampaignMapDefinition invalid;
	invalid.mapName = "MAP99";
	SString error;
	EXPECT_FALSE(M_SetCampaignMapDefinition(project, invalid, &error));
	EXPECT_TRUE(error.good());

	invalid.mapName = "MAP01";
	invalid.normalExit = "MAP99";
	EXPECT_FALSE(M_SetCampaignMapDefinition(project, invalid, &error));

	invalid.normalExit.reset();
	invalid.title = SString(std::string(81, 'x'));
	EXPECT_FALSE(M_SetCampaignMapDefinition(project, invalid, &error));
	invalid.title = "bad\tcontrol";
	EXPECT_FALSE(M_SetCampaignMapDefinition(project, invalid, &error));

	project.mapDefinitions = {
			{ "MAP01", "Valid", "Episode", SString("MAP02"), std::nullopt },
			{ "MAP99", "Orphan", "", std::nullopt, std::nullopt, true }
	};
	project.mapSlots = { "map01", "MAP02", "map01", "BAD-NAME" };
	M_ReconcileCampaignMetadata(project);
	EXPECT_EQ(project.mapSlots, (std::vector<SString>{ "MAP01", "MAP02" }));
	ASSERT_EQ(project.mapDefinitions.size(), 1u);
	EXPECT_EQ(project.mapDefinitions.front().mapName, "MAP01");
}

TEST_F(ProjectTest, NormalizesAndValidatesNewProjectDestinations)
{
	ProjectDestinationValidation wad = M_ValidateProjectDestination(
			getSubPath("new-campaign"), ProjectPackage::wad);
	ASSERT_TRUE(wad.valid()) << wad.message.c_str();
	EXPECT_EQ(wad.destination, getSubPath("new-campaign.wad"));

	ProjectDestinationValidation pk3 = M_ValidateProjectDestination(
			getSubPath("new-campaign.PK3"), ProjectPackage::pk3);
	ASSERT_TRUE(pk3.valid()) << pk3.message.c_str();
	EXPECT_EQ(pk3.destination, getSubPath("new-campaign.PK3"));

	EXPECT_EQ(M_ValidateProjectDestination({}, ProjectPackage::wad).issue,
			ProjectDestinationIssue::empty);
	EXPECT_EQ(M_ValidateProjectDestination(getSubPath("wrong.pk3"),
			ProjectPackage::wad).issue,
			ProjectDestinationIssue::wrongExtension);
	EXPECT_EQ(M_ValidateProjectDestination(getSubPath("new.wad"),
			ProjectPackage::none).issue,
			ProjectDestinationIssue::unsupportedPackage);
	EXPECT_EQ(M_ValidateProjectDestination(getSubPath("doom2.wad"),
			ProjectPackage::wad, true).issue,
			ProjectDestinationIssue::knownIwad);
	EXPECT_EQ(M_ValidateProjectDestination(
			getSubPath("missing/campaign.wad"), ProjectPackage::wad).issue,
			ProjectDestinationIssue::missingParent);
}

TEST_F(ProjectTest, RejectsAnExistingNewProjectDestination)
{
	const fs::path existing = getSubPath("existing.wad");
	auto wad = Wad_file::Open(existing, WadOpenMode::write);
	ASSERT_TRUE(wad);
	wad->writeToDisk();
	wad.reset();
	mDeleteList.push(existing);

	ProjectDestinationValidation validation = M_ValidateProjectDestination(
			existing, ProjectPackage::wad);
	EXPECT_EQ(validation.issue, ProjectDestinationIssue::alreadyExists);
	EXPECT_FALSE(validation.valid());

	validation = M_ValidateProjectDestination(existing / "nested.wad",
			ProjectPackage::wad);
	EXPECT_EQ(validation.issue, ProjectDestinationIssue::parentNotDirectory);
}

TEST_F(ProjectTest, SingleMapCampaignHasNoNextSlot)
{
	auto iwad = Wad_file::Open(getSubPath("doom.wad"), WadOpenMode::write);
	ASSERT_TRUE(iwad);
	AddDoomMap(*iwad, "E1M1");
	AddDoomMap(*iwad, "E1M2");

	ProjectMetadata project = M_NewWadProjectMetadata(getSubPath("one.wad"),
			CampaignMode::singleMap, *iwad);
	ASSERT_EQ(project.mapSlots.size(), 1u);
	EXPECT_EQ(project.mapSlots.front(), "E1M1");
	EXPECT_FALSE(M_NextProjectMap(project, "E1M1"));
}

TEST_F(ProjectTest, EmbeddedMetadataSurvivesPackageReopen)
{
	const fs::path projectPath = getSubPath("metadata.wad");
	auto wad = Wad_file::Open(projectPath, WadOpenMode::write);
	ASSERT_TRUE(wad);

	LoadingData loading;
	loading.gameName = "doom2";
	loading.portName = "vanilla";
	loading.project.version = ProjectMetadata::CURRENT_VERSION;
	loading.project.name = "Metadata Test";
	loading.project.package = ProjectPackage::wad;
	loading.project.campaign = CampaignMode::fullIwad;
	loading.project.mapSlots = { "MAP01", "MAP02" };
	loading.project.mapDefinitions = {
			{ "MAP01", "Entry Hall", "First Episode",
					SString("MAP02"), SString("MAP02") },
			{ "MAP02", "Last Stand", "First Episode",
					SString{}, std::nullopt }
	};
	loading.writeEurekaLump(*wad);
	wad->writeToDisk();
	wad.reset();
	mDeleteList.push(projectPath);

	auto reopened = Wad_file::Open(projectPath, WadOpenMode::read);
	ASSERT_TRUE(reopened);
	const Lump_c *metadataLump = reopened->FindLump(EUREKA_LUMP);
	ASSERT_TRUE(metadataLump);

	ProjectMetadata parsed;
	LumpInputStream stream(*metadataLump);
	SString line;
	while (stream.readLine(line))
	{
		TokenWordParse words(line, true);
		SString key;
		SString value;
		if (words.getNext(key) && words.getNext(value))
			parsed.parseField(key, value);
	}
	M_ReconcileCampaignMetadata(parsed);

	EXPECT_TRUE(parsed.isExplicit());
	EXPECT_EQ(parsed.name, loading.project.name);
	EXPECT_EQ(parsed.package, loading.project.package);
	EXPECT_EQ(parsed.campaign, loading.project.campaign);
	EXPECT_EQ(parsed.mapSlots, loading.project.mapSlots);
	ASSERT_EQ(parsed.mapDefinitions.size(), 2u);
	EXPECT_EQ(parsed.mapDefinitions[0].title, "Entry Hall");
	EXPECT_EQ(parsed.mapDefinitions[0].episode, "First Episode");
	EXPECT_EQ(parsed.mapDefinitions[0].secretExit,
			std::optional<SString>("MAP02"));
	ASSERT_TRUE(parsed.mapDefinitions[1].normalExit.has_value());
	EXPECT_TRUE(parsed.mapDefinitions[1].normalExit->empty());
}

TEST_F(ProjectTest, WadProjectPreservesMapsResourcesAndReadOnlyIwad)
{
	const fs::path iwadPath = getSubPath("doom2.wad");
	const fs::path projectPath = getSubPath("campaign.wad");

	auto writableIwad = Wad_file::Open(iwadPath, WadOpenMode::write);
	ASSERT_TRUE(writableIwad);
	AddDoomMap(*writableIwad, "MAP01");
	AddDoomMap(*writableIwad, "MAP02");
	writableIwad->writeToDisk();
	writableIwad.reset();
	mDeleteList.push(iwadPath);

	std::vector<uint8_t> iwadBefore;
	ASSERT_TRUE(FileLoad(iwadPath, iwadBefore));
	auto readOnlyIwad = Wad_file::Open(iwadPath, WadOpenMode::read);
	ASSERT_TRUE(readOnlyIwad);
	ASSERT_TRUE(readOnlyIwad->IsReadOnly());

	auto project = Wad_file::Open(projectPath, WadOpenMode::write);
	ASSERT_TRUE(project);
	Lump_c &resource = project->AddLump("HERESY");
	resource.Printf("preserve me");
	AddDoomMap(*project, "MAP01");
	project->writeToDisk();
	mDeleteList.push(projectPath);

	AddDoomMap(*project, "MAP02");
	project->writeToDisk();
	project.reset();

	auto reopened = Wad_file::Open(projectPath, WadOpenMode::append);
	ASSERT_TRUE(reopened);
	EXPECT_FALSE(reopened->IsIWAD());
	EXPECT_EQ(reopened->LevelFind("MAP01"), 0);
	EXPECT_EQ(reopened->LevelFind("MAP02"), 1);
	const Lump_c *preserved = reopened->FindLump("HERESY");
	ASSERT_TRUE(preserved);
	EXPECT_EQ(preserved->Length(), 11);

	std::vector<uint8_t> iwadAfter;
	ASSERT_TRUE(FileLoad(iwadPath, iwadAfter));
	EXPECT_EQ(iwadAfter, iwadBefore);
}

TEST_F(ProjectTest, CampaignStatusesPutConfiguredSlotsBeforeAdditionalMaps)
{
	auto package = Wad_file::Open(getSubPath("campaign.wad"),
			WadOpenMode::write);
	ASSERT_TRUE(package);
	AddDoomMap(*package, "MAP99");
	AddDoomMap(*package, "MAP01");
	AddDoomMap(*package, "MAP99");

	ProjectMetadata project;
	project.version = ProjectMetadata::CURRENT_VERSION;
	project.package = ProjectPackage::wad;
	project.campaign = CampaignMode::custom;
	project.mapSlots = { "map01", "MAP02", "MAP01" };
	project.mapDefinitions = {
			{ "MAP01", "The Beginning", "Episode One",
					SString("MAP02"), SString("MAP02") },
			{ "MAP02", "", "Episode Two", std::nullopt, std::nullopt, true }
	};

	std::vector<CampaignMapStatus> statuses = M_CampaignMapStatuses(project,
			*package, "map01", { "MAP01", "map99" });

	ASSERT_EQ(statuses.size(), 3u);
	EXPECT_EQ(statuses[0].name, "MAP01");
	EXPECT_TRUE(statuses[0].configured);
	EXPECT_TRUE(statuses[0].exists);
	EXPECT_TRUE(statuses[0].current);
	EXPECT_TRUE(statuses[0].dirty);
	EXPECT_FALSE(statuses[0].missing());
	EXPECT_EQ(statuses[0].title, "The Beginning");
	EXPECT_EQ(statuses[0].episode, "Episode One");
	EXPECT_EQ(statuses[0].normalExit, std::optional<SString>("MAP02"));
	EXPECT_EQ(statuses[0].secretExit, std::optional<SString>("MAP02"));
	EXPECT_TRUE(statuses[0].campaignEntry);

	EXPECT_EQ(statuses[1].name, "MAP02");
	EXPECT_TRUE(statuses[1].configured);
	EXPECT_FALSE(statuses[1].exists);
	EXPECT_TRUE(statuses[1].missing());
	EXPECT_FALSE(statuses[1].current);
	EXPECT_TRUE(statuses[1].campaignEntry);

	EXPECT_EQ(statuses[2].name, "MAP99");
	EXPECT_FALSE(statuses[2].configured);
	EXPECT_TRUE(statuses[2].exists);
	EXPECT_TRUE(statuses[2].dirty);
	EXPECT_FALSE(statuses[2].campaignEntry);
}
