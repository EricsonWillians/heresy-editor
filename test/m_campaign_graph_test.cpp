//------------------------------------------------------------------------
//  CAMPAIGN GRAPH DIAGNOSTIC TESTS
//------------------------------------------------------------------------

#include "m_campaign_graph.h"

#include "lib_file.h"
#include "testUtils/TempDirContext.hpp"
#include "w_wad.h"

#include "gtest/gtest.h"

#include <algorithm>

namespace
{

void AddDoomMap(Wad_file &wad, const SString &name)
{
	wad.AddLevel(name);
	wad.AddLump("THINGS");
	wad.AddLump("LINEDEFS");
	wad.AddLump("SIDEDEFS");
	wad.AddLump("VERTEXES");
	wad.AddLump("SEGS");
	wad.AddLump("SECTORS");
}

const CampaignGraphDiagnostic *FindDiagnostic(
		const CampaignGraphAnalysis &analysis, CampaignGraphDiagnosticKind kind,
		const SString &mapName)
{
	auto found = std::find_if(analysis.diagnostics.begin(),
			analysis.diagnostics.end(), [&mapName, kind](const auto &diagnostic)
			{
				return diagnostic.kind == kind && diagnostic.involves(mapName);
			});
	return found == analysis.diagnostics.end() ? nullptr : &*found;
}

} // namespace

class CampaignGraphTest : public TempDirContext
{
};

TEST_F(CampaignGraphTest,
		FindsMissingTargetsUnreachableMapsAndPotentialCyclesReadOnly)
{
	const fs::path path = getSubPath("campaign.wad");
	auto package = Wad_file::Open(path, WadOpenMode::write);
	ASSERT_TRUE(package);
	for (const char *map : { "MAP01", "MAP03", "MAP04", "MAP05", "MAP06" })
		AddDoomMap(*package, map);
	package->writeToDisk();
	mDeleteList.push(path);

	ProjectMetadata project;
	project.version = ProjectMetadata::CURRENT_VERSION;
	project.package = ProjectPackage::wad;
	project.campaign = CampaignMode::custom;
	project.mapSlots = { "MAP01", "MAP02", "MAP03", "MAP04", "MAP05", "MAP06" };
	project.mapDefinitions = {
		{ "MAP01", "", "", std::nullopt, SString("MAP03") },
		{ "MAP02", "", "", SString{}, std::nullopt },
		{ "MAP03", "", "", SString("MAP01"), std::nullopt },
		{ "MAP04", "", "", SString("MAP05"), std::nullopt },
		{ "MAP05", "", "", SString("MAP04"), std::nullopt },
		{ "MAP06", "", "", SString{}, SString("MAP06") }
	};
	const auto metadataBefore = project.serializedFields();

	std::vector<uint8_t> before;
	ASSERT_TRUE(FileLoad(path, before));
	const CampaignGraphAnalysis analysis =
			M_AnalyzeCampaignGraph(project, *package);
	EXPECT_EQ(project.serializedFields(), metadataBefore);
	EXPECT_EQ(analysis.entryMaps, (std::vector<SString>{ "MAP01" }));
	EXPECT_EQ(analysis.reachableMaps,
			(std::vector<SString>{ "MAP01", "MAP02", "MAP03" }));
	EXPECT_EQ(analysis.count(CampaignGraphDiagnosticKind::missingRouteTarget),
			1u);
	EXPECT_EQ(analysis.count(CampaignGraphDiagnosticKind::unreachableMap), 3u);
	EXPECT_EQ(analysis.count(CampaignGraphDiagnosticKind::potentialCycle), 3u);

	const CampaignGraphDiagnostic *missing = FindDiagnostic(analysis,
			CampaignGraphDiagnosticKind::missingRouteTarget, "MAP02");
	ASSERT_TRUE(missing);
	ASSERT_EQ(missing->routes.size(), 1u);
	EXPECT_EQ(missing->routes[0].source, "MAP01");
	EXPECT_EQ(missing->routes[0].target, "MAP02");
	EXPECT_EQ(missing->routes[0].exit, CampaignExit::normal);
	EXPECT_TRUE(missing->routes[0].followsCampaignOrder);

	EXPECT_TRUE(FindDiagnostic(analysis,
			CampaignGraphDiagnosticKind::unreachableMap, "MAP04"));
	EXPECT_TRUE(FindDiagnostic(analysis,
			CampaignGraphDiagnosticKind::unreachableMap, "MAP05"));
	EXPECT_TRUE(FindDiagnostic(analysis,
			CampaignGraphDiagnosticKind::unreachableMap, "MAP06"));

	const CampaignGraphDiagnostic *entryCycle = FindDiagnostic(analysis,
			CampaignGraphDiagnosticKind::potentialCycle, "MAP01");
	ASSERT_TRUE(entryCycle);
	EXPECT_EQ(entryCycle->maps,
			(std::vector<SString>{ "MAP01", "MAP03" }));
	const CampaignGraphDiagnostic *detachedCycle = FindDiagnostic(analysis,
			CampaignGraphDiagnosticKind::potentialCycle, "MAP04");
	ASSERT_TRUE(detachedCycle);
	EXPECT_EQ(detachedCycle->maps,
			(std::vector<SString>{ "MAP04", "MAP05" }));
	const CampaignGraphDiagnostic *selfCycle = FindDiagnostic(analysis,
			CampaignGraphDiagnosticKind::potentialCycle, "MAP06");
	ASSERT_TRUE(selfCycle);
	EXPECT_EQ(selfCycle->maps, (std::vector<SString>{ "MAP06" }));

	std::vector<uint8_t> after;
	ASSERT_TRUE(FileLoad(path, after));
	EXPECT_EQ(after, before);
}

TEST_F(CampaignGraphTest, ReportsEveryNormalAndSecretMissingTarget)
{
	auto package = Wad_file::Open(getSubPath("missing-routes.wad"),
			WadOpenMode::write);
	ASSERT_TRUE(package);
	AddDoomMap(*package, "MAP01");

	ProjectMetadata project;
	project.version = ProjectMetadata::CURRENT_VERSION;
	project.package = ProjectPackage::wad;
	project.campaign = CampaignMode::custom;
	project.mapSlots = { "MAP01", "MAP02", "MAP03" };
	project.mapDefinitions = {
		{ "MAP01", "", "", SString("MAP02"), SString("MAP03") },
		{ "MAP02", "", "", SString{}, std::nullopt },
		{ "MAP03", "", "", SString{}, std::nullopt }
	};

	const CampaignGraphAnalysis analysis =
			M_AnalyzeCampaignGraph(project, *package);
	EXPECT_EQ(analysis.count(CampaignGraphDiagnosticKind::missingRouteTarget),
			2u);
	EXPECT_EQ(analysis.reachableMaps, project.mapSlots);
	EXPECT_EQ(analysis.count(CampaignGraphDiagnosticKind::unreachableMap), 0u);
	EXPECT_EQ(analysis.count(CampaignGraphDiagnosticKind::potentialCycle), 0u);
	ASSERT_EQ(analysis.diagnostics[0].routes.size(), 1u);
	ASSERT_EQ(analysis.diagnostics[1].routes.size(), 1u);
	EXPECT_EQ(analysis.diagnostics[0].routes[0].exit, CampaignExit::normal);
	EXPECT_FALSE(analysis.diagnostics[0].routes[0].followsCampaignOrder);
	EXPECT_EQ(analysis.diagnostics[1].routes[0].exit, CampaignExit::secret);
}

TEST_F(CampaignGraphTest, AdditionalEntriesStartIndependentEpisodes)
{
	auto package = Wad_file::Open(getSubPath("episodes.wad"), WadOpenMode::write);
	ASSERT_TRUE(package);
	for (const char *map : { "E1M1", "E1M2", "E2M1", "E2M2" })
		AddDoomMap(*package, map);

	ProjectMetadata project;
	project.version = ProjectMetadata::CURRENT_VERSION;
	project.package = ProjectPackage::wad;
	project.campaign = CampaignMode::custom;
	project.mapSlots = { "E1M1", "E1M2", "E2M1", "E2M2" };
	project.mapDefinitions = {
		{ "E1M2", "", "Episode One", SString{}, std::nullopt },
		{ "E2M1", "", "Episode Two", std::nullopt, std::nullopt, true }
	};

	ProjectMetadata legacy = project;
	legacy.version = 2;
	legacy.mapDefinition("E2M1")->entryPoint = false;
	const CampaignGraphAnalysis legacyAnalysis =
			M_AnalyzeCampaignGraph(legacy, *package);
	EXPECT_EQ(legacyAnalysis.entryMaps,
			(std::vector<SString>{ "E1M1" }));
	EXPECT_EQ(legacyAnalysis.count(
			CampaignGraphDiagnosticKind::unreachableMap), 2u);

	const CampaignGraphAnalysis analysis =
			M_AnalyzeCampaignGraph(project, *package);
	EXPECT_EQ(analysis.entryMaps,
			(std::vector<SString>{ "E1M1", "E2M1" }));
	EXPECT_EQ(analysis.reachableMaps, project.mapSlots);
	EXPECT_TRUE(analysis.diagnostics.empty());
}

TEST_F(CampaignGraphTest, AcceptsACompleteAcyclicCampaign)
{
	auto package = Wad_file::Open(getSubPath("clean.wad"), WadOpenMode::write);
	ASSERT_TRUE(package);
	for (const char *map : { "MAP01", "MAP02", "MAP03" })
		AddDoomMap(*package, map);
	EXPECT_TRUE(M_AnalyzeCampaignGraph(ProjectMetadata{}, *package).
			diagnostics.empty());

	ProjectMetadata project;
	project.version = ProjectMetadata::CURRENT_VERSION;
	project.package = ProjectPackage::wad;
	project.campaign = CampaignMode::custom;
	project.mapSlots = { "MAP01", "MAP02", "MAP03" };

	const CampaignGraphAnalysis analysis =
			M_AnalyzeCampaignGraph(project, *package);
	EXPECT_EQ(analysis.routes.size(), 2u);
	EXPECT_TRUE(analysis.routes[0].followsCampaignOrder);
	EXPECT_TRUE(analysis.routes[1].followsCampaignOrder);
	EXPECT_EQ(analysis.reachableMaps, project.mapSlots);
	EXPECT_TRUE(analysis.diagnostics.empty());
}
