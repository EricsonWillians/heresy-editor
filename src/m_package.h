//------------------------------------------------------------------------
//  EDITABLE PROJECT PACKAGE BACKENDS
//------------------------------------------------------------------------

#ifndef HERESY_M_PACKAGE_H
#define HERESY_M_PACKAGE_H

#include "m_project.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace fs = std::filesystem;

class Wad_file;

enum class Pk3MetadataKind
{
	campaignDefinition,
	resourceDefinition,
	runtimeSource
};

enum class Pk3PreviewState
{
	text,
	empty,
	binary,
	tooLarge,
	unavailable
};

enum class ResourceNamespaceKind
{
	flat,
	sprite,
	texture
};

struct Pk3MetadataEntry
{
	SString path;
	Pk3MetadataKind kind = Pk3MetadataKind::resourceDefinition;
	uint64_t size = 0;
	Pk3PreviewState previewState = Pk3PreviewState::unavailable;
	SString preview;
	SString detail;
};

struct Pk3ResourceGroup
{
	SString pathPrefix;
	SString label;
	size_t entries = 0;
	uint64_t size = 0;
	bool projectedByEditor = false;
};

struct Pk3ProjectedResource
{
	SString path;
	SString editorName;
	ResourceNamespaceKind nameSpace = ResourceNamespaceKind::texture;
	uint64_t size = 0;
};

struct Pk3PackageInventory
{
	fs::path path;
	size_t archiveEntries = 0;
	size_t directoryEntries = 0;
	size_t totalFiles = 0;
	size_t mapFiles = 0;
	size_t editorFiles = 0;
	size_t resourceFiles = 0;
	size_t otherFiles = 0;
	uint64_t totalSize = 0;
	std::vector<Pk3MetadataEntry> metadata;
	std::vector<Pk3ResourceGroup> resources;
	std::vector<Pk3ProjectedResource> projectedResources;
};

inline constexpr uint64_t PK3_METADATA_PREVIEW_LIMIT = 512 * 1024;
inline constexpr uint64_t PK3_METADATA_TOTAL_PREVIEW_LIMIT = 4 * 1024 * 1024;

const char *M_Pk3MetadataKindName(Pk3MetadataKind kind) noexcept;
std::optional<Pk3PackageInventory> M_InspectPk3Package(
		const fs::path &path, SString *error = nullptr) noexcept;

// Package-level persistence is intentionally independent of Document and the
// GUI.  Both backends expose an editable WAD view, allowing the established map
// serializer and navigation code to remain the single source of truth.
class PackageBackend
{
public:
	virtual ~PackageBackend() = default;

	virtual ProjectPackage packageType() const noexcept = 0;
	virtual const fs::path &PathName() const noexcept = 0;
	virtual std::shared_ptr<Wad_file> openEditable() = 0;
};

std::shared_ptr<PackageBackend> M_OpenPackageBackend(const fs::path &path);
std::shared_ptr<PackageBackend> M_CreatePackageBackend(const fs::path &path,
		ProjectPackage package);

std::shared_ptr<Wad_file> M_OpenEditablePackage(const fs::path &path);
bool M_ValidateEditablePackage(const fs::path &path) noexcept;
ProjectPackage M_ProjectPackageForPath(const fs::path &path) noexcept;

#endif // HERESY_M_PACKAGE_H
