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

enum class CampaignExit
{
	normal,
	secret
};

// Optional editor-facing information for one configured campaign slot.
// An absent normalExit follows mapSlots order; a present empty normalExit ends
// the campaign. Secret exits have no implicit fallback. Version 3 may mark
// additional entry points; the first slot remains an implicit entry.
struct CampaignMapDefinition
{
	SString mapName;
	SString title;
	SString episode;
	std::optional<SString> normalExit;
	std::optional<SString> secretExit;
	bool entryPoint = false;

	bool hasMetadata() const noexcept
	{
		return title.good() || episode.good() || normalExit.has_value() ||
				secretExit.has_value() || entryPoint;
	}
};

enum class ProjectDestinationIssue
{
	none,
	empty,
	unsupportedPackage,
	wrongExtension,
	knownIwad,
	alreadyExists,
	missingParent,
	parentNotDirectory
};

struct ProjectDestinationValidation
{
	fs::path destination;
	ProjectDestinationIssue issue = ProjectDestinationIssue::none;
	SString message;

	bool valid() const noexcept
	{
		return issue == ProjectDestinationIssue::none;
	}
};

// Editor-only project intent.  Map data and engine resources remain in the
// package backend; this structure is deliberately independent of the GUI.
struct ProjectMetadata
{
	static constexpr int CURRENT_VERSION = 3;

	int version = 0;
	SString name;
	ProjectPackage package = ProjectPackage::none;
	CampaignMode campaign = CampaignMode::singleMap;
	std::vector<SString> mapSlots;
	std::vector<CampaignMapDefinition> mapDefinitions;

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
	const CampaignMapDefinition *mapDefinition(const SString &mapName) const noexcept;
	CampaignMapDefinition *mapDefinition(const SString &mapName) noexcept;
};

struct CampaignMapStatus
{
	SString name;
	SString title;
	SString episode;
	std::optional<SString> normalExit;
	std::optional<SString> secretExit;
	bool configured = false;
	bool exists = false;
	bool current = false;
	bool dirty = false;
	bool campaignEntry = false;

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

bool M_IsValidCampaignText(const SString &text) noexcept;

bool M_ValidateCampaignMapDefinition(const ProjectMetadata &project,
		const CampaignMapDefinition &definition, SString *error = nullptr);

bool M_SetCampaignMapDefinition(ProjectMetadata &project,
		const CampaignMapDefinition &definition, SString *error = nullptr);

void M_ReconcileCampaignMetadata(ProjectMetadata &project);

bool M_RenameProjectMapMetadata(ProjectMetadata &project,
		const SString &oldName, const SString &newName);

fs::path M_NormalizeProjectDestination(const fs::path &destination,
		ProjectPackage package);

ProjectDestinationValidation M_ValidateProjectDestination(
		const fs::path &destination, ProjectPackage package,
		bool isKnownIwad = false);

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

std::optional<SString> M_ProjectExitTarget(const ProjectMetadata &project,
		const SString &currentMap, CampaignExit exit);

// The first configured slot is always an implicit entry for compatibility.
// Version 3 metadata may add further entry points without changing routes.
std::vector<SString> M_CampaignEntryMaps(const ProjectMetadata &project);

std::vector<CampaignMapStatus> M_CampaignMapStatuses(
		const ProjectMetadata &project, const Wad_file &package,
		const SString &currentMap, const std::vector<SString> &dirtyMaps);

#endif // HERESY_M_PROJECT_H
