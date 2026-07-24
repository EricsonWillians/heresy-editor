//------------------------------------------------------------------------
//  RESOURCE COLLISION AND LOAD-ORDER DIAGNOSTICS
//------------------------------------------------------------------------

#include "m_resource_diagnostics.h"

#include "WadData.h"
#include "w_wad.h"

#include <algorithm>
#include <map>
#include <set>

namespace
{

struct ResourceKey
{
	ResourceNamespaceKind nameSpace;
	SString name;

	bool operator<(const ResourceKey &other) const noexcept
	{
		if (nameSpace != other.nameSpace)
			return nameSpace < other.nameSpace;
		return name < other.name;
	}
};

std::optional<ResourceNamespaceKind> DiagnosticNamespace(WadNamespace nameSpace)
{
	switch (nameSpace)
	{
		case WadNamespace::Flats: return ResourceNamespaceKind::flat;
		case WadNamespace::Sprites: return ResourceNamespaceKind::sprite;
		case WadNamespace::TextureLumps: return ResourceNamespaceKind::texture;
		case WadNamespace::Global: return std::nullopt;
	}
	return std::nullopt;
}

fs::path ComparablePath(const fs::path &path)
{
	std::error_code error;
	fs::path absolute = fs::absolute(path, error);
	return (error ? path : absolute).lexically_normal();
}

bool PathsEqual(const fs::path &left, const fs::path &right)
{
	return SString(ComparablePath(left).u8string()).noCaseEqual(
			ComparablePath(right).u8string());
}

SString DuplicateResolution(ResourceNamespaceKind nameSpace,
		bool archivePathsKnown)
{
	if (nameSpace == ResourceNamespaceKind::sprite)
	{
		return "Duplicate sprite frames are ambiguous because frame and rotation "
				"selection can combine entries; no single winner is asserted.";
	}
	if (archivePathsKnown)
	{
		return "The later projected archive entry is the nominal editor winner. "
				"Invalid image data can still be skipped by the loader.";
	}
	return "The later directory entry is the nominal editor winner. The source "
			"does not retain a distinct archive path for each projected lump.";
}

struct LoadedSource
{
	std::shared_ptr<Wad_file> wad;
	SString label;
	fs::path path;
	size_t loadOrder = 0;
};

std::vector<LoadedSource> LoadedSources(const MasterDir &master)
{
	std::vector<LoadedSource> result;
	const std::vector<std::shared_ptr<Wad_file>> all = master.getAll();
	size_t resourceNumber = 0;
	for (size_t index = 0; index < all.size(); ++index)
	{
		LoadedSource source;
		source.wad = all[index];
		source.path = all[index]->PathName();
		source.loadOrder = index + 1;
		if (all[index] == master.gameWad())
			source.label = "IWAD";
		else if (all[index] == master.editWad())
			source.label = "Project";
		else
		{
			++resourceNumber;
			source.label = SString::printf("Resource %llu",
					static_cast<unsigned long long>(resourceNumber));
		}
		result.push_back(std::move(source));
	}
	return result;
}

} // namespace

size_t ResourceDiagnostics::warningCount() const noexcept
{
	return static_cast<size_t>(std::count_if(conflicts.begin(), conflicts.end(),
			[](const ResourceConflict &conflict)
			{
				return conflict.kind != ResourceConflictKind::loadOrderOverride;
			}));
}

size_t ResourceDiagnostics::overrideCount() const noexcept
{
	return static_cast<size_t>(std::count_if(conflicts.begin(), conflicts.end(),
			[](const ResourceConflict &conflict)
			{
				return conflict.kind == ResourceConflictKind::loadOrderOverride;
			}));
}

const char *M_ResourceNamespaceName(ResourceNamespaceKind nameSpace) noexcept
{
	switch (nameSpace)
	{
		case ResourceNamespaceKind::flat: return "Flat";
		case ResourceNamespaceKind::sprite: return "Sprite";
		case ResourceNamespaceKind::texture: return "Texture";
	}
	return "Resource";
}

const char *M_ResourceConflictKindName(ResourceConflictKind kind) noexcept
{
	switch (kind)
	{
		case ResourceConflictKind::projectedBasename:
			return "Projected-name collision";
		case ResourceConflictKind::duplicateWithinSource:
			return "Duplicate in source";
		case ResourceConflictKind::loadOrderOverride:
			return "Load-order override";
	}
	return "Resource conflict";
}

ResourceDiagnostics M_AnalyzeResourceConflicts(
		const Pk3PackageInventory &inventory, const MasterDir &master)
{
	ResourceDiagnostics diagnostics;
	std::map<ResourceKey, std::vector<const Pk3ProjectedResource *>> projected;
	for (const Pk3ProjectedResource &resource : inventory.projectedResources)
	{
		projected[{ resource.nameSpace, resource.editorName.asUpper() }].push_back(
				&resource);
	}

	std::set<ResourceKey> detailedProjectDuplicates;
	for (const auto &entry : projected)
	{
		if (entry.second.size() < 2)
			continue;
		ResourceConflict conflict;
		conflict.kind = ResourceConflictKind::projectedBasename;
		conflict.nameSpace = entry.first.nameSpace;
		conflict.editorName = entry.first.name;
		for (const Pk3ProjectedResource *resource : entry.second)
		{
			ResourceConflictParticipant participant;
			participant.label = "Archive entry";
			participant.packagePath = inventory.path;
			participant.archivePath = resource->path;
			conflict.participants.push_back(std::move(participant));
		}
		if (conflict.nameSpace != ResourceNamespaceKind::sprite)
			conflict.nominalWinner = conflict.participants.size() - 1;
		conflict.resolution = DuplicateResolution(conflict.nameSpace, true);
		diagnostics.conflicts.push_back(std::move(conflict));
		detailedProjectDuplicates.insert(entry.first);
	}

	const std::vector<LoadedSource> sources = LoadedSources(master);
	using SourceOccurrences = std::map<ResourceKey, size_t>;
	std::vector<SourceOccurrences> occurrences(sources.size());
	std::map<ResourceKey, std::vector<size_t>> sourcesByResource;
	for (size_t sourceIndex = 0; sourceIndex < sources.size(); ++sourceIndex)
	{
		for (const LumpRef &reference : sources[sourceIndex].wad->getDir())
		{
			const std::optional<ResourceNamespaceKind> nameSpace =
					DiagnosticNamespace(reference.ns);
			if (!nameSpace || !reference.lump || reference.lump->Name().empty())
				continue;
			++occurrences[sourceIndex][{ *nameSpace,
					reference.lump->Name().asUpper() }];
		}
		for (const auto &entry : occurrences[sourceIndex])
			sourcesByResource[entry.first].push_back(sourceIndex);
	}

	for (size_t sourceIndex = 0; sourceIndex < sources.size(); ++sourceIndex)
	{
		for (const auto &entry : occurrences[sourceIndex])
		{
			if (entry.second < 2)
				continue;
			if (PathsEqual(sources[sourceIndex].path, inventory.path) &&
					detailedProjectDuplicates.count(entry.first))
			{
				continue;
			}

			ResourceConflict conflict;
			conflict.kind = ResourceConflictKind::duplicateWithinSource;
			conflict.nameSpace = entry.first.nameSpace;
			conflict.editorName = entry.first.name;
			conflict.participants.push_back({ sources[sourceIndex].label,
					sources[sourceIndex].path, {}, sources[sourceIndex].loadOrder,
					entry.second });
			conflict.resolution = DuplicateResolution(conflict.nameSpace, false);
			diagnostics.conflicts.push_back(std::move(conflict));
		}
	}

	for (const auto &entry : sourcesByResource)
	{
		if (entry.second.size() < 2)
			continue;
		ResourceConflict conflict;
		conflict.kind = ResourceConflictKind::loadOrderOverride;
		conflict.nameSpace = entry.first.nameSpace;
		conflict.editorName = entry.first.name;
		for (size_t sourceIndex : entry.second)
		{
			conflict.participants.push_back({ sources[sourceIndex].label,
					sources[sourceIndex].path, {}, sources[sourceIndex].loadOrder,
					occurrences[sourceIndex].at(entry.first) });
		}
		conflict.nominalWinner = conflict.participants.size() - 1;
		conflict.resolution = "Sources are loaded in the displayed order. The last "
				"source is the nominal winner for this exact editor name; invalid "
				"images may be skipped and sprite rotations may combine sources.";
		diagnostics.conflicts.push_back(std::move(conflict));
	}

	std::sort(diagnostics.conflicts.begin(), diagnostics.conflicts.end(),
			[](const ResourceConflict &left, const ResourceConflict &right)
			{
				if (left.kind != right.kind)
					return left.kind < right.kind;
				if (left.nameSpace != right.nameSpace)
					return left.nameSpace < right.nameSpace;
				return left.editorName < right.editorName;
			});
	return diagnostics;
}
