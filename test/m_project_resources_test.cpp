//------------------------------------------------------------------------
//  PROJECT EXTERNAL RESOURCE TESTS
//------------------------------------------------------------------------

#include "m_project_resources.h"

#include "testUtils/TempDirContext.hpp"

#include "gtest/gtest.h"

#include <fstream>

class ProjectResourcesTest : public TempDirContext
{
protected:
	fs::path MakeDirectory(const fs::path &relative)
	{
		const fs::path path = getSubPath(relative);
		EXPECT_TRUE(fs::create_directories(path));
		mDeleteList.push(path);
		return path;
	}

	fs::path MakeFile(const fs::path &relative)
	{
		const fs::path path = getSubPath(relative);
		std::ofstream stream(path);
		EXPECT_TRUE(stream.is_open());
		stream << "resource";
		stream.close();
		mDeleteList.push(path);
		return path;
	}
};

TEST_F(ProjectResourcesTest, DynamicListRetainsMoreThanFourResourcesInOrder)
{
	std::vector<fs::path> resources;
	std::vector<fs::path> expected;
	for (int index = 0; index < 12; ++index)
	{
		const fs::path path = MakeFile(
				SString::printf("resource-%02d.wad", index).c_str());
		expected.push_back(path);
		ProjectResourceValidation validation;
		ASSERT_TRUE(M_AddProjectResource(resources, path, &validation))
				<< validation.message.c_str();
	}

	EXPECT_EQ(resources, expected);
	EXPECT_TRUE(M_ValidateProjectResources(resources).valid());
}

TEST_F(ProjectResourcesTest, SameBasenameInDifferentFoldersIsAllowed)
{
	MakeDirectory("first");
	const fs::path first = MakeFile("first/common.wad");
	MakeDirectory("second");
	const fs::path second = MakeFile("second/common.wad");
	std::vector<fs::path> resources;

	EXPECT_TRUE(M_AddProjectResource(resources, first));
	EXPECT_TRUE(M_AddProjectResource(resources, second));
	EXPECT_EQ(resources, (std::vector<fs::path>{ first, second }));
}

TEST_F(ProjectResourcesTest, DuplicateNormalizedPathIsRejected)
{
	MakeDirectory("packs");
	const fs::path resource = MakeFile("packs/common.pk3");
	std::vector<fs::path> resources = { resource };
	ProjectResourceValidation validation;

	EXPECT_FALSE(M_AddProjectResource(resources,
			resource.parent_path() / "." / resource.filename(), &validation));
	EXPECT_EQ(validation.issue, ProjectResourceIssue::duplicate);
	EXPECT_EQ(validation.index, 1u);
	ASSERT_TRUE(validation.duplicateIndex.has_value());
	EXPECT_EQ(*validation.duplicateIndex, 0u);
	EXPECT_EQ(resources, (std::vector<fs::path>{ resource }));
}

#ifndef _WIN32
TEST_F(ProjectResourcesTest, SymlinkToExistingResourceIsRejectedAsDuplicate)
{
	const fs::path resource = MakeFile("original.wad");
	const fs::path alias = getSubPath("alias.wad");
	std::error_code error;
	fs::create_symlink(resource, alias, error);
	ASSERT_FALSE(error) << error.message();
	mDeleteList.push(alias);
	std::vector<fs::path> resources = { resource };
	ProjectResourceValidation validation;

	EXPECT_FALSE(M_AddProjectResource(resources, alias, &validation));
	EXPECT_EQ(validation.issue, ProjectResourceIssue::duplicate);
	ASSERT_TRUE(validation.duplicateIndex.has_value());
	EXPECT_EQ(*validation.duplicateIndex, 0u);
}
#endif

TEST(ProjectResourceModel, ReordersAndRemovesWithoutLosingOrder)
{
	const fs::path first = "first.wad";
	const fs::path second = "second.pk3";
	const fs::path third = "third";
	std::vector<fs::path> resources = { first, second, third };

	EXPECT_TRUE(M_MoveProjectResource(resources, 2, -1));
	EXPECT_EQ(resources, (std::vector<fs::path>{ first, third, second }));
	EXPECT_TRUE(M_MoveProjectResource(resources, 0, 1));
	EXPECT_EQ(resources, (std::vector<fs::path>{ third, first, second }));
	EXPECT_FALSE(M_MoveProjectResource(resources, 0, -1));
	EXPECT_FALSE(M_MoveProjectResource(resources, 2, 1));
	EXPECT_TRUE(M_RemoveProjectResource(resources, 1));
	EXPECT_EQ(resources, (std::vector<fs::path>{ third, second }));
	EXPECT_FALSE(M_RemoveProjectResource(resources, 4));
}

TEST_F(ProjectResourcesTest, ValidationAcceptsFoldersAndReportsMissingPaths)
{
	const fs::path folder = MakeDirectory("textures");
	EXPECT_TRUE(M_ValidateProjectResources({ folder }).valid());

	const ProjectResourceValidation validation = M_ValidateProjectResources(
			{ folder, getSubPath("missing.wad") });
	EXPECT_EQ(validation.issue, ProjectResourceIssue::missing);
	EXPECT_EQ(validation.index, 1u);
}

TEST(ProjectResourceModel, ValidationReportsEmptyAndDuplicateEntries)
{
	ProjectResourceValidation validation = M_ValidateProjectResources({ { } });
	EXPECT_EQ(validation.issue, ProjectResourceIssue::emptyPath);
	EXPECT_EQ(validation.index, 0u);

	validation = M_ValidateProjectResources({ ".", "./" });
	EXPECT_EQ(validation.issue, ProjectResourceIssue::duplicate);
	EXPECT_EQ(validation.index, 1u);
	ASSERT_TRUE(validation.duplicateIndex.has_value());
	EXPECT_EQ(*validation.duplicateIndex, 0u);
}
