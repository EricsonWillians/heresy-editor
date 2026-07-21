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

	ProjectMetadata parsed;
	for (const auto &field : source.serializedFields())
		EXPECT_TRUE(parsed.parseField(field.first, field.second));

	EXPECT_TRUE(parsed.isExplicit());
	EXPECT_EQ(parsed.version, source.version);
	EXPECT_EQ(parsed.name, source.name);
	EXPECT_EQ(parsed.package, source.package);
	EXPECT_EQ(parsed.campaign, source.campaign);
	EXPECT_EQ(parsed.mapSlots, source.mapSlots);
	EXPECT_FALSE(parsed.parseField("future_setting", "value"));
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

	EXPECT_TRUE(parsed.isExplicit());
	EXPECT_EQ(parsed.name, loading.project.name);
	EXPECT_EQ(parsed.package, loading.project.package);
	EXPECT_EQ(parsed.campaign, loading.project.campaign);
	EXPECT_EQ(parsed.mapSlots, loading.project.mapSlots);
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
