//------------------------------------------------------------------------
//  PROJECT SESSION SIDECAR
//------------------------------------------------------------------------

#ifndef HERESY_M_SESSION_H
#define HERESY_M_SESSION_H

#include "m_iwad.h"
#include "m_strings.h"

#include <filesystem>
#include <optional>

namespace fs = std::filesystem;

// Editor-only state which must not alter the playable WAD/PK3 package.  Paths
// are deliberately portable: iwadFile is a basename and iwadRelative is
// relative to the project package directory.
struct ProjectSession
{
	static constexpr int CURRENT_VERSION = 1;

	int version = CURRENT_VERSION;
	SString activeMap;
	SString navigatorMap;
	SString iwadGame;
	fs::path iwadFile;
	fs::path iwadRelative;
};

fs::path M_ProjectSessionPath(const fs::path &packagePath);

ProjectSession M_MakeProjectSession(const fs::path &packagePath,
		const fs::path &iwadPath, const SString &iwadGame,
		const SString &activeMap, const SString &navigatorMap);

std::optional<ProjectSession> M_LoadProjectSession(const fs::path &packagePath);
void M_SaveProjectSession(const fs::path &packagePath,
		const ProjectSession &session);

std::optional<fs::path> M_ResolveProjectIWAD(const fs::path &packagePath,
		const ProjectSession &session,
		const std::optional<fs::path> &knownIWAD,
		const IWADSearchLocations &locations);

#endif // HERESY_M_SESSION_H
