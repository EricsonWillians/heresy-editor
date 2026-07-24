//------------------------------------------------------------------------
//  CAMPAIGN GRAPH DIAGNOSTICS
//------------------------------------------------------------------------

#ifndef HERESY_M_CAMPAIGN_GRAPH_H
#define HERESY_M_CAMPAIGN_GRAPH_H

#include "m_project.h"

#include <vector>

class Wad_file;

struct CampaignRoute
{
	SString source;
	SString target;
	CampaignExit exit = CampaignExit::normal;
	bool followsCampaignOrder = false;
};

enum class CampaignGraphDiagnosticKind
{
	missingRouteTarget,
	unreachableMap,
	potentialCycle
};

struct CampaignGraphDiagnostic
{
	CampaignGraphDiagnosticKind kind =
			CampaignGraphDiagnosticKind::unreachableMap;
	std::vector<SString> maps;
	std::vector<CampaignRoute> routes;

	bool involves(const SString &mapName) const noexcept;
};

struct CampaignGraphAnalysis
{
	std::vector<SString> entryMaps;
	std::vector<CampaignRoute> routes;
	std::vector<SString> reachableMaps;
	std::vector<CampaignGraphDiagnostic> diagnostics;

	size_t count(CampaignGraphDiagnosticKind kind) const noexcept;
};

// Analyze the configured editor campaign without modifying project metadata or
// package contents. Routes use exactly the same effective-target rules as
// Create Next Map.
CampaignGraphAnalysis M_AnalyzeCampaignGraph(
		const ProjectMetadata &project, const Wad_file &package);

#endif // HERESY_M_CAMPAIGN_GRAPH_H
