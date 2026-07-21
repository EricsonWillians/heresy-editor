//------------------------------------------------------------------------
//  PROJECT AND CAMPAIGN MODEL
//------------------------------------------------------------------------

#include "m_project.h"

#include "w_wad.h"

#include <cstdlib>

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
		if (!slot.empty())
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
