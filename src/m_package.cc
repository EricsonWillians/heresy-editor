//------------------------------------------------------------------------
//  EDITABLE PROJECT PACKAGE BACKENDS
//------------------------------------------------------------------------

#include "m_package.h"

#include "lib_file.h"
#include "m_zip.h"
#include "w_wad.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <stdexcept>

namespace
{

constexpr const char *PROJECT_METADATA_ENTRY = "heresy/project.txt";

std::string LowerASCII(std::string value)
{
	for (char &character : value)
		if (character >= 'A' && character <= 'Z')
			character = static_cast<char>(character - 'A' + 'a');
	return value;
}

bool IsPk3MapEntry(const std::string &name)
{
	const std::string lower = LowerASCII(name);
	if (lower.size() <= 9 || lower.compare(0, 5, "maps/") != 0 ||
			lower.compare(lower.size() - 4, 4, ".wad") != 0)
	{
		return false;
	}
	return lower.find('/', 5) == std::string::npos;
}

SString MapSlotForEntry(const std::string &name)
{
	const size_t slash = name.find_last_of('/');
	const size_t dot = name.find_last_of('.');
	if (dot == std::string::npos || dot <= slash + 1)
		return {};
	SString slot(name.substr(slash + 1, dot - slash - 1));
	slot = slot.asUpper();
	if (slot.length() > 8)
		return {};
	return slot;
}

bool CanWriteFile(const fs::path &path)
{
	std::fstream stream(path, std::ios::in | std::ios::out | std::ios::binary);
	return stream.is_open();
}

class WadPackageBackend final : public PackageBackend
{
public:
	WadPackageBackend(fs::path path, bool create) :
			path_(std::move(path)), create_(create)
	{
	}

	ProjectPackage packageType() const noexcept override
	{
		return ProjectPackage::wad;
	}

	const fs::path &PathName() const noexcept override
	{
		return path_;
	}

	std::shared_ptr<Wad_file> openEditable() override
	{
		return Wad_file::Open(path_, create_ ? WadOpenMode::write :
				WadOpenMode::append);
	}

private:
	fs::path path_;
	bool create_;
};

class Pk3PackageBackend final : public PackageBackend,
		public std::enable_shared_from_this<Pk3PackageBackend>
{
public:
	Pk3PackageBackend(fs::path path, std::shared_ptr<ZipArchive> archive,
			bool writable) :
			path_(std::move(path)), archive_(std::move(archive)),
			writable_(writable)
	{
		scanMaps();
	}

	ProjectPackage packageType() const noexcept override
	{
		return ProjectPackage::pk3;
	}

	const fs::path &PathName() const noexcept override
	{
		return path_;
	}

	std::shared_ptr<Wad_file> openEditable() override
	{
		std::shared_ptr<Pk3PackageBackend> backend = shared_from_this();
		auto result = Wad_file::CreateVirtual(path_,
				[backend](const Wad_file &wad)
				{
					backend->saveAggregate(wad);
				}, writable_ ? WadOpenMode::append : WadOpenMode::read);

		appendResources(*result);
		if (archive_->contains(PROJECT_METADATA_ENTRY))
		{
			const std::vector<uint8_t> metadata =
					archive_->readEntry(PROJECT_METADATA_ENTRY);
			Lump_c &lump = result->AddLump(EUREKA_LUMP);
			if (!metadata.empty())
				lump.Write(metadata.data(), static_cast<int>(metadata.size()));
		}

		for (const MapRecord &record : maps_)
			result->AppendLevelFrom(*record.wad, record.level);
		result->SortLevels();
		return result;
	}

private:
	struct MapRecord
	{
		std::string entryName;
		SString slot;
		std::shared_ptr<Wad_file> wad;
		int level = -1;
		std::vector<byte> baseline;
	};

	struct ResourceRecord
	{
		SString lumpName;
		WadNamespace nameSpace = WadNamespace::Global;
		std::vector<uint8_t> data;
	};

	void scanMaps()
	{
		maps_.clear();
		std::map<SString, std::string> seen;
		for (const std::string &entryName : archive_->entryNames())
		{
			if (!IsPk3MapEntry(entryName))
				continue;

			SString slot = MapSlotForEntry(entryName);
			if (slot.empty())
				continue;
			if (seen.find(slot) != seen.end())
			{
				throw std::runtime_error("PK3 contains duplicate map paths for " +
						std::string(slot.c_str()) + ".");
			}

			std::vector<uint8_t> bytes;
			try
			{
				bytes = archive_->readEntry(entryName);
			}
			catch (const std::runtime_error &error)
			{
				throw std::runtime_error("Cannot edit PK3 map entry " + entryName +
						": " + error.what());
			}

			auto wad = Wad_file::loadFromData(fs::path(entryName), bytes,
					WadOpenMode::append);
			if (!wad || wad->LevelCount() == 0)
				throw std::runtime_error("PK3 map entry is not an editable map WAD: " +
						entryName);

			int level = wad->LevelFind(slot);
			if (level < 0)
				level = wad->LevelFindFirst();
			wad->RenameLump(wad->LevelHeader(level), slot.c_str());

			MapRecord record;
			record.entryName = entryName;
			record.slot = slot;
			record.wad = std::move(wad);
			record.level = level;
			record.baseline = record.wad->serializeLevel(level);
			maps_.push_back(std::move(record));
			seen.emplace(slot, entryName);
		}

		std::sort(maps_.begin(), maps_.end(),
				[](const MapRecord &left, const MapRecord &right)
				{
					return left.slot < right.slot;
				});
	}

	void appendResources(Wad_file &target) const
	{
		std::vector<ResourceRecord> resources;
		for (const std::string &entryName : archive_->entryNames())
		{
			if (entryName.empty() || entryName.back() == '/' ||
					IsPk3MapEntry(entryName) ||
					LowerASCII(entryName) == PROJECT_METADATA_ENTRY)
			{
				continue;
			}

			const size_t slash = entryName.find('/');
			if (slash != std::string::npos &&
					entryName.find('/', slash + 1) != std::string::npos)
			{
				continue;
			}

			ResourceRecord record;
			if (slash != std::string::npos)
			{
				const std::string folder = LowerASCII(entryName.substr(0, slash));
				if (folder == "flats")
					record.nameSpace = WadNamespace::Flats;
				else if (folder == "sprites")
					record.nameSpace = WadNamespace::Sprites;
				else if (folder == "textures")
					record.nameSpace = WadNamespace::TextureLumps;
				else
					continue;
			}

			fs::path resourcePath(entryName);
			if (MatchExtensionNoCase(resourcePath, ".wad"))
				continue;
			record.lumpName = SString(resourcePath.filename().replace_extension().u8string());
			if (record.lumpName.empty())
				continue;
			try
			{
				record.data = archive_->readEntry(entryName);
			}
			catch (const std::runtime_error &)
			{
				continue;
			}
			resources.push_back(std::move(record));
		}

		auto appendNamespace = [&target, &resources](WadNamespace nameSpace,
				const char *start, const char *end)
		{
			const bool hasEntries = std::any_of(resources.begin(), resources.end(),
					[nameSpace](const ResourceRecord &record)
					{
						return record.nameSpace == nameSpace;
					});
			if (!hasEntries)
				return;
			if (nameSpace != WadNamespace::Global)
				target.AddLump(start);
			for (const ResourceRecord &record : resources)
			{
				if (record.nameSpace != nameSpace)
					continue;
				Lump_c &lump = target.AddLump(record.lumpName);
				if (!record.data.empty())
					lump.Write(record.data.data(), static_cast<int>(record.data.size()));
			}
			if (nameSpace != WadNamespace::Global)
				target.AddLump(end);
		};

		appendNamespace(WadNamespace::Global, nullptr, nullptr);
		appendNamespace(WadNamespace::Flats, "F_START", "F_END");
		appendNamespace(WadNamespace::Sprites, "S_START", "S_END");
		appendNamespace(WadNamespace::TextureLumps, "TX_START", "TX_END");
	}

	void saveAggregate(const Wad_file &aggregate)
	{
		if (!writable_)
			throw std::runtime_error("Cannot overwrite a read-only PK3 package.");

		std::map<SString, int> levels;
		for (int level = 0; level < aggregate.LevelCount(); ++level)
		{
			SString slot = aggregate.LevelName(level).asUpper();
			if (!levels.emplace(slot, level).second)
				throw std::runtime_error("PK3 contains duplicate map slots.");
		}

		for (MapRecord &record : maps_)
		{
			auto found = levels.find(record.slot);
			if (found == levels.end())
			{
				archive_->removeEntry(record.entryName);
				continue;
			}

			const std::vector<byte> current =
					aggregate.serializeLevel(found->second);
			if (current != record.baseline)
			{
				record.wad->ReplaceLevelFrom(record.level, aggregate,
						found->second);
				archive_->setEntry(record.entryName, record.wad->serialize());
			}
			levels.erase(found);
		}

		for (const auto &newLevel : levels)
		{
			const std::string entryName = "maps/" +
					std::string(newLevel.first.c_str()) + ".wad";
			if (archive_->contains(entryName))
			{
				throw std::runtime_error("Cannot replace an unrecognized existing PK3 "
						"entry: " + entryName);
			}
			archive_->setEntry(entryName,
					aggregate.serializeLevel(newLevel.second));
		}

		const Lump_c *metadata = aggregate.FindLump(EUREKA_LUMP);
		if (metadata)
		{
			const std::vector<uint8_t> &data = metadata->getData();
			if (!archive_->contains(PROJECT_METADATA_ENTRY) ||
					archive_->readEntry(PROJECT_METADATA_ENTRY) != data)
			{
				archive_->setEntry(PROJECT_METADATA_ENTRY, data);
			}
		}

		archive_->writeToDisk();
		scanMaps();
	}

	fs::path path_;
	std::shared_ptr<ZipArchive> archive_;
	bool writable_;
	std::vector<MapRecord> maps_;
};

} // namespace

ProjectPackage M_ProjectPackageForPath(const fs::path &path) noexcept
{
	if (MatchExtensionNoCase(path, ".pk3") || MatchExtensionNoCase(path, ".zip"))
		return ProjectPackage::pk3;
	if (MatchExtensionNoCase(path, ".wad"))
		return ProjectPackage::wad;
	return ProjectPackage::none;
}

std::shared_ptr<PackageBackend> M_OpenPackageBackend(const fs::path &path)
{
	switch (M_ProjectPackageForPath(path))
	{
		case ProjectPackage::wad:
			return std::make_shared<WadPackageBackend>(path, false);

		case ProjectPackage::pk3:
		{
			auto archive = ZipArchive::Open(path);
			if (!archive)
				return nullptr;
			return std::make_shared<Pk3PackageBackend>(path, archive,
					CanWriteFile(path));
		}

		default:
			return nullptr;
	}
}

std::shared_ptr<PackageBackend> M_CreatePackageBackend(const fs::path &path,
		ProjectPackage package)
{
	if (package == ProjectPackage::wad)
		return std::make_shared<WadPackageBackend>(path, true);
	if (package == ProjectPackage::pk3)
		return std::make_shared<Pk3PackageBackend>(path,
				ZipArchive::Create(path), true);
	return nullptr;
}

std::shared_ptr<Wad_file> M_OpenEditablePackage(const fs::path &path)
{
	auto backend = M_OpenPackageBackend(path);
	return backend ? backend->openEditable() : nullptr;
}

bool M_ValidateEditablePackage(const fs::path &path) noexcept
{
	try
	{
		if (M_ProjectPackageForPath(path) == ProjectPackage::wad)
			return Wad_file::Open(path, WadOpenMode::read) != nullptr;
		if (M_ProjectPackageForPath(path) == ProjectPackage::pk3)
			return ZipArchive::Validate(path);
	}
	catch (...)
	{
	}
	return false;
}
