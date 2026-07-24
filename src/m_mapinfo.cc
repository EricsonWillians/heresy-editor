//------------------------------------------------------------------------
//  MANAGED RUNTIME MAPINFO
//------------------------------------------------------------------------

#include "m_mapinfo.h"

#include "m_zip.h"
#include "w_wad.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>

namespace
{

constexpr std::array<const char *, 4> MAPINFO_NAMES = {
	"mapinfo", "zmapinfo", "umapinfo", "emapinfo"
};

std::string LowerASCII(std::string value)
{
	for (char &character : value)
	{
		if (character >= 'A' && character <= 'Z')
			character = static_cast<char>(character - 'A' + 'a');
	}
	return value;
}

std::string DeclarationName(const std::string &path)
{
	const size_t slash = path.find_last_of('/');
	std::string filename = path.substr(slash == std::string::npos ? 0 : slash + 1);
	const size_t dot = filename.find_last_of('.');
	if (dot != std::string::npos && dot > 0)
		filename.erase(dot);
	return LowerASCII(std::move(filename));
}

bool IsMapInfoDeclaration(const std::string &path)
{
	const std::string name = DeclarationName(path);
	return std::any_of(MAPINFO_NAMES.begin(), MAPINFO_NAMES.end(),
			[&name](const char *candidate)
			{
				return name == candidate;
			});
}

bool IsRootZMapInfo(const std::string &path)
{
	return path.find('/') == std::string::npos &&
			LowerASCII(path) == "zmapinfo";
}

bool HasManagedMarker(const std::vector<uint8_t> &data)
{
	const std::string marker(HERESY_MANAGED_ZMAPINFO_MARKER);
	return data.size() >= marker.size() &&
			std::equal(marker.begin(), marker.end(), data.begin());
}

SString EscapeQuoted(const SString &source)
{
	SString result;
	for (char character : std::string(source.c_str()))
	{
		if (character == '\\' || character == '"')
			result += '\\';
		result += character;
	}
	return result;
}

SString EndSequenceForGame(const SString &gameName)
{
	const SString game = gameName.asLower();
	if (game.noCaseEqual("strife") || game.noCaseStartsWith("strife"))
		return "EndGameS";
	if (game.noCaseEqual("doom2") || game.noCaseEqual("freedoom2") ||
			game.noCaseEqual("tnt") || game.noCaseEqual("plutonia") ||
			game.noCaseEqual("hacx"))
	{
		return "EndGameC";
	}
	return "EndGame1";
}

SString EpisodeName(const ProjectMetadata &project, const SString &entryMap,
		size_t entryIndex)
{
	const CampaignMapDefinition *definition = project.mapDefinition(entryMap);
	if (definition && definition->episode.good())
		return definition->episode;
	if (entryIndex == 0 && project.name.good())
		return project.name;
	if (definition && definition->title.good())
		return definition->title;
	return "Episode starting at " + entryMap;
}

SString ConflictDetail(const std::vector<SString> &declarations)
{
	SString result = "Runtime MAPINFO generation stopped because the package already "
			"contains declaration";
	result += declarations.size() == 1 ? ":" : "s:";
	for (const SString &declaration : declarations)
		result += "\n  " + declaration;
	result += "\n\nRemove or consolidate these declarations manually before generating. "
			"Heresy Editor never overwrites user-authored MAPINFO.";
	return result;
}

} // namespace

bool M_RuntimeMapInfoPortSupported(const SString &portName) noexcept
{
	return portName.noCaseEqual("biaseddoom") || portName.noCaseEqual("gzdoom") ||
			portName.noCaseEqual("zdoom");
}

std::optional<GeneratedRuntimeMapInfo> M_GenerateRuntimeMapInfo(
		const ProjectMetadata &sourceProject, const SString &portName,
		const SString &gameName, SString *error)
{
	if (error)
		error->clear();
	auto fail = [error](const char *message)
			-> std::optional<GeneratedRuntimeMapInfo>
	{
		if (error)
			*error = message;
		return std::nullopt;
	};

	ProjectMetadata project = sourceProject;
	M_ReconcileCampaignMetadata(project);
	if (!project.isExplicit())
		return fail("Runtime MAPINFO generation requires an explicit project.");
	if (!M_RuntimeMapInfoPortSupported(portName))
	{
		return fail("Runtime MAPINFO generation is available for BiasedDoom, "
				"GZDoom, and ZDoom project profiles.");
	}
	if (project.mapSlots.empty())
		return fail("The project has no configured campaign maps.");

	GeneratedRuntimeMapInfo result;
	for (const SString &slot : project.mapSlots)
	{
		const CampaignMapDefinition *definition = project.mapDefinition(slot);
		if (!definition || definition->title.empty())
		{
			result.warnings.push_back(slot +
					" has no title; its runtime title falls back to the map slot.");
		}
	}
	result.text = HERESY_MANAGED_ZMAPINFO_MARKER;
	result.text += "\n// Generated from Heresy Editor project campaign metadata.\n";
	result.text += "// Regenerate this declaration instead of editing it by hand.\n\n";
	for (const SString &warning : result.warnings)
		result.text += "// Title fallback: " + warning + "\n";
	if (!result.warnings.empty())
		result.text += "\n";
	result.text += "clearepisodes\n\n";

	const std::vector<SString> entries = M_CampaignEntryMaps(project);
	for (size_t index = 0; index < entries.size(); ++index)
	{
		result.text += "episode " + entries[index] + "\n{\n";
		result.text += "    name = \"" +
				EscapeQuoted(EpisodeName(project, entries[index], index)) + "\"\n";
		result.text += "}\n\n";
	}

	const SString ending = EndSequenceForGame(gameName);
	for (const SString &slot : project.mapSlots)
	{
		const CampaignMapDefinition *definition = project.mapDefinition(slot);
		SString title = definition ? definition->title : SString{};
		if (title.empty())
			title = slot;

		result.text += "map " + slot + " \"" + EscapeQuoted(title) + "\"\n{\n";
		const std::optional<SString> normal = M_ProjectExitTarget(project, slot,
				CampaignExit::normal);
		result.text += "    next = \"" + (normal ? *normal : ending) + "\"\n";
		const std::optional<SString> secret = M_ProjectExitTarget(project, slot,
				CampaignExit::secret);
		if (secret)
			result.text += "    secretnext = \"" + *secret + "\"\n";
		result.text += "}\n\n";
	}

	return result;
}

RuntimeMapInfoInspection M_InspectRuntimeMapInfo(
		const fs::path &packagePath, ProjectPackage packageType,
		const Wad_file &aggregate)
{
	RuntimeMapInfoInspection result;
	if (packageType == ProjectPackage::pk3)
	{
		std::shared_ptr<ZipArchive> archive = ZipArchive::Open(packagePath);
		if (!archive)
			throw std::runtime_error("The PK3 package could not be opened.");

		std::string managedPath;
		for (const std::string &entry : archive->entryNames())
		{
			if (!IsMapInfoDeclaration(entry))
				continue;
			result.declarations.emplace_back(entry);
			if (IsRootZMapInfo(entry))
				managedPath = entry;
		}
		if (result.declarations.empty())
			return result;
		if (result.declarations.size() == 1 && managedPath.size() > 0)
		{
			const std::vector<uint8_t> managedData = archive->readEntry(managedPath);
			if (HasManagedMarker(managedData))
			{
				result.state = RuntimeMapInfoState::managed;
				result.managedText = std::string(managedData.begin(), managedData.end());
				result.detail = "The existing Heresy Editor managed ZMAPINFO will be updated.";
				return result;
			}
		}
	}
	else if (packageType == ProjectPackage::wad)
	{
		const Lump_c *managed = nullptr;
		for (int index = 0; index < aggregate.NumLumps(); ++index)
		{
			const Lump_c *lump = aggregate.GetLump(index);
			if (!lump || !IsMapInfoDeclaration(lump->Name().c_str()))
				continue;
			result.declarations.push_back(lump->Name());
			if (lump->Name().noCaseEqual("ZMAPINFO"))
				managed = lump;
		}
		if (result.declarations.empty())
			return result;
		if (result.declarations.size() == 1 && managed &&
				HasManagedMarker(managed->getData()))
		{
			result.state = RuntimeMapInfoState::managed;
			result.managedText = std::string(managed->getData().begin(),
					managed->getData().end());
			result.detail = "The existing Heresy Editor managed ZMAPINFO will be updated.";
			return result;
		}
	}
	else
	{
		result.state = RuntimeMapInfoState::conflict;
		result.detail = "Runtime MAPINFO generation requires a WAD or PK3 project.";
		return result;
	}

	result.state = RuntimeMapInfoState::conflict;
	result.detail = ConflictDetail(result.declarations);
	return result;
}

RuntimeMapInfoFreshness M_RuntimeMapInfoFreshness(
		const RuntimeMapInfoInspection &inspection,
		const SString &generatedText) noexcept
{
	switch (inspection.state)
	{
		case RuntimeMapInfoState::absent:
			return RuntimeMapInfoFreshness::absent;
		case RuntimeMapInfoState::conflict:
			return RuntimeMapInfoFreshness::userAuthored;
		case RuntimeMapInfoState::managed:
			return inspection.managedText == generatedText ?
					RuntimeMapInfoFreshness::current : RuntimeMapInfoFreshness::stale;
	}
	return RuntimeMapInfoFreshness::userAuthored;
}

const char *M_RuntimeMapInfoFreshnessName(
		RuntimeMapInfoFreshness freshness) noexcept
{
	switch (freshness)
	{
		case RuntimeMapInfoFreshness::absent: return "not generated";
		case RuntimeMapInfoFreshness::current: return "current";
		case RuntimeMapInfoFreshness::stale: return "out of date";
		case RuntimeMapInfoFreshness::userAuthored: return "user-authored";
	}
	return "unknown";
}

void M_StoreManagedRuntimeMapInfo(Wad_file &aggregate,
		const SString &generatedText)
{
	const std::vector<uint8_t> generated(generatedText.begin(), generatedText.end());
	if (!HasManagedMarker(generated))
		throw std::runtime_error("Generated ZMAPINFO is missing its ownership marker.");

	Lump_c *lump = aggregate.FindLump("ZMAPINFO");
	if (!lump)
		lump = &aggregate.AddLump("ZMAPINFO");
	else
	{
		if (!HasManagedMarker(lump->getData()))
		{
			throw std::runtime_error(
					"Refusing to replace user-authored ZMAPINFO.");
		}
		lump->clearData();
	}
	if (generatedText.good())
		lump->Write(generatedText.c_str(), generatedText.length());
}
