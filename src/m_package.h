//------------------------------------------------------------------------
//  EDITABLE PROJECT PACKAGE BACKENDS
//------------------------------------------------------------------------

#ifndef HERESY_M_PACKAGE_H
#define HERESY_M_PACKAGE_H

#include "m_project.h"

#include <filesystem>
#include <memory>

namespace fs = std::filesystem;

class Wad_file;

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
