//------------------------------------------------------------------------
//  PROJECT AUTOSAVE RECOVERY STORE
//------------------------------------------------------------------------

#include "m_recovery.h"

#include "SafeOutFile.h"
#include "m_package.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <limits>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>

namespace
{

constexpr const char *MANIFEST_NAME = "manifest.txt";
constexpr const char *CONTEXT_NAME = "context.wad";

std::atomic<uint64_t> stagingSequence{0};

std::string PathToUTF8(const fs::path &path)
{
	const std::u8string value = path.u8string();
	return std::string(reinterpret_cast<const char *>(value.data()), value.size());
}

fs::path PathFromUTF8(const std::string &value)
{
	return fs::path(std::u8string(
			reinterpret_cast<const char8_t *>(value.data()), value.size()));
}

fs::path NormalizedPath(const fs::path &path)
{
	std::error_code error;
	fs::path absolute = fs::absolute(path, error);
	return (error ? path : absolute).lexically_normal();
}

std::string NormalizedKeyText(const fs::path &path)
{
	std::string value = PathToUTF8(NormalizedPath(path));
#ifdef _WIN32
	for (char &character : value)
		if (character >= 'A' && character <= 'Z')
			character = static_cast<char>(character - 'A' + 'a');
#endif
	return value;
}

uint64_t FNV1a(const std::string &value) noexcept
{
	uint64_t hash = UINT64_C(14695981039346656037);
	for (unsigned char character : value)
	{
		hash ^= character;
		hash *= UINT64_C(1099511628211);
	}
	return hash;
}

SString HexKey(uint64_t value)
{
	return SString::printf("%016llx",
			static_cast<unsigned long long>(value));
}

int64_t FileStamp(const fs::path &path) noexcept
{
	std::error_code error;
	const fs::file_time_type stamp = fs::last_write_time(path, error);
	return error ? 0 : static_cast<int64_t>(stamp.time_since_epoch().count());
}

int64_t FileSize(const fs::path &path) noexcept
{
	std::error_code error;
	const uintmax_t size = fs::file_size(path, error);
	return error || size > static_cast<uintmax_t>(
			std::numeric_limits<int64_t>::max()) ? 0 :
			static_cast<int64_t>(size);
}

int64_t CurrentTime() noexcept
{
	return std::chrono::duration_cast<std::chrono::seconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();
}

bool SafeRelativeFile(const fs::path &fileName)
{
	return !fileName.empty() && !fileName.is_absolute() &&
			!fileName.has_parent_path() && fileName.filename() == fileName;
}

bool SafeStagingDirectory(const fs::path &directory, const fs::path &project)
{
	if (directory.lexically_normal().parent_path() != project.lexically_normal())
		return false;
	const std::string name = PathToUTF8(directory.filename());
	return name.rfind("staging-", 0) == 0;
}

void RemoveTree(const fs::path &path) noexcept
{
	std::error_code ignored;
	fs::remove_all(path, ignored);
}

void RenameDirectory(const fs::path &source, const fs::path &destination)
{
	std::error_code error;
	if (!fs::exists(source, error) || error)
		return;
	fs::rename(source, destination, error);
	if (error)
		throw fs::filesystem_error("Could not rotate recovery snapshot", source,
				destination, error);
}

} // namespace

fs::path RecoveryStore::projectDirectory(const fs::path &packagePath) const
{
	return root_ / HexKey(FNV1a(NormalizedKeyText(packagePath))).c_str();
}

fs::path RecoveryStore::beginSnapshot(const fs::path &packagePath) const
{
	const fs::path project = projectDirectory(packagePath);
	std::error_code error;
	fs::create_directories(project, error);
	if (error)
		throw fs::filesystem_error("Could not create recovery directory", project,
				error);

	const uint64_t timestamp = static_cast<uint64_t>(
			std::chrono::steady_clock::now().time_since_epoch().count());
	std::random_device entropy;
	const uint64_t random = (static_cast<uint64_t>(entropy()) << 32) ^ entropy();
	for (int attempt = 0; attempt < 100; ++attempt)
	{
		const uint64_t token = timestamp ^ random ^ stagingSequence.fetch_add(1);
		const fs::path staging = project /
				SString::printf("staging-%016llx",
						static_cast<unsigned long long>(token)).c_str();
		error.clear();
		if (fs::create_directory(staging, error))
			return staging;
		if (error)
			throw fs::filesystem_error("Could not create recovery staging directory",
					staging, error);
	}
	throw std::runtime_error("Could not allocate a recovery staging directory.");
}

void RecoveryStore::commitSnapshot(const fs::path &packagePath,
		const fs::path &stagingDirectory, const SString &activeMap,
		const std::vector<RecoveryMapFile> &maps) const
{
	const fs::path project = projectDirectory(packagePath);
	if (!SafeStagingDirectory(stagingDirectory, project))
		throw std::runtime_error("Recovery staging directory does not match project.");
	if (!M_ValidateEditablePackage(stagingDirectory / CONTEXT_NAME))
		throw std::runtime_error("Recovery project context failed validation.");

	std::ostringstream manifest;
	manifest << "HERESY_RECOVERY 1\n";
	manifest << "package " << std::quoted(PathToUTF8(NormalizedPath(packagePath)))
			<< "\n";
	manifest << "created " << CurrentTime() << "\n";
	manifest << "package_stamp " << FileStamp(packagePath) << "\n";
	manifest << "package_size " << FileSize(packagePath) << "\n";
	manifest << "active " << std::quoted(std::string(activeMap.c_str())) << "\n";
	manifest << "context " << std::quoted(std::string(CONTEXT_NAME)) << "\n";

	std::set<SString> mapNames;
	std::set<fs::path> mapFiles;
	for (const RecoveryMapFile &map : maps)
	{
		if (map.mapName.empty() || !SafeRelativeFile(map.fileName) ||
				!mapNames.insert(map.mapName.asUpper()).second ||
				!mapFiles.insert(map.fileName).second ||
				!M_ValidateEditablePackage(stagingDirectory / map.fileName))
		{
			throw std::runtime_error("Recovery map failed validation.");
		}
		manifest << "map " << std::quoted(std::string(map.mapName.c_str())) << " "
				<< std::quoted(PathToUTF8(map.fileName)) << "\n";
	}

	const std::string data = manifest.str();
	BufferedOutFile output(stagingDirectory / MANIFEST_NAME);
	output.write(data.data(), data.size());
	output.commit();

	if (!readGeneration(stagingDirectory, packagePath))
		throw std::runtime_error("Completed recovery snapshot failed validation.");

	RemoveTree(project / std::to_string(GENERATION_COUNT - 1));
	for (int generation = GENERATION_COUNT - 2; generation >= 0; --generation)
	{
		RenameDirectory(project / std::to_string(generation),
				project / std::to_string(generation + 1));
	}
	RenameDirectory(stagingDirectory, project / "0");
}

std::optional<RecoverySnapshot> RecoveryStore::readGeneration(
		const fs::path &directory, const fs::path &packagePath) const
{
	std::ifstream input(directory / MANIFEST_NAME);
	if (!input)
		return {};

	std::string signature;
	int version = 0;
	if (!(input >> signature >> version) || signature != "HERESY_RECOVERY" ||
			version != 1)
	{
		return {};
	}

	RecoverySnapshot snapshot;
	std::set<SString> mapNames;
	std::set<fs::path> mapFiles;
	bool seenPackage = false;
	bool seenCreated = false;
	bool seenPackageStamp = false;
	bool seenPackageSize = false;
	bool seenActive = false;
	bool seenContext = false;
	snapshot.directory = directory;
	std::string key;
	while (input >> key)
	{
		if (key == "package")
		{
			if (seenPackage)
				return {};
			seenPackage = true;
			std::string value;
			if (!(input >> std::quoted(value)))
				return {};
			snapshot.packagePath = PathFromUTF8(value);
		}
		else if (key == "created")
		{
			if (seenCreated)
				return {};
			seenCreated = true;
			if (!(input >> snapshot.createdAt))
				return {};
		}
		else if (key == "package_stamp")
		{
			if (seenPackageStamp)
				return {};
			seenPackageStamp = true;
			if (!(input >> snapshot.packageStamp))
				return {};
		}
		else if (key == "package_size")
		{
			if (seenPackageSize)
				return {};
			seenPackageSize = true;
			if (!(input >> snapshot.packageSize))
				return {};
		}
		else if (key == "active")
		{
			if (seenActive)
				return {};
			seenActive = true;
			std::string value;
			if (!(input >> std::quoted(value)))
				return {};
			snapshot.activeMap = value;
		}
		else if (key == "context")
		{
			if (seenContext)
				return {};
			seenContext = true;
			std::string value;
			if (!(input >> std::quoted(value)))
				return {};
			const fs::path fileName = PathFromUTF8(value);
			if (!SafeRelativeFile(fileName))
				return {};
			snapshot.contextFile = directory / fileName;
		}
		else if (key == "map")
		{
			std::string name;
			std::string file;
			if (!(input >> std::quoted(name) >> std::quoted(file)))
				return {};
			const fs::path fileName = PathFromUTF8(file);
			if (name.empty() || !SafeRelativeFile(fileName) ||
					!mapNames.insert(SString(name).asUpper()).second ||
					!mapFiles.insert(fileName).second)
				return {};
			snapshot.maps.push_back({ SString(name), directory / fileName });
		}
		else
		{
			std::string ignored;
			std::getline(input, ignored);
		}
	}

	if (!seenPackage || !seenCreated || !seenPackageStamp || !seenPackageSize ||
			!seenActive ||
			!seenContext || NormalizedKeyText(snapshot.packagePath) !=
			NormalizedKeyText(packagePath) || snapshot.contextFile.empty() ||
			!M_ValidateEditablePackage(snapshot.contextFile))
	{
		return {};
	}
	for (const RecoveryMapFile &map : snapshot.maps)
		if (!M_ValidateEditablePackage(map.fileName))
			return {};

	snapshot.packageChanged = snapshot.packageStamp != FileStamp(packagePath) ||
			snapshot.packageSize != FileSize(packagePath);
	return snapshot;
}

std::optional<RecoverySnapshot> RecoveryStore::latest(
		const fs::path &packagePath) const
{
	const fs::path project = projectDirectory(packagePath);
	for (int generation = 0; generation < GENERATION_COUNT; ++generation)
	{
		std::optional<RecoverySnapshot> snapshot = readGeneration(
				project / std::to_string(generation), packagePath);
		if (snapshot)
			return snapshot;
	}
	return {};
}

void RecoveryStore::discard(const fs::path &packagePath) const noexcept
{
	RemoveTree(projectDirectory(packagePath));
}
