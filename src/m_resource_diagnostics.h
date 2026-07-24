//------------------------------------------------------------------------
//  RESOURCE COLLISION AND LOAD-ORDER DIAGNOSTICS
//------------------------------------------------------------------------

#ifndef HERESY_M_RESOURCE_DIAGNOSTICS_H
#define HERESY_M_RESOURCE_DIAGNOSTICS_H

#include "m_package.h"

#include <filesystem>
#include <optional>
#include <vector>

namespace fs = std::filesystem;

class MasterDir;

enum class ResourceConflictKind
{
	projectedBasename,
	duplicateWithinSource,
	loadOrderOverride
};

struct ResourceConflictParticipant
{
	SString label;
	fs::path packagePath;
	SString archivePath;
	size_t loadOrder = 0;
	size_t occurrences = 1;
};

struct ResourceConflict
{
	ResourceConflictKind kind = ResourceConflictKind::projectedBasename;
	ResourceNamespaceKind nameSpace = ResourceNamespaceKind::texture;
	SString editorName;
	std::vector<ResourceConflictParticipant> participants;
	std::optional<size_t> nominalWinner;
	SString resolution;
};

struct ResourceDiagnostics
{
	std::vector<ResourceConflict> conflicts;

	size_t warningCount() const noexcept;
	size_t overrideCount() const noexcept;
};

const char *M_ResourceNamespaceName(ResourceNamespaceKind nameSpace) noexcept;
const char *M_ResourceConflictKindName(ResourceConflictKind kind) noexcept;

ResourceDiagnostics M_AnalyzeResourceConflicts(
		const Pk3PackageInventory &inventory, const MasterDir &master);

#endif // HERESY_M_RESOURCE_DIAGNOSTICS_H
