//------------------------------------------------------------------------
//  MANAGED RUNTIME MAPINFO
//------------------------------------------------------------------------

#ifndef HERESY_M_MAPINFO_H
#define HERESY_M_MAPINFO_H

#include "m_project.h"

#include <filesystem>
#include <optional>
#include <vector>

namespace fs = std::filesystem;

class Wad_file;

inline constexpr const char *HERESY_MANAGED_ZMAPINFO_MARKER =
		"// HERESY_EDITOR_MANAGED_ZMAPINFO v1";

struct GeneratedRuntimeMapInfo
{
	SString text;
	std::vector<SString> warnings;
};

enum class RuntimeMapInfoState
{
	absent,
	managed,
	conflict
};

enum class RuntimeMapInfoFreshness
{
	absent,
	current,
	stale,
	userAuthored
};

struct RuntimeMapInfoInspection
{
	RuntimeMapInfoState state = RuntimeMapInfoState::absent;
	std::vector<SString> declarations;
	SString managedText;
	SString detail;

	bool canWrite() const noexcept
	{
		return state != RuntimeMapInfoState::conflict;
	}
};

bool M_RuntimeMapInfoPortSupported(const SString &portName) noexcept;

std::optional<GeneratedRuntimeMapInfo> M_GenerateRuntimeMapInfo(
		const ProjectMetadata &project, const SString &portName,
		const SString &gameName, SString *error = nullptr);

RuntimeMapInfoInspection M_InspectRuntimeMapInfo(
		const fs::path &packagePath, ProjectPackage packageType,
		const Wad_file &aggregate);

RuntimeMapInfoFreshness M_RuntimeMapInfoFreshness(
		const RuntimeMapInfoInspection &inspection,
		const SString &generatedText) noexcept;

const char *M_RuntimeMapInfoFreshnessName(
		RuntimeMapInfoFreshness freshness) noexcept;

// The caller must inspect immediately before writing and refuse conflicts.
// This replaces the sole managed ZMAPINFO declaration or appends a new one.
void M_StoreManagedRuntimeMapInfo(Wad_file &aggregate,
		const SString &generatedText);

#endif // HERESY_M_MAPINFO_H
