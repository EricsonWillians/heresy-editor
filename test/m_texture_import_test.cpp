//------------------------------------------------------------------------
//  SURFACE TEXTURE IMPORT TESTS
//------------------------------------------------------------------------

#include "m_texture_import.h"

#include "lib_file.h"
#include "m_game.h"
#include "m_zip.h"
#include "testUtils/Palette.hpp"
#include "testUtils/TempDirContext.hpp"
#include "WadData.h"
#include "w_wad.h"

#include "gtest/gtest.h"

#include <algorithm>
#include <fstream>

namespace
{

const std::vector<uint8_t> PNG_DATA = {
	137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 73, 72, 68, 82,
	0, 0, 0, 4, 0, 0, 0, 3, 8, 6, 0, 0, 0, 180, 244, 174, 198, 0,
	0, 0, 4, 115, 66, 73, 84, 8, 8, 8, 8, 124, 8, 100, 136, 0, 0,
	0, 59, 73, 68, 65, 84, 8, 153, 5, 193, 49, 17, 192, 32, 16, 0,
	193, 43, 50, 19, 1, 120, 72, 9, 18, 145, 240, 22, 144, 66, 71,
	234, 248, 160, 134, 161, 185, 236, 34, 104, 132, 142, 165, 117,
	122, 189, 189, 83, 114, 102, 159, 155, 47, 1, 85, 29, 106, 52,
	229, 209, 31, 10, 229, 30, 135, 131, 75, 13, 237, 0, 0, 0, 0,
	73, 69, 78, 68, 174, 66, 96, 130
};

const std::vector<uint8_t> TGA_DATA = {
	17, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 3, 0, 32, 8,
	16, 67, 114, 101, 97, 116, 101, 100, 32, 98, 121, 32, 80, 105,
	110, 116, 97, 255, 255, 127, 255, 255, 255, 199, 255, 255, 160,
	143, 255, 255, 38, 0, 255, 192, 192, 199, 255, 238, 238, 246,
	255, 254, 232, 238, 255, 247, 146, 192, 255, 0, 0, 255, 255,
	143, 143, 255, 255, 247, 199, 255, 255, 237, 127, 255, 255
};

std::vector<uint8_t> OnePixelDoomPatch()
{
	return {
		1, 0, 1, 0, 0, 0, 0, 0,
		12, 0, 0, 0,
		0, 1, 0, 7, 0, 255, 0, 0
	};
}

void WriteBytes(const fs::path &path, const std::vector<uint8_t> &data)
{
	std::ofstream output(path, std::ios::binary);
	ASSERT_TRUE(output);
	output.write(reinterpret_cast<const char *>(data.data()),
			static_cast<std::streamsize>(data.size()));
	ASSERT_TRUE(output);
}

std::shared_ptr<Wad_file> MakeGameWad()
{
	std::shared_ptr<Wad_file> game =
			Wad_file::Open("texture-import-iwad.wad", WadOpenMode::write);
	const std::vector<uint8_t> palette = makeGrayscale();
	game->AddLump("PLAYPAL").Write(palette.data(),
			static_cast<int>(palette.size()));
	const std::vector<uint8_t> colormap(32 * 256, 0);
	game->AddLump("COLORMAP").Write(colormap.data(),
			static_cast<int>(colormap.size()));
	return game;
}

void AddResource(Wad_file &wad, PackageResourceKind kind,
		const char *name, uint8_t value)
{
	wad.AddLump(kind == PackageResourceKind::flat ?
			"F_START" : "TX_START");
	Lump_c &lump = wad.AddLump(name);
	const std::vector<uint8_t> data(32, value);
	lump.Write(data.data(), static_cast<int>(data.size()));
	wad.AddLump(kind == PackageResourceKind::flat ?
			"F_END" : "TX_END");
}

void AddCompositeTextureNames(Wad_file &wad)
{
	std::vector<uint8_t> definitions = {
		2, 0, 0, 0,
		12, 0, 0, 0,
		20, 0, 0, 0,
		'A', 'A', 'S', 'H', 'I', 'T', 'T', 'Y',
		'S', 'T', 'O', 'N', 'E', '_', 'W', 'A'
	};
	wad.AddLump("TEXTURE1").Write(definitions.data(),
			static_cast<int>(definitions.size()));
}

} // namespace

class TextureImportTest :
		public TempDirContext,
		public ::testing::WithParamInterface<ProjectPackage>
{
protected:
	void SetUp() override
	{
		TempDirContext::SetUp();
		const char *extension =
				GetParam() == ProjectPackage::pk3 ? ".pk3" : ".wad";
		packagePath = getSubPath(std::string("project") + extension);
		std::shared_ptr<PackageBackend> backend =
				M_CreatePackageBackend(packagePath, GetParam());
		ASSERT_TRUE(backend);
		std::shared_ptr<Wad_file> package = backend->openEditable();
		ASSERT_TRUE(package);
		package->writeToDisk();
		const std::vector<uint8_t> keep = { 9, 8, 7, 6 };
		if (GetParam() == ProjectPackage::wad)
		{
			std::shared_ptr<Wad_file> persisted =
					Wad_file::Open(packagePath, WadOpenMode::append);
			ASSERT_TRUE(persisted);
			persisted->AddLump("KEEP").Write(keep.data(),
					static_cast<int>(keep.size()));
			persisted->writeToDisk();
		}
		else
		{
			std::shared_ptr<ZipArchive> archive = ZipArchive::Open(packagePath);
			ASSERT_TRUE(archive);
			archive->setEntry("docs/keep.bin", keep);
			archive->writeToDisk();
		}
		mDeleteList.push(packagePath);

		sourcePath = getSubPath("Stone Wall.png");
		WriteBytes(sourcePath, PNG_DATA);
		mDeleteList.push(sourcePath);

		config.features.tx_start = 1;
		config.features.mix_textures_flats = 1;
		wad.master.setGameWad(MakeGameWad());
		wad.master.ReplaceEditWad(M_OpenEditablePackage(packagePath));
	}

	fs::path packagePath;
	fs::path sourcePath;
	ConfigData config;
	WadData wad;
};

TEST(TextureImportNames, NormalizesPortableIdentifiers)
{
	EXPECT_EQ(M_NormalizeImportedTextureName("brick wall.png"), "BRICK_WA");
	EXPECT_EQ(M_NormalizeImportedTextureName("ignored.png", "a!b__c"), "A_B_C");
	EXPECT_EQ(M_NormalizeImportedTextureName("____.png"), "TEXTURE");
}

TEST_P(TextureImportTest, PlansAndAppliesAllSurfaceImage)
{
	std::vector<TextureImportRequestItem> requests = { { sourcePath } };
	TextureImportPlan plan = M_PlanTextureImport(wad, config, packagePath,
			requests);
	ASSERT_TRUE(plan.valid());
	ASSERT_EQ(plan.items.size(), 1u);
	EXPECT_EQ(plan.items[0].format, TextureImportFormat::png);
	EXPECT_EQ(plan.items[0].usage, TextureSurfaceUsage::allSurfaces);
	EXPECT_EQ(plan.items[0].width, 4);
	EXPECT_EQ(plan.items[0].height, 3);
	EXPECT_EQ(plan.items[0].resolvedName, "STONE_WA");
	EXPECT_EQ(plan.items[0].destinations.size(), 2u);

	ASSERT_NO_THROW(M_ApplyTextureImport(wad, config, requests, plan));
	std::shared_ptr<Wad_file> reopened = M_OpenEditablePackage(packagePath);
	ASSERT_TRUE(reopened);
	const Lump_c *wall = reopened->FindLumpInNamespace("STONE_WA",
			WadNamespace::TextureLumps);
	const Lump_c *flat = reopened->FindLumpInNamespace("STONE_WA",
			WadNamespace::Flats);
	ASSERT_TRUE(wall);
	ASSERT_TRUE(flat);
	EXPECT_EQ(wall->getData(), PNG_DATA);
	EXPECT_EQ(flat->getData(), PNG_DATA);

	WadData refreshed;
	refreshed.master.ReplaceEditWad(reopened);
	ASSERT_NO_THROW(refreshed.reloadResources(wad.master.gameWad(), config, {}));
	const Img_c *wallImage =
			refreshed.images.getTexture(config, "STONE_WA");
	const Img_c *flatImage =
			refreshed.images.W_GetFlat(config, "STONE_WA");
	ASSERT_TRUE(wallImage);
	ASSERT_TRUE(flatImage);
	EXPECT_EQ(wallImage->width(), 4);
	EXPECT_EQ(wallImage->height(), 3);
	EXPECT_EQ(flatImage->width(), 4);
	EXPECT_EQ(flatImage->height(), 3);
	if (GetParam() == ProjectPackage::wad)
	{
		const Lump_c *keep = reopened->FindLump("KEEP");
		ASSERT_TRUE(keep);
		EXPECT_EQ(keep->getData(), (std::vector<byte>{ 9, 8, 7, 6 }));
	}
	else
	{
		std::shared_ptr<ZipArchive> archive = ZipArchive::Open(packagePath);
		ASSERT_TRUE(archive);
		EXPECT_EQ(archive->readEntry("docs/keep.bin"),
				(std::vector<uint8_t>{ 9, 8, 7, 6 }));
	}
}

TEST_P(TextureImportTest, SafeRenameAndExplicitReplacement)
{
	std::vector<TextureImportRequestItem> requests = { { sourcePath } };
	TextureImportPlan initial = M_PlanTextureImport(wad, config, packagePath,
			requests);
	ASSERT_TRUE(initial.valid());
	M_ApplyTextureImport(wad, config, requests, initial);
	wad.master.ReplaceEditWad(M_OpenEditablePackage(packagePath));

	TextureImportPlan renamed = M_PlanTextureImport(wad, config, packagePath,
			requests);
	ASSERT_TRUE(renamed.valid());
	EXPECT_EQ(renamed.items[0].resolvedName, "STONE_2");
	ASSERT_FALSE(renamed.items[0].conflicts.empty());

	requests[0].requestedName = "STONE_WA";
	requests[0].automaticUsage = false;
	requests[0].usage = TextureSurfaceUsage::allSurfaces;
	requests[0].conflictPolicy = TextureConflictPolicy::overrideLoaded;
	TextureImportPlan unsafeOverride =
			M_PlanTextureImport(wad, config, packagePath, requests);
	EXPECT_FALSE(unsafeOverride.valid());

	requests[0].conflictPolicy = TextureConflictPolicy::replaceProject;
	TextureImportPlan replacement =
			M_PlanTextureImport(wad, config, packagePath, requests);
	ASSERT_TRUE(replacement.valid());
	EXPECT_EQ(replacement.replacementCount(), 2u);
	EXPECT_TRUE(replacement.destructive());
	ASSERT_NO_THROW(M_ApplyTextureImport(wad, config, requests, replacement));
}

TEST_P(TextureImportTest, RejectsAChangedSourceAfterReview)
{
	std::vector<TextureImportRequestItem> requests = { { sourcePath } };
	const TextureImportPlan reviewed =
			M_PlanTextureImport(wad, config, packagePath, requests);
	ASSERT_TRUE(reviewed.valid());
	std::vector<uint8_t> changed = PNG_DATA;
	changed.back() ^= 1;
	WriteBytes(sourcePath, changed);
	std::vector<uint8_t> packageBefore;
	ASSERT_TRUE(FileLoad(packagePath, packageBefore));
	bool callbackCalled = false;
	EXPECT_THROW(M_ApplyTextureImport(wad, config, requests, reviewed, nullptr,
			[&callbackCalled]()
			{
				callbackCalled = true;
			}), std::runtime_error);
	EXPECT_FALSE(callbackCalled);
	std::vector<uint8_t> packageAfter;
	ASSERT_TRUE(FileLoad(packagePath, packageAfter));
	EXPECT_EQ(packageAfter, packageBefore);
}

TEST_P(TextureImportTest, SkipExcludesAnInvalidItem)
{
	const fs::path corruptPath = getSubPath("broken.tga");
	WriteBytes(corruptPath, { 1, 2, 3, 4, 5 });
	mDeleteList.push(corruptPath);
	std::vector<TextureImportRequestItem> requests = {
		{ corruptPath, {}, TextureSurfaceUsage::allSurfaces,
				TextureConflictPolicy::skip },
		{ sourcePath }
	};
	const TextureImportPlan plan =
			M_PlanTextureImport(wad, config, packagePath, requests);
	ASSERT_TRUE(plan.valid());
	ASSERT_EQ(plan.items.size(), 2u);
	EXPECT_TRUE(plan.items[0].skipped);
	EXPECT_EQ(plan.importCount(), 1u);
	EXPECT_NO_THROW(M_ApplyTextureImport(wad, config, requests, plan));
}

TEST_P(TextureImportTest, DetectsContentIndependentOfExtension)
{
	const fs::path disguised = getSubPath("picture.lmp");
	WriteBytes(disguised, PNG_DATA);
	mDeleteList.push(disguised);
	const TextureImportPlan plan = M_PlanTextureImport(wad, config,
			packagePath, { { disguised } });
	ASSERT_TRUE(plan.valid());
	EXPECT_EQ(plan.items[0].format, TextureImportFormat::png);
	EXPECT_EQ(plan.items[0].usage, TextureSurfaceUsage::allSurfaces);
}

TEST_P(TextureImportTest, RejectsDecodedDimensionsAboveTheLimit)
{
	std::vector<uint8_t> oversized = PNG_DATA;
	oversized[16] = 0;
	oversized[17] = 0;
	oversized[18] = 32;
	oversized[19] = 1;
	const fs::path oversizedPath = getSubPath("oversized.png");
	WriteBytes(oversizedPath, oversized);
	mDeleteList.push(oversizedPath);
	const TextureImportPlan plan = M_PlanTextureImport(wad, config,
			packagePath, { { oversizedPath } });
	EXPECT_FALSE(plan.valid());
	EXPECT_TRUE(std::any_of(plan.issues.begin(), plan.issues.end(),
			[](const TextureImportIssue &issue)
			{
				return issue.explanation.findNoCase("8192") != SString::npos;
			}));
}

TEST_P(TextureImportTest, DetectsTgaAndDoomPatchDefaults)
{
	const fs::path tga = getSubPath("court.dat");
	const fs::path patch = getSubPath("wall.dat");
	WriteBytes(tga, TGA_DATA);
	WriteBytes(patch, OnePixelDoomPatch());
	mDeleteList.push(tga);
	mDeleteList.push(patch);
	const TextureImportPlan plan = M_PlanTextureImport(wad, config,
			packagePath, { { tga }, { patch } });
	ASSERT_TRUE(plan.valid());
	ASSERT_EQ(plan.items.size(), 2u);
	EXPECT_EQ(plan.items[0].format, TextureImportFormat::tga);
	EXPECT_EQ(plan.items[0].width, 4);
	EXPECT_EQ(plan.items[0].height, 3);
	EXPECT_TRUE(plan.items[0].hasAlpha);
	EXPECT_EQ(plan.items[0].usage, TextureSurfaceUsage::allSurfaces);
	EXPECT_EQ(plan.items[1].format, TextureImportFormat::doomPatch);
	EXPECT_EQ(plan.items[1].width, 1);
	EXPECT_EQ(plan.items[1].height, 1);
	EXPECT_EQ(plan.items[1].usage, TextureSurfaceUsage::walls);
}

TEST_P(TextureImportTest, EffectiveCatalogsUseTheImportedDimensions)
{
	TextureImportRequestItem request{ sourcePath };
	request.automaticUsage = false;
	request.usage = TextureSurfaceUsage::walls;
	std::vector<TextureImportRequestItem> requests = { request };
	const TextureImportPlan plan =
			M_PlanTextureImport(wad, config, packagePath, requests);
	ASSERT_TRUE(plan.valid());
	M_ApplyTextureImport(wad, config, requests, plan);
	std::shared_ptr<Wad_file> reopened = M_OpenEditablePackage(packagePath);
	ASSERT_TRUE(reopened);
	WadData refreshed = wad;
	refreshed.master.ReplaceEditWad(reopened);
	refreshed.reloadSurfaceImages(
			refreshed.master.gameWad(), config, {});
	const auto walls = refreshed.images.getWallSurfaceImages(config);
	const auto planes = refreshed.images.getPlaneSurfaceImages(config);
	ASSERT_TRUE(walls.count("STONE_WA"));
	ASSERT_TRUE(planes.count("STONE_WA"));
	EXPECT_EQ(walls.at("STONE_WA")->width(), 4);
	EXPECT_EQ(walls.at("STONE_WA")->height(), 3);
	EXPECT_EQ(planes.at("STONE_WA")->width(), 4);
	EXPECT_EQ(planes.at("STONE_WA")->height(), 3);
}

TEST_P(TextureImportTest, ModernImagesRequireACompatiblePort)
{
	ConfigData vanilla;
	const TextureImportPlan plan = M_PlanTextureImport(wad, vanilla,
			packagePath, { { sourcePath } });
	EXPECT_FALSE(plan.valid());
	ASSERT_FALSE(plan.issues.empty());
	EXPECT_TRUE(std::any_of(plan.issues.begin(), plan.issues.end(),
			[](const TextureImportIssue &issue)
			{
				return issue.explanation.findNoCase("active port") !=
						SString::npos;
			}));
}

TEST_P(TextureImportTest, RejectsAStalePackageBeforeBackup)
{
	std::vector<TextureImportRequestItem> requests = { { sourcePath } };
	const TextureImportPlan reviewed =
			M_PlanTextureImport(wad, config, packagePath, requests);
	ASSERT_TRUE(reviewed.valid());
	M_WritePackageResources(packagePath, {
		{ PackageResourceKind::flat, "CHANGED", ".lmp",
				std::vector<uint8_t>(64 * 64, 4), std::nullopt }
	});
	bool callbackCalled = false;
	EXPECT_THROW(M_ApplyTextureImport(wad, config, requests, reviewed, nullptr,
			[&callbackCalled]()
			{
				callbackCalled = true;
			}), std::runtime_error);
	EXPECT_FALSE(callbackCalled);
}

TEST_P(TextureImportTest, TraditionalRawFlatNeedsNoModernWallFeature)
{
	const fs::path rawPath = getSubPath("floor.lmp");
	const std::vector<uint8_t> raw(64 * 64, 7);
	WriteBytes(rawPath, raw);
	mDeleteList.push(rawPath);
	ConfigData vanilla;
	std::vector<TextureImportRequestItem> requests = { { rawPath } };
	const TextureImportPlan plan =
			M_PlanTextureImport(wad, vanilla, packagePath, requests);
	ASSERT_TRUE(plan.valid());
	EXPECT_EQ(plan.items[0].format, TextureImportFormat::rawFlat);
	EXPECT_EQ(plan.items[0].usage, TextureSurfaceUsage::planes);
	ASSERT_EQ(plan.items[0].destinations.size(), 1u);
	EXPECT_EQ(plan.items[0].destinations[0].kind,
			PackageResourceKind::flat);
}

TEST_P(TextureImportTest, ReportsIwadConflictWithoutMutatingIt)
{
	AddResource(*wad.master.gameWad(), PackageResourceKind::wallTexture,
			"STONE_WA", 3);
	const int before = wad.master.gameWad()->NumLumps();
	std::vector<TextureImportRequestItem> requests = { { sourcePath } };
	const TextureImportPlan renamed =
			M_PlanTextureImport(wad, config, packagePath, requests);
	ASSERT_TRUE(renamed.valid());
	EXPECT_EQ(renamed.items[0].resolvedName, "STONE_2");
	ASSERT_FALSE(renamed.items[0].conflicts.empty());
	EXPECT_EQ(renamed.items[0].conflicts[0].label, "IWAD");
	EXPECT_EQ(renamed.items[0].conflicts[0].provenance,
			TextureResourceProvenance::iwad);

	requests[0].conflictPolicy = TextureConflictPolicy::overrideLoaded;
	const TextureImportPlan overridePlan =
			M_PlanTextureImport(wad, config, packagePath, requests);
	ASSERT_TRUE(overridePlan.valid());
	EXPECT_TRUE(overridePlan.destructive());
	M_ApplyTextureImport(wad, config, requests, overridePlan);
	EXPECT_EQ(wad.master.gameWad()->NumLumps(), before);
}

TEST_P(TextureImportTest, FindsClassicCompositeIwadTextureNames)
{
	AddCompositeTextureNames(*wad.master.gameWad());
	std::vector<TextureImportRequestItem> requests = { { sourcePath } };
	const TextureImportPlan renamed =
			M_PlanTextureImport(wad, config, packagePath, requests);
	ASSERT_TRUE(renamed.valid());
	EXPECT_EQ(renamed.items[0].resolvedName, "STONE_2");
	ASSERT_FALSE(renamed.items[0].conflicts.empty());
	EXPECT_EQ(renamed.items[0].conflicts[0].label, "IWAD");
	EXPECT_EQ(renamed.items[0].conflicts[0].entryPath,
			"TEXTURE1/STONE_WA");
}

TEST_P(TextureImportTest, ExplicitBatchCollisionBlocksEverything)
{
	TextureImportRequestItem first{ sourcePath };
	first.conflictPolicy = TextureConflictPolicy::overrideLoaded;
	const fs::path secondPath = getSubPath("duplicate.png");
	WriteBytes(secondPath, PNG_DATA);
	mDeleteList.push(secondPath);
	TextureImportRequestItem second{ secondPath };
	second.requestedName = "STONE_WA";
	second.conflictPolicy = TextureConflictPolicy::overrideLoaded;
	const TextureImportPlan plan = M_PlanTextureImport(wad, config,
			packagePath, { first, second });
	EXPECT_FALSE(plan.valid());
}

TEST_P(TextureImportTest, ReservedStructuralNamesAreNeverOverwritten)
{
	TextureImportRequestItem safe{ sourcePath };
	safe.requestedName = "THINGS";
	const TextureImportPlan renamed =
			M_PlanTextureImport(wad, config, packagePath, { safe });
	ASSERT_TRUE(renamed.valid());
	EXPECT_EQ(renamed.items[0].resolvedName, "THINGS_2");

	safe.conflictPolicy = TextureConflictPolicy::overrideLoaded;
	const TextureImportPlan blocked =
			M_PlanTextureImport(wad, config, packagePath, { safe });
	EXPECT_FALSE(blocked.valid());
}

TEST_P(TextureImportTest, NewWadNamespacesNeverEncloseMapLumps)
{
	if (GetParam() != ProjectPackage::wad)
		return;
	std::shared_ptr<Wad_file> package =
			Wad_file::Open(packagePath, WadOpenMode::append);
	ASSERT_TRUE(package);
	package->AddLevel("MAP01");
	for (const char *part :
			{ "THINGS", "LINEDEFS", "SIDEDEFS", "VERTEXES" })
	{
		package->AddLump(part);
	}
	package->writeToDisk();
	wad.master.ReplaceEditWad(M_OpenEditablePackage(packagePath));

	std::vector<TextureImportRequestItem> requests = { { sourcePath } };
	const TextureImportPlan plan =
			M_PlanTextureImport(wad, config, packagePath, requests);
	ASSERT_TRUE(plan.valid());
	M_ApplyTextureImport(wad, config, requests, plan);

	std::shared_ptr<Wad_file> reopened =
			Wad_file::Open(packagePath, WadOpenMode::read);
	ASSERT_TRUE(reopened);
	ASSERT_EQ(reopened->LevelCount(), 1);
	EXPECT_GT(reopened->FindLumpNum("TX_START"),
			reopened->LevelLastLump(0));
	EXPECT_GT(reopened->FindLumpNum("F_START"),
			reopened->LevelLastLump(0));
}

INSTANTIATE_TEST_SUITE_P(WadAndPk3, TextureImportTest,
		::testing::Values(ProjectPackage::wad, ProjectPackage::pk3));
