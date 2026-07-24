//------------------------------------------------------------------------
//  RESOURCE DIAGNOSTIC TESTS
//------------------------------------------------------------------------

#include "m_resource_diagnostics.h"

#include "lib_file.h"
#include "m_config.h"
#include "m_game.h"
#include "m_package.h"
#include "m_zip.h"
#include "testUtils/Palette.hpp"
#include "testUtils/TempDirContext.hpp"
#include "WadData.h"
#include "w_wad.h"

#include "gtest/gtest.h"

namespace
{

void AddNamespacedLump(Wad_file &wad, WadNamespace nameSpace,
		const char *name, uint8_t value)
{
	const char *start = nameSpace == WadNamespace::Flats ? "F_START" :
			nameSpace == WadNamespace::Sprites ? "S_START" : "TX_START";
	const char *end = nameSpace == WadNamespace::Flats ? "F_END" :
			nameSpace == WadNamespace::Sprites ? "S_END" : "TX_END";
	wad.AddLump(start);
	Lump_c &lump = wad.AddLump(name);
	const size_t size = nameSpace == WadNamespace::Flats ? 64 * 64 : 1;
	const std::vector<uint8_t> data(size, value);
	lump.Write(data.data(), static_cast<int>(data.size()));
	wad.AddLump(end);
}

const ResourceConflict *FindConflict(const ResourceDiagnostics &diagnostics,
		ResourceConflictKind kind, ResourceNamespaceKind nameSpace,
		const char *name)
{
	auto found = std::find_if(diagnostics.conflicts.begin(),
			diagnostics.conflicts.end(),
			[kind, nameSpace, name](const ResourceConflict &conflict)
			{
				return conflict.kind == kind && conflict.nameSpace == nameSpace &&
						conflict.editorName.noCaseEqual(name);
			});
	return found == diagnostics.conflicts.end() ? nullptr : &*found;
}

} // namespace

class ResourceDiagnosticsTest : public TempDirContext
{
};

TEST_F(ResourceDiagnosticsTest,
		ProjectedCollisionsUseEditorNamesWithoutChangingArchivePaths)
{
	const fs::path path = getSubPath("collisions.pk3");
	auto zip = ZipArchive::Create(path);
	zip->setEntry("textures/first/LongNameAlpha.png", { 1 });
	zip->setEntry("textures/second/longnameBeta.jpg", { 2 });
	zip->setEntry("flats/a/STONE.lmp", std::vector<uint8_t>(64 * 64, 3));
	zip->setEntry("flats/b/stone.png", std::vector<uint8_t>(64 * 64, 4));
	zip->setEntry("sprites/a/POSSA1.png", { 5 });
	zip->setEntry("sprites/b/possa1.lmp", { 6 });
	zip->setEntry("flats/c/SAME.png", { 7 });
	zip->setEntry("textures/c/SAME.png", { 8 });
	zip->setEntry("textures/embedded/archive.wad", { 9 });
	zip->setEntry("textures/empty/EMPTY.png", {});
	zip->writeToDisk();
	mDeleteList.push(path);

	std::vector<uint8_t> before;
	ASSERT_TRUE(FileLoad(path, before));
	SString error;
	std::optional<Pk3PackageInventory> inventory =
			M_InspectPk3Package(path, &error);
	ASSERT_TRUE(inventory) << error.c_str();
	ASSERT_EQ(inventory->projectedResources.size(), 8u);

	MasterDir master;
	const ResourceDiagnostics diagnostics =
			M_AnalyzeResourceConflicts(*inventory, master);
	EXPECT_EQ(diagnostics.warningCount(), 3u);
	EXPECT_EQ(diagnostics.overrideCount(), 0u);

	const ResourceConflict *texture = FindConflict(diagnostics,
			ResourceConflictKind::projectedBasename,
			ResourceNamespaceKind::texture, "LONGNAME");
	ASSERT_TRUE(texture);
	ASSERT_EQ(texture->participants.size(), 2u);
	ASSERT_TRUE(texture->nominalWinner);
	EXPECT_EQ(*texture->nominalWinner, 1u);
	EXPECT_EQ(texture->participants[0].archivePath,
			"textures/first/LongNameAlpha.png");
	EXPECT_EQ(texture->participants[1].archivePath,
			"textures/second/longnameBeta.jpg");

	const ResourceConflict *flat = FindConflict(diagnostics,
			ResourceConflictKind::projectedBasename,
			ResourceNamespaceKind::flat, "STONE");
	ASSERT_TRUE(flat);
	ASSERT_TRUE(flat->nominalWinner);
	EXPECT_EQ(*flat->nominalWinner, 1u);

	const ResourceConflict *sprite = FindConflict(diagnostics,
			ResourceConflictKind::projectedBasename,
			ResourceNamespaceKind::sprite, "POSSA1");
	ASSERT_TRUE(sprite);
	EXPECT_FALSE(sprite->nominalWinner);
	EXPECT_NE(sprite->resolution.findNoCase("ambiguous"), SString::npos);
	EXPECT_FALSE(FindConflict(diagnostics,
			ResourceConflictKind::projectedBasename,
			ResourceNamespaceKind::flat, "SAME"));

	auto game = Wad_file::Open(getSubPath("doom2.wad"), WadOpenMode::write);
	ASSERT_TRUE(game);
	const std::vector<uint8_t> palette = makeGrayscale();
	game->AddLump("PLAYPAL").Write(palette.data(),
			static_cast<int>(palette.size()));
	const std::vector<uint8_t> colormap(32 * 256, 0);
	game->AddLump("COLORMAP").Write(colormap.data(),
			static_cast<int>(colormap.size()));
	auto project = M_OpenEditablePackage(path);
	ASSERT_TRUE(project);
	WadData loaded;
	loaded.master.ReplaceEditWad(project);
	ConfigData config;
	ASSERT_NO_THROW(loaded.reloadResources(game, config, {}));
	const Img_c *effectiveFlat = loaded.images.W_GetFlat(config, "STONE");
	ASSERT_TRUE(effectiveFlat);
	EXPECT_EQ(effectiveFlat->buf()[0], 4);

	std::vector<uint8_t> after;
	ASSERT_TRUE(FileLoad(path, after));
	EXPECT_EQ(after, before);
}

TEST_F(ResourceDiagnosticsTest, ReportsLoadedSourceOrderAndNominalWinner)
{
	auto game = Wad_file::Open(getSubPath("doom2.wad"), WadOpenMode::write);
	auto first = Wad_file::Open(getSubPath("first.wad"), WadOpenMode::write);
	auto second = Wad_file::Open(getSubPath("second.wad"), WadOpenMode::write);
	auto project = Wad_file::Open(getSubPath("project.pk3"), WadOpenMode::write);
	ASSERT_TRUE(game && first && second && project);

	const std::vector<uint8_t> palette = makeGrayscale();
	game->AddLump("PLAYPAL").Write(palette.data(), static_cast<int>(palette.size()));
	const std::vector<uint8_t> colormap(32 * 256, 0);
	game->AddLump("COLORMAP").Write(colormap.data(),
			static_cast<int>(colormap.size()));
	AddNamespacedLump(*game, WadNamespace::Flats, "STONE", 1);
	AddNamespacedLump(*game, WadNamespace::TextureLumps, "WALL", 1);
	AddNamespacedLump(*game, WadNamespace::Sprites, "POSSA1", 1);

	AddNamespacedLump(*first, WadNamespace::Flats, "STONE", 2);
	AddNamespacedLump(*first, WadNamespace::TextureLumps, "WALL", 2);
	AddNamespacedLump(*first, WadNamespace::Sprites, "POSSA1", 2);
	AddNamespacedLump(*first, WadNamespace::Flats, "DUPFLAT", 2);
	AddNamespacedLump(*first, WadNamespace::Flats, "DUPFLAT", 3);
	AddNamespacedLump(*second, WadNamespace::Flats, "STONE", 3);
	AddNamespacedLump(*project, WadNamespace::Flats, "STONE", 4);
	AddNamespacedLump(*project, WadNamespace::TextureLumps, "WALL", 4);
	AddNamespacedLump(*project, WadNamespace::Sprites, "POSSA1", 4);

	MasterDir master;
	master.setGameWad(game);
	master.setResources({ first, second });
	master.ReplaceEditWad(project);
	Pk3PackageInventory inventory;
	inventory.path = project->PathName();
	const ResourceDiagnostics diagnostics =
			M_AnalyzeResourceConflicts(inventory, master);
	EXPECT_EQ(diagnostics.warningCount(), 1u);
	EXPECT_EQ(diagnostics.overrideCount(), 3u);

	const ResourceConflict *stone = FindConflict(diagnostics,
			ResourceConflictKind::loadOrderOverride,
			ResourceNamespaceKind::flat, "STONE");
	ASSERT_TRUE(stone);
	ASSERT_EQ(stone->participants.size(), 4u);
	EXPECT_EQ(stone->participants[0].label, "IWAD");
	EXPECT_EQ(stone->participants[1].label, "Resource 1");
	EXPECT_EQ(stone->participants[2].label, "Resource 2");
	EXPECT_EQ(stone->participants[3].label, "Project");
	ASSERT_TRUE(stone->nominalWinner);
	EXPECT_EQ(*stone->nominalWinner, 3u);
	EXPECT_EQ(stone->participants[3].loadOrder, 4u);

	const ResourceConflict *duplicate = FindConflict(diagnostics,
			ResourceConflictKind::duplicateWithinSource,
			ResourceNamespaceKind::flat, "DUPFLAT");
	ASSERT_TRUE(duplicate);
	ASSERT_EQ(duplicate->participants.size(), 1u);
	EXPECT_EQ(duplicate->participants[0].label, "Resource 1");
	EXPECT_EQ(duplicate->participants[0].occurrences, 2u);
	EXPECT_FALSE(duplicate->nominalWinner);

	WadData loaded;
	loaded.master.ReplaceEditWad(project);
	ConfigData config;
	ASSERT_NO_THROW(loaded.reloadResources(game, config, { first, second }));
	const Img_c *effectiveFlat = loaded.images.W_GetFlat(config, "STONE");
	ASSERT_TRUE(effectiveFlat);
	EXPECT_EQ(effectiveFlat->buf()[0], 4);
	const std::vector<SpriteLumpRef> sprites =
			loaded.master.findFirstSpriteLump("POSS");
	ASSERT_FALSE(sprites.empty());
	ASSERT_TRUE(sprites.front().lump);
	EXPECT_EQ(sprites.front().lump->getData().front(), 4);
}
