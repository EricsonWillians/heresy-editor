//------------------------------------------------------------------------
//  PROJECT AND CAMPAIGN MODEL
//------------------------------------------------------------------------

#ifndef HERESY_M_PROJECT_H
#define HERESY_M_PROJECT_H

#include "m_strings.h"

#include <filesystem>
#include <optional>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

class Wad_file;

enum class ProjectPackage
{
	none,
	wad,
	pk3
};

enum class CampaignMode
{
	singleMap,
	fullIwad,
	custom
};

// Editor-only project intent.  Map data and engine resources remain in the
// package backend; this structure is deliberately independent of the GUI.
struct ProjectMetadata
{
	static constexpr int CURRENT_VERSION = 1;

	int version = 0;
	SString name;
	ProjectPackage package = ProjectPackage::none;
	CampaignMode campaign = CampaignMode::singleMap;
	std::vector<SString> mapSlots;

	bool isExplicit() const noexcept
	{
		return version > 0 && package != ProjectPackage::none;
	}

	void clear()
	{
		*this = ProjectMetadata{};
	}

	// Return true when the key belongs to project metadata, including when its
	// value is unknown.  This lets callers retain forward-compatible parsing.
	bool parseField(const SString &key, const SString &value);
	std::vector<std::pair<SString, SString>> serializedFields() const;
};

struct CampaignMapStatus
{
	SString name;
	bool configured = false;
	bool exists = false;
	bool current = false;
	bool dirty = false;

	bool missing() const noexcept
	{
		return configured && !exists;
	}
};

std::vector<SString> M_ProjectMapSlots(const Wad_file &iwad);

bool M_IsValidProjectMapName(const SString &name) noexcept;

std::optional<std::vector<SString>> M_ParseCustomMapSlots(
		const SString &text, SString *error = nullptr);

SString M_FormatCustomMapSlots(const std::vector<SString> &slots);

void M_RefreshProjectMapSlots(ProjectMetadata &project, const Wad_file &iwad);

ProjectMetadata M_NewProjectMetadata(const fs::path &packagePath,
		ProjectPackage package, CampaignMode campaign, const Wad_file &iwad);

inline ProjectMetadata M_NewWadProjectMetadata(const fs::path &packagePath,
		CampaignMode campaign, const Wad_file &iwad)
{
	return M_NewProjectMetadata(packagePath, ProjectPackage::wad, campaign, iwad);
}

std::optional<SString> M_NextProjectMap(const ProjectMetadata &project,
		const SString &currentMap);

std::vector<CampaignMapStatus> M_CampaignMapStatuses(
		const ProjectMetadata &project, const Wad_file &package,
		const SString &currentMap, const std::vector<SString> &dirtyMaps);

#endif // HERESY_M_PROJECT_H
