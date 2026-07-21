//------------------------------------------------------------------------
//  PROJECT PACKAGE BACKEND TESTS
//------------------------------------------------------------------------

#include "m_package.h"

#include "lib_file.h"
#include "m_zip.h"
#include "testUtils/TempDirContext.hpp"
#include "w_wad.h"

#include "gtest/gtest.h"

class PackageBackendTest : public TempDirContext
{
protected:
	static void AddDoomMap(Wad_file &wad, const SString &name)
	{
		wad.AddLevel(name);
		wad.AddLump("THINGS");
		wad.AddLump("LINEDEFS");
		wad.AddLump("SIDEDEFS");
		wad.AddLump("VERTEXES");
		wad.AddLump("SEGS");
		wad.AddLump("SECTORS");
	}
};

TEST_F(PackageBackendTest, FactoryKeepsWadCompatibility)
{
	const fs::path path = getSubPath("ordinary.wad");
	auto backend = M_CreatePackageBackend(path, ProjectPackage::wad);
	ASSERT_TRUE(backend);
	EXPECT_EQ(backend->packageType(), ProjectPackage::wad);
	auto wad = backend->openEditable();
	ASSERT_TRUE(wad);
	AddDoomMap(*wad, "MAP01");
	wad->writeToDisk();
	mDeleteList.push(path);

	auto reopened = M_OpenEditablePackage(path);
	ASSERT_TRUE(reopened);
	EXPECT_FALSE(reopened->IsPackageBacked());
	EXPECT_EQ(reopened->LevelFind("MAP01"), 0);
}

TEST_F(PackageBackendTest, Pk3MapMetadataResourcesAndUnknownEntriesRoundTrip)
{
	const fs::path path = getSubPath("campaign.pk3");
	auto backend = M_CreatePackageBackend(path, ProjectPackage::pk3);
	ASSERT_TRUE(backend);
	EXPECT_EQ(backend->packageType(), ProjectPackage::pk3);
	auto edit = backend->openEditable();
	ASSERT_TRUE(edit);
	ASSERT_TRUE(edit->IsPackageBacked());
	AddDoomMap(*edit, "MAP01");
	Lump_c &metadata = edit->AddLump(EUREKA_LUMP);
	metadata.Printf("# Heresy Editor project info\nproject_package pk3\n");
	edit->writeToDisk();
	mDeleteList.push(path);

	auto zip = ZipArchive::Open(path);
	ASSERT_TRUE(zip);
	ASSERT_TRUE(zip->contains("maps/MAP01.wad"));
	ASSERT_TRUE(zip->contains("heresy/project.txt"));
	zip->setEntry("docs/untouched.txt", std::vector<uint8_t>(8192, 'U'));
	zip->setEntry("textures/STONE.png", { 0x89, 'P', 'N', 'G' });
	zip->writeToDisk();
	{
		auto verifyZip = ZipArchive::Open(path);
		ASSERT_TRUE(verifyZip);
		const std::vector<uint8_t> mapData = verifyZip->readEntry("maps/MAP01.wad");
		auto verifyMap = Wad_file::loadFromData("verify.wad", mapData,
				WadOpenMode::read);
		ASSERT_TRUE(verifyMap);
		ASSERT_GE(verifyMap->LevelFind("MAP01"), 0);
	}

	std::vector<uint8_t> unknownBefore =
			ZipArchive::Open(path)->readEntry("docs/untouched.txt");

	edit = M_OpenEditablePackage(path);
	ASSERT_TRUE(edit);
	ASSERT_EQ(edit->LevelCount(), 1);
	EXPECT_EQ(edit->LevelName(0), "MAP01");
	ASSERT_TRUE(edit->FindLump(EUREKA_LUMP));
	ASSERT_TRUE(edit->FindLump("STONE"));

	const int things = edit->LevelLookupLump(0, "THINGS");
	ASSERT_GE(things, 0);
	edit->GetLump(things)->clearData();
	edit->GetLump(things)->Printf("changed map data");
	edit->writeToDisk();

	zip = ZipArchive::Open(path);
	ASSERT_TRUE(zip);
	EXPECT_EQ(zip->readEntry("docs/untouched.txt"), unknownBefore);
	EXPECT_EQ(zip->readEntry("textures/STONE.png"),
			(std::vector<uint8_t>{ 0x89, 'P', 'N', 'G' }));

	edit = M_OpenEditablePackage(path);
	ASSERT_TRUE(edit);
	ASSERT_EQ(edit->LevelCount(), 1);
	const int reopenedThings = edit->LevelLookupLump(0, "THINGS");
	ASSERT_GE(reopenedThings, 0);
	const std::vector<byte> &thingsData = edit->GetLump(reopenedThings)->getData();
	EXPECT_EQ(std::string(thingsData.begin(), thingsData.end()), "changed map data");
}

TEST_F(PackageBackendTest, Pk3SupportsSequentialMapCreationAndDeletion)
{
	const fs::path path = getSubPath("maps.pk3");
	auto backend = M_CreatePackageBackend(path, ProjectPackage::pk3);
	auto edit = backend->openEditable();
	ASSERT_TRUE(edit);
	AddDoomMap(*edit, "MAP01");
	AddDoomMap(*edit, "MAP02");
	edit->writeToDisk();
	mDeleteList.push(path);

	edit = M_OpenEditablePackage(path);
	ASSERT_TRUE(edit);
	ASSERT_EQ(edit->LevelCount(), 2);
	EXPECT_GE(edit->LevelFind("MAP01"), 0);
	EXPECT_GE(edit->LevelFind("MAP02"), 0);
	edit->RemoveLevel(edit->LevelFind("MAP01"));
	edit->writeToDisk();

	auto zip = ZipArchive::Open(path);
	ASSERT_TRUE(zip);
	EXPECT_FALSE(zip->contains("maps/MAP01.wad"));
	EXPECT_TRUE(zip->contains("maps/MAP02.wad"));
}
