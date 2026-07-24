//------------------------------------------------------------------------
//  EDITABLE PROJECT PACKAGE BACKENDS
//------------------------------------------------------------------------

#include "m_package.h"

#include "lib_file.h"
#include "m_mapinfo.h"
#include "m_zip.h"
#include "w_wad.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <initializer_list>
#include <map>
#include <optional>
#include <stdexcept>

namespace
{

constexpr const char *PROJECT_METADATA_ENTRY = "heresy/project.txt";
constexpr const char *RUNTIME_MAPINFO_ENTRY = "ZMAPINFO";

bool IsManagedRuntimeMapInfo(const std::vector<uint8_t> &data)
{
	const std::string marker(HERESY_MANAGED_ZMAPINFO_MARKER);
	return data.size() >= marker.size() &&
			std::equal(marker.begin(), marker.end(), data.begin());
}

std::string LowerASCII(std::string value)
{
	for (char &character : value)
		if (character >= 'A' && character <= 'Z')
			character = static_cast<char>(character - 'A' + 'a');
	return value;
}

std::string LumpNameForEntry(const std::string &entryName)
{
	const size_t slash = entryName.find_last_of('/');
	std::string filename = entryName.substr(slash == std::string::npos ? 0 :
			slash + 1);
	const size_t dot = filename.find_last_of('.');
	if (dot != std::string::npos && dot > 0)
		filename.erase(dot);
	return LowerASCII(std::move(filename));
}

bool MatchesAny(const std::string &name,
		std::initializer_list<const char *> candidates)
{
	return std::any_of(candidates.begin(), candidates.end(),
			[&name](const char *candidate)
			{
				return name == candidate;
			});
}

std::optional<Pk3MetadataKind> MetadataKindForEntry(
		const std::string &entryName)
{
	const size_t slash = entryName.find('/');
	const std::string folder = slash == std::string::npos ? "" :
			LowerASCII(entryName.substr(0, slash));
	const std::string lowerPath = LowerASCII(entryName);
	const size_t dot = lowerPath.find_last_of('.');
	const std::string extension = dot == std::string::npos ? "" :
			lowerPath.substr(dot);
	if (MatchesAny(folder, { "acs", "actors", "decorate", "zscript" }) ||
			MatchesAny(extension, { ".acs", ".zs" }))
	{
		return Pk3MetadataKind::runtimeSource;
	}

	const std::string name = LumpNameForEntry(entryName);
	if (MatchesAny(name, { "mapinfo", "zmapinfo", "umapinfo", "emapinfo",
			"gameinfo", "musinfo" }))
	{
		return Pk3MetadataKind::campaignDefinition;
	}
	if (MatchesAny(name, { "animdefs", "brightmap", "cvarinfo", "decaldef",
			"decaldefs", "fontdefs", "gldefs", "keyconf", "language",
			"lockdefs", "menudef", "modeldef", "reverbs", "sbarinfo",
			"skyboxes", "sndinfo", "sndseq", "switches", "terrain",
			"textures", "voxeldef", "x11r6rgb" }))
	{
		return Pk3MetadataKind::resourceDefinition;
	}
	if (MatchesAny(name, { "decorate", "dehacked", "loadacs", "zscript" }))
		return Pk3MetadataKind::runtimeSource;
	return std::nullopt;
}

struct ResourceGroupSpec
{
	const char *prefix;
	const char *label;
	bool projectedByEditor;
};

constexpr std::array<ResourceGroupSpec, 16> RESOURCE_GROUPS = {{
	{ "brightmaps", "Brightmaps", false },
	{ "colormaps", "Colormaps", false },
	{ "filter", "Game filters", false },
	{ "flats", "Flats", true },
	{ "fonts", "Fonts", false },
	{ "graphics", "Graphics", false },
	{ "hires", "High-resolution replacements", false },
	{ "materials", "Materials", false },
	{ "models", "Models", false },
	{ "music", "Music", false },
	{ "patches", "Patches", false },
	{ "shaders", "Shaders", false },
	{ "sounds", "Sounds", false },
	{ "sprites", "Sprites", true },
	{ "textures", "Textures", true },
	{ "voxels", "Voxels", false }
}};

std::optional<size_t> ResourceGroupForEntry(const std::string &entryName)
{
	const size_t slash = entryName.find('/');
	if (slash == std::string::npos || slash == 0)
		return std::nullopt;
	const std::string prefix = LowerASCII(entryName.substr(0, slash));
	for (size_t index = 0; index < RESOURCE_GROUPS.size(); ++index)
		if (prefix == RESOURCE_GROUPS[index].prefix)
			return index;
	return std::nullopt;
}

bool IsPreviewableText(const std::vector<uint8_t> &data)
{
	for (size_t index = 0; index < data.size();)
	{
		const uint8_t first = data[index];
		if (first < 0x80)
		{
			if ((first < 0x20 && first != '\t' && first != '\n' && first != '\r') ||
					first == 0x7f)
				return false;
			++index;
			continue;
		}

		size_t length = 0;
		if (first >= 0xc2 && first <= 0xdf)
			length = 2;
		else if (first >= 0xe0 && first <= 0xef)
			length = 3;
		else if (first >= 0xf0 && first <= 0xf4)
			length = 4;
		else
			return false;
		if (data.size() - index < length)
			return false;
		for (size_t continuation = 1; continuation < length; ++continuation)
			if ((data[index + continuation] & 0xc0) != 0x80)
				return false;
		if ((first == 0xe0 && data[index + 1] < 0xa0) ||
				(first == 0xc2 && data[index + 1] >= 0x80 &&
						data[index + 1] <= 0x9f) ||
				(first == 0xed && data[index + 1] >= 0xa0) ||
				(first == 0xf0 && data[index + 1] < 0x90) ||
				(first == 0xf4 && data[index + 1] >= 0x90))
		{
			return false;
		}
		index += length;
	}
	return true;
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

std::optional<WadNamespace> ResourceNamespaceForEntry(const std::string &name)
{
	const size_t slash = name.find('/');
	if (slash == std::string::npos)
		return WadNamespace::Global;

	const std::string folder = LowerASCII(name.substr(0, slash));
	if (folder == "flats")
		return WadNamespace::Flats;
	if (folder == "sprites")
		return WadNamespace::Sprites;
	if (folder == "textures")
		return WadNamespace::TextureLumps;
	return std::nullopt;
}

std::optional<ResourceNamespaceKind> DiagnosticNamespaceForEntry(
		const std::string &name)
{
	const std::optional<WadNamespace> nameSpace = ResourceNamespaceForEntry(name);
	if (!nameSpace)
		return std::nullopt;
	switch (*nameSpace)
	{
		case WadNamespace::Flats: return ResourceNamespaceKind::flat;
		case WadNamespace::Sprites: return ResourceNamespaceKind::sprite;
		case WadNamespace::TextureLumps: return ResourceNamespaceKind::texture;
		case WadNamespace::Global: return std::nullopt;
	}
	return std::nullopt;
}

SString ProjectedLumpNameForEntry(const std::string &entryName)
{
	fs::path resourcePath(entryName);
	return SString(resourcePath.filename().replace_extension().u8string()).
			asUpper().substr(0, 8);
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

			ResourceRecord record;
			const std::optional<WadNamespace> nameSpace =
					ResourceNamespaceForEntry(entryName);
			if (!nameSpace)
				continue;
			record.nameSpace = *nameSpace;

			const fs::path resourcePath(entryName);
			if (MatchExtensionNoCase(resourcePath, ".wad"))
				continue;
			record.lumpName = ProjectedLumpNameForEntry(entryName);
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

		// Root global declarations are normally projected read-only resources.
		// ZMAPINFO is the single exception: the explicit runtime generator owns a
		// marker-protected root entry and updates it through this package view so
		// later project saves cannot accidentally discard it.
		const Lump_c *runtimeMapInfo = aggregate.FindLump(RUNTIME_MAPINFO_ENTRY);
		if (runtimeMapInfo && IsManagedRuntimeMapInfo(runtimeMapInfo->getData()))
		{
			const std::vector<uint8_t> &data = runtimeMapInfo->getData();
			if (archive_->contains(RUNTIME_MAPINFO_ENTRY))
			{
				const std::vector<uint8_t> existing =
						archive_->readEntry(RUNTIME_MAPINFO_ENTRY);
				if (!IsManagedRuntimeMapInfo(existing) && existing != data)
				{
					throw std::runtime_error("Refusing to replace user-authored root "
							"ZMAPINFO in the PK3 package.");
				}
				if (existing != data)
					archive_->setEntry(RUNTIME_MAPINFO_ENTRY, data);
			}
			else
				archive_->setEntry(RUNTIME_MAPINFO_ENTRY, data);
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

const char *M_Pk3MetadataKindName(Pk3MetadataKind kind) noexcept
{
	switch (kind)
	{
		case Pk3MetadataKind::campaignDefinition:
			return "Campaign definition";
		case Pk3MetadataKind::resourceDefinition:
			return "Resource definition";
		case Pk3MetadataKind::runtimeSource:
			return "Runtime source";
	}
	return "Metadata";
}

std::optional<Pk3PackageInventory> M_InspectPk3Package(
		const fs::path &path, SString *error) noexcept
{
	try
	{
		if (error)
			error->clear();
		if (M_ProjectPackageForPath(path) != ProjectPackage::pk3)
		{
			if (error)
				*error = "Package metadata inspection requires a PK3 or ZIP archive.";
			return std::nullopt;
		}

		std::shared_ptr<ZipArchive> archive = ZipArchive::Open(path);
		if (!archive)
			throw std::runtime_error("The package could not be opened.");

		Pk3PackageInventory inventory;
		inventory.path = path;
		for (const ResourceGroupSpec &group : RESOURCE_GROUPS)
		{
			inventory.resources.push_back({ group.prefix, group.label, 0, 0,
					group.projectedByEditor });
		}

		const std::vector<ZipEntryInfo> entries = archive->entryInfos();
		inventory.archiveEntries = entries.size();
		uint64_t previewBytes = 0;
		for (const ZipEntryInfo &entry : entries)
		{
			if (entry.directory)
			{
				++inventory.directoryEntries;
				continue;
			}
			++inventory.totalFiles;
			inventory.totalSize += entry.uncompressedSize;

			if (IsPk3MapEntry(entry.name))
			{
				++inventory.mapFiles;
				continue;
			}
			if (LowerASCII(entry.name) == PROJECT_METADATA_ENTRY)
			{
				++inventory.editorFiles;
				continue;
			}

			const fs::path resourcePath(entry.name);
			if (!entry.encrypted && entry.uncompressedSize > 0 &&
					(entry.compressionMethod == 0 || entry.compressionMethod == 8) &&
					!MatchExtensionNoCase(resourcePath, ".wad"))
			{
				if (const std::optional<ResourceNamespaceKind> nameSpace =
						DiagnosticNamespaceForEntry(entry.name))
				{
					const SString editorName = ProjectedLumpNameForEntry(entry.name);
					if (editorName.good())
					{
						inventory.projectedResources.push_back({ entry.name, editorName,
								*nameSpace, entry.uncompressedSize });
					}
				}
			}

			if (const std::optional<Pk3MetadataKind> kind =
					MetadataKindForEntry(entry.name))
			{
				Pk3MetadataEntry metadata;
				metadata.path = entry.name;
				metadata.kind = *kind;
				metadata.size = entry.uncompressedSize;
				if (entry.encrypted)
				{
					metadata.previewState = Pk3PreviewState::unavailable;
					metadata.detail = "Encrypted entry; content was not read.";
				}
				else if (entry.uncompressedSize > PK3_METADATA_PREVIEW_LIMIT)
				{
					metadata.previewState = Pk3PreviewState::tooLarge;
					metadata.detail = "Entry exceeds the safe 512 KiB preview limit.";
				}
				else if (entry.uncompressedSize >
						PK3_METADATA_TOTAL_PREVIEW_LIMIT - previewBytes)
				{
					metadata.previewState = Pk3PreviewState::tooLarge;
					metadata.detail = "The package has reached the safe 4 MiB total preview budget.";
				}
				else
				{
					previewBytes += entry.uncompressedSize;
					try
					{
						const std::vector<uint8_t> content =
								archive->readEntry(entry.name);
						if (content.empty())
						{
							metadata.previewState = Pk3PreviewState::empty;
							metadata.detail = "The entry is empty.";
						}
						else if (!IsPreviewableText(content))
						{
							metadata.previewState = Pk3PreviewState::binary;
							metadata.detail = "Binary or control data; no text preview is shown.";
						}
						else
						{
							metadata.previewState = Pk3PreviewState::text;
							metadata.preview = std::string(content.begin(), content.end());
							metadata.detail = *kind == Pk3MetadataKind::runtimeSource ?
									"Displayed verbatim; runtime code is not interpreted." :
									"Displayed verbatim; declarations are not interpreted.";
						}
					}
					catch (const std::runtime_error &readError)
					{
						metadata.previewState = Pk3PreviewState::unavailable;
						metadata.detail = SString::printf("Content unavailable: %s",
								readError.what());
					}
				}
				inventory.metadata.push_back(std::move(metadata));
				continue;
			}

			if (const std::optional<size_t> group =
					ResourceGroupForEntry(entry.name))
			{
				Pk3ResourceGroup &summary = inventory.resources[*group];
				++summary.entries;
				summary.size += entry.uncompressedSize;
				++inventory.resourceFiles;
				continue;
			}

			++inventory.otherFiles;
		}

		inventory.resources.erase(std::remove_if(inventory.resources.begin(),
				inventory.resources.end(), [](const Pk3ResourceGroup &group)
				{
					return group.entries == 0;
				}), inventory.resources.end());
		std::sort(inventory.metadata.begin(), inventory.metadata.end(),
				[](const Pk3MetadataEntry &left, const Pk3MetadataEntry &right)
				{
					if (left.kind != right.kind)
						return left.kind < right.kind;
					return left.path.asLower() < right.path.asLower();
				});
		return inventory;
	}
	catch (const std::exception &exception)
	{
		if (error)
			*error = exception.what();
		return std::nullopt;
	}
	catch (...)
	{
		if (error)
			*error = "The package could not be inspected.";
		return std::nullopt;
	}
}

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
