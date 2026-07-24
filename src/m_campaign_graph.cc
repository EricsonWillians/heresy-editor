//------------------------------------------------------------------------
//  CAMPAIGN GRAPH DIAGNOSTICS
//------------------------------------------------------------------------

#include "m_campaign_graph.h"

#include "w_wad.h"

#include <algorithm>
#include <functional>
#include <map>

namespace
{

bool ContainsMap(const std::vector<SString> &maps, const SString &mapName)
{
	return std::any_of(maps.begin(), maps.end(),
			[&mapName](const SString &candidate)
			{
				return candidate.noCaseEqual(mapName);
			});
}

bool ExplicitNormalRoute(const ProjectMetadata &project,
		const SString &mapName)
{
	const CampaignMapDefinition *definition = project.mapDefinition(mapName);
	return definition && definition->normalExit &&
			definition->normalExit->good();
}

} // namespace

bool CampaignGraphDiagnostic::involves(const SString &mapName) const noexcept
{
	return ContainsMap(maps, mapName);
}

size_t CampaignGraphAnalysis::count(
		CampaignGraphDiagnosticKind kind) const noexcept
{
	return static_cast<size_t>(std::count_if(diagnostics.begin(),
			diagnostics.end(), [kind](const CampaignGraphDiagnostic &diagnostic)
			{
				return diagnostic.kind == kind;
			}));
}

CampaignGraphAnalysis M_AnalyzeCampaignGraph(
		const ProjectMetadata &sourceProject, const Wad_file &package)
{
	CampaignGraphAnalysis analysis;
	ProjectMetadata project = sourceProject;
	M_ReconcileCampaignMetadata(project);
	if (!project.isExplicit() || project.mapSlots.empty())
		return analysis;

	analysis.entryMaps = M_CampaignEntryMaps(project);
	std::map<SString, size_t> mapIndices;
	for (size_t index = 0; index < project.mapSlots.size(); ++index)
		mapIndices.emplace(project.mapSlots[index], index);

	for (const SString &source : project.mapSlots)
	{
		if (const std::optional<SString> normal = M_ProjectExitTarget(project,
				source, CampaignExit::normal))
		{
			analysis.routes.push_back({ source, *normal, CampaignExit::normal,
					!ExplicitNormalRoute(project, source) });
		}
		if (const std::optional<SString> secret = M_ProjectExitTarget(project,
				source, CampaignExit::secret))
		{
			analysis.routes.push_back({ source, *secret, CampaignExit::secret,
					false });
		}
	}

	std::vector<std::vector<size_t>> adjacency(project.mapSlots.size());
	for (const CampaignRoute &route : analysis.routes)
	{
		auto source = mapIndices.find(route.source);
		auto target = mapIndices.find(route.target);
		if (source == mapIndices.end() || target == mapIndices.end())
			continue;
		adjacency[source->second].push_back(target->second);
		if (package.LevelFind(route.target) < 0)
		{
			CampaignGraphDiagnostic diagnostic;
			diagnostic.kind = CampaignGraphDiagnosticKind::missingRouteTarget;
			diagnostic.maps = { route.source };
			if (!route.target.noCaseEqual(route.source))
				diagnostic.maps.push_back(route.target);
			diagnostic.routes = { route };
			analysis.diagnostics.push_back(std::move(diagnostic));
		}
	}

	std::vector<bool> reached(project.mapSlots.size(), false);
	std::vector<size_t> pending;
	for (auto entry = analysis.entryMaps.rbegin();
			entry != analysis.entryMaps.rend(); ++entry)
	{
		auto found = mapIndices.find(*entry);
		if (found != mapIndices.end())
			pending.push_back(found->second);
	}
	while (!pending.empty())
	{
		const size_t current = pending.back();
		pending.pop_back();
		if (reached[current])
			continue;
		reached[current] = true;
		for (auto next = adjacency[current].rbegin();
				next != adjacency[current].rend(); ++next)
		{
			if (!reached[*next])
				pending.push_back(*next);
		}
	}
	for (size_t index = 0; index < project.mapSlots.size(); ++index)
	{
		if (reached[index])
		{
			analysis.reachableMaps.push_back(project.mapSlots[index]);
			continue;
		}
		CampaignGraphDiagnostic diagnostic;
		diagnostic.kind = CampaignGraphDiagnosticKind::unreachableMap;
		diagnostic.maps = { project.mapSlots[index] };
		analysis.diagnostics.push_back(std::move(diagnostic));
	}

	// Tarjan's algorithm produces one deterministic diagnostic for each
	// strongly connected component that can loop, including self-routes.
	const size_t unvisited = project.mapSlots.size();
	std::vector<size_t> indices(project.mapSlots.size(), unvisited);
	std::vector<size_t> lowLinks(project.mapSlots.size(), unvisited);
	std::vector<size_t> stack;
	std::vector<bool> onStack(project.mapSlots.size(), false);
	size_t nextIndex = 0;
	std::vector<std::vector<size_t>> cyclicComponents;
	std::function<void(size_t)> visit = [&](size_t node)
	{
		indices[node] = nextIndex;
		lowLinks[node] = nextIndex;
		++nextIndex;
		stack.push_back(node);
		onStack[node] = true;

		for (size_t target : adjacency[node])
		{
			if (indices[target] == unvisited)
			{
				visit(target);
				lowLinks[node] = std::min(lowLinks[node], lowLinks[target]);
			}
			else if (onStack[target])
			{
				lowLinks[node] = std::min(lowLinks[node], indices[target]);
			}
		}

		if (lowLinks[node] != indices[node])
			return;
		std::vector<size_t> component;
		for (;;)
		{
			const size_t member = stack.back();
			stack.pop_back();
			onStack[member] = false;
			component.push_back(member);
			if (member == node)
				break;
		}
		const bool selfCycle = component.size() == 1 &&
				std::find(adjacency[node].begin(), adjacency[node].end(), node) !=
						adjacency[node].end();
		if (component.size() > 1 || selfCycle)
		{
			std::sort(component.begin(), component.end());
			cyclicComponents.push_back(std::move(component));
		}
	};
	for (size_t node = 0; node < project.mapSlots.size(); ++node)
		if (indices[node] == unvisited)
			visit(node);
	std::sort(cyclicComponents.begin(), cyclicComponents.end(),
			[](const std::vector<size_t> &left,
					const std::vector<size_t> &right)
			{
				return left.front() < right.front();
			});

	for (const std::vector<size_t> &component : cyclicComponents)
	{
		CampaignGraphDiagnostic diagnostic;
		diagnostic.kind = CampaignGraphDiagnosticKind::potentialCycle;
		for (size_t member : component)
			diagnostic.maps.push_back(project.mapSlots[member]);
		for (const CampaignRoute &route : analysis.routes)
		{
			if (ContainsMap(diagnostic.maps, route.source) &&
					ContainsMap(diagnostic.maps, route.target))
			{
				diagnostic.routes.push_back(route);
			}
		}
		analysis.diagnostics.push_back(std::move(diagnostic));
	}

	return analysis;
}
