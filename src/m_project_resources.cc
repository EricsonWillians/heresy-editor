//------------------------------------------------------------------------
//  PROJECT EXTERNAL RESOURCES
//------------------------------------------------------------------------

#include "m_project_resources.h"

#include <algorithm>

namespace
{

std::string PathText(const fs::path &path)
{
	const std::u8string text = path.u8string();
	return std::string(reinterpret_cast<const char *>(text.data()), text.size());
}

fs::path NormalizedAbsolutePath(const fs::path &path)
{
	std::error_code error;
	fs::path absolute = fs::absolute(path, error);
	return (error ? path : absolute).lexically_normal();
}

ProjectResourceValidation ValidateResourceAt(
		const std::vector<fs::path> &resources, size_t index)
{
	ProjectResourceValidation validation;
	validation.index = index;

	const fs::path &resource = resources[index];
	if (resource.empty())
	{
		validation.issue = ProjectResourceIssue::emptyPath;
		validation.message = SString::printf("Resource %zu has no path.",
				index + 1);
		return validation;
	}

	std::error_code error;
	const fs::file_status status = fs::status(resource, error);
	if (error || !fs::exists(status))
	{
		validation.issue = ProjectResourceIssue::missing;
		validation.message = SString::printf(
				"Resource %zu is no longer available:\n%s", index + 1,
				PathText(resource).c_str());
		return validation;
	}

	if (!fs::is_regular_file(status) && !fs::is_directory(status))
	{
		validation.issue = ProjectResourceIssue::unsupportedType;
		validation.message = SString::printf(
				"Resource %zu is not a regular file or folder:\n%s", index + 1,
				PathText(resource).c_str());
		return validation;
	}

	for (size_t previous = 0; previous < index; ++previous)
	{
		if (!M_ProjectResourcePathsEqual(resources[previous], resource))
			continue;

		validation.issue = ProjectResourceIssue::duplicate;
		validation.duplicateIndex = previous;
		validation.message = SString::printf(
				"Resource %zu duplicates resource %zu:\n%s", index + 1,
				previous + 1, PathText(resource).c_str());
		return validation;
	}

	return validation;
}

} // namespace


bool M_ProjectResourcePathsEqual(const fs::path &first,
		const fs::path &second)
{
	if (first.empty() || second.empty())
		return first.empty() && second.empty();

	std::error_code error;
	if (fs::equivalent(first, second, error) && !error)
		return true;

	const fs::path normalizedFirst = NormalizedAbsolutePath(first);
	const fs::path normalizedSecond = NormalizedAbsolutePath(second);
#ifdef _WIN32
	const SString firstText = normalizedFirst.u8string();
	const SString secondText = normalizedSecond.u8string();
	return firstText.noCaseEqual(secondText);
#else
	return normalizedFirst == normalizedSecond;
#endif
}


ProjectResourceValidation M_ValidateProjectResources(
		const std::vector<fs::path> &resources)
{
	for (size_t index = 0; index < resources.size(); ++index)
	{
		ProjectResourceValidation validation = ValidateResourceAt(resources,
				index);
		if (!validation.valid())
			return validation;
	}

	return {};
}


bool M_AddProjectResource(std::vector<fs::path> &resources,
		const fs::path &resource, ProjectResourceValidation *validation)
{
	std::vector<fs::path> candidate = resources;
	candidate.push_back(resource);
	ProjectResourceValidation result = ValidateResourceAt(candidate,
			candidate.size() - 1);
	if (validation)
		*validation = result;
	if (!result.valid())
		return false;

	resources.push_back(resource);
	return true;
}


bool M_RemoveProjectResource(std::vector<fs::path> &resources, size_t index)
{
	if (index >= resources.size())
		return false;

	resources.erase(resources.begin() + index);
	return true;
}


bool M_MoveProjectResource(std::vector<fs::path> &resources, size_t index,
		int direction)
{
	if ((direction != -1 && direction != 1) || index >= resources.size())
		return false;

	if ((direction < 0 && index == 0) ||
			(direction > 0 && index + 1 >= resources.size()))
		return false;

	const size_t destination = direction < 0 ? index - 1 : index + 1;
	std::swap(resources[index], resources[destination]);
	return true;
}
