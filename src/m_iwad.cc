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

#include "m_iwad.h"

#include "main.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <system_error>

namespace
{

#ifdef WIN32
constexpr char PathListSeparator = ';';
#else
constexpr char PathListSeparator = ':';
#endif

using ExpectedIWADs = std::map<std::string, SString>;
using FoundIWADs = std::map<SString, fs::path>;

std::string EnvironmentValue(const char *name)
{
	const char *value = UTF8_getenv(name);
	return value ? std::string(value) : std::string();
}

fs::path UTF8Path(const std::string &value)
{
	return fs::path(reinterpret_cast<const char8_t *>(value.c_str()));
}

std::string PathText(const fs::path &path)
{
	const std::u8string text = path.generic_u8string();
	return std::string(reinterpret_cast<const char *>(text.c_str()), text.size());
}

std::string LowerASCII(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch)
	{
		return static_cast<char>(std::tolower(ch));
	});
	return value;
}

std::string UpperASCII(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch)
	{
		return static_cast<char>(std::toupper(ch));
	});
	return value;
}

int FilenameCasePriority(const fs::path &path)
{
	const std::string filename = PathText(path.filename());
	if(filename == LowerASCII(filename))
		return 0;
	if(filename == UpperASCII(filename))
		return 1;
	return 2;
}

std::string PathKey(const fs::path &path)
{
	std::error_code error;
	fs::path normalized = fs::weakly_canonical(path, error);
	if(error)
	{
		error.clear();
		normalized = fs::absolute(path, error);
		if(error)
			normalized = path;
	}

	std::string key = PathText(normalized.lexically_normal());
#ifdef WIN32
	key = LowerASCII(key);
#endif
	return key;
}

fs::path AbsolutePath(const fs::path &path)
{
	std::error_code error;
	fs::path absolute = fs::absolute(path, error);
	return error ? path.lexically_normal() : absolute.lexically_normal();
}

void AppendUniquePath(std::vector<fs::path> &paths, std::set<std::string> &keys,
		const fs::path &path)
{
	if(path.empty())
		return;

	if(keys.insert(PathKey(path)).second)
		paths.push_back(path);
}

std::vector<fs::path> ParsePathList(const std::string &value, char separator)
{
	std::vector<fs::path> result;
	size_t begin = 0;
	while(begin <= value.size())
	{
		const size_t end = value.find(separator, begin);
		const std::string item = value.substr(begin,
				end == std::string::npos ? std::string::npos : end - begin);
		if(!item.empty())
			result.push_back(UTF8Path(item));
		if(end == std::string::npos)
			break;
		begin = end + 1;
	}
	return result;
}

#ifdef WIN32
fs::path RegistryPath(HKEY root, const wchar_t *keyName, const wchar_t *valueName,
		REGSAM registryView = 0)
{
	HKEY key = nullptr;
	if(RegOpenKeyExW(root, keyName, 0, KEY_QUERY_VALUE | registryView, &key) != ERROR_SUCCESS)
		return {};

	DWORD type = 0;
	DWORD bytes = 0;
	LONG result = RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &bytes);
	if(result != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || bytes == 0)
	{
		RegCloseKey(key);
		return {};
	}

	std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1, L'\0');
	result = RegQueryValueExW(key, valueName, nullptr, &type,
			reinterpret_cast<BYTE *>(buffer.data()), &bytes);
	RegCloseKey(key);
	if(result != ERROR_SUCCESS)
		return {};

	if(type == REG_EXPAND_SZ)
	{
		const DWORD expandedSize = ExpandEnvironmentStringsW(buffer.data(), nullptr, 0);
		if(expandedSize > 0)
		{
			std::vector<wchar_t> expanded(expandedSize, L'\0');
			if(ExpandEnvironmentStringsW(buffer.data(), expanded.data(), expandedSize) > 0)
				return fs::path(expanded.data());
		}
	}
	return fs::path(buffer.data());
}
#endif

bool IsIWADFile(const fs::path &path)
{
	std::ifstream stream(path, std::ios::binary);
	char magic[4] = {};
	return stream.read(magic, sizeof(magic)) &&
			magic[0] == 'I' && magic[1] == 'W' && magic[2] == 'A' && magic[3] == 'D';
}

ExpectedIWADs MakeExpectedIWADs(const std::vector<SString> &games)
{
	ExpectedIWADs expected;
	for(const SString &game : games)
	{
		const SString normalized = game.asLower();
		expected[(normalized + ".wad").get()] = normalized;
	}
	return expected;
}

bool AllIWADsFound(const ExpectedIWADs &expected, const FoundIWADs &found)
{
	return found.size() >= expected.size();
}

bool RecordIWAD(const fs::path &path, const ExpectedIWADs &expected, FoundIWADs &found)
{
	const std::string filename = LowerASCII(PathText(path.filename()));
	const auto wanted = expected.find(filename);
	if(wanted == expected.end() || found.find(wanted->second) != found.end() || !IsIWADFile(path))
		return false;

	found[wanted->second] = AbsolutePath(path);
	gLog.debugPrintf("  found %s: %s\n", wanted->second.c_str(), PathText(path).c_str());
	return true;
}

void ScanFlatDirectory(const fs::path &directory, const ExpectedIWADs &expected,
		FoundIWADs &found)
{
	if(directory.empty() || AllIWADsFound(expected, found))
		return;

	std::error_code error;
	if(!fs::is_directory(directory, error))
		return;

	gLog.debugPrintf("Scanning IWAD directory: %s\n", PathText(directory).c_str());

	// Exact probes avoid enumeration on case-sensitive Unix filesystems.  On
	// Windows and macOS they could open a differently-cased file and return the
	// spelling of the probe instead of the real directory entry.
#if !defined(WIN32) && !defined(__APPLE__)
	for(const auto &[filename, game] : expected)
	{
		if(found.find(game) != found.end())
			continue;
		RecordIWAD(directory / UTF8Path(filename), expected, found);
		if(found.find(game) == found.end())
		{
			SString uppercase(filename);
			RecordIWAD(directory / UTF8Path(uppercase.asUpper().get()), expected, found);
		}
	}

	if(AllIWADsFound(expected, found))
		return;
#endif

	fs::directory_iterator iterator(directory, fs::directory_options::skip_permission_denied, error);
	const fs::directory_iterator end;
	std::vector<fs::path> candidates;
	while(!error && iterator != end)
	{
		const fs::directory_entry entry = *iterator;
		iterator.increment(error);

		std::error_code typeError;
		if(entry.is_regular_file(typeError))
			candidates.push_back(entry.path());
	}

	std::sort(candidates.begin(), candidates.end(), [](const fs::path &left, const fs::path &right)
	{
		const std::string leftName = PathText(left.filename());
		const std::string rightName = PathText(right.filename());
		const std::string leftLower = LowerASCII(leftName);
		const std::string rightLower = LowerASCII(rightName);
		if(leftLower != rightLower)
			return leftLower < rightLower;

		const int leftPriority = FilenameCasePriority(left);
		const int rightPriority = FilenameCasePriority(right);
		if(leftPriority != rightPriority)
			return leftPriority < rightPriority;
		return leftName < rightName;
	});

	for(const fs::path &candidate : candidates)
	{
		if(AllIWADsFound(expected, found))
			break;
		RecordIWAD(candidate, expected, found);
	}
}

bool IsRecognizedGameDirectory(const fs::path &path)
{
	std::string name = LowerASCII(PathText(path.filename()));
	if(name.ends_with(".app"))
		name.resize(name.size() - 4);

	const bool classicDoom = name.starts_with("the ultimate doom") ||
			name.starts_with("ultimate doom") || name.starts_with("doom ii") ||
			name.starts_with("doom 2") || name.starts_with("doom + doom") ||
			name.starts_with("doom classic") || name.starts_with("final doom") ||
			name.starts_with("master levels of doom") ||
			(name.starts_with("doom 3") && name.find("bfg") != std::string::npos);
	return classicDoom || name.starts_with("heretic") || name.starts_with("hexen") ||
			name.starts_with("strife") || name.starts_with("freedoom");
}

bool IsIWADContainerDirectory(const fs::path &path)
{
	const std::string name = LowerASCII(PathText(path.filename()));
	static const std::set<std::string> names =
	{
		"base", "classic", "classics", "content", "contents", "data", "dos",
		"dosbox", "doom", "doom2", "doom ii", "finaldoom", "game", "gamedata",
		"heretic", "hexen", "master", "plutonia", "rerelease", "resources",
		"streamingassets", "strife", "tnt", "wad", "wads"
	};
	return names.find(name) != names.end();
}

void ScanGameDirectory(const fs::path &gameDirectory, const ExpectedIWADs &expected,
		FoundIWADs &found)
{
	// These cover known Steam, GOG, Unity re-release, BFG, and macOS layouts.
	// Their order deliberately prefers the original classic-game data.
	static const std::vector<fs::path> knownRelativeDirectories =
	{
		"", "base", "base/doom2", "base/tnt", "base/plutonia", "base/wads",
		"base/wads/doom", "base/wads/doom2", "rerelease",
		"DOOM_Data/StreamingAssets", "DOOM II_Data/StreamingAssets",
		"Heretic_Data/StreamingAssets", "Hexen_Data/StreamingAssets",
		"Strife_Data/StreamingAssets", "Contents/Resources"
	};
	for(const fs::path &relative : knownRelativeDirectories)
	{
		ScanFlatDirectory(gameDirectory / relative, expected, found);
		if(AllIWADsFound(expected, found))
			return;
	}

	struct PendingDirectory
	{
		fs::path path;
		int depth;
	};

	constexpr int MaximumDepth = 6;
	constexpr size_t MaximumDirectories = 128;
	std::vector<PendingDirectory> pending = {{gameDirectory, 0}};
	std::set<std::string> visited;

	while(!pending.empty() && visited.size() < MaximumDirectories &&
			!AllIWADsFound(expected, found))
	{
		PendingDirectory current = std::move(pending.back());
		pending.pop_back();
		if(!visited.insert(PathKey(current.path)).second)
			continue;

		std::error_code error;
		fs::directory_iterator iterator(current.path,
				fs::directory_options::skip_permission_denied, error);
		const fs::directory_iterator end;
		while(!error && iterator != end)
		{
			const fs::directory_entry entry = *iterator;
			iterator.increment(error);

			std::error_code typeError;
			if(entry.is_regular_file(typeError))
			{
				RecordIWAD(entry.path(), expected, found);
			}
			else if(current.depth < MaximumDepth && entry.is_directory(typeError) &&
					IsIWADContainerDirectory(entry.path()))
			{
				pending.push_back({entry.path(), current.depth + 1});
			}
		}
	}
}

void ScanGameCollection(const fs::path &collection, const ExpectedIWADs &expected,
		FoundIWADs &found)
{
	if(collection.empty() || AllIWADsFound(expected, found))
		return;

	std::error_code error;
	fs::directory_iterator iterator(collection,
			fs::directory_options::skip_permission_denied, error);
	const fs::directory_iterator end;
	while(!error && iterator != end && !AllIWADsFound(expected, found))
	{
		const fs::directory_entry entry = *iterator;
		iterator.increment(error);

		std::error_code typeError;
		if(entry.is_directory(typeError) && IsRecognizedGameDirectory(entry.path()))
			ScanGameDirectory(entry.path(), expected, found);
	}
}

std::vector<std::string> VDFQuotedTokens(const std::string &line)
{
	std::vector<std::string> tokens;
	for(size_t index = 0; index < line.size(); ++index)
	{
		if(line[index] != '"')
			continue;

		std::string token;
		for(++index; index < line.size() && line[index] != '"'; ++index)
		{
			if(line[index] == '\\' && index + 1 < line.size() &&
					(line[index + 1] == '\\' || line[index + 1] == '"'))
				token.push_back(line[++index]);
			else
				token.push_back(line[index]);
		}
		tokens.push_back(std::move(token));
	}
	return tokens;
}

bool LooksLikeAbsolutePath(const std::string &value)
{
	if(value.empty())
		return false;
	if(UTF8Path(value).is_absolute())
		return true;
	return (value.size() >= 3 && std::isalpha(static_cast<unsigned char>(value[0])) &&
			value[1] == ':' && (value[2] == '\\' || value[2] == '/')) ||
			value.starts_with("\\\\");
}

std::vector<fs::path> ParseSteamLibraryFile(const fs::path &filename)
{
	std::vector<fs::path> paths;
	std::set<std::string> keys;
	std::ifstream stream(filename);
	std::string line;
	while(std::getline(stream, line))
	{
		const std::vector<std::string> tokens = VDFQuotedTokens(line);
		if(tokens.size() < 2)
			continue;

		for(size_t index = 0; index + 1 < tokens.size(); ++index)
		{
			const bool namedPath = LowerASCII(tokens[index]) == "path";
			const bool legacyNumber = !tokens[index].empty() &&
					std::all_of(tokens[index].begin(), tokens[index].end(),
					[](unsigned char ch) { return std::isdigit(ch); });
			if((namedPath || legacyNumber) && LooksLikeAbsolutePath(tokens[index + 1]))
				AppendUniquePath(paths, keys, UTF8Path(tokens[index + 1]));
		}
	}
	return paths;
}

fs::path SteamRootFromCandidate(fs::path candidate)
{
	const std::string leaf = LowerASCII(PathText(candidate.filename()));
	if(leaf == "common" && LowerASCII(PathText(candidate.parent_path().filename())) == "steamapps")
		return candidate.parent_path().parent_path();
	if(leaf == "steamapps")
		return candidate.parent_path();
	return candidate;
}

void ScanSteamInstallation(const fs::path &candidate, const ExpectedIWADs &expected,
		FoundIWADs &found, std::set<std::string> &scannedCollections)
{
	const fs::path steamRoot = SteamRootFromCandidate(candidate);
	std::vector<fs::path> libraryRoots;
	std::set<std::string> libraryKeys;
	AppendUniquePath(libraryRoots, libraryKeys, steamRoot);

	for(const fs::path &manifest :
			{steamRoot / "steamapps" / "libraryfolders.vdf",
			 steamRoot / "config" / "libraryfolders.vdf"})
	{
		for(const fs::path &path : ParseSteamLibraryFile(manifest))
			AppendUniquePath(libraryRoots, libraryKeys, path);
	}

	for(const fs::path &libraryRoot : libraryRoots)
	{
		const fs::path common = libraryRoot / "steamapps" / "common";
		if(scannedCollections.insert(PathKey(common)).second)
			ScanGameCollection(common, expected, found);
		if(AllIWADsFound(expected, found))
			return;
	}
}

} // namespace


IWADSearchLocations M_SystemIWADSearchLocations(const fs::path &configDirectory,
		const fs::path &legacyConfigDirectory)
{
	IWADSearchLocations locations;
	std::set<std::string> preferredKeys;
	std::set<std::string> steamKeys;
	std::set<std::string> collectionKeys;
	std::set<std::string> fallbackKeys;

	AppendUniquePath(locations.preferredDirectories, preferredKeys, configDirectory / "iwads");
	if(!legacyConfigDirectory.empty())
		AppendUniquePath(locations.preferredDirectories, preferredKeys,
				legacyConfigDirectory / "iwads");

	for(const fs::path &path : ParsePathList(EnvironmentValue("DOOMWADPATH"), PathListSeparator))
		AppendUniquePath(locations.preferredDirectories, preferredKeys, path);
	const std::string doomWadDir = EnvironmentValue("DOOMWADDIR");
	if(!doomWadDir.empty())
		AppendUniquePath(locations.preferredDirectories, preferredKeys, UTF8Path(doomWadDir));

	std::string userHomeText = EnvironmentValue("HOME");
#ifdef WIN32
	if(userHomeText.empty())
		userHomeText = EnvironmentValue("USERPROFILE");
#endif
	const fs::path userHome = UTF8Path(userHomeText);

	const std::string steamEnvironment = EnvironmentValue("STEAM_COMPAT_CLIENT_INSTALL_PATH");
	if(!steamEnvironment.empty())
		AppendUniquePath(locations.steamInstallations, steamKeys, UTF8Path(steamEnvironment));

#ifdef WIN32
	AppendUniquePath(locations.steamInstallations, steamKeys,
			RegistryPath(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath"));
	for(REGSAM view : {KEY_WOW64_32KEY, KEY_WOW64_64KEY})
	{
		AppendUniquePath(locations.steamInstallations, steamKeys,
				RegistryPath(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath", view));
	}
	for(const char *variable : {"ProgramFiles(x86)", "ProgramFiles", "ProgramW6432"})
	{
		const std::string value = EnvironmentValue(variable);
		if(value.empty())
			continue;
		const fs::path root = UTF8Path(value);
		AppendUniquePath(locations.steamInstallations, steamKeys, root / "Steam");
		AppendUniquePath(locations.gameCollections, collectionKeys, root / "GOG Galaxy" / "Games");
	}
	AppendUniquePath(locations.steamInstallations, steamKeys,
			userHome / "AppData" / "Local" / "Steam");
	AppendUniquePath(locations.gameCollections, collectionKeys, "C:/GOG Games");
	AppendUniquePath(locations.gameCollections, collectionKeys, userHome / "Games");
	for(const fs::path &path : {fs::path("C:/doom"), fs::path("C:/doom2"), fs::path("C:/doom95")})
		AppendUniquePath(locations.fallbackDirectories, fallbackKeys, path);
	const std::string appdata = EnvironmentValue("APPDATA");
	if(!appdata.empty())
		AppendUniquePath(locations.fallbackDirectories, fallbackKeys, UTF8Path(appdata) / "gzdoom");
#elif defined(__APPLE__)
	AppendUniquePath(locations.steamInstallations, steamKeys,
			userHome / "Library" / "Application Support" / "Steam");
	AppendUniquePath(locations.gameCollections, collectionKeys, "/Applications");
	AppendUniquePath(locations.gameCollections, collectionKeys, userHome / "Applications");
	AppendUniquePath(locations.gameCollections, collectionKeys, userHome / "Games");
	AppendUniquePath(locations.fallbackDirectories, fallbackKeys,
			userHome / "Library" / "Application Support" / "gzdoom");
#else
	for(const fs::path &steam :
			{userHome / ".steam" / "debian-installation",
			 userHome / ".steam" / "steam",
			 userHome / ".steam" / "root",
			 userHome / ".local" / "share" / "Steam",
			 userHome / ".var" / "app" / "com.valvesoftware.Steam" / "data" / "Steam",
			 userHome / "snap" / "steam" / "common" / ".local" / "share" / "Steam"})
	{
		AppendUniquePath(locations.steamInstallations, steamKeys, steam);
	}
	AppendUniquePath(locations.gameCollections, collectionKeys, userHome / "Games");
	AppendUniquePath(locations.gameCollections, collectionKeys, userHome / "games");
	AppendUniquePath(locations.gameCollections, collectionKeys, userHome / "GOG Games");
	AppendUniquePath(locations.gameCollections, collectionKeys, userHome / "Games" / "Heroic");

	for(const fs::path &base :
			{fs::path("/usr/share/games"), fs::path("/usr/share"),
			 fs::path("/usr/local/share/games"), fs::path("/usr/local/share"),
			 fs::path("/usr/local/games")})
	{
		for(const fs::path &game : {fs::path("doom"), fs::path("heretic"),
				fs::path("hexen"), fs::path("strife")})
		{
			AppendUniquePath(locations.fallbackDirectories, fallbackKeys, base / game);
		}
	}
	AppendUniquePath(locations.fallbackDirectories, fallbackKeys, "/opt/doom");

	const std::string xdgDataHome = EnvironmentValue("XDG_DATA_HOME");
	const fs::path dataHome = xdgDataHome.empty() ? userHome / ".local" / "share" :
			UTF8Path(xdgDataHome);
	for(const fs::path &path :
			{dataHome / "doom", dataHome / "games" / "doom", dataHome / "gzdoom",
			 userHome / ".config" / "gzdoom",
			 userHome / ".var" / "app" / "org.zdoom.GZDoom" / ".config" / "gzdoom"})
	{
		AppendUniquePath(locations.fallbackDirectories, fallbackKeys, path);
	}

	std::string xdgDataDirs = EnvironmentValue("XDG_DATA_DIRS");
	if(xdgDataDirs.empty())
		xdgDataDirs = "/usr/local/share:/usr/share";
	for(const fs::path &base : ParsePathList(xdgDataDirs, ':'))
	{
		for(const fs::path &game : {fs::path("doom"), fs::path("heretic"),
				fs::path("hexen"), fs::path("strife")})
		{
			AppendUniquePath(locations.fallbackDirectories, fallbackKeys, base / game);
			AppendUniquePath(locations.fallbackDirectories, fallbackKeys, base / "games" / game);
		}
	}
#endif

	for(const fs::path &path :
			{userHome / "iwads", userHome / ".iwads", userHome / "doom",
			 userHome / "Doom", userHome / "Games" / "Doom", userHome / "games" / "doom"})
	{
		AppendUniquePath(locations.fallbackDirectories, fallbackKeys, path);
	}

	std::error_code error;
	AppendUniquePath(locations.fallbackDirectories, fallbackKeys, fs::current_path(error));
	return locations;
}


std::map<SString, fs::path> M_DiscoverIWADs(const std::vector<SString> &games,
		const IWADSearchLocations &locations)
{
	const ExpectedIWADs expected = MakeExpectedIWADs(games);
	FoundIWADs found;
	std::set<std::string> scannedDirectories;
	std::set<std::string> scannedCollections;

	auto scanDirectories = [&](const std::vector<fs::path> &directories)
	{
		for(const fs::path &directory : directories)
		{
			if(scannedDirectories.insert(PathKey(directory)).second)
				ScanFlatDirectory(directory, expected, found);
			if(AllIWADsFound(expected, found))
				break;
		}
	};

	scanDirectories(locations.preferredDirectories);
	if(!AllIWADsFound(expected, found))
	{
		for(const fs::path &steam : locations.steamInstallations)
		{
			ScanSteamInstallation(steam, expected, found, scannedCollections);
			if(AllIWADsFound(expected, found))
				break;
		}
	}
	if(!AllIWADsFound(expected, found))
	{
		for(const fs::path &collection : locations.gameCollections)
		{
			if(scannedCollections.insert(PathKey(collection)).second)
				ScanGameCollection(collection, expected, found);
			if(AllIWADsFound(expected, found))
				break;
		}
	}
	if(!AllIWADsFound(expected, found))
		scanDirectories(locations.fallbackDirectories);

	return found;
}


bool M_IsIWADForGame(const fs::path &path, const SString &game)
{
	const ExpectedIWADs expected = MakeExpectedIWADs({game});
	FoundIWADs found;
	return RecordIWAD(path, expected, found);
}

//--- editor settings ---
// vi:ts=4:sw=4:noexpandtab
