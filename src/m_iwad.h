//------------------------------------------------------------------------
//  IWAD DISCOVERY
//------------------------------------------------------------------------
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 2
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
//  GNU General Public License for more details.
//

#ifndef HERESY_M_IWAD_H
#define HERESY_M_IWAD_H

#include "m_strings.h"

#include <filesystem>
#include <map>
#include <vector>

namespace fs = std::filesystem;

// Ordered IWAD search locations. User-configured directories take priority,
// launcher installations come next, and generic fallback directories are last.
struct IWADSearchLocations
{
	std::vector<fs::path> preferredDirectories;
	std::vector<fs::path> steamInstallations;
	std::vector<fs::path> gameCollections;
	std::vector<fs::path> fallbackDirectories;
};

IWADSearchLocations M_SystemIWADSearchLocations(
		const fs::path &configDirectory, const fs::path &legacyConfigDirectory);

std::map<SString, fs::path> M_DiscoverIWADs(
		const std::vector<SString> &games, const IWADSearchLocations &locations);

// Validate both the IWAD header and the canonical game filename.  This is
// intentionally stricter than Wad_file::Validate(), which also accepts PWADs.
bool M_IsIWADForGame(const fs::path &path, const SString &game);

#endif // HERESY_M_IWAD_H

//--- editor settings ---
// vi:ts=4:sw=4:noexpandtab
