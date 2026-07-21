//------------------------------------------------------------------------
//  PROJECT AND CAMPAIGN MODEL
//------------------------------------------------------------------------

#include "m_project.h"

#include "w_wad.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>

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

} // namespace

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

	return false;
}

std::vector<std::pair<SString, SString>> ProjectMetadata::serializedFields() const
{
	if (!isExplicit())
		return {};

	std::vector<std::pair<SString, SString>> fields;
	fields.emplace_back("project_version", SString(version));
	fields.emplace_back("project_name", name);
	fields.emplace_back("project_package", PackageName(package));
	fields.emplace_back("campaign_mode", CampaignName(campaign));

	for (const SString &slot : mapSlots)
		fields.emplace_back("map_slot", slot);

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
	if (!project.isExplicit() || project.campaign == CampaignMode::singleMap)
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

std::vector<CampaignMapStatus> M_CampaignMapStatuses(
		const ProjectMetadata &project, const Wad_file &package,
		const SString &currentMap, const std::vector<SString> &dirtyMaps)
{
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
		status.configured = true;
		status.exists = package.LevelFind(slot) >= 0;
		status.current = slot.noCaseEqual(currentMap);
		status.dirty = isDirty(slot);
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
