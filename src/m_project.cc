//------------------------------------------------------------------------
//  PROJECT AND CAMPAIGN MODEL
//------------------------------------------------------------------------

#include "m_project.h"

#include "w_wad.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>
#include <system_error>

namespace
{

const char *PackageName(ProjectPackage package) noexcept
{
	switch (package)
	{
		case ProjectPackage::wad: return "wad";
		case ProjectPackage::pk3: return "pk3";
		default: return "none";
	}
}

const char *CampaignName(CampaignMode campaign) noexcept
{
	switch (campaign)
	{
		case CampaignMode::fullIwad: return "full_iwad";
		case CampaignMode::custom: return "custom";
		default: return "single_map";
	}
}

constexpr size_t MAX_CAMPAIGN_TEXT_LENGTH = 80;

bool ParseMapFieldName(const SString &key, const char *prefix,
		SString &mapName)
{
	if (!key.startsWith(prefix))
		return false;
	mapName = key.substr(strlen(prefix)).asUpper();
	return true;
}

bool ContainsMap(const std::vector<SString> &slots, const SString &mapName)
{
	return std::any_of(slots.begin(), slots.end(),
			[&mapName](const SString &slot)
			{
				return slot.noCaseEqual(mapName);
			});
}

CampaignMapDefinition &EnsureMapDefinition(ProjectMetadata &project,
		const SString &mapName)
{
	if (CampaignMapDefinition *existing = project.mapDefinition(mapName))
		return *existing;
	project.mapDefinitions.push_back({});
	project.mapDefinitions.back().mapName = mapName.asUpper();
	return project.mapDefinitions.back();
}

int RequiredCampaignMetadataVersion(const ProjectMetadata &project)
{
	if (project.mapDefinitions.empty())
		return 1;
	const bool hasAdditionalEntry = std::any_of(project.mapDefinitions.begin(),
			project.mapDefinitions.end(), [](const CampaignMapDefinition &definition)
			{
				return definition.entryPoint;
			});
	return hasAdditionalEntry ? 3 : 2;
}

} // namespace

const CampaignMapDefinition *ProjectMetadata::mapDefinition(
		const SString &mapName) const noexcept
{
	auto found = std::find_if(mapDefinitions.begin(), mapDefinitions.end(),
			[&mapName](const CampaignMapDefinition &definition)
			{
				return definition.mapName.noCaseEqual(mapName);
			});
	return found == mapDefinitions.end() ? nullptr : &*found;
}

CampaignMapDefinition *ProjectMetadata::mapDefinition(
		const SString &mapName) noexcept
{
	auto found = std::find_if(mapDefinitions.begin(), mapDefinitions.end(),
			[&mapName](const CampaignMapDefinition &definition)
			{
				return definition.mapName.noCaseEqual(mapName);
			});
	return found == mapDefinitions.end() ? nullptr : &*found;
}

bool ProjectMetadata::parseField(const SString &key, const SString &value)
{
	if (key == "project_version")
	{
		char *end = nullptr;
		long parsed = std::strtol(value.c_str(), &end, 10);
		version = (end && *end == '\0' && parsed > 0 && parsed <= 1000) ?
				static_cast<int>(parsed) : 0;
		return true;
	}

	if (key == "project_name")
	{
		name = value;
		return true;
	}

	if (key == "project_package")
	{
		if (value.noCaseEqual("wad"))
			package = ProjectPackage::wad;
		else if (value.noCaseEqual("pk3"))
			package = ProjectPackage::pk3;
		else
			package = ProjectPackage::none;
		return true;
	}

	if (key == "campaign_mode")
	{
		if (value.noCaseEqual("full_iwad"))
			campaign = CampaignMode::fullIwad;
		else if (value.noCaseEqual("custom"))
			campaign = CampaignMode::custom;
		else
			campaign = CampaignMode::singleMap;
		return true;
	}

	if (key == "map_slot")
	{
		SString slot = value.asUpper();
		const bool duplicate = std::any_of(mapSlots.begin(), mapSlots.end(),
				[&slot](const SString &existing)
				{
					return existing.noCaseEqual(slot);
				});
		if (M_IsValidProjectMapName(slot) && !duplicate)
			mapSlots.push_back(slot);
		return true;
	}

	SString mapName;
	if (ParseMapFieldName(key, "map_title_", mapName))
	{
		if (M_IsValidProjectMapName(mapName) && M_IsValidCampaignText(value))
			EnsureMapDefinition(*this, mapName).title = value;
		return true;
	}
	if (ParseMapFieldName(key, "map_episode_", mapName))
	{
		if (M_IsValidProjectMapName(mapName) && M_IsValidCampaignText(value))
			EnsureMapDefinition(*this, mapName).episode = value;
		return true;
	}
	if (ParseMapFieldName(key, "map_entry_", mapName))
	{
		if (M_IsValidProjectMapName(mapName))
		{
			if (value == "1" || value.noCaseEqual("true") ||
					value.noCaseEqual("yes"))
			{
				EnsureMapDefinition(*this, mapName).entryPoint = true;
			}
			else if (value == "0" || value.noCaseEqual("false") ||
					value.noCaseEqual("no"))
			{
				EnsureMapDefinition(*this, mapName).entryPoint = false;
			}
		}
		return true;
	}
	auto parseExit = [this, &mapName, &value](const SString &key,
			const char *prefix, bool secret)
	{
		if (!ParseMapFieldName(key, prefix, mapName))
			return false;
		if (!M_IsValidProjectMapName(mapName))
			return true;

		CampaignMapDefinition &definition = EnsureMapDefinition(*this, mapName);
		std::optional<SString> &target = secret ? definition.secretExit :
				definition.normalExit;
		if (value == "-")
			target = SString{};
		else if (M_IsValidProjectMapName(value))
			target = value.asUpper();
		return true;
	};
	if (parseExit(key, "map_next_", false) ||
			parseExit(key, "map_secret_", true))
	{
		return true;
	}

	return false;
}

std::vector<std::pair<SString, SString>> ProjectMetadata::serializedFields() const
{
	if (!isExplicit())
		return {};

	ProjectMetadata normalized = *this;
	M_ReconcileCampaignMetadata(normalized);
	const int serializedVersion = std::max(normalized.version,
			RequiredCampaignMetadataVersion(normalized));

	std::vector<std::pair<SString, SString>> fields;
	fields.emplace_back("project_version", SString(serializedVersion));
	fields.emplace_back("project_name", normalized.name);
	fields.emplace_back("project_package", PackageName(normalized.package));
	fields.emplace_back("campaign_mode", CampaignName(normalized.campaign));

	for (const SString &slot : normalized.mapSlots)
		fields.emplace_back("map_slot", slot);

	for (const SString &slot : normalized.mapSlots)
	{
		const CampaignMapDefinition *definition =
				normalized.mapDefinition(slot);
		if (!definition)
			continue;
		if (definition->title.good())
			fields.emplace_back("map_title_" + slot, definition->title);
		if (definition->episode.good())
			fields.emplace_back("map_episode_" + slot, definition->episode);
		if (definition->entryPoint)
			fields.emplace_back("map_entry_" + slot, "1");
		if (definition->normalExit)
			fields.emplace_back("map_next_" + slot,
					definition->normalExit->good() ? *definition->normalExit : "-");
		if (definition->secretExit && definition->secretExit->good())
			fields.emplace_back("map_secret_" + slot, *definition->secretExit);
	}

	return fields;
}

std::vector<SString> M_ProjectMapSlots(const Wad_file &iwad)
{
	std::vector<SString> result;
	result.reserve(iwad.LevelCount());

	for (int level = 0; level < iwad.LevelCount(); ++level)
		result.push_back(iwad.LevelName(level));

	return result;
}


bool M_IsValidProjectMapName(const SString &name) noexcept
{
	if (name.empty() || name.size() > 8)
		return false;
	return std::all_of(name.begin(), name.end(), [](char ch)
	{
		return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
				(ch >= '0' && ch <= '9') || ch == '_';
	});
}


std::optional<std::vector<SString>> M_ParseCustomMapSlots(
		const SString &text, SString *error)
{
	std::vector<SString> result;
	std::set<SString> seen;
	SString current;
	auto fail = [error](const SString &message)
	{
		if (error)
			*error = message;
		return std::optional<std::vector<SString>>{};
	};
	auto flush = [&]() -> bool
	{
		if (current.empty())
			return true;
		SString slot = current.asUpper();
		current.clear();
		if (!M_IsValidProjectMapName(slot))
		{
			if (error)
				*error = SString::printf("Invalid map slot '%s'. Use 1-8 letters, "
						"digits, or underscores.", slot.c_str());
			return false;
		}
		if (!seen.insert(slot).second)
		{
			if (error)
				*error = SString::printf("Map slot '%s' appears more than once.",
						slot.c_str());
			return false;
		}
		result.push_back(slot);
		return true;
	};

	for (unsigned char ch : text)
	{
		if (std::isspace(ch) || ch == ',' || ch == ';')
		{
			if (!flush())
				return {};
		}
		else
			current += static_cast<char>(ch);
	}
	if (!flush())
		return {};
	if (result.empty())
		return fail("A custom campaign needs at least one map slot.");
	if (result.size() > 256)
		return fail("A custom campaign cannot contain more than 256 map slots.");
	return result;
}


SString M_FormatCustomMapSlots(const std::vector<SString> &slots)
{
	SString result;
	for (const SString &slot : slots)
	{
		if (result.good())
			result += ", ";
		result += slot.asUpper();
	}
	return result;
}

bool M_IsValidCampaignText(const SString &text) noexcept
{
	if (text.size() > MAX_CAMPAIGN_TEXT_LENGTH)
		return false;
	return std::all_of(text.begin(), text.end(), [](unsigned char character)
	{
		return character >= 0x20 && character != 0x7f;
	});
}

bool M_ValidateCampaignMapDefinition(const ProjectMetadata &project,
		const CampaignMapDefinition &definition, SString *error)
{
	auto fail = [error](const SString &message)
	{
		if (error)
			*error = message;
		return false;
	};

	if (!M_IsValidProjectMapName(definition.mapName) ||
			!ContainsMap(project.mapSlots, definition.mapName))
	{
		return fail("Campaign details must belong to a configured map slot.");
	}
	if (!M_IsValidCampaignText(definition.title))
		return fail("Map titles must be at most 80 characters and contain no control characters.");
	if (!M_IsValidCampaignText(definition.episode))
		return fail("Episode names must be at most 80 characters and contain no control characters.");

	auto validateTarget = [&project, &fail](const std::optional<SString> &target,
			const char *kind)
	{
		if (!target || target->empty())
			return true;
		if (!M_IsValidProjectMapName(*target) ||
				!ContainsMap(project.mapSlots, *target))
		{
			return fail(SString::printf("The %s exit target '%s' is not a configured map slot.",
					kind, target->c_str()));
		}
		return true;
	};
	return validateTarget(definition.normalExit, "normal") &&
			validateTarget(definition.secretExit, "secret");
}

bool M_SetCampaignMapDefinition(ProjectMetadata &project,
		const CampaignMapDefinition &definition, SString *error)
{
	CampaignMapDefinition normalized = definition;
	normalized.mapName = normalized.mapName.asUpper();
	if (normalized.normalExit && normalized.normalExit->good())
		normalized.normalExit = normalized.normalExit->asUpper();
	if (normalized.secretExit && normalized.secretExit->good())
		normalized.secretExit = normalized.secretExit->asUpper();
	if (normalized.secretExit && normalized.secretExit->empty())
		normalized.secretExit.reset();
	if (!project.mapSlots.empty() &&
			project.mapSlots.front().noCaseEqual(normalized.mapName))
	{
		normalized.entryPoint = false;
	}

	if (!M_ValidateCampaignMapDefinition(project, normalized, error))
		return false;
	const int requiredVersion = normalized.entryPoint ?
			ProjectMetadata::CURRENT_VERSION : 2;

	auto existing = std::find_if(project.mapDefinitions.begin(),
			project.mapDefinitions.end(),
			[&normalized](const CampaignMapDefinition &candidate)
			{
				return candidate.mapName.noCaseEqual(normalized.mapName);
			});
	if (!normalized.hasMetadata())
	{
		if (existing != project.mapDefinitions.end())
			project.mapDefinitions.erase(existing);
		else
			return true;
	}
	else if (existing == project.mapDefinitions.end())
	{
		project.mapDefinitions.push_back(std::move(normalized));
	}
	else
	{
		*existing = std::move(normalized);
	}
	project.version = std::max(project.version, requiredVersion);
	M_ReconcileCampaignMetadata(project);
	return true;
}

void M_ReconcileCampaignMetadata(ProjectMetadata &project)
{
	std::vector<SString> normalizedSlots;
	for (const SString &slot : project.mapSlots)
	{
		const SString normalized = slot.asUpper();
		if (!M_IsValidProjectMapName(normalized) ||
				ContainsMap(normalizedSlots, normalized))
		{
			continue;
		}
		normalizedSlots.push_back(normalized);
	}
	project.mapSlots = std::move(normalizedSlots);

	std::vector<CampaignMapDefinition> definitions;
	for (const SString &slot : project.mapSlots)
	{
		const CampaignMapDefinition *source = project.mapDefinition(slot);
		if (!source)
			continue;
		CampaignMapDefinition definition = *source;
		definition.mapName = slot;
		if (slot.noCaseEqual(project.mapSlots.front()))
			definition.entryPoint = false;
		if (!M_IsValidCampaignText(definition.title))
			definition.title.clear();
		if (!M_IsValidCampaignText(definition.episode))
			definition.episode.clear();
		auto reconcileTarget = [&project](std::optional<SString> &target,
				bool emptyIsMeaningful)
		{
			if (!target)
				return;
			if (target->empty())
			{
				if (!emptyIsMeaningful)
					target.reset();
				return;
			}
			*target = target->asUpper();
			if (!ContainsMap(project.mapSlots, *target))
				target.reset();
		};
		reconcileTarget(definition.normalExit, true);
		reconcileTarget(definition.secretExit, false);
		if (definition.hasMetadata())
			definitions.push_back(std::move(definition));
	}
	project.mapDefinitions = std::move(definitions);
	if (project.version > 0)
		project.version = std::max(project.version,
				RequiredCampaignMetadataVersion(project));
}

bool M_RenameProjectMapMetadata(ProjectMetadata &project,
		const SString &oldName, const SString &newName)
{
	if (!M_IsValidProjectMapName(oldName) ||
			!M_IsValidProjectMapName(newName) || oldName.noCaseEqual(newName))
	{
		return false;
	}
	if (ContainsMap(project.mapSlots, newName))
		return false;

	bool changed = false;
	for (SString &slot : project.mapSlots)
	{
		if (slot.noCaseEqual(oldName))
		{
			slot = newName.asUpper();
			changed = true;
		}
	}
	for (CampaignMapDefinition &definition : project.mapDefinitions)
	{
		if (definition.mapName.noCaseEqual(oldName))
		{
			definition.mapName = newName.asUpper();
			changed = true;
		}
		auto renameTarget = [&oldName, &newName, &changed](
				std::optional<SString> &target)
		{
			if (target && target->noCaseEqual(oldName))
			{
				*target = newName.asUpper();
				changed = true;
			}
		};
		renameTarget(definition.normalExit);
		renameTarget(definition.secretExit);
	}
	if (changed)
	{
		M_ReconcileCampaignMetadata(project);
	}
	return changed;
}

fs::path M_NormalizeProjectDestination(const fs::path &destination,
		ProjectPackage package)
{
	if (destination.empty() || destination.has_extension())
		return destination;

	if (package == ProjectPackage::wad)
		return fs::path(destination.u8string() + u8".wad");
	if (package == ProjectPackage::pk3)
		return fs::path(destination.u8string() + u8".pk3");
	return destination;
}

ProjectDestinationValidation M_ValidateProjectDestination(
		const fs::path &destination, ProjectPackage package, bool isKnownIwad)
{
	ProjectDestinationValidation result;
	result.destination = M_NormalizeProjectDestination(destination, package);

	auto fail = [&result](ProjectDestinationIssue issue, const SString &message)
	{
		result.issue = issue;
		result.message = message;
		return result;
	};

	if (result.destination.empty())
		return fail(ProjectDestinationIssue::empty,
				"Choose where the new project package will be created.");

	const char *requiredExtension = nullptr;
	const char *packageName = nullptr;
	if (package == ProjectPackage::wad)
	{
		requiredExtension = ".wad";
		packageName = "WAD";
	}
	else if (package == ProjectPackage::pk3)
	{
		requiredExtension = ".pk3";
		packageName = "PK3";
	}
	else
	{
		return fail(ProjectDestinationIssue::unsupportedPackage,
				"Choose a supported project package type.");
	}

	const SString extension(result.destination.extension().string());
	if (!extension.noCaseEqual(requiredExtension))
	{
		return fail(ProjectDestinationIssue::wrongExtension,
				SString::printf("A %s project destination must use the %s extension.",
						packageName, requiredExtension));
	}

	if (isKnownIwad)
	{
		return fail(ProjectDestinationIssue::knownIwad,
				"The destination is a known game IWAD and cannot be overwritten.");
	}

	fs::path parent = result.destination.parent_path();
	if (parent.empty())
		parent = ".";
	std::error_code error;
	if (!fs::exists(parent, error))
	{
		return fail(ProjectDestinationIssue::missingParent,
				"The destination folder does not exist.");
	}
	if (error)
	{
		return fail(ProjectDestinationIssue::missingParent,
				"The destination folder could not be inspected.");
	}
	if (!fs::is_directory(parent, error) || error)
	{
		return fail(ProjectDestinationIssue::parentNotDirectory,
				"The destination parent is not a folder.");
	}
	if (fs::exists(result.destination, error))
	{
		return fail(ProjectDestinationIssue::alreadyExists,
				"The destination already exists. Choose a new package name.");
	}
	if (error)
	{
		return fail(ProjectDestinationIssue::missingParent,
				"The destination could not be inspected.");
	}

	return result;
}

void M_RefreshProjectMapSlots(ProjectMetadata &project, const Wad_file &iwad)
{
	if (!project.isExplicit() || project.campaign == CampaignMode::custom)
		return;

	project.mapSlots = M_ProjectMapSlots(iwad);
	if (project.campaign == CampaignMode::singleMap &&
			project.mapSlots.size() > 1)
	{
		project.mapSlots.resize(1);
	}
	M_ReconcileCampaignMetadata(project);
}

ProjectMetadata M_NewProjectMetadata(const fs::path &packagePath,
		ProjectPackage package, CampaignMode campaign, const Wad_file &iwad)
{
	ProjectMetadata project;
	project.version = ProjectMetadata::CURRENT_VERSION;
	project.name = packagePath.stem().u8string();
	project.package = package;
	project.campaign = campaign;
	M_RefreshProjectMapSlots(project, iwad);

	return project;
}

std::optional<SString> M_NextProjectMap(const ProjectMetadata &project,
		const SString &currentMap)
{
	return M_ProjectExitTarget(project, currentMap, CampaignExit::normal);
}

std::optional<SString> M_ProjectExitTarget(const ProjectMetadata &project,
		const SString &currentMap, CampaignExit exit)
{
	if (!project.isExplicit() || project.campaign == CampaignMode::singleMap)
		return {};
	if (const CampaignMapDefinition *definition =
			project.mapDefinition(currentMap))
	{
		const std::optional<SString> &target = exit == CampaignExit::secret ?
				definition->secretExit : definition->normalExit;
		if (target)
		{
			if (target->empty())
				return {};
			if (ContainsMap(project.mapSlots, *target))
				return *target;
		}
	}
	if (exit == CampaignExit::secret)
		return {};

	for (size_t index = 0; index < project.mapSlots.size(); ++index)
	{
		if (!project.mapSlots[index].noCaseEqual(currentMap))
			continue;

		if (index + 1 < project.mapSlots.size())
			return project.mapSlots[index + 1];

		return {};
	}

	return {};
}

std::vector<SString> M_CampaignEntryMaps(const ProjectMetadata &project)
{
	std::vector<SString> result;
	if (!project.isExplicit() || project.mapSlots.empty())
		return result;
	for (size_t index = 0; index < project.mapSlots.size(); ++index)
	{
		const SString slot = project.mapSlots[index].asUpper();
		const CampaignMapDefinition *definition = project.mapDefinition(slot);
		if (index == 0 || (definition && definition->entryPoint))
		{
			if (!ContainsMap(result, slot))
				result.push_back(slot);
		}
	}
	return result;
}

std::vector<CampaignMapStatus> M_CampaignMapStatuses(
		const ProjectMetadata &project, const Wad_file &package,
		const SString &currentMap, const std::vector<SString> &dirtyMaps)
{
	const std::vector<SString> entryMaps = M_CampaignEntryMaps(project);
	auto isDirty = [&dirtyMaps](const SString &name)
	{
		return std::any_of(dirtyMaps.begin(), dirtyMaps.end(),
				[&name](const SString &dirty)
				{
					return dirty.noCaseEqual(name);
				});
	};

	std::vector<CampaignMapStatus> result;
	for (const SString &slot : project.mapSlots)
	{
		const bool duplicate = std::any_of(result.begin(), result.end(),
				[&slot](const CampaignMapStatus &status)
				{
					return status.name.noCaseEqual(slot);
				});
		if (duplicate)
			continue;

		CampaignMapStatus status;
		status.name = slot.asUpper();
		if (const CampaignMapDefinition *definition = project.mapDefinition(slot))
		{
			status.title = definition->title;
			status.episode = definition->episode;
			status.normalExit = definition->normalExit;
			status.secretExit = definition->secretExit;
		}
		status.configured = true;
		status.exists = package.LevelFind(slot) >= 0;
		status.current = slot.noCaseEqual(currentMap);
		status.dirty = isDirty(slot);
		status.campaignEntry = ContainsMap(entryMaps, slot);
		result.push_back(std::move(status));
	}

	std::vector<SString> extras;
	for (int level = 0; level < package.LevelCount(); ++level)
	{
		const SString &name = package.LevelName(level);
		const bool configured = std::any_of(result.begin(), result.end(),
				[&name](const CampaignMapStatus &status)
				{
					return status.name.noCaseEqual(name);
				});
		const bool alreadyAdditional = std::any_of(extras.begin(), extras.end(),
				[&name](const SString &extra)
				{
					return extra.noCaseEqual(name);
				});
		if (!configured && !alreadyAdditional)
			extras.push_back(name);
	}
	std::sort(extras.begin(), extras.end());

	for (const SString &name : extras)
	{
		CampaignMapStatus status;
		status.name = name.asUpper();
		status.exists = true;
		status.current = name.noCaseEqual(currentMap);
		status.dirty = isDirty(name);
		result.push_back(std::move(status));
	}

	return result;
}
