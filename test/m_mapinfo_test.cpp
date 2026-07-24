//------------------------------------------------------------------------
//  MANAGED RUNTIME MAPINFO TESTS
//------------------------------------------------------------------------

#include "m_mapinfo.h"

#include "m_package.h"
#include "m_zip.h"
#include "testUtils/TempDirContext.hpp"
#include "w_wad.h"

#include "gtest/gtest.h"

namespace
{

ProjectMetadata CampaignProject(ProjectPackage package = ProjectPackage::wad)
{
	ProjectMetadata project;
	project.version = ProjectMetadata::CURRENT_VERSION;
	project.name = "The \"Long\" Road";
	project.package = package;
	project.campaign = CampaignMode::custom;
	project.mapSlots = { "MAP01", "MAP02", "MAP31" };

	CampaignMapDefinition first;
	first.mapName = "MAP01";
	first.title = "Arrival\\Departure";
	first.secretExit = SString("MAP31");
	project.mapDefinitions.push_back(first);

	CampaignMapDefinition second;
	second.mapName = "MAP02";
	second.normalExit = SString("");
	project.mapDefinitions.push_back(second);

	CampaignMapDefinition secret;
	secret.mapName = "MAP31";
	secret.title = "The Secret";
	secret.episode = "Secret Route";
	secret.entryPoint = true;
	secret.normalExit = SString("MAP02");
	project.mapDefinitions.push_back(secret);
	return project;
}

std::vector<uint8_t> Bytes(const char *text)
{
	return std::vector<uint8_t>(text, text + std::char_traits<char>::length(text));
}

} // namespace

TEST(RuntimeMapInfoTest, GeneratesEpisodesRoutesEndingsAndEscapedTitles)
{
	SString error;
	auto generated = M_GenerateRuntimeMapInfo(CampaignProject(), "biaseddoom",
			"doom2", &error);
	ASSERT_TRUE(generated) << error.c_str();
	EXPECT_TRUE(generated->text.startsWith(HERESY_MANAGED_ZMAPINFO_MARKER));
	EXPECT_NE(generated->text.find("clearepisodes"), SString::npos);
	EXPECT_NE(generated->text.find(
			"episode MAP01\n{\n    name = \"The \\\"Long\\\" Road\""),
			SString::npos);
	EXPECT_NE(generated->text.find(
			"episode MAP31\n{\n    name = \"Secret Route\""), SString::npos);
	EXPECT_NE(generated->text.find(
			"map MAP01 \"Arrival\\\\Departure\""), SString::npos);
	EXPECT_NE(generated->text.find("next = \"MAP02\""), SString::npos);
	EXPECT_NE(generated->text.find("secretnext = \"MAP31\""), SString::npos);
	EXPECT_NE(generated->text.find("next = \"EndGameC\""), SString::npos);
	EXPECT_EQ(generated->warnings.size(), 1u);
	EXPECT_NE(generated->warnings.front().find("MAP02 has no title"), SString::npos);
}

TEST(RuntimeMapInfoTest, SingleMapUsesGameEndingAndTitleFallback)
{
	ProjectMetadata project = CampaignProject();
	project.campaign = CampaignMode::singleMap;
	project.mapSlots = { "E1M1" };
	project.mapDefinitions.clear();
	auto generated = M_GenerateRuntimeMapInfo(project, "gzdoom", "heretic");
	ASSERT_TRUE(generated);
	EXPECT_NE(generated->text.find("map E1M1 \"E1M1\""), SString::npos);
	EXPECT_NE(generated->text.find("next = \"EndGame1\""), SString::npos);
	ASSERT_EQ(generated->warnings.size(), 1u);
}

TEST(RuntimeMapInfoTest, RejectsImplicitAndUnsupportedProjects)
{
	SString error;
	ProjectMetadata project;
	EXPECT_FALSE(M_GenerateRuntimeMapInfo(project, "gzdoom", "doom2", &error));
	EXPECT_NE(error.find("explicit project"), SString::npos);

	project = CampaignProject();
	EXPECT_FALSE(M_GenerateRuntimeMapInfo(project, "vanilla", "doom2", &error));
	EXPECT_NE(error.find("BiasedDoom"), SString::npos);
}

TEST(RuntimeMapInfoTest, ClassifiesManagedFreshnessWithoutTreatingUserDataAsStale)
{
	RuntimeMapInfoInspection inspection;
	EXPECT_EQ(M_RuntimeMapInfoFreshness(inspection, "generated"),
			RuntimeMapInfoFreshness::absent);
	EXPECT_STREQ(M_RuntimeMapInfoFreshnessName(
			RuntimeMapInfoFreshness::absent), "not generated");

	inspection.state = RuntimeMapInfoState::managed;
	inspection.managedText = "generated";
	EXPECT_EQ(M_RuntimeMapInfoFreshness(inspection, "generated"),
			RuntimeMapInfoFreshness::current);
	EXPECT_EQ(M_RuntimeMapInfoFreshness(inspection, "changed"),
			RuntimeMapInfoFreshness::stale);

	inspection.state = RuntimeMapInfoState::conflict;
	EXPECT_EQ(M_RuntimeMapInfoFreshness(inspection, "changed"),
			RuntimeMapInfoFreshness::userAuthored);
	EXPECT_STREQ(M_RuntimeMapInfoFreshnessName(
			RuntimeMapInfoFreshness::userAuthored), "user-authored");
}

class RuntimeMapInfoPackageTest : public TempDirContext
{
};

TEST_F(RuntimeMapInfoPackageTest, WadInspectionOnlyAcceptsSoleManagedDeclaration)
{
	const fs::path path = getSubPath("campaign.wad");
	auto wad = Wad_file::Open(path, WadOpenMode::write);
	ASSERT_TRUE(wad);
	EXPECT_EQ(M_InspectRuntimeMapInfo(path, ProjectPackage::wad, *wad).state,
			RuntimeMapInfoState::absent);

	M_StoreManagedRuntimeMapInfo(*wad,
			SString(HERESY_MANAGED_ZMAPINFO_MARKER) + "\nmanaged\n");
	const RuntimeMapInfoInspection managed = M_InspectRuntimeMapInfo(path,
			ProjectPackage::wad, *wad);
	EXPECT_EQ(managed.state, RuntimeMapInfoState::managed);
	EXPECT_EQ(managed.managedText,
			SString(HERESY_MANAGED_ZMAPINFO_MARKER) + "\nmanaged\n");

	wad->AddLump("MAPINFO").Printf("user authored\n");
	const RuntimeMapInfoInspection conflict = M_InspectRuntimeMapInfo(path,
			ProjectPackage::wad, *wad);
	EXPECT_EQ(conflict.state, RuntimeMapInfoState::conflict);
	EXPECT_EQ(conflict.declarations.size(), 2u);
	EXPECT_NE(conflict.detail.find("never overwrites"), SString::npos);
}

TEST_F(RuntimeMapInfoPackageTest, UserAuthoredWadZMapInfoIsAConflict)
{
	auto wad = Wad_file::Open(getSubPath("user.wad"), WadOpenMode::write);
	ASSERT_TRUE(wad);
	wad->AddLump("ZMAPINFO").Printf("map MAP01 \"User title\" {}\n");
	const RuntimeMapInfoInspection inspection = M_InspectRuntimeMapInfo(
			wad->PathName(), ProjectPackage::wad, *wad);
	EXPECT_EQ(inspection.state, RuntimeMapInfoState::conflict);
	ASSERT_EQ(inspection.declarations.size(), 1u);
	EXPECT_EQ(inspection.declarations.front(), "ZMAPINFO");
	EXPECT_THROW(M_StoreManagedRuntimeMapInfo(*wad,
			SString(HERESY_MANAGED_ZMAPINFO_MARKER) + "\nreplacement\n"),
			std::runtime_error);
	EXPECT_EQ(std::string(wad->FindLump("ZMAPINFO")->getData().begin(),
			wad->FindLump("ZMAPINFO")->getData().end()),
			"map MAP01 \"User title\" {}\n");
}

TEST_F(RuntimeMapInfoPackageTest, Pk3InspectionFindsNestedAndAlternateDeclarations)
{
	const fs::path path = getSubPath("conflict.pk3");
	auto archive = ZipArchive::Create(path);
	archive->setEntry("defs/ZMAPINFO.txt", Bytes("nested"));
	archive->setEntry("UMAPINFO", Bytes("user"));
	archive->writeToDisk();
	mDeleteList.push(path);

	auto aggregate = Wad_file::CreateVirtual(path, [](const Wad_file &) {});
	const RuntimeMapInfoInspection inspection = M_InspectRuntimeMapInfo(path,
			ProjectPackage::pk3, *aggregate);
	EXPECT_EQ(inspection.state, RuntimeMapInfoState::conflict);
	EXPECT_EQ(inspection.declarations.size(), 2u);
	EXPECT_NE(inspection.detail.find("defs/ZMAPINFO.txt"), SString::npos);
}

TEST_F(RuntimeMapInfoPackageTest, ManagedPk3EntryUpdatesAndSurvivesLaterSaves)
{
	const fs::path path = getSubPath("managed.pk3");
	auto backend = M_CreatePackageBackend(path, ProjectPackage::pk3);
	ASSERT_TRUE(backend);
	auto aggregate = backend->openEditable();
	ASSERT_TRUE(aggregate);
	aggregate->writeToDisk();

	auto generated = M_GenerateRuntimeMapInfo(CampaignProject(ProjectPackage::pk3),
			"gzdoom", "doom2");
	ASSERT_TRUE(generated);
	EXPECT_EQ(M_InspectRuntimeMapInfo(path, ProjectPackage::pk3, *aggregate).state,
			RuntimeMapInfoState::absent);
	M_StoreManagedRuntimeMapInfo(*aggregate, generated->text);
	aggregate->writeToDisk();
	mDeleteList.push(path);

	auto archive = ZipArchive::Open(path);
	ASSERT_TRUE(archive);
	ASSERT_TRUE(archive->contains("ZMAPINFO"));
	EXPECT_EQ(archive->readEntry("ZMAPINFO"),
			std::vector<uint8_t>(generated->text.c_str(),
					generated->text.c_str() + generated->text.length()));
	archive->setEntry("docs/untouched.bin", { 1, 2, 3, 4, 5 });
	archive->writeToDisk();

	auto reopened = M_OpenEditablePackage(path);
	ASSERT_TRUE(reopened);
	const RuntimeMapInfoInspection originalInspection = M_InspectRuntimeMapInfo(
			path, ProjectPackage::pk3, *reopened);
	EXPECT_EQ(originalInspection.state, RuntimeMapInfoState::managed);
	EXPECT_EQ(M_RuntimeMapInfoFreshness(originalInspection, generated->text),
			RuntimeMapInfoFreshness::current);
	ProjectMetadata updatedProject = CampaignProject(ProjectPackage::pk3);
	updatedProject.mapDefinition("MAP01")->title = "Updated Arrival";
	auto updated = M_GenerateRuntimeMapInfo(updatedProject, "gzdoom", "doom2");
	ASSERT_TRUE(updated);
	EXPECT_EQ(M_RuntimeMapInfoFreshness(originalInspection, updated->text),
			RuntimeMapInfoFreshness::stale);
	M_StoreManagedRuntimeMapInfo(*reopened, updated->text);
	reopened->writeToDisk();

	archive = ZipArchive::Open(path);
	ASSERT_TRUE(archive);
	EXPECT_EQ(archive->readEntry("ZMAPINFO"),
			std::vector<uint8_t>(updated->text.c_str(),
					updated->text.c_str() + updated->text.length()));
	EXPECT_EQ(archive->readEntry("docs/untouched.bin"),
			(std::vector<uint8_t>{ 1, 2, 3, 4, 5 }));
	const RuntimeMapInfoInspection updatedInspection = M_InspectRuntimeMapInfo(
			path, ProjectPackage::pk3, *reopened);
	EXPECT_EQ(M_RuntimeMapInfoFreshness(updatedInspection, updated->text),
			RuntimeMapInfoFreshness::current);
}

TEST_F(RuntimeMapInfoPackageTest, UserPk3ZMapInfoIsNeverManaged)
{
	const fs::path path = getSubPath("user.pk3");
	auto archive = ZipArchive::Create(path);
	archive->setEntry("zmapinfo", Bytes("user authored"));
	archive->writeToDisk();
	mDeleteList.push(path);
	auto aggregate = M_OpenEditablePackage(path);
	ASSERT_TRUE(aggregate);
	const RuntimeMapInfoInspection inspection = M_InspectRuntimeMapInfo(path,
			ProjectPackage::pk3, *aggregate);
	EXPECT_EQ(inspection.state, RuntimeMapInfoState::conflict);
	ASSERT_EQ(inspection.declarations.size(), 1u);
	EXPECT_EQ(inspection.declarations.front(), "zmapinfo");

	// Even a caller that ignores inspection cannot route a managed replacement
	// through the package backend over this user-owned declaration.
	EXPECT_THROW(M_StoreManagedRuntimeMapInfo(*aggregate,
			SString(HERESY_MANAGED_ZMAPINFO_MARKER) + "\nreplacement\n"),
			std::runtime_error);
	Lump_c *direct = aggregate->FindLump("ZMAPINFO");
	ASSERT_NE(direct, nullptr);
	direct->clearData();
	direct->Printf("%s\ndirect replacement\n", HERESY_MANAGED_ZMAPINFO_MARKER);
	EXPECT_THROW(aggregate->writeToDisk(), std::runtime_error);
	archive = ZipArchive::Open(path);
	ASSERT_TRUE(archive);
	EXPECT_EQ(archive->readEntry("zmapinfo"), Bytes("user authored"));
}
