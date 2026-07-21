//------------------------------------------------------------------------
//  RECENT FILES / KNOWN IWADS / BACKUPS
//------------------------------------------------------------------------
//
//  Eureka DOOM Editor
//
//  Copyright (C) 2012-2018 Andrew Apted
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

#include "Errors.h"
#include "Instance.h"
#include "main.h"
#include "m_config.h"
#include "m_files.h"
#include "m_game.h"
#include "m_iwad.h"
#include "m_package.h"
#include "m_loadsave.h"
#include "m_parse.h"
#include "m_streams.h"
#include "m_testmap.h"
#include "w_wad.h"

#include "ui_window.h"

#include <filesystem>
#include <limits>
namespace fs = std::filesystem;

// list of known iwads (mapping GAME name --> PATH)

void RecentKnowledge::addIWAD(const fs::path &path)
{
	fs::path absolute_name = fs::absolute(path);

	const SString &game = GameNameFromIWAD(path);

	known_iwads[game] = absolute_name;
}

// returns a string, with each name separated by a '|' character,
// hence directly usable with the FL_Choice::add() method.
//
SString RecentKnowledge::collectGamesForMenu(int *exist_val, const char *exist_name) const
{
	std::map<SString, fs::path>::const_iterator KI;

	SString result;
	result.reserve(2000);

	int index = 0;

	for (KI = known_iwads.begin() ; KI != known_iwads.end() ; KI++, index++)
	{
		const SString &name = KI->first;

		if (result[0])
			result += '|';

		result += name;

		if (name.noCaseEqual(exist_name))
			*exist_val = index;
	}

	return result;
}

void RecentKnowledge::writeKnownIWADs(std::ostream &os) const
{
	std::map<SString, fs::path>::const_iterator KI;

	for (KI = known_iwads.begin() ; KI != known_iwads.end() ; KI++)
	{
		os << "known_iwad " << KI->first.spaceEscape() << " " << escape(KI->second) << std::endl;
	}
}


void M_ValidateGivenFiles()
{
	for (const fs::path &pwad : global::Pwad_list)
	{
		if (!M_ValidateEditablePackage(pwad))
			ThrowException("Given WAD/PK3 package does not exist or is invalid: %s\n",
						   reinterpret_cast<const char *>(pwad.u8string().c_str()));
	}
}


int M_FindGivenFile(const fs::path &filename)
{
	for (int i = 0 ; i < (int)global::Pwad_list.size() ; i++)
		if (global::Pwad_list[i] == filename)
			return i;

	return -1;  // Not Found
}


//------------------------------------------------------------------------
//  PORT PATH HANDLING
//------------------------------------------------------------------------



//
// Parse port path
//
void RecentKnowledge::parsePortPath(const SString &name, const SString &cpath)
{
	SString path(cpath);
	path.trimLeadingSpaces();
	size_t pos = path.find('|');
	if(pos == std::string::npos)
	{
		// TODO : Warn
		return;
	}

	// terminate arguments
	path.erase(0, pos + 1);

	setPortPath(name, fs::path(reinterpret_cast<const char8_t *>(path.c_str())));
	if(gInstance && gInstance->main_win)
		testmap::updateMenuName(gInstance->main_win->menu_bar, gInstance->loaded);

	// parse any other arguments
	// [ none needed atm.... ]
}

void RecentKnowledge::writePortPaths(std::ostream &os) const
{
	std::map<SString, fs::path>::const_iterator IT;

	for (IT = port_paths.begin() ; IT != port_paths.end() ; IT++)
	{
		const fs::path& info = IT->second;
		os << "port_path " << IT->first.spaceEscape() << " |" << escape(info) << std::endl;
	}
}

// Recent files

RecentFiles_c::Deque::iterator RecentFiles_c::find(const fs::path &file)
{
	// ignore the path when matching filenames
	SString A = file.filename().u8string();

	for(auto it = list.begin(); it != list.end(); ++it)
	{
		SString B = it->file.filename().u8string();
		if(!A.noCaseEqual(B))
			continue;

		return it;
	}

	return list.end();	// not found
}

void RecentFiles_c::push_front(const fs::path &file, const SString &map)
{
	if(list.size() >= MAX_RECENT)
	{
		list.pop_back();
	}
	list.push_front({file, map});
}

void RecentFiles_c::insert(const fs::path &file, const SString &map)
{
	// ensure filename (without any path) is unique
	auto it = find(file);

	if(it != list.end())
		list.erase(it);

	push_front(file, map);
}

void RecentFiles_c::Write(std::ostream &stream) const
{
	// file is in opposite order, newest at the end
	// (this allows the parser to merely insert() items in the
	//  order they are read).

	for(auto it = list.rbegin(); it != list.rend(); ++it)
		stream << "recent " << it->map.spaceEscape() << " " << escape(it->file) << std::endl;
}

SString RecentFiles_c::Format(int index) const
{
	SYS_ASSERT(index < (int)list.size());

	SString name = list[index].file.filename().u8string();

	return SString::printf("%s%s%d:  %-.42s", (index < 9) ? "  " : "",
		(index < 9) ? "&" : "", 1 + index, name.c_str());
}

const RecentMap &RecentFiles_c::Lookup(int index) const
{
	SYS_ASSERT(index >= 0);
	SYS_ASSERT(index < (int)list.size());

	return list[index];
}


RecentProjects_c::Deque::iterator RecentProjects_c::find(const fs::path &file)
{
	std::error_code absoluteError;
	const fs::path absolute = fs::absolute(file, absoluteError).lexically_normal();
	for (auto it = list.begin(); it != list.end(); ++it)
	{
		std::error_code equivalentError;
		if (fs::equivalent(file, it->file, equivalentError) && !equivalentError)
			return it;

		std::error_code otherError;
		const fs::path other = fs::absolute(it->file, otherError).lexically_normal();
		if (absoluteError || otherError)
			continue;
#ifdef WIN32
		if (SString(absolute.u8string()).noCaseEqual(SString(other.u8string())))
			return it;
#else
		if (absolute == other)
			return it;
#endif
	}
	return list.end();
}


void RecentProjects_c::insert(const fs::path &file, const SString &map)
{
	auto existing = find(file);
	if (existing != list.end())
		list.erase(existing);
	if (list.size() >= MAX_RECENT)
		list.pop_back();
	list.push_front({GetAbsolutePath(file), map.asUpper()});
}


void RecentProjects_c::Write(std::ostream &stream) const
{
	for (auto it = list.rbegin(); it != list.rend(); ++it)
		stream << "recent_project " << it->map.spaceEscape() << " " <<
				escape(it->file) << std::endl;
}


SString RecentProjects_c::Format(int index) const
{
	SYS_ASSERT(index >= 0 && index < static_cast<int>(list.size()));
	const fs::path &path = list[index].file;
	const SString file = path.filename().u8string();
	const SString parent = path.parent_path().filename().u8string();
	return SString::printf("%s%s%d:  %-.30s  —  %-.18s",
			index < 9 ? "  " : "", index < 9 ? "&" : "", index + 1,
			file.c_str(), parent.c_str());
}


const RecentMap &RecentProjects_c::Lookup(int index) const
{
	SYS_ASSERT(index >= 0 && index < static_cast<int>(list.size()));
	return list[index];
}

namespace global
{
RecentKnowledge recent;
}

//
// Parse miscellaneous config
//
void RecentKnowledge::parseMiscConfig(std::istream &is)
{
	SString line;
	while(M_ReadTextLine(line, is))
	{
		SString keyword;
		TokenWordParse parse(line, true);
		if(!parse.getNext(keyword))
			continue;	// blank line
		if(keyword == "recent")
		{
			SString map;
			fs::path path;
			if(!parse.getNext(map))
			{
				gLog.printf("Expected map name after 'recent' in recents config\n");
				continue;
			}
			if(!parse.getNext(path))
			{
				gLog.printf("Expected WAD path as second arg in 'recent' in recents config\n");
				continue;
			}
			if(M_ValidateEditablePackage(path))
				files.insert(path, map);
			else
				gLog.printf("  no longer exists: %s\n", reinterpret_cast<const char *>(path.u8string().c_str()));
		}
		else if(keyword == "recent_project")
		{
			SString map;
			fs::path path;
			if (!parse.getNext(map) || !parse.getNext(path))
			{
				gLog.printf("Expected map and path after 'recent_project' in recents config\n");
				continue;
			}
			if (M_ValidateEditablePackage(path))
				projects.insert(path, map);
			else
				gLog.printf("  project no longer exists: %s\n",
						reinterpret_cast<const char *>(path.u8string().c_str()));
		}
		else if(keyword == "known_iwad")
		{
			SString name;
			fs::path path;
			if(!parse.getNext(name))
			{
				gLog.printf("Expected IWAD name after 'known_iwad' in recents config\n");
				continue;
			}
			if(!parse.getNext(path))
			{
				gLog.printf("Expected WAD path as second arg in 'known_iwad' in recents config\n");
				continue;
			}
			// ignore plain freedoom.wad (backwards compatibility)
			if(name.noCaseEqual("freedoom"))
				gLog.printf("  ignoring for compatibility: %s\n", reinterpret_cast<const char *>(path.u8string().c_str()));
			else if(Wad_file::Validate(path))
				known_iwads[name] = path;
			else
				gLog.printf("  no longer exists: %s\n", reinterpret_cast<const char *>(path.u8string().c_str()));
		}
		else if(keyword == "port_path")
		{
			SString name, barpath, path;
			if(!parse.getNext(name))
			{
				gLog.printf("Expected port name after 'port_path' in recents config\n");
				continue;
			}
			if(!parse.getNext(barpath))
			{
				gLog.printf("Expected | followed by port path after port name\n");
				continue;
			}
			if(parse.getNext(path))	// allow space after |
				barpath += path;
			parsePortPath(name, barpath);
		}
		else
			gLog.printf("Unknown keyword '%s' in recents config\n", keyword.c_str());
	}
}


void RecentKnowledge::load(const fs::path &home_dir, const fs::path &old_home_dir)
{
	fs::path filename = home_dir / "misc.cfg";

	std::ifstream is(filename);
	if(!is.is_open())
	{
		gLog.printf("No recent list at: %s\n", reinterpret_cast<const char *>(filename.u8string().c_str()));
		if(!old_home_dir.empty())
		{
			filename = old_home_dir / "misc.cfg";
			is.open(filename);
			if(!is.is_open())
			{
				gLog.printf("No recent list at: %s\n", reinterpret_cast<const char *>(filename.u8string().c_str()));
				return;
			}
		}
		else
			return;
	}

	gLog.printf("Reading recent list from: %s\n", reinterpret_cast<const char *>(filename.u8string().c_str()));

	files.clear();
	projects.clear();
	known_iwads.clear();
	port_paths.clear();

	parseMiscConfig(is);
}

void RecentKnowledge::save(const fs::path &home_dir) const
{
	fs::path filename = home_dir / "misc.cfg";

	std::ofstream os(filename, std::ios::trunc);
	if(!os.is_open())
	{
		gLog.printf("Failed to save recent list to: %s\n", reinterpret_cast<const char *>(filename.u8string().c_str()));
		return;
	}

	gLog.printf("Writing recent list to: %s\n", reinterpret_cast<const char *>(filename.u8string().c_str()));
	os << "# Heresy Editor miscellaneous settings" << std::endl;

	files.Write(os);
	projects.Write(os);

	writeKnownIWADs(os);

	writePortPaths(os);
}

void M_OpenRecentFromMenu(void *priv_data)
{
	SYS_ASSERT(priv_data);

	RecentMap *data = (RecentMap *)priv_data;

	try
	{
		OpenFileMap(data->file, data->map);
	}
	catch (const std::runtime_error& e)
	{
		DLG_ShowError(false, "Could not open %s of %s: %s", data->map.c_str(), reinterpret_cast<const char *>(data->file.u8string().c_str()), e.what());
	}
}


void M_OpenRecentProjectFromMenu(void *priv_data)
{
	SYS_ASSERT(priv_data);
	RecentMap *data = static_cast<RecentMap *>(priv_data);
	try
	{
		OpenFileMap(data->file, data->map);
	}
	catch (const std::runtime_error &error)
	{
		DLG_ShowError(false, "Could not open project %s: %s",
				reinterpret_cast<const char *>(data->file.u8string().c_str()),
				error.what());
	}
}

void RecentKnowledge::addRecent(const fs::path &filename, const SString &map_name, const fs::path &home_dir)
{
	files.insert(GetAbsolutePath(filename), map_name);

	save(home_dir);  // why wait?
}


void RecentKnowledge::addRecentProject(const fs::path &filename,
		const SString &map_name, const fs::path &home_dir)
{
	projects.insert(filename, map_name);
	if (!home_dir.empty())
		save(home_dir);
}


bool Instance::M_TryOpenMostRecent()
{
	const RecentMap *selected = nullptr;
	if (global::recent.getProjects().getSize() > 0)
		selected = &global::recent.getProjects().Lookup(0);
	else if (global::recent.getFiles().getSize() > 0)
		selected = &global::recent.getFiles().Lookup(0);
	if (!selected)
		return false;

	RecentMap recentMap = *selected;

	// M_LoadRecent has already validated the filename, so this should
	// normally work.

	std::shared_ptr<Wad_file> wad = M_OpenEditablePackage(recentMap.file);

	if (! wad)
	{
		gLog.printf("Failed to load most recent pwad: %s\n", reinterpret_cast<const char *>(recentMap.file.u8string().c_str()));
		return false;
	}

	// make sure at least one level can be loaded
	if (wad->LevelCount() == 0)
	{
		gLog.printf("No levels in most recent pwad: %s\n", reinterpret_cast<const char *>(recentMap.file.u8string().c_str()));

		return false;
	}

	/* -- OK -- */

	if (wad->LevelFind(recentMap.map) >= 0)
		loaded.levelName = recentMap.map;
	else
		loaded.levelName.clear();

	this->wad.master.ReplaceEditWad(wad);

	return true;
}


//------------------------------------------------------------------------
//  IWAD DISCOVERY
//------------------------------------------------------------------------

//
// search for iwads in various places
//
void RecentKnowledge::lookForIWADs(const fs::path &install_dir, const fs::path &home_dir,
		const fs::path &old_home_dir)
{
	gLog.printf("Looking for IWADs....\n");

	std::vector<SString> game_list = M_CollectKnownDefs({install_dir, old_home_dir, home_dir},
			"games");

	std::vector<SString> missing_games;
	for (const SString &game : game_list)
	{
		if (!queryIWAD(game))
			missing_games.push_back(game);
	}

	const IWADSearchLocations locations = M_SystemIWADSearchLocations(home_dir, old_home_dir);
	const std::map<SString, fs::path> discovered = M_DiscoverIWADs(missing_games, locations);
	for(const auto &[game, path] : discovered)
	{
		gLog.printf("Found '%s' IWAD file: %s\n", game.c_str(),
				reinterpret_cast<const char *>(path.u8string().c_str()));
		addIWAD(path);
	}

	save(home_dir);
}


fs::path Instance::M_PickDefaultIWAD() const
{
	// guess either DOOM or DOOM 2 based on level names
	const char *default_game = "doom2";

	if (!loaded.levelName.empty() && safe_toupper(loaded.levelName[0]) == 'E')
	{
		default_game = "doom";
	}
	else if (wad.master.editWad())
	{
		int idx = wad.master.editWad()->LevelFindFirst();

		if (idx >= 0)
		{
			idx = wad.master.editWad()->LevelHeader(idx);
			const SString &name = wad.master.editWad()->GetLump(idx)->Name();

			if (safe_toupper(name[0]) == 'E')
				default_game = "doom";
		}
	}

	gLog.debugPrintf("pick default iwad, trying: '%s'\n", default_game);

	const fs::path *result;

	result = global::recent.queryIWAD(default_game);
	if (result)
		return *result;

	// try FreeDoom

	if (strcmp(default_game, "doom") == 0)
		default_game = "freedoom1";
	else
		default_game = "freedoom2";

	gLog.debugPrintf("pick default iwad, trying: '%s'\n", default_game);

	result = global::recent.queryIWAD(default_game);
	if (result)
		return *result;

	// try any known iwad

	gLog.debugPrintf("pick default iwad, trying first known iwad...\n");

	result = global::recent.getFirstIWAD();
	if (result)
		return *result;

	// nothing left to try
	gLog.debugPrintf("pick default iwad failed.\n");

	return "";
}


static void M_AddResource_Unique(LoadingData &loading, const fs::path & filename)
{
	// check if base filename (without path) already exists
	for (const fs::path &resource : loading.resourceList)
	{
		SString A = filename.filename().u8string();
		SString B = resource.filename().u8string();

		if(A.noCaseEqual(B))
			return;		// found it
	}

	loading.resourceList.push_back(filename);
}


//
// returns false if user wants to cancel the load
//
bool LoadingData::parseEurekaLump(const fs::path &home_dir, const fs::path &old_home_dir,
		const fs::path &install_dir, const RecentKnowledge &recent, const Wad_file *wad,
		bool keep_cmd_line_args)
{
	gLog.printf("Parsing '%s' lump\n", EUREKA_LUMP);
	project.clear();

	const Lump_c * lump = wad->FindLump(EUREKA_LUMP);

	if (! lump)
	{
		gLog.printf("--> does not exist.\n");
		return true;
	}

	LumpInputStream stream(*lump);

	const fs::path *new_iwad = nullptr;
	SString new_port;

	std::vector<fs::path> new_resources;

	SString line;

	std::optional<SString> testingCommandLine;

	while (stream.readLine(line))
	{
		TokenWordParse parse(line, true);
		SString key, value;
		if(!parse.getNext(key))
			continue;	// empty line
		if(!parse.getNext(value))
		{
			gLog.printf("WARNING: bad syntax in %s lump\n", EUREKA_LUMP);
			continue;
		}

		if (key == "game")
		{
			if (! M_CanLoadDefinitions(home_dir, old_home_dir, install_dir, GAMES_DIR, value))
			{
				gLog.printf("  unknown game: %s\n", value.c_str() /* show full path */);

				int res = DLG_Confirm({ "&Ignore", "&Cancel Load" },
				                      "Warning: the pwad specifies an unsupported "
									  "game:\n\n          %s", value.c_str());
				if (res == 1)
					return false;
			}
			else
			{
				new_iwad = recent.queryIWAD(value);

				if (!new_iwad)
				{
					int res = DLG_Confirm({ "&Ignore", "&Cancel Load" },
					                      "Warning: the pwad specifies an IWAD "
										  "which cannot be found:\n\n          %s.wad",
										  value.c_str());
					if (res == 1)
						return false;
				}
			}
		}
		else if (key == "resource")
		{
			fs::path resourcePath = fs::path(reinterpret_cast<const char8_t *>(value.c_str()));

			if(resourcePath.is_relative())
			{
				fs::path wadDirPath = wad->PathName().parent_path();
				resourcePath = (wadDirPath / resourcePath).lexically_normal();
			}

			// if not found at expected location, try same place as PWAD
			if (!fs::exists(resourcePath))
			{
				gLog.printf("  file not found: %s\n", reinterpret_cast<const char *>(resourcePath.u8string().c_str()));
				fs::path wadDirPath = wad->PathName().parent_path();
				resourcePath = wadDirPath / resourcePath.filename();

				gLog.printf("  trying: %s\n", reinterpret_cast<const char *>(resourcePath.u8string().c_str()));
			}
			// Still doesn't exist? Try IWAD path, if any
			if (!fs::exists(resourcePath) && new_iwad)
			{
				fs::path wadDirPath = new_iwad->parent_path();
				resourcePath = wadDirPath / resourcePath.filename();
				gLog.printf("  trying: %s\n", reinterpret_cast<const char *>(resourcePath.u8string().c_str()));
			}

			if (fs::exists(resourcePath))
				new_resources.push_back(resourcePath);
			else
			{
				DLG_Notify("Warning: the pwad specifies a resource "
				           "which cannot be found:\n\n%s", value.c_str());
			}
		}
		else if (key == "port")
		{
			if (M_CanLoadDefinitions(home_dir, old_home_dir, install_dir, PORTS_DIR, value))
			{
				new_port = value;
			}
			else
			{
				gLog.printf("  unknown port: %s\n", value.c_str());

				DLG_Notify("Warning: the pwad specifies an unknown port:\n\n%s", value.c_str());
			}
		}
		else if (key == "testing_command_line")
		{
			testingCommandLine = value;
		}
		else if (project.parseField(key, value))
		{
			// Project fields are owned by the GUI-independent project model.
		}
		else
		{
			gLog.printf("WARNING: unknown keyword '%s' in %s lump\n", key.c_str(), EUREKA_LUMP);
			continue;
		}
	}

	/* OK */

	// When 'keep_cmd_line_args' is true, we do not override any value which
	// has been set via command line arguments.  In other words, cmd line
	// arguments will override the EUREKA_LUMP.
	//
	// Resources are trickier, we merge the EUREKA_LUMP resources into the ones
	// supplied on the command line, ensuring that we don't get any duplicates.

	if (new_iwad)
	{
		if (! (keep_cmd_line_args && !iwadName.empty()))
			iwadName = *new_iwad;
	}

	if (!new_port.empty())
	{
		if (! (keep_cmd_line_args && !portName.empty()))
			portName = new_port;
	}

	if (testingCommandLine.has_value())
		this->testingCommandLine = *testingCommandLine;

	if (! keep_cmd_line_args)
		resourceList.clear();

	for (const fs::path &resource : new_resources)
	{
		M_AddResource_Unique(*this, resource);
	}

	return true;
}


void LoadingData::writeEurekaLump(Wad_file &wad, bool absoluteResources) const
{
	gLog.printf("Writing '%s' lump\n", EUREKA_LUMP);

	int oldie = wad.FindLumpNum(EUREKA_LUMP);
	if (oldie >= 0)
		wad.RemoveLumps(oldie, 1);

	Lump_c &lump = wad.AddLump(EUREKA_LUMP);

	lump.Printf("# Heresy Editor project info\n");

	for (const auto &field : project.serializedFields())
		lump.Printf("%s %s\n", field.first.c_str(), field.second.spaceEscape().c_str());

	if (!gameName.empty())
		lump.Printf("game %s\n", gameName.c_str());

	lump.Printf("testing_command_line %s\n", testingCommandLine.spaceEscape().c_str());

	if (!portName.empty())
		lump.Printf("port %s\n", portName.c_str());

	fs::path pwadPath = fs::absolute(wad.PathName()).remove_filename();

	for (const fs::path &resource : resourceList)
	{
		fs::path absoluteResourcePath = fs::absolute(resource);
		fs::path storedPath = absoluteResources ? absoluteResourcePath :
				fs::proximate(absoluteResourcePath, pwadPath);

		lump.Printf("resource %s\n", escape(storedPath).c_str());
	}
}


//------------------------------------------------------------------------
//  BACKUP SYSTEM
//------------------------------------------------------------------------


// config variables
int config::backup_max_files = 30;
int config::backup_max_space = 60;  // MB
int config::autosave_interval = 2;  // minutes; zero disables recovery snapshots


struct backup_scan_data_t
{
	int low;
	int high;
};


static void backup_scan_file(const fs::path &name, int flags, void *priv_dat)
{
	backup_scan_data_t * data = (backup_scan_data_t *)priv_dat;

	if (flags & SCAN_F_Hidden)
		return;

	if (flags & SCAN_F_IsDir)
		return;

	if (! safe_isdigit(name.u8string()[0]))
		return;

	int num = atoi(name.u8string());

	data->low  = std::min(data->low,  num);
	data->high = std::max(data->high, num);
}


inline static fs::path Backup_Name(const fs::path &dir_name, int slot,
		const fs::path &extension)
{
	fs::path name = std::to_string(slot);
	name += extension.empty() ? fs::path(".wad") : extension;
	return dir_name / name;
}


static void Backup_Prune(const fs::path &dir_name, int b_low, int b_high,
		int wad_size, const fs::path &extension)
{
	// Note: the logic here for checking space is very crude, it assumes
	//       all existing backups have the same size as the currrent wad.

	// do calculations in KB units
	wad_size = wad_size / 1024 + 1;

	int backup_num = 2 + config::backup_max_space * 1024 / wad_size;

	if (backup_num > config::backup_max_files)
		backup_num = config::backup_max_files;

	for ( ; b_low <= b_high - backup_num ; b_low++)
	{
		FileDelete(Backup_Name(dir_name, b_low, extension));
	}
}

void M_BackupWad(const Wad_file *wad)
{
	// disabled ?
	if (config::backup_max_files <= 0 || config::backup_max_space <= 0)
		return;

	// convert wad filename to a directory name in $cache_dir/backups

	fs::path filename = global::cache_dir / "backups" / wad->PathName().filename();
	fs::path dir_name = ReplaceExtension(filename, NULL);

	gLog.debugPrintf("dir_name for backup: '%s'\n", reinterpret_cast<const char *>(dir_name.u8string().c_str()));

	// create the directory if it doesn't already exist
	// (this will fail if it DOES already exist, but that's OK)
	FileMakeDir(dir_name);

	// scan directory to determine lowest and highest numbers in use
	backup_scan_data_t  scan_data;

	scan_data.low  = (1 << 30);
	scan_data.high = 0;

	if (ScanDirectory(dir_name, backup_scan_file, &scan_data) < 0)
	{
		// Hmmm, show a dialog ??
		gLog.printf("WARNING: backup failed (cannot scan dir)\n");
		return;
	}

	int b_low  = scan_data.low;
	int b_high = scan_data.high;
	const fs::path extension = wad->PathName().extension();

	// actually back-up the file

	fs::path dest_name = Backup_Name(dir_name, b_high + 1, extension);

	bool copiedReadOnly = false;
	if(wad->IsReadOnly())
	{
		try
		{
			fs::copy(wad->PathName(), dest_name);
			copiedReadOnly = true;
		}
		catch(const std::runtime_error &e)
		{
			gLog.printf("Failed copying directly %s to %s (%s). Trying to re-save the package.\n", reinterpret_cast<const char *>(wad->PathName().u8string().c_str()), reinterpret_cast<const char *>(dest_name.u8string().c_str()), e.what());
		}
	}
	if (!copiedReadOnly && ! wad->Backup(dest_name))
	{
		// Hmmm, show a dialog ??
		gLog.printf("WARNING: backup failed (cannot copy file)\n");
		return;
	}

	// Include the copy just written in the bounds and use the real package size
	// when possible. A PK3's projected WAD size can differ substantially from
	// its compressed archive size.
	const int new_high = b_high + 1;
	if (b_low > new_high)
		b_low = new_high;
	int package_size = wad->TotalSize();
	std::error_code sizeError;
	const uintmax_t fileSize = fs::file_size(wad->PathName(), sizeError);
	if (!sizeError)
		package_size = static_cast<int>(std::min<uintmax_t>(fileSize,
				std::numeric_limits<int>::max()));
	Backup_Prune(dir_name, b_low, new_high, package_size, extension);

	gLog.printf("Backed up package to: %s\n", reinterpret_cast<const char *>(dest_name.u8string().c_str()));
}

bool RecentKnowledge::hasIwadByPath(const fs::path &path) const
{
	for(const auto &pair : known_iwads)
		if(SString(GetAbsolutePath(pair.second).u8string()).noCaseEqual(SString(GetAbsolutePath(path).u8string())))
			return true;
	return false;
}

//--- editor settings ---
// vi:ts=4:sw=4:noexpandtab
