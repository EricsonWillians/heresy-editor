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

#include <optional>
#include <vector>

struct LoadingData;

// Ordered, injectable search inputs keep executable discovery deterministic and
// make it possible to test without depending on programs installed on the host.
struct PortExecutableSearchLocations
{
	fs::path configuredPath;
	std::vector<fs::path> searchDirectories;
	std::vector<fs::path> fallbackCandidates;
};

PortExecutableSearchLocations M_SystemPortExecutableSearchLocations();
std::optional<fs::path> M_FindPortExecutable(const SString &port,
		const PortExecutableSearchLocations &locations);

namespace testmap
{
void updateMenuName(Fl_Sys_Menu_Bar *bar, const LoadingData &loading);
}

#endif
