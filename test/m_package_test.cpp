//------------------------------------------------------------------------
//  PROJECT PACKAGE BACKEND TESTS
//------------------------------------------------------------------------

#include "m_package.h"

#include "lib_file.h"
#include "main.h"
#include "m_loadsave.h"
#include "m_zip.h"
#include "testUtils/Palette.hpp"
#include "testUtils/TempDirContext.hpp"
#include "WadData.h"
#include "w_wad.h"

#include "gtest/gtest.h"

namespace
{

class SourceDefinitionPaths
{
public:
	SourceDefinitionPaths() :
			install_(global::install_dir),
			home_(global::home_dir),
			oldHome_(global::old_linux_home_and_cache_dir)
	{
		global::install_dir = fs::path(HERESY_TEST_SOURCE_DIR);
		global::home_dir.clear();
		global::old_linux_home_and_cache_dir.clear();
	}

	~SourceDefinitionPaths()
	{
		global::install_dir = install_;
		global::home_dir = home_;
		global::old_linux_home_and_cache_dir = oldHome_;
	}

private:
	fs::path install_;
	fs::path home_;
	fs::path oldHome_;
};

} // namespace

class PackageBackendTest : public TempDirContext
{
protected:
	static void AddDoomMap(Wad_file &wad, const SString &name)
	{
		wad.AddLevel(name);
		wad.AddLump("THINGS");
		wad.AddLump("LINEDEFS");
		wad.AddLump("SIDEDEFS");
		wad.AddLump("VERTEXES");
		wad.AddLump("SEGS");
		wad.AddLump("SECTORS");
	}

	static void ExpectResource(const Wad_file &wad, const char *name,
			WadNamespace nameSpace, const std::vector<uint8_t> &expected)
	{
		const Lump_c *lump = wad.FindLumpInNamespace(name, nameSpace);
		ASSERT_NE(lump, nullptr);
		EXPECT_EQ(lump->getData(), expected);
	}
};

TEST_F(PackageBackendTest, FactoryKeepsWadCompatibility)
{
	const fs::path path = getSubPath("ordinary.wad");
	auto backend = M_CreatePackageBackend(path, ProjectPackage::wad);
	ASSERT_TRUE(backend);
	EXPECT_EQ(backend->packageType(), ProjectPackage::wad);
	auto wad = backend->openEditable();
	ASSERT_TRUE(wad);
	AddDoomMap(*wad, "MAP01");
	wad->writeToDisk();
	mDeleteList.push(path);

	auto reopened = M_OpenEditablePackage(path);
	ASSERT_TRUE(reopened);
	EXPECT_FALSE(reopened->IsPackageBacked());
	EXPECT_EQ(reopened->LevelFind("MAP01"), 0);
}

TEST_F(PackageBackendTest, Pk3MapMetadataResourcesAndUnknownEntriesRoundTrip)
{
	const fs::path path = getSubPath("campaign.pk3");
	auto backend = M_CreatePackageBackend(path, ProjectPackage::pk3);
	ASSERT_TRUE(backend);
	EXPECT_EQ(backend->packageType(), ProjectPackage::pk3);
	auto edit = backend->openEditable();
	ASSERT_TRUE(edit);
	ASSERT_TRUE(edit->IsPackageBacked());
	AddDoomMap(*edit, "MAP01");
	Lump_c &metadata = edit->AddLump(EUREKA_LUMP);
	metadata.Printf("# Heresy Editor project info\nproject_package pk3\n");
	edit->writeToDisk();
	mDeleteList.push(path);

	auto zip = ZipArchive::Open(path);
	ASSERT_TRUE(zip);
	ASSERT_TRUE(zip->contains("maps/MAP01.wad"));
	ASSERT_TRUE(zip->contains("heresy/project.txt"));
	zip->setEntry("docs/untouched.txt", std::vector<uint8_t>(8192, 'U'));
	zip->setEntry("textures/STONE.png", { 0x89, 'P', 'N', 'G' });
	zip->writeToDisk();
	{
		auto verifyZip = ZipArchive::Open(path);
		ASSERT_TRUE(verifyZip);
		const std::vector<uint8_t> mapData = verifyZip->readEntry("maps/MAP01.wad");
		auto verifyMap = Wad_file::loadFromData("verify.wad", mapData,
				WadOpenMode::read);
		ASSERT_TRUE(verifyMap);
		ASSERT_GE(verifyMap->LevelFind("MAP01"), 0);
	}

	std::vector<uint8_t> unknownBefore =
			ZipArchive::Open(path)->readEntry("docs/untouched.txt");

	edit = M_OpenEditablePackage(path);
	ASSERT_TRUE(edit);
	ASSERT_EQ(edit->LevelCount(), 1);
	EXPECT_EQ(edit->LevelName(0), "MAP01");
	ASSERT_TRUE(edit->FindLump(EUREKA_LUMP));
	ASSERT_TRUE(edit->FindLump("STONE"));
	EXPECT_NE(edit->FindLumpInNamespace("STONE", WadNamespace::TextureLumps),
			nullptr);

	const int things = edit->LevelLookupLump(0, "THINGS");
	ASSERT_GE(things, 0);
	edit->GetLump(things)->clearData();
	edit->GetLump(things)->Printf("changed map data");
	edit->writeToDisk();

	zip = ZipArchive::Open(path);
	ASSERT_TRUE(zip);
	EXPECT_EQ(zip->readEntry("docs/untouched.txt"), unknownBefore);
	EXPECT_EQ(zip->readEntry("textures/STONE.png"),
			(std::vector<uint8_t>{ 0x89, 'P', 'N', 'G' }));

	edit = M_OpenEditablePackage(path);
	ASSERT_TRUE(edit);
	ASSERT_EQ(edit->LevelCount(), 1);
	const int reopenedThings = edit->LevelLookupLump(0, "THINGS");
	ASSERT_GE(reopenedThings, 0);
	const std::vector<byte> &thingsData = edit->GetLump(reopenedThings)->getData();
	EXPECT_EQ(std::string(thingsData.begin(), thingsData.end()), "changed map data");
}

TEST_F(PackageBackendTest, Pk3ProjectsDeepResourceTreesIntoCorrectNamespaces)
{
	const fs::path path = getSubPath("deep-resources.pk3");
	auto backend = M_CreatePackageBackend(path, ProjectPackage::pk3);
	ASSERT_TRUE(backend);
	auto edit = backend->openEditable();
	ASSERT_TRUE(edit);
	AddDoomMap(*edit, "MAP01");
	edit->writeToDisk();
	mDeleteList.push(path);

	const std::vector<uint8_t> textureData(4096, 'T');
	const std::vector<uint8_t> flatData(4096, 'F');
	const std::vector<uint8_t> spriteData(4096, 'S');
	auto zip = ZipArchive::Open(path);
	ASSERT_TRUE(zip);
	zip->setEntry("textures/environment/castle/BRICK01.png", textureData);
	zip->setEntry("FlAtS/terrain/caves/ROCKFLAT.lmp", flatData);
	zip->setEntry("SPRITES/monsters/generated/DEMOA1.png", spriteData);
	zip->setEntry("graphics/ui/deep/IGNORED.png", {'I'});
	zip->setEntry("textures/embedded/archive.wad", {'P', 'W', 'A', 'D'});
	zip->writeToDisk();

	edit = M_OpenEditablePackage(path);
	ASSERT_TRUE(edit);
	ExpectResource(*edit, "BRICK01", WadNamespace::TextureLumps, textureData);
	ExpectResource(*edit, "ROCKFLAT", WadNamespace::Flats, flatData);
	ExpectResource(*edit, "DEMOA1", WadNamespace::Sprites, spriteData);
	EXPECT_EQ(edit->FindLumpInNamespace("BRICK01", WadNamespace::Global), nullptr);
	EXPECT_EQ(edit->FindLump("IGNORED"), nullptr);
	EXPECT_EQ(edit->FindLump("ARCHIVE"), nullptr);

	const int things = edit->LevelLookupLump(edit->LevelFind("MAP01"), "THINGS");
	ASSERT_GE(things, 0);
	edit->GetLump(things)->Printf("map update");
	edit->writeToDisk();

	zip = ZipArchive::Open(path);
	ASSERT_TRUE(zip);
	EXPECT_EQ(zip->readEntry("textures/environment/castle/BRICK01.png"), textureData);
	EXPECT_EQ(zip->readEntry("FlAtS/terrain/caves/ROCKFLAT.lmp"), flatData);
	EXPECT_EQ(zip->readEntry("SPRITES/monsters/generated/DEMOA1.png"), spriteData);

	edit = M_OpenEditablePackage(path);
	ASSERT_TRUE(edit);
	ExpectResource(*edit, "BRICK01", WadNamespace::TextureLumps, textureData);
	ExpectResource(*edit, "ROCKFLAT", WadNamespace::Flats, flatData);
	ExpectResource(*edit, "DEMOA1", WadNamespace::Sprites, spriteData);
}

TEST_F(PackageBackendTest, Pk3MetadataInventoryIsReadOnlyAndClassifiesContent)
{
	const fs::path path = getSubPath("metadata-inventory.pk3");
	auto backend = M_CreatePackageBackend(path, ProjectPackage::pk3);
	ASSERT_TRUE(backend);
	auto edit = backend->openEditable();
	ASSERT_TRUE(edit);
	AddDoomMap(*edit, "MAP01");
	edit->AddLump(EUREKA_LUMP).Printf("project_package pk3\n");
	edit->writeToDisk();
	mDeleteList.push(path);

	const std::vector<uint8_t> mapInfo = {
			'm', 'a', 'p', ' ', 'M', 'A', 'P', '0', '1', ' ', '{', '}', '\n'
	};
	const std::vector<uint8_t> zscript = {
			'v', 'e', 'r', 's', 'i', 'o', 'n', ' ', '"', '4', '.', '1', '1', '"', ';', '\n'
	};
	const std::vector<uint8_t> unknown = { 'k', 'e', 'e', 'p' };
	const std::vector<uint8_t> texture = { 0x89, 'P', 'N', 'G' };
	auto zip = ZipArchive::Open(path);
	ASSERT_TRUE(zip);
	zip->setEntry("MAPINFO", mapInfo);
	zip->setEntry("defs/ZMAPINFO.txt", { 'i', 'n', 'c', 'l', 'u', 'd', 'e' });
	zip->setEntry("TEXTURES.txt", { 'T', 'e', 'x', 't', 'u', 'r', 'e' });
	zip->setEntry("ZSCRIPT", zscript);
	zip->setEntry("zscript/actors/Boss.zs", { 'c', 'l', 'a', 's', 's' });
	zip->setEntry("acs/library.o", { 0, 'A', 'C', 'S' });
	zip->setEntry("binary/GAMEINFO.lmp", { 0, 1, 2 });
	zip->setEntry("invalid/LANGUAGE.txt", { 0xc3, 0x28 });
	zip->setEntry("empty/SNDINFO", {});
	zip->setEntry("huge/ANIMDEFS.txt",
			std::vector<uint8_t>(PK3_METADATA_PREVIEW_LIMIT + 1, 'A'));
	zip->setEntry("textures/deep/WALL.png", texture);
	zip->setEntry("sounds/weapons/shot.ogg", { 'O', 'g', 'g', 'S' });
	zip->setEntry("docs/readme.md", unknown);
	zip->setEntry("explicit-directory/", {});
	zip->writeToDisk();

	std::vector<uint8_t> beforeInspection;
	ASSERT_TRUE(FileLoad(path, beforeInspection));
	SString error;
	std::optional<Pk3PackageInventory> inventory =
			M_InspectPk3Package(path, &error);
	ASSERT_TRUE(inventory) << error.c_str();
	EXPECT_EQ(inventory->archiveEntries, 16u);
	EXPECT_EQ(inventory->directoryEntries, 1u);
	EXPECT_EQ(inventory->totalFiles, 15u);
	EXPECT_EQ(inventory->mapFiles, 1u);
	EXPECT_EQ(inventory->editorFiles, 1u);
	EXPECT_EQ(inventory->metadata.size(), 10u);
	EXPECT_EQ(inventory->resourceFiles, 2u);
	EXPECT_EQ(inventory->otherFiles, 1u);

	auto metadata = [&inventory](const char *wanted)
	{
		return std::find_if(inventory->metadata.begin(), inventory->metadata.end(),
				[wanted](const Pk3MetadataEntry &entry)
				{
					return entry.path.noCaseEqual(wanted);
				});
	};
	auto mapInfoEntry = metadata("MAPINFO");
	ASSERT_NE(mapInfoEntry, inventory->metadata.end());
	EXPECT_EQ(mapInfoEntry->kind, Pk3MetadataKind::campaignDefinition);
	EXPECT_EQ(mapInfoEntry->previewState, Pk3PreviewState::text);
	EXPECT_EQ(mapInfoEntry->preview,
			std::string(mapInfo.begin(), mapInfo.end()));
	auto runtimeEntry = metadata("ZSCRIPT");
	ASSERT_NE(runtimeEntry, inventory->metadata.end());
	EXPECT_EQ(runtimeEntry->kind, Pk3MetadataKind::runtimeSource);
	EXPECT_NE(runtimeEntry->detail.findNoCase("not interpreted"),
			SString::npos);
	auto binaryEntry = metadata("binary/GAMEINFO.lmp");
	ASSERT_NE(binaryEntry, inventory->metadata.end());
	EXPECT_EQ(binaryEntry->previewState, Pk3PreviewState::binary);
	auto invalidUtf8 = metadata("invalid/LANGUAGE.txt");
	ASSERT_NE(invalidUtf8, inventory->metadata.end());
	EXPECT_EQ(invalidUtf8->previewState, Pk3PreviewState::binary);
	auto emptyEntry = metadata("empty/SNDINFO");
	ASSERT_NE(emptyEntry, inventory->metadata.end());
	EXPECT_EQ(emptyEntry->previewState, Pk3PreviewState::empty);
	auto largeEntry = metadata("huge/ANIMDEFS.txt");
	ASSERT_NE(largeEntry, inventory->metadata.end());
	EXPECT_EQ(largeEntry->previewState, Pk3PreviewState::tooLarge);

	auto resource = [&inventory](const char *prefix)
	{
		return std::find_if(inventory->resources.begin(), inventory->resources.end(),
				[prefix](const Pk3ResourceGroup &group)
				{
					return group.pathPrefix.noCaseEqual(prefix);
				});
	};
	auto textures = resource("textures");
	ASSERT_NE(textures, inventory->resources.end());
	EXPECT_EQ(textures->entries, 1u);
	EXPECT_TRUE(textures->projectedByEditor);
	auto sounds = resource("sounds");
	ASSERT_NE(sounds, inventory->resources.end());
	EXPECT_EQ(sounds->entries, 1u);
	EXPECT_FALSE(sounds->projectedByEditor);
	ASSERT_EQ(inventory->projectedResources.size(), 1u);
	EXPECT_EQ(inventory->projectedResources[0].path,
			"textures/deep/WALL.png");
	EXPECT_EQ(inventory->projectedResources[0].editorName, "WALL");
	EXPECT_EQ(inventory->projectedResources[0].nameSpace,
			ResourceNamespaceKind::texture);

	std::vector<uint8_t> afterInspection;
	ASSERT_TRUE(FileLoad(path, afterInspection));
	EXPECT_EQ(afterInspection, beforeInspection);

	edit = M_OpenEditablePackage(path);
	ASSERT_TRUE(edit);
	const int things = edit->LevelLookupLump(edit->LevelFind("MAP01"), "THINGS");
	ASSERT_GE(things, 0);
	edit->GetLump(things)->Printf("changed");
	edit->writeToDisk();
	zip = ZipArchive::Open(path);
	ASSERT_TRUE(zip);
	EXPECT_EQ(zip->readEntry("MAPINFO"), mapInfo);
	EXPECT_EQ(zip->readEntry("ZSCRIPT"), zscript);
	EXPECT_EQ(zip->readEntry("docs/readme.md"), unknown);
	EXPECT_EQ(zip->readEntry("textures/deep/WALL.png"), texture);

	EXPECT_FALSE(M_InspectPk3Package(getSubPath("ordinary.wad"), &error));
	EXPECT_TRUE(error.good());
}

TEST_F(PackageBackendTest, Pk3MetadataInventoryCapsAggregatePreviewMemory)
{
	const fs::path path = getSubPath("metadata-preview-budget.pk3");
	auto zip = ZipArchive::Create(path);
	const std::vector<uint8_t> declaration(PK3_METADATA_PREVIEW_LIMIT, 'M');
	for (int index = 0; index < 9; ++index)
	{
		zip->setEntry("defs/" + std::to_string(index) + "/MAPINFO.txt",
				declaration);
	}
	zip->writeToDisk();
	mDeleteList.push(path);

	SString error;
	std::optional<Pk3PackageInventory> inventory =
			M_InspectPk3Package(path, &error);
	ASSERT_TRUE(inventory) << error.c_str();
	ASSERT_EQ(inventory->metadata.size(), 9u);

	size_t textEntries = 0;
	size_t limitedEntries = 0;
	uint64_t previewBytes = 0;
	for (const Pk3MetadataEntry &entry : inventory->metadata)
	{
		if (entry.previewState == Pk3PreviewState::text)
		{
			++textEntries;
			previewBytes += entry.preview.size();
		}
		if (entry.previewState == Pk3PreviewState::tooLarge)
			++limitedEntries;
	}
	EXPECT_EQ(textEntries, 8u);
	EXPECT_EQ(limitedEntries, 1u);
	EXPECT_EQ(previewBytes, PK3_METADATA_TOTAL_PREVIEW_LIMIT);
}

TEST_F(PackageBackendTest, Pk3SupportsSequentialMapCreationAndDeletion)
{
	const fs::path path = getSubPath("maps.pk3");
	auto backend = M_CreatePackageBackend(path, ProjectPackage::pk3);
	auto edit = backend->openEditable();
	ASSERT_TRUE(edit);
	AddDoomMap(*edit, "MAP01");
	AddDoomMap(*edit, "MAP02");
	edit->writeToDisk();
	mDeleteList.push(path);

	edit = M_OpenEditablePackage(path);
	ASSERT_TRUE(edit);
	ASSERT_EQ(edit->LevelCount(), 2);
	EXPECT_GE(edit->LevelFind("MAP01"), 0);
	EXPECT_GE(edit->LevelFind("MAP02"), 0);
	edit->RemoveLevel(edit->LevelFind("MAP01"));
	edit->writeToDisk();

	auto zip = ZipArchive::Open(path);
	ASSERT_TRUE(zip);
	EXPECT_FALSE(zip->contains("maps/MAP01.wad"));
	EXPECT_TRUE(zip->contains("maps/MAP02.wad"));
}

TEST_F(PackageBackendTest, ResourceReloadUsesPackageBeingOpened)
{
	SourceDefinitionPaths definitions;
	const fs::path iwadPath = getSubPath("doom2.wad");
	auto iwad = Wad_file::Open(iwadPath, WadOpenMode::write);
	ASSERT_TRUE(iwad);
	const std::vector<uint8_t> palette = makeGrayscale();
	Lump_c &playpal = iwad->AddLump("PLAYPAL");
	playpal.Write(palette.data(), static_cast<int>(palette.size()));
	const std::vector<uint8_t> colormap(32 * 256, 0);
	Lump_c &colormapLump = iwad->AddLump("COLORMAP");
	colormapLump.Write(colormap.data(), static_cast<int>(colormap.size()));
	iwad->writeToDisk();
	mDeleteList.push(iwadPath);

	const fs::path packagePath = getSubPath("opened.pk3");
	auto backend = M_CreatePackageBackend(packagePath, ProjectPackage::pk3);
	ASSERT_TRUE(backend);
	auto package = backend->openEditable();
	ASSERT_TRUE(package);
	AddDoomMap(*package, "MAP01");
	package->writeToDisk();
	mDeleteList.push(packagePath);
	auto zip = ZipArchive::Open(packagePath);
	ASSERT_TRUE(zip);
	const std::vector<uint8_t> flatData(64 * 64, 1);
	zip->setEntry("flats/generated/terrain/NEWFLAT.lmp", flatData);
	zip->writeToDisk();
	package = M_OpenEditablePackage(packagePath);
	ASSERT_TRUE(package);

	auto stalePackage = Wad_file::Open("stale.wad", WadOpenMode::write);
	stalePackage->AddLump("F_START");
	Lump_c &oldFlat = stalePackage->AddLump("OLDFLAT");
	oldFlat.Write(flatData.data(), static_cast<int>(flatData.size()));
	stalePackage->AddLump("F_END");
	WadData current;
	current.master.ReplaceEditWad(stalePackage);

	LoadingData loading;
	loading.iwadName = iwadPath;
	loading.portName = "biaseddoom";
	loading.levelFormat = MapFormat::doom;
	NewResources loaded = loadResources(loading, current, package);

	EXPECT_EQ(loaded.waddata.master.editWad(), package);
	EXPECT_TRUE(loaded.waddata.images.W_FlatIsKnown(loaded.config, "NEWFLAT"));
	EXPECT_FALSE(loaded.waddata.images.W_FlatIsKnown(loaded.config, "OLDFLAT"));
}
