//------------------------------------------------------------------------
//  PRESERVATION-SAFE ZIP ARCHIVES
//------------------------------------------------------------------------

#ifndef HERESY_M_ZIP_H
#define HERESY_M_ZIP_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct ZipEntryInfo
{
	std::string name;
	uint64_t compressedSize = 0;
	uint64_t uncompressedSize = 0;
	uint16_t compressionMethod = 0;
	bool encrypted = false;
	bool directory = false;
};

// A deliberately small ZIP package layer for PK3 projects.  Existing entries
// retain their complete local records and central-directory metadata unless a
// caller explicitly replaces or removes them.  New entries use portable STORE
// or raw-DEFLATE encoding through zlib.
class ZipArchive
{
public:
	~ZipArchive();

	static std::shared_ptr<ZipArchive> Create(const fs::path &path);
	static std::shared_ptr<ZipArchive> Open(const fs::path &path);
	static bool Validate(const fs::path &path) noexcept;

	const fs::path &PathName() const noexcept
	{
		return path_;
	}

	std::vector<std::string> entryNames() const;
	std::vector<ZipEntryInfo> entryInfos() const;
	bool contains(const std::string &name) const noexcept;
	std::vector<uint8_t> readEntry(const std::string &name) const;

	void setEntry(const std::string &name, const std::vector<uint8_t> &data);
	void removeEntry(const std::string &name);

	std::vector<uint8_t> serialize() const;
	void writeToDisk() const;

private:
	struct Entry;

	explicit ZipArchive(const fs::path &path);

	Entry *findUnique(const std::string &name);
	const Entry *findUnique(const std::string &name) const;
	void parse(std::vector<uint8_t> data);

	fs::path path_;
	std::vector<uint8_t> source_;
	std::vector<uint8_t> preamble_;
	std::vector<uint8_t> centralTail_;
	std::vector<uint8_t> postCentral_;
	std::vector<uint8_t> archiveComment_;
	std::vector<Entry> entries_;
};

#endif // HERESY_M_ZIP_H
