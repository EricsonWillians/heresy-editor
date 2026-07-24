//------------------------------------------------------------------------
//  PROJECT EXTERNAL RESOURCES
//------------------------------------------------------------------------

#ifndef HERESY_M_PROJECT_RESOURCES_H
#define HERESY_M_PROJECT_RESOURCES_H

#include "m_strings.h"

#include <filesystem>
#include <optional>
#include <vector>

namespace fs = std::filesystem;

enum class ProjectResourceIssue
{
	none,
	emptyPath,
	missing,
	unsupportedType,
	duplicate
};

struct ProjectResourceValidation
{
	ProjectResourceIssue issue = ProjectResourceIssue::none;
	size_t index = 0;
	std::optional<size_t> duplicateIndex;
	SString message;

	bool valid() const noexcept
	{
		return issue == ProjectResourceIssue::none;
	}
};

// Existing filesystem objects are compared by identity.  The normalized-path
// fallback also handles alternate spellings of the same path deterministically.
bool M_ProjectResourcePathsEqual(const fs::path &first,
		const fs::path &second);

ProjectResourceValidation M_ValidateProjectResources(
		const std::vector<fs::path> &resources);

bool M_AddProjectResource(std::vector<fs::path> &resources,
		const fs::path &resource,
		ProjectResourceValidation *validation = nullptr);

bool M_RemoveProjectResource(std::vector<fs::path> &resources, size_t index);
bool M_MoveProjectResource(std::vector<fs::path> &resources, size_t index,
		int direction);

#endif  /* HERESY_M_PROJECT_RESOURCES_H */
