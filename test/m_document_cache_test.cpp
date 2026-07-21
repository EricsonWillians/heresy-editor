//------------------------------------------------------------------------
//  BOUNDED MAP DOCUMENT CACHE TESTS
//------------------------------------------------------------------------

#include "m_document_cache.h"

#include "Instance.h"
#include "e_basis.h"

#include "gtest/gtest.h"

class MapDocumentCacheTest : public ::testing::Test
{
protected:
	Instance instance;

	void SetUp() override
	{
		instance.Editor_Init();
	}

	Document makeDocument(byte marker, bool dirty = false)
	{
		Document document(instance);
		document.headerData = { marker };
		if (dirty)
		{
			EditOperation operation(document.basis);
			operation.changeLump(LumpType::header,
					std::vector<byte>{ marker, marker });
		}
		return document;
	}
};

TEST_F(MapDocumentCacheTest, TakePreservesDocumentAndLoadingState)
{
	MapDocumentCache cache;
	LoadingData loading;
	loading.levelName = "MAP01";
	loading.gameName = "doom2";

	Document document = makeDocument(17, true);
	ASSERT_TRUE(cache.store("map01", std::move(document), loading));
	EXPECT_TRUE(cache.contains("MAP01"));
	EXPECT_TRUE(cache.isDirty("map01"));

	std::optional<CachedMapDocument> cached = cache.take("MaP01");
	ASSERT_TRUE(cached);
	EXPECT_EQ(cached->mapName, "MAP01");
	EXPECT_EQ(cached->loading.levelName, "MAP01");
	EXPECT_EQ(cached->loading.gameName, "doom2");
	EXPECT_EQ(cached->document.headerData, (std::vector<byte>{ 17, 17 }));
	EXPECT_TRUE(cached->document.hasChanges());
	ASSERT_TRUE(cached->document.basis.undo());
	EXPECT_EQ(cached->document.headerData, (std::vector<byte>{ 17 }));
	EXPECT_TRUE(cache.mapNames().empty());
}

TEST_F(MapDocumentCacheTest, EvictsLeastRecentlyStoredCleanDocument)
{
	MapDocumentCache cache(2);
	LoadingData loading;
	Document first = makeDocument(1);
	Document second = makeDocument(2);
	Document third = makeDocument(3);

	ASSERT_TRUE(cache.store("MAP01", std::move(first), loading));
	ASSERT_TRUE(cache.store("MAP02", std::move(second), loading));
	ASSERT_TRUE(cache.store("MAP03", std::move(third), loading));

	EXPECT_FALSE(cache.contains("MAP01"));
	EXPECT_TRUE(cache.contains("MAP02"));
	EXPECT_TRUE(cache.contains("MAP03"));
	EXPECT_EQ(cache.mapNames(),
			(std::vector<SString>{ "MAP03", "MAP02" }));
}

TEST_F(MapDocumentCacheTest, NeverEvictsDirtyDocuments)
{
	MapDocumentCache cache(2);
	LoadingData loading;
	Document first = makeDocument(1, true);
	Document second = makeDocument(2, true);
	Document candidate = makeDocument(3);

	ASSERT_TRUE(cache.store("MAP01", std::move(first), loading));
	ASSERT_TRUE(cache.store("MAP02", std::move(second), loading));
	EXPECT_FALSE(cache.store("MAP03", std::move(candidate), loading));

	EXPECT_EQ(cache.size(), 2u);
	EXPECT_EQ(cache.dirtyCount(), 2u);
	EXPECT_EQ(cache.dirtyMapNames(),
			(std::vector<SString>{ "MAP01", "MAP02" }));
	EXPECT_EQ(candidate.headerData, (std::vector<byte>{ 3 }));
}

TEST_F(MapDocumentCacheTest, RenameAndEraseAreCaseInsensitive)
{
	MapDocumentCache cache;
	LoadingData loading;
	Document first = makeDocument(1);
	Document second = makeDocument(2);

	ASSERT_TRUE(cache.store("map01", std::move(first), loading));
	ASSERT_TRUE(cache.store("map02", std::move(second), loading));
	cache.rename("MAP01", "Map02");

	EXPECT_EQ(cache.size(), 1u);
	EXPECT_TRUE(cache.contains("MAP02"));
	std::optional<CachedMapDocument> renamed = cache.take("map02");
	ASSERT_TRUE(renamed);
	EXPECT_EQ(renamed->document.headerData, (std::vector<byte>{ 1 }));

	ASSERT_TRUE(cache.store("e1m1", std::move(renamed->document), loading));
	cache.rename("E1M1", "e1m1");
	EXPECT_EQ(cache.size(), 1u);
	cache.erase("E1m1");
	EXPECT_EQ(cache.size(), 0u);
}

TEST_F(MapDocumentCacheTest, LoadingContextUpdatesWithoutChangingMapIdentity)
{
	MapDocumentCache cache;
	LoadingData original;
	original.levelName = "MAP01";
	original.gameName = "doom2";
	Document document = makeDocument(1, true);
	ASSERT_TRUE(cache.store("MAP01", std::move(document), original));

	LoadingData updated;
	updated.levelName = "MAP02";
	updated.gameName = "heretic";
	updated.portName = "zdoom";
	updated.project.version = ProjectMetadata::CURRENT_VERSION;
	updated.project.package = ProjectPackage::pk3;
	cache.updateLoadingContext(updated);

	std::optional<CachedMapDocument> cached = cache.take("MAP01");
	ASSERT_TRUE(cached);
	EXPECT_EQ(cached->loading.levelName, "MAP01");
	EXPECT_EQ(cached->loading.gameName, "heretic");
	EXPECT_EQ(cached->loading.portName, "zdoom");
	EXPECT_EQ(cached->loading.project.package, ProjectPackage::pk3);
	EXPECT_TRUE(cached->document.hasChanges());
}

TEST_F(MapDocumentCacheTest, InstanceReportsActiveAndCachedDirtyMaps)
{
	LoadingData cachedLoading;
	cachedLoading.levelName = "MAP03";
	Document cached = makeDocument(3, true);
	ASSERT_TRUE(instance.documentCache.store("MAP03", std::move(cached),
			cachedLoading));

	instance.loaded.levelName = "MAP08";
	instance.level.headerData = { 8 };
	{
		EditOperation operation(instance.level.basis);
		operation.changeLump(LumpType::header, std::vector<byte>{ 8, 8 });
	}

	EXPECT_EQ(instance.Project_DirtyMapNames(),
			(std::vector<SString>{ "MAP03", "MAP08" }));
}
