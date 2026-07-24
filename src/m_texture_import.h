//------------------------------------------------------------------------
//  SURFACE TEXTURE IMPORT PLANNING
//------------------------------------------------------------------------

#ifndef HERESY_M_TEXTURE_IMPORT_H
#define HERESY_M_TEXTURE_IMPORT_H

#include "m_package.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace fs = std::filesystem;

class Document;
class Img_c;
struct ConfigData;
struct WadData;

enum class TextureSurfaceUsage
{
	walls,
	planes,
	allSurfaces
};

enum class TextureConflictPolicy
{
	renameImported,
	overrideLoaded,
	replaceProject,
	skip
};

enum class TextureImportFormat
{
	unknown,
	png,
	jpeg,
	tga,
	doomPatch,
	rawFlat
};

enum class TextureImportSeverity
{
	warning,
	error
};

enum class TextureResourceProvenance
{
	iwad,
	externalResource,
	project
};

struct TextureResourceOwner
{
	SString label;
	fs::path packagePath;
	SString entryPath;
	PackageResourceKind kind = PackageResourceKind::wallTexture;
	TextureResourceProvenance provenance =
			TextureResourceProvenance::externalResource;
	size_t loadOrder = 0;
	size_t occurrences = 1;
	bool project = false;
	bool replaceableProjectEntry = false;
};

struct TextureImportRequestItem
{
	fs::path source;
	SString requestedName;
	TextureSurfaceUsage usage = TextureSurfaceUsage::allSurfaces;
	TextureConflictPolicy conflictPolicy =
			TextureConflictPolicy::renameImported;
	bool automaticUsage = true;
};

struct TextureImportIssue
{
	TextureImportSeverity severity = TextureImportSeverity::warning;
	int item = -1;
	SString explanation;
};

struct TextureImportDestination
{
	PackageResourceKind kind = PackageResourceKind::wallTexture;
	SString packagePath;
	std::optional<SString> replaceEntryPath;
};

struct TextureImportPlanItem
{
	fs::path source;
	SString requestedName;
	SString resolvedName;
	TextureSurfaceUsage usage = TextureSurfaceUsage::allSurfaces;
	TextureConflictPolicy conflictPolicy =
			TextureConflictPolicy::renameImported;
	TextureImportFormat format = TextureImportFormat::unknown;
	SString extension;
	int width = 0;
	int height = 0;
	bool hasAlpha = false;
	bool skipped = false;
	uint64_t byteSize = 0;
	uint64_t contentHash = 0;
	size_t activeMapUsages = 0;
	std::vector<TextureResourceOwner> conflicts;
	std::vector<TextureImportDestination> destinations;
	std::vector<uint8_t> data;
	std::shared_ptr<Img_c> preview;
};

struct TextureImportPlan
{
	fs::path packagePath;
	ProjectPackage package = ProjectPackage::none;
	uint64_t packageSize = 0;
	int64_t packageWriteTime = 0;
	uint64_t totalBytes = 0;
	std::vector<TextureImportPlanItem> items;
	std::vector<TextureImportIssue> issues;

	bool valid() const noexcept;
	size_t importCount() const noexcept;
	size_t replacementCount() const noexcept;
	bool destructive() const noexcept;
};

inline constexpr uint64_t TEXTURE_IMPORT_MAX_FILE_BYTES =
		256ull * 1024ull * 1024ull;
inline constexpr uint64_t TEXTURE_IMPORT_MAX_BATCH_BYTES =
		1024ull * 1024ull * 1024ull;
inline constexpr int TEXTURE_IMPORT_MAX_DIMENSION = 8192;

const char *M_TextureSurfaceUsageName(TextureSurfaceUsage usage) noexcept;
const char *M_TextureConflictPolicyName(TextureConflictPolicy policy) noexcept;
const char *M_TextureImportFormatName(TextureImportFormat format) noexcept;

SString M_NormalizeImportedTextureName(const fs::path &source,
		const SString &requested = {});

TextureImportPlan M_PlanTextureImport(const WadData &wad,
		const ConfigData &config, const fs::path &packagePath,
		const std::vector<TextureImportRequestItem> &requests,
		const Document *activeDocument = nullptr);

void M_ApplyTextureImport(const WadData &wad, const ConfigData &config,
		const std::vector<TextureImportRequestItem> &requests,
		const TextureImportPlan &reviewed,
		const Document *activeDocument = nullptr,
		const std::function<void()> &beforeWrite = {});

#endif // HERESY_M_TEXTURE_IMPORT_H
