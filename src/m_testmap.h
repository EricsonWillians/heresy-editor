//------------------------------------------------------------------------
//  TEST (PLAY) THE MAP
//------------------------------------------------------------------------
//
//  Eureka DOOM Editor
//
//  Copyright (C) 2016 Andrew Apted
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 2
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//------------------------------------------------------------------------

#ifndef M_TESTMAP_H_
#define M_TESTMAP_H_

#include "FL/Fl_Sys_Menu_Bar.H"

#include "lib_file.h"
#include "m_strings.h"

#include <functional>
#include <optional>
#include <vector>

struct LoadingData;

enum class PortExecutablePathIssue
{
	none,
	empty,
	notFound,
	directory,
	notExecutable,
	unsupportedType,
	inaccessible
};

struct PortExecutablePathValidation
{
	PortExecutablePathIssue issue = PortExecutablePathIssue::empty;
	SString message;

	bool valid() const noexcept
	{
		return issue == PortExecutablePathIssue::none;
	}
};

// Ordered, injectable search inputs keep executable discovery deterministic and
// make it possible to test without depending on programs installed on the host.
struct PortExecutableSearchLocations
{
	std::vector<fs::path> configuredPaths;
	std::vector<fs::path> searchDirectories;
	std::vector<fs::path> fallbackCandidates;
	std::vector<fs::path> recursiveRoots;
};

enum class PortExecutableSearchPhase
{
	configuredPath,
	systemPath,
	fallbackCandidate,
	recursiveDirectory
};

struct PortExecutableSearchProgress
{
	PortExecutableSearchPhase phase =
			PortExecutableSearchPhase::configuredPath;
	fs::path location;
	size_t candidatesChecked = 0;
	size_t directoriesScanned = 0;
};

// Callbacks may run on a worker thread.  They must not access FLTK widgets.
struct PortExecutableSearchControl
{
	std::function<bool()> cancelled;
	std::function<void(const PortExecutableSearchProgress &)> progress;
};

PortExecutableSearchLocations M_SystemPortExecutableSearchLocations(const SString &port);
bool M_PortExecutableAutoDiscoverySupported(const SString &port) noexcept;
PortExecutablePathValidation M_ValidatePortExecutablePath(
		const fs::path &path) noexcept;
std::optional<fs::path> M_FindPortExecutable(const SString &port,
		const PortExecutableSearchLocations &locations,
		const PortExecutableSearchControl *control = nullptr);

namespace testmap
{
void updateMenuName(Fl_Sys_Menu_Bar *bar, const LoadingData &loading);
}

#endif
