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

#include "Errors.h"
#include "Instance.h"

#include "main.h"

#include "m_files.h"
#include "m_loadsave.h"
#include "m_mapinfo.h"
#include "m_package.h"
#include "m_parse.h"
#include "m_testmap.h"
#include "w_wad.h"

#include "ui_menu.h"
#include "ui_window.h"

#include <cctype>
#include <chrono>
#include <deque>
#include <atomic>
#include <mutex>
#include <set>
#include <system_error>
#include <thread>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

class DirChangeContext
{
public:
	explicit DirChangeContext(const fs::path &path);
	~DirChangeContext();
private:
	char old_dir[FL_PATH_MAX] = {};
};

DirChangeContext::DirChangeContext(const fs::path &path)
{
	// remember the previous working directory
	if(!getcwd(old_dir, sizeof(old_dir)))
		old_dir[0] = '\0';
	old_dir[FL_PATH_MAX - 1] = '\0';	// just in case
	gLog.printf("Changing current dir to: %s\n", reinterpret_cast<const char *>(path.u8string().c_str()));
	if(!FileChangeDir(path))
		throw std::runtime_error("Failed changing directory to port location");
}

DirChangeContext::~DirChangeContext()
{
	// restore previous working directory
	if(*old_dir)
		FileChangeDir(fs::path(reinterpret_cast<const char8_t *>(old_dir)));
}

static SString QueryName(const SString &port, const SString &cgame)
{
	SYS_ASSERT(port.good());

	SString game = cgame;

	if (port.noCaseEqual("vanilla"))
	{
		if (game.empty())
			game = "doom2";

		return "vanilla_" + game;
	}

	return port;
}

static bool isMacOSAppBundle(const fs::path &path)
{
#ifdef __APPLE__
	std::u8string u8path = path.u8string();
	const char *cpath = reinterpret_cast<const char *>(u8path.c_str());
	CFStringRef pathString = CFStringCreateWithCString(kCFAllocatorDefault, cpath, kCFStringEncodingUTF8);
	if(!pathString)
	{
		gLog.printf("ERROR: Failed allocating macOS app bundle path CF string: %s\n", cpath);
		return false;
	}
	CFURLRef url = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, pathString, kCFURLPOSIXPathStyle, true);

	CFRelease(pathString);
	if(!url)
	{
		gLog.printf("ERROR: Failed allocating macOS app bundle CF URL: %s\n", cpath);

		return false;
	}

	CFBundleRef bundle = CFBundleCreate(kCFAllocatorDefault, url);
	CFRelease(url);
	if(!bundle)
	{
		gLog.printf("Could not load, or invalid macOS app CF bundle: %s\n", cpath);

		return false;
	}

	CFDictionaryRef infoDict = CFBundleGetInfoDictionary(bundle);
	CFRelease(bundle);

	return !!infoDict;
#else
	return false;
#endif
}

namespace
{

#ifdef WIN32
constexpr char PathListSeparator = ';';
#else
constexpr char PathListSeparator = ':';
#endif

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
	std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch)
	{
		return static_cast<char>(std::tolower(ch));
	});
#endif
	return key;
}

fs::path AbsolutePath(const fs::path &path)
{
	std::error_code error;
	const fs::path absolute = fs::absolute(path, error);
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
	std::vector<fs::path> paths;
	size_t start = 0;
	while(start <= value.size())
	{
		const size_t end = value.find(separator, start);
		const std::string item = value.substr(start,
				end == std::string::npos ? std::string::npos : end - start);
		if(!item.empty())
			paths.push_back(UTF8Path(item));
		if(end == std::string::npos)
			break;
		start = end + 1;
	}
	return paths;
}

std::vector<fs::path> PortExecutableNames(const SString &port)
{
	std::vector<fs::path> names;
	auto appendEngine = [&](const char *lowerName, const char *displayName)
	{
#ifdef WIN32
		names.emplace_back(std::string(lowerName) + ".exe");
		names.emplace_back(lowerName);
#elif defined(__APPLE__)
		names.emplace_back(lowerName);
		names.emplace_back(std::string(displayName) + ".app");
#else
		names.emplace_back(lowerName);
		names.emplace_back(std::string(displayName) + ".AppImage");
#endif
	};

	if(port.noCaseEqual("biaseddoom"))
	{
		appendEngine("biaseddoom", "BiasedDoom");
		appendEngine("gzdoom", "GZDoom");
	}
	else if(port.noCaseEqual("zdoom") || port.noCaseEqual("gzdoom"))
	{
		appendEngine("gzdoom", "GZDoom");
		appendEngine("biaseddoom", "BiasedDoom");
		appendEngine("zdoom", "ZDoom");
	}
	return names;
}

bool IsExecutableCandidate(const fs::path &path)
{
	if(path.empty())
		return false;

	std::error_code error;
	if(fs::is_regular_file(path, error) && !error)
	{
#ifndef WIN32
		return access(path.c_str(), X_OK) == 0;
#else
		return true;
#endif
	}

#ifdef __APPLE__
	error.clear();
	if(path.extension() == ".app" && fs::is_directory(path, error) && !error)
		return isMacOSAppBundle(path);
#endif
	return false;
}

bool SearchCancelled(const PortExecutableSearchControl *control)
{
	return control && control->cancelled && control->cancelled();
}

void ReportSearchProgress(const PortExecutableSearchControl *control,
		PortExecutableSearchPhase phase, const fs::path &location,
		size_t candidatesChecked, size_t directoriesScanned)
{
	if(control && control->progress)
	{
		control->progress({phase, location, candidatesChecked,
				directoriesScanned});
	}
}

void AppendBuildCandidates(std::vector<fs::path> &candidates,
		std::set<std::string> &keys, const fs::path &sourceRoot,
		const std::vector<fs::path> &executableNames)
{
	if(sourceRoot.empty())
		return;
	for(const fs::path &configuration :
			{fs::path(), fs::path("Release"), fs::path("RelWithDebInfo"), fs::path("Debug")})
	{
		for(const fs::path &name : executableNames)
			AppendUniquePath(candidates, keys, sourceRoot / "build" / configuration / name);
	}
}

std::optional<fs::path> ScanExecutableTree(const fs::path &root,
		const std::vector<fs::path> &executableNames, std::set<std::string> &checked,
		std::set<std::string> &visitedDirectories,
		const PortExecutableSearchControl *control, size_t &candidatesChecked,
		size_t &directoriesScanned)
{
	if(root.empty() || SearchCancelled(control))
		return std::nullopt;

	auto check = [&](const fs::path &candidate) -> std::optional<fs::path>
	{
		if(SearchCancelled(control) || candidate.empty() ||
				!checked.insert(PathKey(candidate)).second)
			return std::nullopt;
		++candidatesChecked;
		ReportSearchProgress(control,
				PortExecutableSearchPhase::recursiveDirectory, candidate,
				candidatesChecked, directoriesScanned);
		if(SearchCancelled(control))
			return std::nullopt;
		if(IsExecutableCandidate(candidate))
			return AbsolutePath(candidate);
		return std::nullopt;
	};

	std::vector<std::string> wantedNames;
	wantedNames.reserve(executableNames.size());
	for(const fs::path &name : executableNames)
		wantedNames.push_back(LowerASCII(PathText(name.filename())));

	if(const std::string rootName = LowerASCII(PathText(root.filename()));
			std::find(wantedNames.begin(), wantedNames.end(), rootName) != wantedNames.end())
	{
		if(const std::optional<fs::path> found = check(root))
			return found;
	}

	// A breadth-first walk finds normal install and build layouts before deeply
	// nested caches. Directory symlinks are not followed, avoiding cycles while
	// still allowing a symlinked executable itself to be selected.
	std::deque<fs::path> pending = {root};
	while(!pending.empty())
	{
		if(SearchCancelled(control))
			return std::nullopt;
		const fs::path directory = std::move(pending.front());
		pending.pop_front();
		if(!visitedDirectories.insert(PathKey(directory)).second)
			continue;
		++directoriesScanned;
		ReportSearchProgress(control,
				PortExecutableSearchPhase::recursiveDirectory, directory,
				candidatesChecked, directoriesScanned);
		if(SearchCancelled(control))
			return std::nullopt;

		std::error_code error;
		fs::directory_iterator iterator(directory,
				fs::directory_options::skip_permission_denied, error);
		const fs::directory_iterator end;
		std::vector<fs::directory_entry> entries;
		while(!error && iterator != end)
		{
			if(SearchCancelled(control))
				return std::nullopt;
			entries.push_back(*iterator);
			iterator.increment(error);
		}

		std::sort(entries.begin(), entries.end(), [](const fs::directory_entry &left,
				const fs::directory_entry &right)
		{
			const std::string leftName = PathText(left.path().filename());
			const std::string rightName = PathText(right.path().filename());
			const std::string leftLower = LowerASCII(leftName);
			const std::string rightLower = LowerASCII(rightName);
			return leftLower == rightLower ? leftName < rightName : leftLower < rightLower;
		});
		if(SearchCancelled(control))
			return std::nullopt;

		// Preserve engine preference even when multiple matching filenames occur
		// in the same directory (BiasedDoom first for its dedicated profile,
		// GZDoom first for the shared ZDoom profile).
		for(const std::string &wanted : wantedNames)
		{
			for(const fs::directory_entry &entry : entries)
			{
				if(SearchCancelled(control))
					return std::nullopt;
				if(LowerASCII(PathText(entry.path().filename())) != wanted)
					continue;
				if(const std::optional<fs::path> found = check(entry.path()))
					return found;
			}
		}

		for(const fs::directory_entry &entry : entries)
		{
			if(SearchCancelled(control))
				return std::nullopt;
			std::error_code typeError;
			if(entry.is_symlink(typeError))
				continue;
			typeError.clear();
			if(entry.is_directory(typeError) && !typeError)
				pending.push_back(entry.path());
		}
	}
	return std::nullopt;
}

} // namespace

PortExecutableSearchLocations M_SystemPortExecutableSearchLocations(const SString &port)
{
	PortExecutableSearchLocations locations;
	const std::vector<fs::path> names = PortExecutableNames(port);
	if(names.empty())
		return locations;

	std::set<std::string> configuredKeys;
	auto appendEnvironmentPath = [&](const char *variable)
	{
		AppendUniquePath(locations.configuredPaths, configuredKeys,
				UTF8Path(EnvironmentValue(variable)));
	};
	if(port.noCaseEqual("biaseddoom"))
	{
		appendEnvironmentPath("BIASEDDOOM_EXE");
		appendEnvironmentPath("GZDOOM_EXE");
	}
	else
	{
		appendEnvironmentPath("GZDOOM_EXE");
		appendEnvironmentPath("BIASEDDOOM_EXE");
	}

	std::set<std::string> searchKeys;
	for(const fs::path &directory : ParsePathList(EnvironmentValue("PATH"), PathListSeparator))
		AppendUniquePath(locations.searchDirectories, searchKeys, directory);

	std::set<std::string> fallbackKeys;
	const fs::path installDirectory = global::install_dir.empty() ? fs::path() :
			AbsolutePath(global::install_dir);
	auto appendDirectory = [&](const fs::path &directory)
	{
		if(directory.empty())
			return;
		for(const fs::path &name : names)
			AppendUniquePath(locations.fallbackCandidates, fallbackKeys, directory / name);
	};

	// Portable bundles may place the engine beside the editor data, while a
	// system installation normally places both executables in the same prefix.
	appendDirectory(installDirectory);
	fs::path installPrefix;
	if(installDirectory.filename() == "heresy" || installDirectory.filename() == "eureka")
	{
		if(installDirectory.parent_path().filename() == "share")
			installPrefix = installDirectory.parent_path().parent_path();
		else
			installPrefix = installDirectory.parent_path();
	}
	if(!installPrefix.empty())
	{
		appendDirectory(installPrefix / "bin");
		appendDirectory(installPrefix / "games");
	}

	std::string userHomeText = EnvironmentValue("HOME");
#ifdef WIN32
	if(userHomeText.empty())
		userHomeText = EnvironmentValue("USERPROFILE");
#endif
	const fs::path userHome = UTF8Path(userHomeText);

	// Probe the most common source-tree layouts before starting the recursive
	// search. This makes the usual local-development case effectively instant.
	if(!userHome.empty())
	{
		for(const fs::path &sourceRoot :
				{userHome / "workspace" / "BiasedDoom", userHome / "BiasedDoom",
				 userHome / "workspace" / "GZDoom", userHome / "GZDoom",
				 userHome / "workspace" / "gzdoom", userHome / "gzdoom"})
		{
			AppendBuildCandidates(locations.fallbackCandidates, fallbackKeys, sourceRoot, names);
		}
	}
	if(!installDirectory.empty())
	{
		for(const fs::path &project : {fs::path("BiasedDoom"), fs::path("GZDoom"),
				fs::path("gzdoom")})
		{
			AppendBuildCandidates(locations.fallbackCandidates, fallbackKeys,
					installDirectory.parent_path() / project, names);
		}
	}

	std::set<std::string> recursiveKeys;
	auto appendRecursiveRoot = [&](const fs::path &root)
	{
		if(root.empty() || root == root.root_path())
			return;
		AppendUniquePath(locations.recursiveRoots, recursiveKeys, root);
	};
	appendRecursiveRoot(installDirectory);
	if(!installPrefix.empty() && installPrefix != fs::path("/usr"))
		appendRecursiveRoot(installPrefix);
	if(!userHome.empty())
	{
		// Narrow, likely roots go first; the final home-root scan catches custom
		// checkout and installation layouts at any nesting depth.
		appendRecursiveRoot(userHome / "workspace");
		appendRecursiveRoot(userHome / "Applications");
		appendRecursiveRoot(userHome / "Games");
		appendRecursiveRoot(userHome / ".local");
		appendRecursiveRoot(userHome);
	}

#ifdef WIN32
	for(const char *variable : {"LOCALAPPDATA", "ProgramFiles", "ProgramFiles(x86)", "ProgramW6432"})
	{
		const fs::path root = UTF8Path(EnvironmentValue(variable));
		if(root.empty())
			continue;
		const fs::path installationRoot = std::string(variable) == "LOCALAPPDATA" ?
				root / "Programs" : root;
		appendDirectory(installationRoot / "BiasedDoom");
		appendDirectory(installationRoot / "GZDoom");
		appendRecursiveRoot(installationRoot);
	}
#elif defined(__APPLE__)
	AppendUniquePath(locations.fallbackCandidates, fallbackKeys,
			userHome / "Applications" / "BiasedDoom.app");
	AppendUniquePath(locations.fallbackCandidates, fallbackKeys,
			userHome / "Applications" / "GZDoom.app");
	AppendUniquePath(locations.fallbackCandidates, fallbackKeys,
			"/Applications/BiasedDoom.app");
	AppendUniquePath(locations.fallbackCandidates, fallbackKeys,
			"/Applications/GZDoom.app");
	appendDirectory("/usr/local/bin");
	appendDirectory("/opt/homebrew/bin");
	appendRecursiveRoot("/Applications");
	appendRecursiveRoot("/usr/local");
	appendRecursiveRoot("/opt/homebrew");
#else
	appendDirectory(userHome / ".local" / "bin");
	appendDirectory("/usr/local/bin");
	appendDirectory("/usr/local/games");
	appendDirectory("/usr/bin");
	appendDirectory("/usr/games");
	appendDirectory("/opt/BiasedDoom");
	appendDirectory("/opt/GZDoom");
	AppendUniquePath(locations.fallbackCandidates, fallbackKeys,
			userHome / "Applications" / "BiasedDoom.AppImage");
	AppendUniquePath(locations.fallbackCandidates, fallbackKeys,
			userHome / "Applications" / "biaseddoom.AppImage");
	AppendUniquePath(locations.fallbackCandidates, fallbackKeys,
			userHome / "Applications" / "GZDoom.AppImage");
	AppendUniquePath(locations.fallbackCandidates, fallbackKeys,
			userHome / "Applications" / "gzdoom.AppImage");
	appendRecursiveRoot("/usr/local");
	appendRecursiveRoot("/opt");
#endif

	return locations;
}

std::optional<fs::path> M_FindPortExecutable(const SString &port,
		const PortExecutableSearchLocations &locations,
		const PortExecutableSearchControl *control)
{
	const std::vector<fs::path> names = PortExecutableNames(port);
	if(names.empty())
		return std::nullopt;

	std::set<std::string> checked;
	size_t candidatesChecked = 0;
	size_t directoriesScanned = 0;
	auto check = [&](const fs::path &candidate,
			PortExecutableSearchPhase phase) -> std::optional<fs::path>
	{
		if(SearchCancelled(control) || candidate.empty() ||
				!checked.insert(PathKey(candidate)).second)
			return std::nullopt;
		++candidatesChecked;
		ReportSearchProgress(control, phase, candidate, candidatesChecked,
				directoriesScanned);
		if(SearchCancelled(control))
			return std::nullopt;
		if(IsExecutableCandidate(candidate))
			return AbsolutePath(candidate);
		return std::nullopt;
	};

	for(const fs::path &configured : locations.configuredPaths)
	{
		if(const std::optional<fs::path> found = check(configured,
				PortExecutableSearchPhase::configuredPath))
			return found;
	}
	if(SearchCancelled(control))
		return std::nullopt;
	for(const fs::path &directory : locations.searchDirectories)
	{
		for(const fs::path &name : names)
		{
			if(const std::optional<fs::path> found = check(directory / name,
					PortExecutableSearchPhase::systemPath))
				return found;
		}
		if(SearchCancelled(control))
			return std::nullopt;
	}
	for(const fs::path &candidate : locations.fallbackCandidates)
	{
		if(const std::optional<fs::path> found = check(candidate,
				PortExecutableSearchPhase::fallbackCandidate))
			return found;
	}
	if(SearchCancelled(control))
		return std::nullopt;

	std::set<std::string> visitedDirectories;
	for(const fs::path &root : locations.recursiveRoots)
	{
		if(SearchCancelled(control))
			return std::nullopt;
		if(const std::optional<fs::path> found = ScanExecutableTree(root, names, checked,
				visitedDirectories, control, candidatesChecked,
				directoriesScanned))
		{
			return found;
		}
	}
	return std::nullopt;
}

bool M_PortExecutableAutoDiscoverySupported(const SString &port) noexcept
{
	return !PortExecutableNames(port).empty();
}

PortExecutablePathValidation M_ValidatePortExecutablePath(
		const fs::path &path) noexcept
{
	if(path.empty())
	{
		return {PortExecutablePathIssue::empty,
				"No engine selected. Type a path, browse, or use Auto Detect."};
	}
	if(IsExecutableCandidate(path))
	{
		return {PortExecutablePathIssue::none,
				"Ready — the selected engine path is executable."};
	}

	std::error_code error;
	const bool exists = fs::exists(path, error);
	if(error)
	{
		return {PortExecutablePathIssue::inaccessible,
				"This path could not be inspected. Check its permissions and parent folder."};
	}
	if(!exists)
	{
		return {PortExecutablePathIssue::notFound,
				"No file exists at this path."};
	}
	if(fs::is_directory(path, error) && !error)
	{
		return {PortExecutablePathIssue::directory,
				"This is a folder, not a launchable engine file."};
	}
	if(error)
	{
		return {PortExecutablePathIssue::inaccessible,
				"This path could not be inspected. Check its permissions."};
	}
	if(fs::is_regular_file(path, error) && !error)
	{
		return {PortExecutablePathIssue::notExecutable,
				"The selected file is not executable. On Unix, check its execute permission."};
	}
	if(error)
	{
		return {PortExecutablePathIssue::inaccessible,
				"This path could not be inspected. Check its permissions."};
	}
	return {PortExecutablePathIssue::unsupportedType,
			"The selected path is not a regular executable file."};
}

static bool M_IsPortPathValid(const fs::path &path)
{
	return M_ValidatePortExecutablePath(path).valid();
}

namespace
{

struct AutoPortPathResult
{
	std::optional<fs::path> executable;
	bool cancelled = false;
};

class UI_PortExecutableDiscovery : public UI_Escapable_Window
{
private:
	Fl_Box *status_ = nullptr;
	Fl_Progress *progress_ = nullptr;
	Fl_Button *cancel_ = nullptr;
	std::atomic<bool> cancelRequested_{false};

	static void cancelCallback(Fl_Widget *, void *data)
	{
		auto that = static_cast<UI_PortExecutableDiscovery *>(data);
		that->cancelRequested_.store(true, std::memory_order_release);
		that->cancel_->deactivate();
		that->status_->copy_label(
				"Cancelling engine discovery...\nWaiting for the current filesystem operation to finish.");
		that->redraw();
	}

	static const char *PhaseLabel(PortExecutableSearchPhase phase)
	{
		switch(phase)
		{
			case PortExecutableSearchPhase::configuredPath:
				return "Checking configured engine paths";
			case PortExecutableSearchPhase::systemPath:
				return "Checking the system PATH";
			case PortExecutableSearchPhase::fallbackCandidate:
				return "Checking common installation locations";
			case PortExecutableSearchPhase::recursiveDirectory:
				return "Searching installation and home folders";
		}
		return "Searching for a game engine";
	}

public:
	explicit UI_PortExecutableDiscovery(const SString &port) :
		UI_Escapable_Window(640, 215, "Game Engine Discovery")
	{
		SString target = port.noCaseEqual("biaseddoom") ?
				"Searching for BiasedDoom or GZDoom" :
				"Searching for GZDoom or BiasedDoom";
		Fl_Box *heading = new Fl_Box(20, 16, 600, 30);
		heading->copy_label(target.c_str());
		heading->labelfont(FL_HELVETICA_BOLD);
		heading->labelsize(17);
		heading->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

		status_ = new Fl_Box(20, 52, 600, 67,
				"Checking configured paths and common installation locations...");
		status_->align(FL_ALIGN_TOP | FL_ALIGN_LEFT | FL_ALIGN_INSIDE |
				FL_ALIGN_WRAP);

		progress_ = new Fl_Progress(20, 128, 600, 20);
		progress_->minimum(0);
		progress_->maximum(100);
		progress_->value(0);
		progress_->label("Searching...");
		progress_->color(FL_LIGHT2, fl_rgb_color(0x34, 0x78, 0xC2));

		cancel_ = new Fl_Button(500, 165, 120, 32, "Cancel Search");
		cancel_->callback(cancelCallback, this);

		end();
		resizable(nullptr);
		callback(cancelCallback, this);
	}

	AutoPortPathResult Run(const SString &port,
			const PortExecutableSearchLocations &locations)
	{
		struct SharedProgress
		{
			std::mutex mutex;
			PortExecutableSearchProgress latest;
			bool received = false;
		} shared;

		std::atomic<bool> finished{false};
		std::optional<fs::path> executable;
		std::exception_ptr workerError;
		PortExecutableSearchControl control;
		control.cancelled = [&]()
		{
			return cancelRequested_.load(std::memory_order_acquire);
		};
		control.progress = [&](const PortExecutableSearchProgress &update)
		{
			std::lock_guard<std::mutex> lock(shared.mutex);
			shared.latest = update;
			shared.received = true;
		};

		std::thread worker([&]()
		{
			try
			{
				executable = M_FindPortExecutable(port, locations, &control);
			}
			catch(...)
			{
				workerError = std::current_exception();
			}
			finished.store(true, std::memory_order_release);
		});

		const auto started = std::chrono::steady_clock::now();
		set_modal();
		show();
		while(!finished.load(std::memory_order_acquire))
		{
			Fl::wait(0.03);
			const auto elapsed = std::chrono::steady_clock::now() - started;
			if(cancelRequested_.load(std::memory_order_acquire))
				continue;

			PortExecutableSearchProgress latest;
			bool received = false;
			{
				std::lock_guard<std::mutex> lock(shared.mutex);
				latest = shared.latest;
				received = shared.received;
			}
			if(received)
			{
				const SString detail = SString::printf(
						"%s — %zu folder%s scanned, %zu candidate%s checked\n%s",
						PhaseLabel(latest.phase), latest.directoriesScanned,
						latest.directoriesScanned == 1 ? "" : "s",
						latest.candidatesChecked,
						latest.candidatesChecked == 1 ? "" : "s",
						PathText(latest.location).c_str());
				status_->copy_label(detail.c_str());
			}
			const auto milliseconds = std::chrono::duration_cast<
					std::chrono::milliseconds>(elapsed).count();
			progress_->value(static_cast<double>((milliseconds / 12) % 101));
			redraw();
		}

		worker.join();
		hide();
		if(workerError)
			std::rethrow_exception(workerError);

		const bool cancelled = cancelRequested_.load(std::memory_order_acquire);
		return {cancelled ? std::optional<fs::path>{} : executable, cancelled};
	}
};

AutoPortPathResult M_AutoFindPortPath(const SString &port)
{
	const PortExecutableSearchLocations locations =
			M_SystemPortExecutableSearchLocations(port);
	if(locations.configuredPaths.empty() && locations.searchDirectories.empty() &&
			locations.fallbackCandidates.empty() &&
			locations.recursiveRoots.empty())
	{
		return {};
	}
	return UI_PortExecutableDiscovery(port).Run(port, locations);
}

} // namespace

class UI_PortPathDialog : public UI_Escapable_Window
{
public:
	static const int PADDING = 20;
	static const int LABEL_HEIGHT = 30;
	static const int INTER_LABEL_SPACE = 5;
	static const int LABEL_TEXT_BOX_SPACE = 15;
	static const int TEXT_BOX_LEFT = 118;
	static const int TEXT_BOX_HEIGHT = 26;
	static const int TEXT_BOX_BUTTON_SPACE = 10;
	static const int BROWSE_BUTTON_WIDTH = 90;
	static const int AUTO_BUTTON_WIDTH = 110;
	static const int STATUS_HEIGHT = 42;
	static const int PADDING_BEFORE_BOTTOM = 45;
	static const int BOTTOM_BAR_HEIGHT = 70;
	static const int BOTTOM_BAR_OUTSET = 10;
	static const int BOTTOM_BUTTON_WIDTH = 95;
	static const int BOTTOM_BUTTON_HEIGHT = 30;
	static const int BOTTOM_PADDING = 15;
	static const int BOTTOM_RIGHT_PADDING = 25;
	static const int BOTTOM_BUTTON_SPACING = 45;

	Fl_Input *exe_input;
	Fl_Box *path_status;

	Fl_Button *ok_but;
	Fl_Button *cancel_but;

	// the chosen engine path, or empty if cancelled
	fs::path exe_name;

	bool want_close = false;

	const Instance& inst;
	const SString port_name;

private:
	Fl_Input* other_args;
	SString command_line;

public:
	void SetStatus(const SString &message)
	{
		path_status->copy_label(message.c_str());
		path_status->redraw();
	}

	void SetEXE(const fs::path &newbie, bool automaticallyDetected = false)
	{
		exe_name = newbie;
		exe_input->value(reinterpret_cast<const char *>(exe_name.u8string().c_str()));
		const PortExecutablePathValidation validation =
				M_ValidatePortExecutablePath(exe_name);
		if (validation.valid())
		{
			ok_but->activate();
			SetStatus(automaticallyDetected ?
					"Automatically detected — the engine path is executable and ready." :
					validation.message);
		}
		else
		{
			ok_but->deactivate();
			SetStatus(validation.message);
		}
	}

	void SetCommandLine(const SString& command_line)
	{
		this->command_line = command_line;
		other_args->value(command_line.c_str());
	}

	void HideCommandLine()
	{
		other_args->deactivate();
	}

	const char* getCommandLine() const
	{
		return other_args->value();
	}

	static void ok_callback(Fl_Widget *w, void *data)
	{
		UI_PortPathDialog * that = (UI_PortPathDialog *)data;

		that->want_close = true;
	}

	static void close_callback(Fl_Widget *w, void *data)
	{
		UI_PortPathDialog * that = (UI_PortPathDialog *)data;

		that->SetEXE("");

		that->want_close = true;
	}

	static void find_callback(Fl_Widget *w, void *data)
	{
		UI_PortPathDialog * that = (UI_PortPathDialog *)data;

		Fl_Native_File_Chooser chooser;

		chooser.title("Pick the game engine");
		chooser.type(Fl_Native_File_Chooser::BROWSE_FILE);

		fs::path initialDirectory = that->inst.Main_FileOpFolder();
		if(!that->exe_name.empty())
		{
			std::error_code error;
			const fs::path engineDirectory = FilenameGetPath(that->exe_name);
			if(fs::is_directory(engineDirectory, error) && !error)
				initialDirectory = engineDirectory;
		}
		chooser.directory(reinterpret_cast<const char *>(initialDirectory.u8string().c_str()));

		switch (chooser.show())
		{
			case -1:  // error
				DLG_Notify("Unable to use that game engine:\n\n%s", chooser.errmsg());
				return;

			case 1:  // cancelled
				return;

			default:
				break;  // OK
		}

		// we assume the chosen file exists

		that->SetEXE(fs::path(reinterpret_cast<const char8_t *>(chooser.filename())));
	}

	static void path_callback(Fl_Widget *, void *data)
	{
		UI_PortPathDialog *that = static_cast<UI_PortPathDialog *>(data);
		that->exe_name = UTF8Path(that->exe_input->value());
		const PortExecutablePathValidation validation =
				M_ValidatePortExecutablePath(that->exe_name);
		if(validation.valid())
			that->ok_but->activate();
		else
			that->ok_but->deactivate();
		that->SetStatus(validation.message);
	}

	static void auto_callback(Fl_Widget *, void *data)
	{
		UI_PortPathDialog *that = static_cast<UI_PortPathDialog *>(data);
		const AutoPortPathResult discovery = M_AutoFindPortPath(that->port_name);
		if(discovery.cancelled)
		{
			that->SetStatus("Automatic search cancelled. The existing selection was kept.");
			return;
		}
		if(discovery.executable)
		{
			that->SetEXE(*discovery.executable, true);
			return;
		}
		that->SetStatus(M_IsPortPathValid(that->exe_name) ?
				"No additional engine was found. The existing valid selection was kept." :
				"No engine was found automatically. Type a path or use Browse.");
	}

public:



	UI_PortPathDialog(const SString &displayName, const SString &portName,
			const Instance &inst) :
		UI_Escapable_Window(760, PADDING + LABEL_HEIGHT * 2 + INTER_LABEL_SPACE +
				LABEL_TEXT_BOX_SPACE + TEXT_BOX_HEIGHT + 8 + STATUS_HEIGHT +
				LABEL_TEXT_BOX_SPACE + TEXT_BOX_HEIGHT + PADDING_BEFORE_BOTTOM +
				BOTTOM_BAR_HEIGHT, "Port Settings"),
		inst(inst), port_name(portName)
	{


		char message_buf[256];

		snprintf(message_buf, sizeof(message_buf), "Setting up the game engine for %s.", displayName.c_str());

		Fl_Box *header = new Fl_Box(FL_NO_BOX, PADDING, PADDING, w() - 2 * PADDING, LABEL_HEIGHT, "");
		header->copy_label(message_buf);
		header->align(FL_ALIGN_INSIDE | FL_ALIGN_LEFT);

		header = new Fl_Box(FL_NO_BOX, PADDING, header->y() + header->h() + INTER_LABEL_SPACE, w() - 2 * PADDING, LABEL_HEIGHT,
		           "This is only needed for the Test in Game command.");
		header->align(FL_ALIGN_INSIDE | FL_ALIGN_LEFT);


		const int buttonSpace = BROWSE_BUTTON_WIDTH + AUTO_BUTTON_WIDTH +
				TEXT_BOX_BUTTON_SPACE * 2;
		exe_input = new Fl_Input(TEXT_BOX_LEFT,
			header->y() + header->h() + LABEL_TEXT_BOX_SPACE,
			w() - TEXT_BOX_LEFT - PADDING - buttonSpace, TEXT_BOX_HEIGHT,
			"Engine path: ");
		exe_input->when(FL_WHEN_CHANGED);
		exe_input->callback(path_callback, this);

		Fl_Button *find_but = new Fl_Button(exe_input->x() + exe_input->w() +
				TEXT_BOX_BUTTON_SPACE, exe_input->y(), BROWSE_BUTTON_WIDTH,
				TEXT_BOX_HEIGHT, "Browse...");
		find_but->callback((Fl_Callback*)find_callback, this);
		Fl_Button *auto_but = new Fl_Button(find_but->x() + find_but->w() +
				TEXT_BOX_BUTTON_SPACE, exe_input->y(), AUTO_BUTTON_WIDTH,
				TEXT_BOX_HEIGHT, "Auto Detect");
		auto_but->callback((Fl_Callback*)auto_callback, this);
		if(!M_PortExecutableAutoDiscoverySupported(port_name))
		{
			auto_but->deactivate();
			auto_but->tooltip("Automatic discovery is available for BiasedDoom, "
					"GZDoom, and ZDoom profiles.");
		}

		Fl_Box *status_label = new Fl_Box(PADDING, exe_input->y() +
				exe_input->h() + 8, TEXT_BOX_LEFT - PADDING - 8, STATUS_HEIGHT,
				"Status:");
		status_label->align(FL_ALIGN_TOP | FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
		path_status = new Fl_Box(FL_THIN_DOWN_BOX, TEXT_BOX_LEFT,
				status_label->y(), w() - TEXT_BOX_LEFT - PADDING, STATUS_HEIGHT, "");
		path_status->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
		path_status->color(FL_BACKGROUND2_COLOR);

		other_args = new Fl_Input(TEXT_BOX_LEFT, path_status->y() +
				path_status->h() + LABEL_TEXT_BOX_SPACE,
				w() - TEXT_BOX_LEFT - PADDING, TEXT_BOX_HEIGHT, "Command line: ");

		/* bottom buttons */

		Fl_Group * grp = new Fl_Group(0, h() - BOTTOM_BAR_HEIGHT + BOTTOM_BAR_OUTSET, w(), BOTTOM_BAR_HEIGHT);
		grp->box(FL_FLAT_BOX);
		grp->color(WINDOW_BG, WINDOW_BG);
		{
			cancel_but = new Fl_Button(w() - BOTTOM_RIGHT_PADDING - BOTTOM_BUTTON_WIDTH * 2 - BOTTOM_BUTTON_SPACING, h() - BOTTOM_BUTTON_HEIGHT - BOTTOM_PADDING, BOTTOM_BUTTON_WIDTH, BOTTOM_BUTTON_HEIGHT, "Cancel");
			cancel_but->callback(close_callback, this);

			ok_but = new Fl_Button(w() - BOTTOM_BUTTON_WIDTH - BOTTOM_RIGHT_PADDING, h() - BOTTOM_BUTTON_HEIGHT - BOTTOM_PADDING, BOTTOM_BUTTON_WIDTH, BOTTOM_BUTTON_HEIGHT, "OK");
			ok_but->labelfont(FL_HELVETICA_BOLD);
			ok_but->callback(ok_callback, this);
			ok_but->shortcut(FL_Enter);
			ok_but->deactivate();
		}
		grp->end();

		end();

		resizable(NULL);

		callback(close_callback, this);
		SetEXE({});
	}

	virtual ~UI_PortPathDialog()
	{ }

	// returns true if user clicked OK
	bool Run()
	{
		set_modal();
		show();

		while (! want_close)
			Fl::wait(0.2);

		return !exe_name.empty();
	}
};


bool Instance::M_PortSetupDialog(const SString &port, const SString &game,
		const std::optional<SString>& commandLine, bool autoDiscover)
{
	SString name_buf;

	if (port.noCaseEqual("vanilla"))
		name_buf = "vanilla " + game.asTitle();
	else if (port.noCaseEqual("mbf"))	// temp hack for aesthetics
		name_buf = "MBF";
	else if (port.noCaseEqual("biaseddoom"))
		name_buf = "BiasedDoom";
	else
		name_buf = port.asTitle();

	UI_PortPathDialog dialog(name_buf, port, *this);

	// populate the EXE name from existing info, if exists
	const fs::path *info = global::recent.queryPortPath(QueryName(port, game));

	if (info && M_IsPortPathValid(*info))
		dialog.SetEXE(*info);
	else if(autoDiscover)
	{
		const AutoPortPathResult discovery = M_AutoFindPortPath(port);
		if(discovery.executable)
			dialog.SetEXE(*discovery.executable);
	}

	if (commandLine)
		dialog.SetCommandLine(*commandLine);
	else
		dialog.HideCommandLine();

	bool ok = dialog.Run();

	if (ok)
	{
		// persist the new port settings
		global::recent.setPortPath(QueryName(port, game),
								   GetAbsolutePath(dialog.exe_name));
		if(main_win)
			testmap::updateMenuName(main_win->menu_bar, loaded);

		global::recent.save(global::home_dir);

		if (commandLine)
		{
			const SString updatedCommandLine = dialog.getCommandLine();
			if (updatedCommandLine != loaded.testingCommandLine)
			{
				loaded.testingCommandLine = updatedCommandLine;
				Project_MarkMetadataDirty();
			}
		}
	}

	return ok;
}


//------------------------------------------------------------------------

static void CalcWarpString(const SString& levelName, std::vector<SString> &args)
{
	SYS_ASSERT(!levelName.empty());
	// FIXME : EDGE allows a full name: -warp MAP03
	//         Eternity too.
	//         ZDOOM too, but different syntax: +map MAP03

	// most common syntax is "MAP##" or "MAP###"
	if (levelName.length() >= 4 && levelName.noCaseStartsWith("MAP") && safe_isdigit(levelName[3]))
	{
		long number = strtol(levelName.c_str() + 3, nullptr, 10);
		args.push_back("-warp");
		args.push_back(std::to_string(number));
		return;
	}

	// detect "E#M#" syntax of Ultimate-Doom and Heretic, which need
	// a pair of numbers after -warp
	if (levelName.length() >= 4 && !safe_isdigit(levelName[0]) && safe_isdigit(levelName[1]) &&
		!safe_isdigit(levelName[2]) && safe_isdigit(levelName[3]))
	{
		args.push_back("-warp");
		args.push_back(SString::printf("%c", levelName[1]));
		args.push_back(levelName.c_str() + 3);
		return;
	}

	// map name is non-standard, find the first digit group and hope
	// for the best...

	size_t digitPos = levelName.findDigit();
	if (digitPos != std::string::npos)
	{
		args.push_back("-warp");
		args.push_back(levelName.c_str() + digitPos);
		return;
	}

	// no digits at all, oh shit!
}

static void GrabWadNamesArgs(const Instance& inst, std::vector<SString> &args)
{
	bool has_file = false;
	int use_merge = 0;

	// see if we should use the "-merge" parameter, which is
	// required for Chocolate-Doom and derivates like Crispy Doom.
	// TODO : is there a better way to do this?
	if (inst.loaded.portName.noCaseEqual("vanilla"))
	{
		use_merge = 1;
	}

	// always specify the iwad
	if (inst.wad.master.gameWad())
	{
		args.push_back("-iwad");
		std::u8string u8path = inst.wad.master.gameWad()->PathName().u8string();
		std::string path(u8path.begin(), u8path.end());
		args.push_back(path);
	}

	// add any resource wads
	for (const std::shared_ptr<Wad_file>& wad : inst.wad.master.resourceWads())
	{
		if (use_merge == 1)
			args.push_back("-merge");
		else if (!use_merge && !has_file)
			args.push_back("-file");
		std::u8string u8path = wad->PathName().u8string();
		std::string path(u8path.begin(), u8path.end());
		args.push_back(path);

		if (use_merge)
			use_merge++;
		else
			has_file = true;
	}

	// the current PWAD, if exists, must be last
	if (inst.wad.master.editWad())
	{
		if (!has_file)
			args.push_back("-file");
		std::u8string u8path = inst.wad.master.editWad()->PathName().u8string();
		std::string path(u8path.begin(), u8path.end());
		args.push_back(path);
	}
}

static SString buildArgString(const std::vector<SString>& args, bool backslash)
{
	SString result;
	for (const SString& arg : args)
	{
		if (!result.empty())
			result += " ";
		result += arg.spaceEscape(backslash);
	}
	return result;
}

static void logArgs(const SString& args)
{
	gLog.printf("Testing map using the following command:\n");
	gLog.printf("--> %s\n", args.c_str());
}

#ifdef _WIN32
// On Windows the process is started as if user ran it individually
static void testMapOnWindows(const Instance &inst, const fs::path& portPath)
{
	std::vector<SString> args;
	GrabWadNamesArgs(inst, args);
	CalcWarpString(inst.loaded.levelName, args);

	SString argString = inst.loaded.testingCommandLine + " " + buildArgString(args, false);
	logArgs(argString);
	std::wstring argsWide = UTF8ToWide(argString.c_str());

	HINSTANCE result = ShellExecuteW(nullptr, L"open", portPath.wstring().c_str(), argsWide.c_str(),
		FilenameGetPath(portPath).wstring().c_str(), SW_SHOW);
	if ((INT_PTR)result <= 32)
	{
		DWORD error = GetLastError();
		ThrowException("Failed starting %s: error %s\n\n%s", portPath.u8string().c_str(),
			GetShellExecuteErrorMessage(result).c_str(), GetWindowsErrorMessage(error).c_str());
	}
	inst.Status_Set("Started the game");
}
#else

static void testMapOnMacBundle(const Instance &inst, const fs::path& portPath)
{
	std::vector<SString> args;
	GrabWadNamesArgs(inst, args);
	CalcWarpString(inst.loaded.levelName, args);

	SString argString = SString("/usr/bin/open -a ") + SString(portPath.u8string()).spaceEscape(true) + " --args " + inst.loaded.testingCommandLine + " " + buildArgString(args, true);
	logArgs(argString);

	int ret = system(argString.c_str());
	if(ret == -1)
	{
		ThrowException("Failed system to start %s: %s", reinterpret_cast<const char *>(portPath.u8string().c_str()),
					   GetErrorMessage(errno).c_str());
	}
}

static void testMapOnPOSIX(const Instance &inst, const fs::path& portPath)
{
	std::vector<SString> args;
	GrabWadNamesArgs(inst, args);
	CalcWarpString(inst.loaded.levelName, args);

	SString arg;
	TokenWordParse parse(inst.loaded.testingCommandLine, false);
	while(parse.getNext(arg))
		args.push_back(arg);
	args.insert(args.begin(), portPath.u8string());

	std::vector<char *> argv;
	argv.reserve(args.size() + 2);
	fs::path portName = portPath.filename();
	SString argString;
	for(SString &arg : args)
	{
		argString += arg + " ";
		argv.push_back(arg.get().data());
	}

	argv.push_back(nullptr);

	logArgs(argString);

	pid_t pid = fork();
	if(pid == -1)
	{
		// fail
		ThrowException("Failed forking to start %s: %s", reinterpret_cast<const char *>(portName.u8string().c_str()),
					   GetErrorMessage(errno).c_str());
	}
	else if(pid == 0)
	{
		// child process
		try
		{
			DirChangeContext dirChangeContext(FilenameGetPath(portPath));
			execvp(reinterpret_cast<const char *>(portPath.u8string().c_str()), argv.data());

			// on failure
			int err = errno;
			gLog.printf("--> Failed starting %s: %s\n", reinterpret_cast<const char *>(portName.u8string().c_str()), GetErrorMessage(err).c_str());

			_exit(err);
		}
		catch(const std::exception &e)
		{
			gLog.printf("--> Failed starting %s: %s\n", reinterpret_cast<const char *>(portName.u8string().c_str()), e.what());
			_exit(EXIT_FAILURE);
		}
		catch(...)
		{
			// Need to guard it best here
			_exit(EXIT_FAILURE);
		}
	}

	// Parent process. Continue work.
}
#endif

void Instance::CMD_ChangeTestSettings()
{
	try
	{
		M_PortSetupDialog(loaded.portName, loaded.gameName, loaded.testingCommandLine);
	}
	catch (const std::runtime_error& e)
	{
		Beep("Failed: %s\n", e.what());
	}
}

bool Instance::Project_CheckRuntimeMapInfoBeforeTest()
{
	std::shared_ptr<Wad_file> package = wad.master.editWad();
	if (!package || !loaded.project.isExplicit() ||
			!M_RuntimeMapInfoPortSupported(loaded.portName))
	{
		return true;
	}

	SString generationError;
	const std::optional<GeneratedRuntimeMapInfo> generated =
			M_GenerateRuntimeMapInfo(loaded.project, loaded.portName,
					loaded.gameName, &generationError);
	if (!generated)
	{
		gLog.printf("WARNING: runtime MAPINFO preflight skipped: %s\n",
				generationError.c_str());
		return true;
	}

	try
	{
		const RuntimeMapInfoInspection inspection = M_InspectRuntimeMapInfo(
				package->PathName(), M_ProjectPackageForPath(package->PathName()),
				*package);
		const RuntimeMapInfoFreshness freshness = M_RuntimeMapInfoFreshness(
				inspection, generated->text);
		if (freshness != RuntimeMapInfoFreshness::stale)
			return true;

		if (package->IsReadOnly())
		{
			return DLG_Confirm({ "Cancel", "Launch &Anyway" },
					"The managed runtime ZMAPINFO is out of date, but this project is "
					"read-only. Current campaign titles, entries, or routes differ from "
					"the declaration the engine will load.\n\nLaunch with the existing "
					"runtime declaration?") == 1;
		}

		const int choice = DLG_Confirm(
				{ "Cancel", "Launch &Anyway", "&Review and Update" },
				"The managed runtime ZMAPINFO is out of date. Current campaign titles, "
				"entries, or routes differ from the declaration the engine will load.\n\n"
				"Review the exact replacement and update it before launching?");
		if (choice == 0)
			return false;
		if (choice == 1)
			return true;
		return Project_GenerateRuntimeMapInfo();
	}
	catch (const std::runtime_error &error)
	{
		// Freshness is advisory and must not make an otherwise valid direct test
		// launch impossible. The explicit generator still reports hard errors.
		gLog.printf("WARNING: runtime MAPINFO preflight could not inspect the "
				"package: %s\n", error.what());
		return true;
	}
}

void Instance::CMD_TestMap()
{
	struct RestoreCanvasFocus
	{
		Instance &inst;

		~RestoreCanvasFocus()
		{
			// Test launch commonly follows a save confirmation or a menu
			// action. Keep map-level shortcuts, especially Undo and Redo,
			// directed at the canvas when the user returns from the engine.
			if (inst.main_win && inst.main_win->canvas)
				Fl::focus(inst.main_win->canvas);
		}
	} restoreCanvasFocus{*this};

	try
	{
		if (level.hasChanges())
		{
			if (DLG_Confirm({ "Cancel", "&Save" },
				"You have unsaved changes, do you want to save them now "
				"and build the nodes?") <= 0)
			{
				return;
			}

			if (!M_SaveMap(false))
				return;
		}
		if (!Project_CheckRuntimeMapInfoBeforeTest())
			return;


		// check if we know the executable path, if not then ask
		const fs::path* info = global::recent.queryPortPath(QueryName(loaded.portName,
			loaded.gameName));

		if (!info || !M_IsPortPathValid(*info))
		{
			const AutoPortPathResult discovery = M_AutoFindPortPath(loaded.portName);
			if(discovery.cancelled)
			{
				Status_Set("Engine discovery cancelled");
				return;
			}
			if(discovery.executable)
			{
				gLog.printf("Automatically found %s executable: %s\n",
						loaded.portName.c_str(),
						reinterpret_cast<const char *>(discovery.executable->u8string().c_str()));
				global::recent.setPortPath(QueryName(loaded.portName, loaded.gameName),
						*discovery.executable);
				if(!global::home_dir.empty())
					global::recent.save(global::home_dir);
				if(main_win)
					testmap::updateMenuName(main_win->menu_bar, loaded);
			}
			else if (!M_PortSetupDialog(loaded.portName, loaded.gameName,
					loaded.testingCommandLine, false))
			{
				return;
			}

			info = global::recent.queryPortPath(QueryName(loaded.portName, loaded.gameName));
		}

		// this generally can't happen, but we check anyway...
		if (!info || !M_IsPortPathValid(*info))
		{
			Beep("invalid game engine path");
			return;
		}

		Status_Set("TESTING MAP");
		if (main_win && main_win->canvas)
			Fl::focus(main_win->canvas);
		if(main_win)
			main_win->redraw();
		Fl::wait(0.1);
		Fl::wait(0.1);

#ifdef _WIN32
		testMapOnWindows(*this, *info);
#else
		if(isMacOSAppBundle(*info))
			testMapOnMacBundle(*this, *info);
		else
			testMapOnPOSIX(*this, *info);
#endif
		if(main_win)
			main_win->redraw();
		Fl::wait(0.1);
		Fl::wait(0.1);

	}
	catch(const std::runtime_error &e)
	{
		Status_Set("Failed testing map");
		DLG_ShowError(false, "Could not start map for testing: %s", e.what());
	}

}

namespace testmap
{
void updateMenuName(Fl_Sys_Menu_Bar *bar, const LoadingData &loading)
{
	if(loading.portName.empty() || loading.gameName.empty())
		return;	// premature
	const fs::path* info = global::recent.queryPortPath(QueryName(loading.portName,
		loading.gameName));
	if(!info || !M_IsPortPathValid(*info))
		menu::setTestMapDetail(bar, "");
	else
		menu::setTestMapDetail(bar, SString(info->filename().replace_extension().u8string()));
}
}


//--- editor settings ---
// vi:ts=4:sw=4:noexpandtab
