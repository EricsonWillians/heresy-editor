//------------------------------------------------------------------------
//  SURFACE TEXTURE IMPORT PLANNING
//------------------------------------------------------------------------

#include "m_texture_import.h"

#include "Document.h"
#include "lib_file.h"
#include "m_game.h"
#include "WadData.h"
#include "w_loadpic.h"
#include "w_wad.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>

namespace
{

struct ExistingResource
{
	SString name;
	TextureResourceOwner owner;
};

bool PathEqual(const fs::path &left, const fs::path &right)
{
	std::error_code leftError;
	std::error_code rightError;
	const fs::path leftAbsolute = fs::absolute(left, leftError).lexically_normal();
	const fs::path rightAbsolute =
			fs::absolute(right, rightError).lexically_normal();
	return SString((leftError ? left : leftAbsolute).u8string()).noCaseEqual(
			(rightError ? right : rightAbsolute).u8string());
}

uint64_t ContentHash(const std::vector<uint8_t> &data) noexcept
{
	uint64_t hash = 1469598103934665603ull;
	for (uint8_t value : data)
	{
		hash ^= value;
		hash *= 1099511628211ull;
	}
	return hash;
}

bool IsModernImage(TextureImportFormat format) noexcept
{
	return format == TextureImportFormat::png ||
			format == TextureImportFormat::jpeg ||
			format == TextureImportFormat::tga;
}

uint16_t ReadLittle16(const std::vector<uint8_t> &data, size_t offset)
{
	if (offset > data.size() || data.size() - offset < 2)
		throw std::runtime_error("Truncated image header.");
	return static_cast<uint16_t>(data[offset]) |
			(static_cast<uint16_t>(data[offset + 1]) << 8);
}

uint32_t ReadLittle32(const std::vector<uint8_t> &data, size_t offset)
{
	if (offset > data.size() || data.size() - offset < 4)
		throw std::runtime_error("Truncated image header.");
	return static_cast<uint32_t>(data[offset]) |
			(static_cast<uint32_t>(data[offset + 1]) << 8) |
			(static_cast<uint32_t>(data[offset + 2]) << 16) |
			(static_cast<uint32_t>(data[offset + 3]) << 24);
}

uint32_t ReadBig32(const std::vector<uint8_t> &data, size_t offset)
{
	if (offset > data.size() || data.size() - offset < 4)
		throw std::runtime_error("Truncated image header.");
	return (static_cast<uint32_t>(data[offset]) << 24) |
			(static_cast<uint32_t>(data[offset + 1]) << 16) |
			(static_cast<uint32_t>(data[offset + 2]) << 8) |
			static_cast<uint32_t>(data[offset + 3]);
}

bool IsPng(const std::vector<uint8_t> &data) noexcept
{
	static constexpr uint8_t signature[] =
			{ 137, 80, 78, 71, 13, 10, 26, 10 };
	return data.size() >= sizeof(signature) &&
			std::equal(std::begin(signature), std::end(signature),
					data.begin());
}

bool IsJpeg(const std::vector<uint8_t> &data) noexcept
{
	return data.size() >= 4 && data[0] == 0xff && data[1] == 0xd8 &&
			data[2] == 0xff;
}

bool IsTga(const std::vector<uint8_t> &data) noexcept
{
	if (data.size() < 18)
		return false;
	const uint8_t colorMap = data[1];
	const uint8_t type = data[2];
	const uint8_t depth = data[16];
	return ReadLittle16(data, 12) > 0 && ReadLittle16(data, 14) > 0 &&
			(colorMap == 0 || colorMap == 1) &&
			(type == 1 || type == 2 || type == 3 ||
					type == 9 || type == 10 || type == 11) &&
			(depth == 8 || depth == 15 || depth == 16 ||
					depth == 24 || depth == 32);
}

std::optional<std::pair<int, int>> JpegDimensions(
		const std::vector<uint8_t> &data)
{
	if (!IsJpeg(data))
		return std::nullopt;
	size_t position = 2;
	while (position + 1 < data.size())
	{
		while (position < data.size() && data[position] != 0xff)
			++position;
		while (position < data.size() && data[position] == 0xff)
			++position;
		if (position >= data.size())
			break;
		const uint8_t marker = data[position++];
		if (marker == 0xd8 || marker == 0xd9 ||
				(marker >= 0xd0 && marker <= 0xd7) || marker == 0x01)
		{
			continue;
		}
		if (position + 2 > data.size())
			break;
		const uint16_t length =
				(static_cast<uint16_t>(data[position]) << 8) |
				data[position + 1];
		if (length < 2 || position + length > data.size())
			break;
		const bool startOfFrame =
				(marker >= 0xc0 && marker <= 0xc3) ||
				(marker >= 0xc5 && marker <= 0xc7) ||
				(marker >= 0xc9 && marker <= 0xcb) ||
				(marker >= 0xcd && marker <= 0xcf);
		if (startOfFrame && length >= 7)
		{
			const int height =
					(static_cast<int>(data[position + 3]) << 8) |
					data[position + 4];
			const int width =
					(static_cast<int>(data[position + 5]) << 8) |
					data[position + 6];
			return std::pair<int, int>{ width, height };
		}
		position += length;
	}
	return std::nullopt;
}

bool ValidateDoomPatch(const std::vector<uint8_t> &data)
{
	if (data.size() < 12)
		return false;
	const uint16_t width = ReadLittle16(data, 0);
	const uint16_t height = ReadLittle16(data, 2);
	if (width == 0 || width > 2048 || height == 0 || height > 512 ||
			data.size() < 8u + static_cast<size_t>(width) * 4u)
	{
		return false;
	}
	for (size_t column = 0; column < width; ++column)
	{
		size_t position = ReadLittle32(data, 8 + column * 4);
		if (position >= data.size())
			return false;
		bool terminated = false;
		while (position < data.size())
		{
			if (data[position] == 0xff)
			{
				terminated = true;
				break;
			}
			if (data.size() - position < 4)
				return false;
			const size_t length = data[position + 1];
			if (length > data.size() - position - 4)
				return false;
			position += length + 4;
		}
		if (!terminated)
			return false;
	}
	return true;
}

std::optional<std::pair<int, int>> EncodedDimensions(
		const std::vector<uint8_t> &data, TextureImportFormat format)
{
	switch (format)
	{
		case TextureImportFormat::png:
			if (data.size() < 24 || !IsPng(data))
				return std::nullopt;
			return std::pair<int, int>{
					static_cast<int>(ReadBig32(data, 16)),
					static_cast<int>(ReadBig32(data, 20)) };
		case TextureImportFormat::jpeg:
			return JpegDimensions(data);
		case TextureImportFormat::tga:
			if (!IsTga(data))
				return std::nullopt;
			return std::pair<int, int>{
					static_cast<int>(ReadLittle16(data, 12)),
					static_cast<int>(ReadLittle16(data, 14)) };
		case TextureImportFormat::doomPatch:
			if (!ValidateDoomPatch(data))
				return std::nullopt;
			return std::pair<int, int>{
					static_cast<int>(ReadLittle16(data, 0)),
					static_cast<int>(ReadLittle16(data, 2)) };
		case TextureImportFormat::rawFlat:
			return std::pair<int, int>{ 64, 64 };
		case TextureImportFormat::unknown:
			break;
	}
	return std::nullopt;
}

bool EncodedSupportsAlpha(const std::vector<uint8_t> &data,
		TextureImportFormat format, const Img_c &decoded)
{
	if (format == TextureImportFormat::png && data.size() >= 26)
	{
		const uint8_t colorType = data[25];
		if (colorType == 4 || colorType == 6)
			return true;
		for (size_t position = 8; position + 12 <= data.size();)
		{
			const uint32_t length = ReadBig32(data, position);
			if (length > data.size() - position - 12)
				break;
			if (data[position + 4] == 't' &&
					data[position + 5] == 'R' &&
					data[position + 6] == 'N' &&
					data[position + 7] == 'S')
				return true;
			position += static_cast<size_t>(length) + 12;
		}
	}
	if (format == TextureImportFormat::tga && data.size() >= 18)
		return data[16] == 32 && (data[17] & 0x0f) > 0;
	if (format == TextureImportFormat::doomPatch)
		return true;
	return decoded.has_transparent();
}

SString ExtensionForFormat(TextureImportFormat format)
{
	switch (format)
	{
		case TextureImportFormat::png: return ".png";
		case TextureImportFormat::jpeg: return ".jpg";
		case TextureImportFormat::tga: return ".tga";
		case TextureImportFormat::doomPatch:
		case TextureImportFormat::rawFlat: return ".lmp";
		case TextureImportFormat::unknown: return {};
	}
	return {};
}

std::vector<PackageResourceKind> DestinationKinds(TextureSurfaceUsage usage)
{
	switch (usage)
	{
		case TextureSurfaceUsage::walls:
			return { PackageResourceKind::wallTexture };
		case TextureSurfaceUsage::planes:
			return { PackageResourceKind::flat };
		case TextureSurfaceUsage::allSurfaces:
			return { PackageResourceKind::wallTexture,
					PackageResourceKind::flat };
	}
	return {};
}

SString DestinationPath(ProjectPackage package, PackageResourceKind kind,
		const SString &name, const SString &extension)
{
	if (package == ProjectPackage::wad)
		return kind == PackageResourceKind::flat ?
				SString::printf("F_START/%s", name.c_str()) :
				SString::printf("TX_START/%s", name.c_str());
	return SString::printf("%s/%s%s",
			kind == PackageResourceKind::flat ? "flats" : "textures",
			name.c_str(), extension.c_str());
}

bool IsReservedName(const SString &name) noexcept
{
	static const char *const reserved[] = {
		"F_START", "F_END", "FF_START", "FF_END",
		"TX_START", "TX_END", "S_START", "S_END",
		"SS_START", "SS_END", "__EUREKA",
		"THINGS", "LINEDEFS", "SIDEDEFS", "VERTEXES", "SEGS",
		"SSECTORS", "NODES", "SECTORS", "REJECT", "BLOCKMAP",
		"BEHAVIOR", "SCRIPTS", "TEXTMAP", "ENDMAP", "ZNODES",
		"GL_VERT", "GL_SEGS", "GL_SSECT", "GL_NODES",
		"TEXTURE1", "TEXTURE2", "PNAMES", "PLAYPAL", "COLORMAP"
	};
	for (const char *candidate : reserved)
		if (name.noCaseEqual(candidate))
			return true;
	const SString upper = name.asUpper();
	if (upper.length() == 5 && upper.substr(0, 3) == "MAP" &&
			upper[3] >= '0' && upper[3] <= '9' &&
			upper[4] >= '0' && upper[4] <= '9')
	{
		return true;
	}
	if (upper.length() == 4 && upper[0] == 'E' &&
			upper[1] >= '0' && upper[1] <= '9' &&
			upper[2] == 'M' &&
			upper[3] >= '0' && upper[3] <= '9')
	{
		return true;
	}
	return false;
}

std::vector<ExistingResource> LoadedResources(const WadData &wad,
		const fs::path &packagePath)
{
	std::vector<ExistingResource> result;
	const std::vector<std::shared_ptr<Wad_file>> sources = wad.master.getAll();
	const std::vector<PackageResourceEntry> projectEntries =
			M_ListPackageResources(packagePath);
	size_t resourceNumber = 0;
	for (size_t sourceIndex = 0; sourceIndex < sources.size(); ++sourceIndex)
	{
		const bool isProject = sources[sourceIndex] == wad.master.editWad() ||
				PathEqual(sources[sourceIndex]->PathName(), packagePath);
		SString label;
		if (sources[sourceIndex] == wad.master.gameWad())
			label = "IWAD";
		else if (isProject)
			label = "Project";
		else
			label = SString::printf("Resource %llu",
					static_cast<unsigned long long>(++resourceNumber));

		std::map<std::pair<PackageResourceKind, SString>, size_t> occurrences;
		std::map<std::pair<PackageResourceKind, SString>, SString> entryPaths;
		for (const LumpRef &reference : sources[sourceIndex]->getDir())
		{
			if (!reference.lump)
				continue;
			PackageResourceKind kind;
			if (reference.ns == WadNamespace::Flats)
				kind = PackageResourceKind::flat;
			else if (reference.ns == WadNamespace::TextureLumps)
				kind = PackageResourceKind::wallTexture;
			else
				continue;
			const auto key =
					std::make_pair(kind, reference.lump->Name().asUpper());
			++occurrences[key];
			if (!entryPaths.count(key))
				entryPaths[key] = SString::printf("%s/%s",
						kind == PackageResourceKind::flat ?
								"flat namespace" : "texture namespace",
						reference.lump->Name().c_str());
		}

		for (const char *textureLumpName : { "TEXTURE1", "TEXTURE2" })
		{
			const Lump_c *textureLump =
					sources[sourceIndex]->FindLumpInNamespace(
							textureLumpName, WadNamespace::Global);
			if (!textureLump)
				continue;
			const std::vector<byte> &data = textureLump->getData();
			if (data.size() < 4)
				continue;
			const int32_t count =
					static_cast<int32_t>(ReadLittle32(data, 0));
			if (count < 0 || count > (1 << 20) ||
					data.size() < 4u + static_cast<size_t>(count) * 4u)
			{
				continue;
			}
			for (int32_t index = 0; index < count; ++index)
			{
				const uint32_t offset =
						ReadLittle32(data, 4u +
								static_cast<size_t>(index) * 4u);
				if (offset > data.size() || data.size() - offset < 8)
					continue;
				char rawName[9] = {};
				memcpy(rawName, data.data() + offset, 8);
				const SString name = SString(rawName).asUpper();
				if (name.empty() ||
						(strcmp(textureLumpName, "TEXTURE1") == 0 &&
								index == 0))
				{
					continue;
				}
				const auto key =
						std::make_pair(PackageResourceKind::wallTexture,
								name);
				++occurrences[key];
				entryPaths[key] =
						SString::printf("%s/%s", textureLumpName,
								name.c_str());
			}
		}

		for (const auto &entry : occurrences)
		{
			TextureResourceOwner owner;
			owner.label = label;
			owner.packagePath = sources[sourceIndex]->PathName();
			owner.kind = entry.first.first;
			owner.provenance =
					sources[sourceIndex] == wad.master.gameWad() ?
							TextureResourceProvenance::iwad :
					isProject ? TextureResourceProvenance::project :
							TextureResourceProvenance::externalResource;
			owner.loadOrder = sourceIndex + 1;
			owner.occurrences = entry.second;
			owner.project = isProject;
			auto entryPath = entryPaths.find(entry.first);
			if (entryPath != entryPaths.end())
				owner.entryPath = entryPath->second;
			if (isProject)
			{
				std::vector<const PackageResourceEntry *> matches;
				for (const PackageResourceEntry &projectEntry : projectEntries)
				if (projectEntry.kind == owner.kind &&
						projectEntry.editorName.noCaseEqual(entry.first.second))
					matches.push_back(&projectEntry);
				if (matches.size() == 1 &&
						entry.second == 1)
				{
					owner.entryPath = matches.front()->entryPath;
					owner.replaceableProjectEntry = true;
				}
			}
			result.push_back({ entry.first.second, std::move(owner) });
		}
	}
	return result;
}

std::vector<TextureResourceOwner> FindConflicts(
		const std::vector<ExistingResource> &existing, const SString &name)
{
	std::vector<TextureResourceOwner> result;
	for (const ExistingResource &resource : existing)
		if (resource.name.noCaseEqual(name))
			result.push_back(resource.owner);
	return result;
}

bool NameUsed(const std::vector<ExistingResource> &existing,
		const std::set<SString> &planned, const SString &name)
{
	if (planned.count(name.asUpper()) || IsReservedName(name))
		return true;
	return std::any_of(existing.begin(), existing.end(),
			[&name](const ExistingResource &resource)
			{
				return resource.name.noCaseEqual(name);
			});
}

SString UniqueName(const std::vector<ExistingResource> &existing,
		const std::set<SString> &planned, const SString &base)
{
	if (!NameUsed(existing, planned, base))
		return base;
	for (unsigned int suffix = 2; suffix < 1000000; ++suffix)
	{
		const SString suffixText = SString::printf("_%u", suffix);
		const size_t prefixLength = suffixText.length() >= 8 ?
				0 : 8 - suffixText.length();
		SString prefix = base.substr(0, prefixLength);
		while (!prefix.empty() && prefix.back() == '_')
			prefix.pop_back();
		SString candidate = prefix + suffixText;
		if (!NameUsed(existing, planned, candidate))
			return candidate;
	}
	return {};
}

std::shared_ptr<Img_c> DecodeImage(const WadData &wad,
		const ConfigData &config, const std::vector<uint8_t> &data,
		const SString &name, TextureImportFormat &format,
		SString &error)
{
	Lump_c lump(name);
	lump.setData(std::vector<byte>(data.begin(), data.end()));
	ImageFormat detected = ImageFormat::unrecognized;
	if (IsPng(data))
		detected = ImageFormat::png;
	else if (IsJpeg(data))
		detected = ImageFormat::jpeg;
	else if (IsTga(data))
		detected = ImageFormat::tga;
	else
		detected = W_DetectImageFormat(lump);
	std::optional<Img_c> decoded;
	switch (detected)
	{
		case ImageFormat::png:
			format = TextureImportFormat::png;
			break;
		case ImageFormat::jpeg:
			format = TextureImportFormat::jpeg;
			break;
		case ImageFormat::tga:
			format = TextureImportFormat::tga;
			break;
		case ImageFormat::doom:
			format = TextureImportFormat::doomPatch;
			break;
		default:
			break;
	}
	if (format != TextureImportFormat::unknown)
	{
		const std::optional<std::pair<int, int>> dimensions =
				EncodedDimensions(data, format);
		if (!dimensions)
		{
			error = "The image header or pixel data is malformed.";
			return {};
		}
		if (dimensions->first <= 0 || dimensions->second <= 0 ||
				dimensions->first > TEXTURE_IMPORT_MAX_DIMENSION ||
				dimensions->second > TEXTURE_IMPORT_MAX_DIMENSION)
		{
			error = "Decoded dimensions must be between 1 and 8192 pixels.";
			return {};
		}
		switch (format)
		{
			case TextureImportFormat::png:
				decoded = LoadImage_PNG(lump, name);
				break;
			case TextureImportFormat::jpeg:
				decoded = LoadImage_JPEG(lump, name);
				break;
			case TextureImportFormat::tga:
				decoded = LoadImage_TGA(lump, name);
				break;
			case TextureImportFormat::doomPatch:
			{
				Img_c image;
				if (LoadPicture(wad.palette, config, image, lump, name, 0, 0))
					decoded = std::move(image);
				break;
			}
			default:
				break;
		}
	}
	if (!decoded && format == TextureImportFormat::unknown &&
			data.size() == 64u * 64u)
	{
		Img_c image(64, 64, false);
		for (size_t index = 0; index < data.size(); ++index)
		{
			const uint8_t value = data[index];
			image.wbuf()[index] = value == TRANS_PIXEL ?
					static_cast<img_pixel_t>(wad.palette.getTransReplace()) :
					static_cast<img_pixel_t>(value);
		}
		format = TextureImportFormat::rawFlat;
		decoded = std::move(image);
	}
	if (!decoded)
	{
		if (error.empty())
			error = "The file is not a supported or valid surface image.";
		return {};
	}
	return std::make_shared<Img_c>(std::move(*decoded));
}

int64_t WriteTimeValue(const fs::path &path)
{
	std::error_code error;
	const fs::file_time_type time = fs::last_write_time(path, error);
	if (error)
		return 0;
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
			time.time_since_epoch()).count();
}

uint64_t FileSizeValue(const fs::path &path)
{
	std::error_code error;
	const uintmax_t size = fs::file_size(path, error);
	return error ? 0 : static_cast<uint64_t>(size);
}

size_t CountActiveUsages(const Document *document, const SString &name)
{
	if (!document)
		return 0;
	size_t result = 0;
	for (const std::shared_ptr<SideDef> &side : document->sidedefs)
	{
		if (side->UpperTex().noCaseEqual(name))
			++result;
		if (side->MidTex().noCaseEqual(name))
			++result;
		if (side->LowerTex().noCaseEqual(name))
			++result;
	}
	for (const std::shared_ptr<Sector> &sector : document->sectors)
	{
		if (sector->FloorTex().noCaseEqual(name))
			++result;
		if (sector->CeilTex().noCaseEqual(name))
			++result;
	}
	return result;
}

void AddIssue(TextureImportPlan &plan, TextureImportSeverity severity,
		int item, const SString &explanation)
{
	plan.issues.push_back({ severity, item, explanation });
}

bool SameReviewedPlan(const TextureImportPlan &left,
		const TextureImportPlan &right)
{
	if (left.packagePath != right.packagePath ||
			left.packageSize != right.packageSize ||
			left.packageWriteTime != right.packageWriteTime ||
			left.items.size() != right.items.size())
		return false;
	for (size_t index = 0; index < left.items.size(); ++index)
	{
		const TextureImportPlanItem &a = left.items[index];
		const TextureImportPlanItem &b = right.items[index];
		if (a.source != b.source || a.resolvedName != b.resolvedName ||
				a.usage != b.usage || a.conflictPolicy != b.conflictPolicy ||
				a.format != b.format || a.byteSize != b.byteSize ||
				a.contentHash != b.contentHash || a.skipped != b.skipped ||
				a.destinations.size() != b.destinations.size())
			return false;
		for (size_t destination = 0;
				destination < a.destinations.size(); ++destination)
		{
			const TextureImportDestination &ad = a.destinations[destination];
			const TextureImportDestination &bd = b.destinations[destination];
			if (ad.kind != bd.kind || ad.packagePath != bd.packagePath ||
					ad.replaceEntryPath != bd.replaceEntryPath)
				return false;
		}
	}
	return true;
}

} // namespace

bool TextureImportPlan::valid() const noexcept
{
	return package != ProjectPackage::none && importCount() > 0 &&
			std::none_of(issues.begin(), issues.end(),
					[](const TextureImportIssue &issue)
					{
						return issue.severity == TextureImportSeverity::error;
					});
}

size_t TextureImportPlan::importCount() const noexcept
{
	return static_cast<size_t>(std::count_if(items.begin(), items.end(),
			[](const TextureImportPlanItem &item)
			{
				return !item.skipped && !item.destinations.empty();
			}));
}

size_t TextureImportPlan::replacementCount() const noexcept
{
	size_t result = 0;
	for (const TextureImportPlanItem &item : items)
		for (const TextureImportDestination &destination : item.destinations)
			if (destination.replaceEntryPath)
				++result;
	return result;
}

bool TextureImportPlan::destructive() const noexcept
{
	return replacementCount() > 0 ||
			std::any_of(items.begin(), items.end(),
					[](const TextureImportPlanItem &item)
					{
						return !item.skipped &&
								item.conflictPolicy ==
										TextureConflictPolicy::overrideLoaded &&
								!item.conflicts.empty();
					});
}

const char *M_TextureSurfaceUsageName(TextureSurfaceUsage usage) noexcept
{
	switch (usage)
	{
		case TextureSurfaceUsage::walls: return "Walls";
		case TextureSurfaceUsage::planes: return "Floors/Ceilings";
		case TextureSurfaceUsage::allSurfaces: return "All surfaces";
	}
	return "Surfaces";
}

const char *M_TextureConflictPolicyName(TextureConflictPolicy policy) noexcept
{
	switch (policy)
	{
		case TextureConflictPolicy::renameImported: return "Rename imported";
		case TextureConflictPolicy::overrideLoaded: return "Override loaded";
		case TextureConflictPolicy::replaceProject: return "Replace project";
		case TextureConflictPolicy::skip: return "Skip";
	}
	return "Conflict";
}

const char *M_TextureImportFormatName(TextureImportFormat format) noexcept
{
	switch (format)
	{
		case TextureImportFormat::png: return "PNG";
		case TextureImportFormat::jpeg: return "JPEG";
		case TextureImportFormat::tga: return "TGA";
		case TextureImportFormat::doomPatch: return "Doom patch";
		case TextureImportFormat::rawFlat: return "Raw 64x64 flat";
		case TextureImportFormat::unknown: return "Unknown";
	}
	return "Unknown";
}

SString M_NormalizeImportedTextureName(const fs::path &source,
		const SString &requested)
{
	SString input = requested;
	if (input.empty())
		input = source.stem().u8string();
	SString result;
	bool previousUnderscore = false;
	for (char character : input)
	{
		unsigned char value = static_cast<unsigned char>(character);
		char normalized = '_';
		if (value >= 'a' && value <= 'z')
			normalized = static_cast<char>(value - 'a' + 'A');
		else if ((value >= 'A' && value <= 'Z') ||
				(value >= '0' && value <= '9') ||
				value == '_' || value == '-')
			normalized = static_cast<char>(value);
		if (normalized == '_' && previousUnderscore)
			continue;
		result.push_back(normalized);
		previousUnderscore = normalized == '_';
		if (result.length() == 8)
			break;
	}
	while (!result.empty() && result.back() == '_')
		result.pop_back();
	if (result.empty())
		result = "TEXTURE";
	return result;
}

TextureImportPlan M_PlanTextureImport(const WadData &wad,
		const ConfigData &config, const fs::path &packagePath,
		const std::vector<TextureImportRequestItem> &requests,
		const Document *activeDocument)
{
	TextureImportPlan plan;
	plan.packagePath = packagePath;
	plan.package = M_ProjectPackageForPath(packagePath);
	plan.packageSize = FileSizeValue(packagePath);
	plan.packageWriteTime = WriteTimeValue(packagePath);

	if (plan.package == ProjectPackage::none ||
			!M_ValidateEditablePackage(packagePath))
	{
		AddIssue(plan, TextureImportSeverity::error, -1,
				"Texture import requires an existing WAD or PK3 package.");
		return plan;
	}
	const std::shared_ptr<Wad_file> editable =
			M_OpenEditablePackage(packagePath);
	if (!editable || editable->IsReadOnly())
	{
		AddIssue(plan, TextureImportSeverity::error, -1,
				"The project package is not writable.");
		return plan;
	}

	std::vector<ExistingResource> existing;
	try
	{
		existing = LoadedResources(wad, packagePath);
	}
	catch (const std::exception &error)
	{
		AddIssue(plan, TextureImportSeverity::error, -1,
				SString::printf("Could not inspect loaded resources: %s",
						error.what()));
		return plan;
	}

	std::set<SString> plannedNames;
	for (size_t index = 0; index < requests.size(); ++index)
	{
		const TextureImportRequestItem &request = requests[index];
		TextureImportPlanItem item;
		item.source = request.source;
		item.requestedName =
				M_NormalizeImportedTextureName(request.source,
						request.requestedName);
		item.resolvedName = item.requestedName;
		item.conflictPolicy = request.conflictPolicy;
		item.skipped =
				request.conflictPolicy == TextureConflictPolicy::skip;
		const SString originalName = request.requestedName.empty() ?
				SString(request.source.stem().u8string()) :
				request.requestedName;
		if (!originalName.noCaseEqual(item.requestedName))
		{
			AddIssue(plan, TextureImportSeverity::warning,
					static_cast<int>(index),
					SString::printf("The portable map name is %s.",
							item.requestedName.c_str()));
		}

		std::error_code sizeError;
		const uintmax_t sourceSize = fs::file_size(request.source, sizeError);
		if (sizeError)
		{
			AddIssue(plan, TextureImportSeverity::error,
					static_cast<int>(index),
					"The source file is missing or unreadable.");
			plan.items.push_back(std::move(item));
			continue;
		}
		if (sourceSize == 0 || sourceSize > TEXTURE_IMPORT_MAX_FILE_BYTES)
		{
			AddIssue(plan, TextureImportSeverity::error,
					static_cast<int>(index),
					"The source must contain 1 byte to 256 MiB.");
			plan.items.push_back(std::move(item));
			continue;
		}
		if (!FileLoad(request.source, item.data))
		{
			AddIssue(plan, TextureImportSeverity::error,
					static_cast<int>(index),
					"The source file could not be read.");
			plan.items.push_back(std::move(item));
			continue;
		}
		item.byteSize = item.data.size();
		item.contentHash = ContentHash(item.data);
		if (!item.skipped)
			plan.totalBytes += item.byteSize;
		SString decodeError;
		item.preview = DecodeImage(wad, config, item.data,
				item.requestedName, item.format, decodeError);
		if (!item.preview)
		{
			AddIssue(plan, TextureImportSeverity::error,
					static_cast<int>(index),
					decodeError);
			plan.items.push_back(std::move(item));
			continue;
		}
		item.width = item.preview->width();
		item.height = item.preview->height();
		item.hasAlpha =
				EncodedSupportsAlpha(item.data, item.format, *item.preview);
		item.extension = ExtensionForFormat(item.format);
		if (item.width <= 0 || item.height <= 0 ||
				item.width > TEXTURE_IMPORT_MAX_DIMENSION ||
				item.height > TEXTURE_IMPORT_MAX_DIMENSION)
		{
			AddIssue(plan, TextureImportSeverity::error,
					static_cast<int>(index),
					"Decoded dimensions must be between 1 and 8192 pixels.");
		}

		item.usage = request.usage;
		if (request.automaticUsage)
		{
			if (item.format == TextureImportFormat::doomPatch)
				item.usage = TextureSurfaceUsage::walls;
			else if (item.format == TextureImportFormat::rawFlat)
				item.usage = TextureSurfaceUsage::planes;
			else
				item.usage = TextureSurfaceUsage::allSurfaces;
		}
		if (item.format == TextureImportFormat::doomPatch &&
				item.usage != TextureSurfaceUsage::walls)
		{
			AddIssue(plan, TextureImportSeverity::error,
					static_cast<int>(index),
					"Doom patches are wall-only in this importer.");
		}
		if (item.format == TextureImportFormat::rawFlat &&
				item.usage != TextureSurfaceUsage::planes)
		{
			AddIssue(plan, TextureImportSeverity::error,
					static_cast<int>(index),
					"Traditional raw flats are floor/ceiling-only.");
		}
		if ((IsModernImage(item.format) ||
				item.format == TextureImportFormat::doomPatch) &&
				!config.features.tx_start)
		{
			AddIssue(plan, TextureImportSeverity::error,
					static_cast<int>(index),
					"The active port does not support standalone modern image "
					"namespaces. Classic PNAMES/TEXTURE1 composition and "
					"palette conversion are not implemented.");
		}

		item.conflicts = FindConflicts(existing, item.requestedName);
		item.activeMapUsages =
				CountActiveUsages(activeDocument, item.requestedName);
		if (item.skipped)
		{
			plan.items.push_back(std::move(item));
			continue;
		}

		if (request.conflictPolicy ==
				TextureConflictPolicy::renameImported)
		{
			item.resolvedName = UniqueName(existing, plannedNames,
					item.requestedName);
			if (item.resolvedName.empty())
			{
				AddIssue(plan, TextureImportSeverity::error,
						static_cast<int>(index),
						"Could not generate a unique portable resource name.");
			}
			else if (!item.resolvedName.noCaseEqual(item.requestedName))
			{
				AddIssue(plan, TextureImportSeverity::warning,
						static_cast<int>(index),
						SString::printf("%s is already loaded, reserved, or "
								"used by this batch; the import will use %s.",
								item.requestedName.c_str(),
								item.resolvedName.c_str()));
			}
		}
		else if (request.conflictPolicy ==
				TextureConflictPolicy::overrideLoaded)
		{
			const bool projectConflict = std::any_of(
					item.conflicts.begin(), item.conflicts.end(),
					[](const TextureResourceOwner &owner)
					{
						return owner.project;
					});
			if (projectConflict)
			{
				AddIssue(plan, TextureImportSeverity::error,
						static_cast<int>(index),
						"Use Replace project for a name already owned by this "
						"package.");
			}
			else if (item.conflicts.empty())
			{
				AddIssue(plan, TextureImportSeverity::warning,
						static_cast<int>(index),
						"No loaded resource requires an override; this will be "
						"a normal import.");
			}
			else
			{
				AddIssue(plan, TextureImportSeverity::warning,
						static_cast<int>(index),
						SString::printf("The project resource will override %zu "
								"loaded definition%s named %s.",
								item.conflicts.size(),
								item.conflicts.size() == 1 ? "" : "s",
								item.requestedName.c_str()));
			}
		}

		const std::vector<PackageResourceKind> kinds =
				DestinationKinds(item.usage);
		for (PackageResourceKind kind : kinds)
		{
			TextureImportDestination destination;
			destination.kind = kind;
			destination.packagePath = DestinationPath(plan.package, kind,
					item.resolvedName, item.extension);
			if (request.conflictPolicy ==
					TextureConflictPolicy::replaceProject)
			{
				std::vector<const TextureResourceOwner *> matches;
				for (const TextureResourceOwner &owner : item.conflicts)
					if (owner.project && owner.kind == kind)
						matches.push_back(&owner);
				if (matches.size() != 1 || matches.front()->occurrences != 1 ||
						!matches.front()->replaceableProjectEntry ||
						matches.front()->entryPath.empty())
				{
					AddIssue(plan, TextureImportSeverity::error,
							static_cast<int>(index),
							SString::printf("The project %s named %s is missing "
									"or ambiguous and cannot be replaced safely.",
									kind == PackageResourceKind::flat ?
											"flat" : "wall texture",
									item.requestedName.c_str()));
				}
				else
					destination.replaceEntryPath =
							matches.front()->entryPath;
			}
			item.destinations.push_back(std::move(destination));
		}
		if (IsReservedName(item.resolvedName))
		{
			AddIssue(plan, TextureImportSeverity::error,
					static_cast<int>(index),
					"The resolved name is reserved for package structure.");
		}
		if (plannedNames.count(item.resolvedName.asUpper()))
		{
			AddIssue(plan, TextureImportSeverity::error,
					static_cast<int>(index),
					"Two imported files resolve to the same resource name.");
		}
		else
			plannedNames.insert(item.resolvedName.asUpper());
		plan.items.push_back(std::move(item));
	}

	for (TextureImportIssue &issue : plan.issues)
		if (issue.item >= 0 &&
				static_cast<size_t>(issue.item) < requests.size() &&
				requests[static_cast<size_t>(issue.item)].conflictPolicy ==
						TextureConflictPolicy::skip)
		{
			issue.severity = TextureImportSeverity::warning;
		}
	if (plan.totalBytes > TEXTURE_IMPORT_MAX_BATCH_BYTES)
		AddIssue(plan, TextureImportSeverity::error, -1,
				"The reviewed batch exceeds the 1 GiB safety limit.");
	if (requests.empty())
		AddIssue(plan, TextureImportSeverity::error, -1,
				"Choose at least one image to import.");
	else if (plan.importCount() == 0)
		AddIssue(plan, TextureImportSeverity::error, -1,
				"Every selected image is skipped or invalid.");
	return plan;
}

void M_ApplyTextureImport(const WadData &wad, const ConfigData &config,
		const std::vector<TextureImportRequestItem> &requests,
		const TextureImportPlan &reviewed,
		const Document *activeDocument,
		const std::function<void()> &beforeWrite)
{
	const TextureImportPlan current = M_PlanTextureImport(wad, config,
			reviewed.packagePath, requests, activeDocument);
	if (!current.valid())
		throw std::runtime_error("The texture import is no longer valid.");
	if (!SameReviewedPlan(reviewed, current))
		throw std::runtime_error("A source or the project package changed while "
				"the import was being reviewed. Review the updated plan.");

	std::vector<PackageResourceWrite> writes;
	for (const TextureImportPlanItem &item : current.items)
	{
		if (item.skipped)
			continue;
		for (const TextureImportDestination &destination : item.destinations)
		{
			writes.push_back({ destination.kind, item.resolvedName,
					item.extension, item.data,
					destination.replaceEntryPath });
		}
	}
	if (beforeWrite)
		beforeWrite();
	M_WritePackageResources(current.packagePath, writes);
}
