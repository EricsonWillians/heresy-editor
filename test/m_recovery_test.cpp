//------------------------------------------------------------------------
//  PROJECT AUTOSAVE RECOVERY STORE TESTS
//------------------------------------------------------------------------

#include "m_recovery.h"

#include "testUtils/TempDirContext.hpp"
#include "w_wad.h"

#include "gtest/gtest.h"

#include <fstream>

class RecoveryStoreTest : public TempDirContext
{
protected:
	void TearDown() override
	{
		std::error_code error;
		fs::remove_all(mTempDir, error);
		EXPECT_FALSE(error);
		mTempDir.clear();
	}

	fs::path packagePath() const
	{
		return getSubPath("campaign.wad");
	}

	void createContext(const fs::path &path, int marker = 0)
	{
		auto wad = Wad_file::Open(path, WadOpenMode::write);
		ASSERT_TRUE(wad);
		Lump_c &context = wad->AddLump(EUREKA_LUMP);
		context.Printf("# recovery context %d\n", marker);
		wad->writeToDisk();
	}

	void createMap(const fs::path &path, const SString &name, int marker)
	{
		auto wad = Wad_file::Open(path, WadOpenMode::write);
		ASSERT_TRUE(wad);
		Lump_c *header = wad->AddLevel(name);
		ASSERT_TRUE(header);
		header->Printf("%d", marker);
		wad->AddLump("THINGS");
		wad->AddLump("LINEDEFS");
		wad->AddLump("SIDEDEFS");
		wad->AddLump("VERTEXES");
		wad->AddLump("SECTORS");
		wad->writeToDisk();
	}

	void writeSnapshot(RecoveryStore &store, const SString &active, int marker)
	{
		const fs::path staging = store.beginSnapshot(packagePath());
		createContext(staging / "context.wad", marker);
		createMap(staging / "map-0.wad", active, marker);
		store.commitSnapshot(packagePath(), staging, active,
				{ { active, "map-0.wad" } });
	}
};

TEST_F(RecoveryStoreTest, CommitsAndLoadsValidatedSnapshot)
{
	createContext(packagePath());
	RecoveryStore store(getSubPath("recovery"));
	writeSnapshot(store, "MAP01", 1);

	std::optional<RecoverySnapshot> snapshot = store.latest(packagePath());
	ASSERT_TRUE(snapshot);
	EXPECT_EQ(snapshot->packagePath, fs::absolute(packagePath()).lexically_normal());
	EXPECT_EQ(snapshot->activeMap, "MAP01");
	EXPECT_FALSE(snapshot->packageChanged);
	ASSERT_EQ(snapshot->maps.size(), 1u);
	EXPECT_EQ(snapshot->maps[0].mapName, "MAP01");
	EXPECT_TRUE(fs::exists(snapshot->contextFile));
	EXPECT_TRUE(fs::exists(snapshot->maps[0].fileName));
}

TEST_F(RecoveryStoreTest, RotatesThreeGenerationsAndFallsBackFromCorruption)
{
	createContext(packagePath());
	RecoveryStore store(getSubPath("recovery"));
	writeSnapshot(store, "MAP01", 1);
	writeSnapshot(store, "MAP02", 2);
	writeSnapshot(store, "MAP03", 3);
	writeSnapshot(store, "MAP04", 4);

	const fs::path project = store.projectDirectory(packagePath());
	EXPECT_TRUE(fs::is_directory(project / "0"));
	EXPECT_TRUE(fs::is_directory(project / "1"));
	EXPECT_TRUE(fs::is_directory(project / "2"));
	EXPECT_FALSE(fs::exists(project / "3"));

	std::ofstream corrupt(project / "0" / "manifest.txt",
			std::ios::trunc);
	corrupt << "not a recovery manifest\n";
	corrupt.close();

	std::optional<RecoverySnapshot> fallback = store.latest(packagePath());
	ASSERT_TRUE(fallback);
	EXPECT_EQ(fallback->activeMap, "MAP03");
}

TEST_F(RecoveryStoreTest, DetectsPackageChangesAfterAutosave)
{
	createContext(packagePath());
	RecoveryStore store(getSubPath("recovery"));
	writeSnapshot(store, "MAP01", 1);
	const fs::file_time_type originalStamp = fs::last_write_time(packagePath());

	auto package = Wad_file::Open(packagePath(), WadOpenMode::append);
	ASSERT_TRUE(package);
	package->AddLump("CHANGED");
	package->writeToDisk();
	package.reset();
	// Simulate a filesystem with coarse timestamp resolution. Size must still
	// reveal the external package update.
	fs::last_write_time(packagePath(), originalStamp);

	std::optional<RecoverySnapshot> snapshot = store.latest(packagePath());
	ASSERT_TRUE(snapshot);
	EXPECT_TRUE(snapshot->packageChanged);
}

TEST_F(RecoveryStoreTest, DiscardsOnlyTheSelectedProject)
{
	createContext(packagePath());
	const fs::path otherPackage = getSubPath("other.wad");
	createContext(otherPackage);
	RecoveryStore store(getSubPath("recovery"));
	writeSnapshot(store, "MAP01", 1);

	const fs::path otherStaging = store.beginSnapshot(otherPackage);
	createContext(otherStaging / "context.wad");
	createMap(otherStaging / "map-0.wad", "E1M1", 2);
	store.commitSnapshot(otherPackage, otherStaging, "E1M1",
			{ { "E1M1", "map-0.wad" } });

	store.discard(packagePath());
	EXPECT_FALSE(store.latest(packagePath()));
	EXPECT_TRUE(store.latest(otherPackage));
}

TEST_F(RecoveryStoreTest, RejectsDuplicateMapEntries)
{
	createContext(packagePath());
	RecoveryStore store(getSubPath("recovery"));
	const fs::path staging = store.beginSnapshot(packagePath());
	createContext(staging / "context.wad");
	createMap(staging / "map-0.wad", "MAP01", 1);
	createMap(staging / "map-1.wad", "MAP01", 2);

	EXPECT_THROW(store.commitSnapshot(packagePath(), staging, "MAP01",
			{ { "MAP01", "map-0.wad" }, { "map01", "map-1.wad" } }),
			std::runtime_error);
	EXPECT_FALSE(store.latest(packagePath()));
}
