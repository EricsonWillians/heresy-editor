//------------------------------------------------------------------------
//  PROJECT SESSION SIDECAR TESTS
//------------------------------------------------------------------------

#include "m_session.h"

#include "lib_file.h"
#include "testUtils/TempDirContext.hpp"

#include "gtest/gtest.h"

#include <fstream>

class ProjectSessionTest : public TempDirContext
{
protected:
	void TearDown() override
	{
		std::error_code error;
		fs::remove_all(mTempDir, error);
		EXPECT_FALSE(error);
		mTempDir.clear();
	}

	fs::path makeDirectory(const fs::path &relative)
	{
		const fs::path path = getSubPath(relative);
		fs::create_directories(path);
		return path;
	}

	fs::path makeIWAD(const fs::path &relative, bool iwad = true)
	{
		makeDirectory(relative.parent_path());
		const fs::path path = getSubPath(relative);
		std::ofstream stream(path, std::ios::binary);
		const char header[12] =
		{
			iwad ? 'I' : 'P', 'W', 'A', 'D',
			0, 0, 0, 0, 12, 0, 0, 0
		};
		stream.write(header, sizeof(header));
		return path;
	}
};


TEST_F(ProjectSessionTest, RoundTripsOnlyPortableHints)
{
	const fs::path package = getSubPath("bundle/projects/My Campaign.wad");
	makeDirectory("bundle/projects");
	const fs::path iwad = makeIWAD("bundle/iwads/DOOM2.WAD");

	ProjectSession source = M_MakeProjectSession(package, iwad, "doom2",
			"map02", "map07");
	ASSERT_EQ(source.iwadFile, "DOOM2.WAD");
	ASSERT_TRUE(source.iwadRelative.is_relative());
	M_SaveProjectSession(package, source);

	std::optional<ProjectSession> loaded = M_LoadProjectSession(package);
	ASSERT_TRUE(loaded);
	EXPECT_EQ(loaded->activeMap, "MAP02");
	EXPECT_EQ(loaded->navigatorMap, "MAP07");
	EXPECT_EQ(loaded->iwadGame, "doom2");
	EXPECT_EQ(loaded->iwadFile, "DOOM2.WAD");
	EXPECT_EQ(loaded->iwadRelative,
			(fs::path("..") / "iwads" / "DOOM2.WAD"));

	std::vector<uint8_t> bytes;
	ASSERT_TRUE(FileLoad(M_ProjectSessionPath(package), bytes));
	const std::string text(bytes.begin(), bytes.end());
	EXPECT_EQ(text.find(mTempDir.string()), std::string::npos);
	EXPECT_NE(text.find("iwad_relative"), std::string::npos);
}


TEST_F(ProjectSessionTest, RejectsCorruptDuplicateAndAbsoluteFields)
{
	const fs::path package = getSubPath("campaign.wad");
	const fs::path sidecar = M_ProjectSessionPath(package);
	auto write = [&sidecar](const char *contents)
	{
		std::ofstream stream(sidecar, std::ios::trunc);
		stream << contents;
	};

	write("session_version 1\nsession_version 1\nactive_map MAP01\n");
	EXPECT_FALSE(M_LoadProjectSession(package));
	write("session_version 1\niwad_relative /private/doom2.wad\n");
	EXPECT_FALSE(M_LoadProjectSession(package));
	write("session_version 2\nactive_map MAP01\n");
	EXPECT_FALSE(M_LoadProjectSession(package));
	write("active_map MAP01\n");
	EXPECT_FALSE(M_LoadProjectSession(package));
}


TEST_F(ProjectSessionTest, FailedValidationPreservesExistingSidecar)
{
	const fs::path package = getSubPath("campaign.wad");
	ProjectSession original;
	original.activeMap = "MAP01";
	original.iwadGame = "doom2";
	original.iwadFile = "doom2.wad";
	M_SaveProjectSession(package, original);

	ProjectSession oversized = original;
	oversized.iwadGame = std::string(70 * 1024, 'x');
	EXPECT_THROW(M_SaveProjectSession(package, oversized), std::runtime_error);

	std::optional<ProjectSession> restored = M_LoadProjectSession(package);
	ASSERT_TRUE(restored);
	EXPECT_EQ(restored->activeMap, original.activeMap);
	EXPECT_EQ(restored->iwadGame, original.iwadGame);
	EXPECT_EQ(restored->iwadFile, original.iwadFile);
}


TEST_F(ProjectSessionTest, ResolvesRelocatedBundleBeforeStaleKnownPath)
{
	const fs::path oldPackage = getSubPath("old/projects/campaign.wad");
	makeDirectory("old/projects");
	const fs::path oldIWAD = makeIWAD("old/iwads/doom2.wad");
	ProjectSession session = M_MakeProjectSession(oldPackage, oldIWAD,
			"doom2", "MAP01", "MAP02");

	const fs::path newPackage = getSubPath("relocated/projects/campaign.wad");
	makeDirectory("relocated/projects");
	const fs::path relocatedIWAD = makeIWAD("relocated/iwads/doom2.wad");

	IWADSearchLocations locations;
	std::optional<fs::path> resolved = M_ResolveProjectIWAD(newPackage,
			session, oldIWAD, locations);
	ASSERT_TRUE(resolved);
	EXPECT_EQ(*resolved, fs::absolute(relocatedIWAD).lexically_normal());
}


TEST_F(ProjectSessionTest, RejectsPwadHintAndFallsBackToDiscovery)
{
	const fs::path package = getSubPath("project/campaign.wad");
	makeDirectory("project");
	const fs::path pwad = makeIWAD("project/doom2.wad", false);
	const fs::path discoveredIWAD = makeIWAD("library/doom2.wad");

	ProjectSession session = M_MakeProjectSession(package, pwad, "doom2",
			"MAP01", "MAP01");
	IWADSearchLocations locations;
	locations.preferredDirectories = { discoveredIWAD.parent_path() };
	std::optional<fs::path> resolved = M_ResolveProjectIWAD(package, session,
			std::nullopt, locations);
	ASSERT_TRUE(resolved);
	EXPECT_EQ(*resolved, fs::absolute(discoveredIWAD).lexically_normal());
}
