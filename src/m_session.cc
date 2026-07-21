//------------------------------------------------------------------------
//  PROJECT SESSION SIDECAR
//------------------------------------------------------------------------

#include "m_session.h"

#include "SafeOutFile.h"
#include "lib_file.h"
#include "lib_util.h"
#include "m_parse.h"
#include "m_project.h"
#include "m_streams.h"

#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>

namespace
{

constexpr uintmax_t MaxSessionBytes = 64 * 1024;

std::optional<ProjectSession> LoadSessionFile(const fs::path &path)
{
	std::error_code sizeError;
	const uintmax_t size = fs::file_size(path, sizeError);
	if (sizeError || size > MaxSessionBytes)
		return {};

	std::ifstream stream(path);
	if (!stream.is_open())
		return {};

	ProjectSession session;
	bool haveVersion = false;
	std::set<SString> seen;
	SString line;
	while (M_ReadTextLine(line, stream))
	{
		TokenWordParse parse(line, true);
		SString key;
		SString value;
		if (!parse.getNext(key))
			continue;
		if (!parse.getNext(value))
			return {};

		SString extra;
		if (parse.getNext(extra) || !seen.insert(key).second)
			return {};

		if (key == "session_version")
		{
			char *end = nullptr;
			const long version = std::strtol(value.c_str(), &end, 10);
			if (!end || *end != '\0' || version != ProjectSession::CURRENT_VERSION)
				return {};
			session.version = static_cast<int>(version);
			haveVersion = true;
		}
		else if (key == "active_map")
		{
			if (!M_IsValidProjectMapName(value))
				return {};
			session.activeMap = value.asUpper();
		}
		else if (key == "navigator_map")
		{
			if (!M_IsValidProjectMapName(value))
				return {};
			session.navigatorMap = value.asUpper();
		}
		else if (key == "iwad_game")
		{
			if (value.empty() || value.size() > 128)
				return {};
			session.iwadGame = value.asLower();
		}
		else if (key == "iwad_file")
		{
			const fs::path file(reinterpret_cast<const char8_t *>(value.c_str()));
			if (file.empty() || file != file.filename())
				return {};
			session.iwadFile = file;
		}
		else if (key == "iwad_relative")
		{
			const fs::path relative(reinterpret_cast<const char8_t *>(value.c_str()));
			if (relative.empty() || relative.is_absolute())
				return {};
			session.iwadRelative = relative.lexically_normal();
		}
		// Unknown fields are ignored so a compatible future writer can add
		// optional hints without breaking older versions.
	}

	if (!haveVersion)
		return {};
	return session;
}

std::string SerializeSession(const ProjectSession &session)
{
	std::ostringstream stream;
	stream << "# Heresy Editor project session\n";
	stream << "session_version " << ProjectSession::CURRENT_VERSION << '\n';
	if (session.activeMap.good())
		stream << "active_map " << session.activeMap.asUpper() << '\n';
	if (session.navigatorMap.good())
		stream << "navigator_map " << session.navigatorMap.asUpper() << '\n';
	if (session.iwadGame.good())
		stream << "iwad_game " << session.iwadGame.asLower().spaceEscape() << '\n';
	if (!session.iwadFile.empty())
		stream << "iwad_file " << escape(session.iwadFile) << '\n';
	if (!session.iwadRelative.empty())
		stream << "iwad_relative " << escape(session.iwadRelative) << '\n';
	return stream.str();
}

} // namespace


fs::path M_ProjectSessionPath(const fs::path &packagePath)
{
	fs::path result = packagePath;
	result += ".heresy";
	return result;
}


ProjectSession M_MakeProjectSession(const fs::path &packagePath,
		const fs::path &iwadPath, const SString &iwadGame,
		const SString &activeMap, const SString &navigatorMap)
{
	ProjectSession session;
	session.activeMap = activeMap.asUpper();
	session.navigatorMap = navigatorMap.asUpper();
	session.iwadGame = iwadGame.asLower();
	session.iwadFile = iwadPath.filename();

	if (!packagePath.empty() && !iwadPath.empty())
	{
		std::error_code error;
		const fs::path packageDirectory = fs::absolute(packagePath, error).parent_path();
		if (!error)
		{
			const fs::path absoluteIWAD = fs::absolute(iwadPath, error);
			if (!error)
			{
				const fs::path relative = fs::relative(absoluteIWAD, packageDirectory, error);
				if (!error && !relative.empty() && relative.is_relative())
					session.iwadRelative = relative.lexically_normal();
			}
		}
	}
	return session;
}


std::optional<ProjectSession> M_LoadProjectSession(const fs::path &packagePath)
{
	return LoadSessionFile(M_ProjectSessionPath(packagePath));
}


void M_SaveProjectSession(const fs::path &packagePath,
		const ProjectSession &session)
{
	if (packagePath.empty())
		return;
	if (session.activeMap.good() && !M_IsValidProjectMapName(session.activeMap))
		throw std::runtime_error("Invalid active map in project session.");
	if (session.navigatorMap.good() &&
			!M_IsValidProjectMapName(session.navigatorMap))
		throw std::runtime_error("Invalid navigator map in project session.");
	if (!session.iwadFile.empty() && session.iwadFile != session.iwadFile.filename())
		throw std::runtime_error("Project session IWAD hint must be a filename.");
	if (!session.iwadRelative.empty() && session.iwadRelative.is_absolute())
		throw std::runtime_error("Project session IWAD hint must be relative.");

	const fs::path sidecar = M_ProjectSessionPath(packagePath);
	const std::string contents = SerializeSession(session);
	BufferedOutFile output(sidecar);
	output.write(contents.data(), contents.size());
	output.commit([](const fs::path &temporary)
	{
		return LoadSessionFile(temporary).has_value();
	});
}


std::optional<fs::path> M_ResolveProjectIWAD(const fs::path &packagePath,
		const ProjectSession &session,
		const std::optional<fs::path> &knownIWAD,
		const IWADSearchLocations &locations)
{
	if (session.iwadGame.empty())
		return {};

	const fs::path directory = packagePath.parent_path();
	std::vector<fs::path> candidates;
	if (!session.iwadRelative.empty())
		candidates.push_back((directory / session.iwadRelative).lexically_normal());
	if (!session.iwadFile.empty())
		candidates.push_back(directory / session.iwadFile);
	if (knownIWAD)
		candidates.push_back(*knownIWAD);

	std::set<fs::path> visited;
	for (const fs::path &candidate : candidates)
	{
		std::error_code error;
		const fs::path absolute = fs::absolute(candidate, error).lexically_normal();
		const fs::path checked = error ? candidate.lexically_normal() : absolute;
		if (visited.insert(checked).second &&
				M_IsIWADForGame(checked, session.iwadGame))
			return checked;
	}

	IWADSearchLocations augmented = locations;
	if (!directory.empty())
		augmented.preferredDirectories.insert(
				augmented.preferredDirectories.begin(), directory);
	const std::map<SString, fs::path> discovered =
			M_DiscoverIWADs({session.iwadGame}, augmented);
	const auto found = discovered.find(session.iwadGame.asLower());
	return found == discovered.end() ? std::optional<fs::path>{} :
			std::optional<fs::path>{found->second};
}
