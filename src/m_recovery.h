//------------------------------------------------------------------------
//  PROJECT AUTOSAVE RECOVERY STORE
//------------------------------------------------------------------------

#ifndef HERESY_M_RECOVERY_H
#define HERESY_M_RECOVERY_H

#include "m_strings.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace fs = std::filesystem;

struct RecoveryMapFile
{
	SString mapName;
	fs::path fileName;
};

struct RecoverySnapshot
{
	fs::path directory;
	fs::path packagePath;
	fs::path contextFile;
	SString activeMap;
	int64_t createdAt = 0;
	int64_t packageStamp = 0;
	int64_t packageSize = 0;
	bool packageChanged = false;
	std::vector<RecoveryMapFile> maps;
};

// Recovery data is intentionally separate from the project package.  Each
// generation contains one WAD per dirty map plus a context WAD and a manifest.
// Generation zero is newest; two older validated generations are retained.
class RecoveryStore
{
public:
	static constexpr int GENERATION_COUNT = 3;

	explicit RecoveryStore(fs::path root) : root_(std::move(root))
	{
	}

	fs::path beginSnapshot(const fs::path &packagePath) const;
	void commitSnapshot(const fs::path &packagePath,
			const fs::path &stagingDirectory, const SString &activeMap,
			const std::vector<RecoveryMapFile> &maps) const;

	std::optional<RecoverySnapshot> latest(
			const fs::path &packagePath) const;
	void discard(const fs::path &packagePath) const noexcept;

	fs::path projectDirectory(const fs::path &packagePath) const;

private:
	std::optional<RecoverySnapshot> readGeneration(
			const fs::path &directory, const fs::path &packagePath) const;

	fs::path root_;
};

#endif // HERESY_M_RECOVERY_H
