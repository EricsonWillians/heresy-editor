//------------------------------------------------------------------------
//  ATOMIC PROJECT SAVE AND AUTOSAVE TESTS
//------------------------------------------------------------------------

#include "Instance.h"
#include "e_basis.h"
#include "m_config.h"
#include "m_files.h"
#include "m_package.h"
#include "m_parse.h"
#include "m_recovery.h"
#include "m_session.h"
#include "m_streams.h"
#include "testUtils/TempDirContext.hpp"

#include "gtest/gtest.h"

class ProjectSaveTest : public TempDirContext,
		public ::testing::WithParamInterface<ProjectPackage>
{
protected:
	Instance instance;
	fs::path previousCache;
	fs::path previousHome;
	int previousBackupFiles = 0;
	int previousBackupSpace = 0;

	void SetUp() override
	{
		TempDirContext::SetUp();
		instance.Editor_Init();
		previousCache = global::cache_dir;
		previousHome = global::home_dir;
		previousBackupFiles = config::backup_max_files;
		previousBackupSpace = config::backup_max_space;
		global::cache_dir = getSubPath("cache");
		global::home_dir.clear();
		config::backup_max_files = 0;
		fs::create_directories(global::cache_dir / "recovery");
	}

	void TearDown() override
	{
		DLG_Confirm_Override = nullptr;
		config::backup_max_files = previousBackupFiles;
		config::backup_max_space = previousBackupSpace;
		global::cache_dir = previousCache;
		global::home_dir = previousHome;
		std::error_code error;
		fs::remove_all(mTempDir, error);
		EXPECT_FALSE(error);
		mTempDir.clear();
	}

	fs::path packagePath() const
	{
		return getSubPath(GetParam() == ProjectPackage::pk3 ?
				"campaign.pk3" : "campaign.wad");
	}

	void addEmptyMap(Wad_file &wad, const SString &name, byte marker)
	{
		Lump_c *header = wad.AddLevel(name);
		ASSERT_TRUE(header);
		header->Write(&marker, 1);
		wad.AddLump("THINGS");
		wad.AddLump("LINEDEFS");
		wad.AddLump("SIDEDEFS");
		wad.AddLump("VERTEXES");
		wad.AddLump("SEGS");
		wad.AddLump("SSECTORS");
		wad.AddLump("NODES");
		wad.AddLump("SECTORS");
		wad.AddLump("REJECT");
		wad.AddLump("BLOCKMAP");
	}

	std::shared_ptr<Wad_file> createPackage()
	{
		std::shared_ptr<PackageBackend> backend = M_CreatePackageBackend(
				packagePath(), GetParam());
		EXPECT_TRUE(backend);
		std::shared_ptr<Wad_file> package = backend->openEditable();
		EXPECT_TRUE(package);
		addEmptyMap(*package, "MAP01", 1);
		addEmptyMap(*package, "MAP02", 2);
		package->writeToDisk();
		return package;
	}

	Document dirtyDocument(byte before, byte after)
	{
		Document document(instance);
		document.headerData = { before };
		EditOperation operation(document.basis);
		operation.changeLump(LumpType::header, std::vector<byte>{ after });
		return document;
	}

	ProjectMetadata readProjectMetadata(const Wad_file &package)
	{
		ProjectMetadata project;
		const Lump_c *metadata = package.FindLump(EUREKA_LUMP);
		EXPECT_TRUE(metadata);
		if (!metadata)
			return project;
		LumpInputStream stream(*metadata);
		SString line;
		while (stream.readLine(line))
		{
			TokenWordParse words(line, true);
			SString key;
			SString value;
			if (words.getNext(key) && words.getNext(value))
				project.parseField(key, value);
		}
		M_ReconcileCampaignMetadata(project);
		return project;
	}
};

TEST_P(ProjectSaveTest, SavesEveryDirtyMapInOnePackageCommit)
{
	std::shared_ptr<Wad_file> package = createPackage();
	instance.wad.master.ReplaceEditWad(package);
	instance.loaded.levelName = "MAP01";
	instance.loaded.levelFormat = MapFormat::doom;
	instance.level = dirtyDocument(1, 11);

	LoadingData cachedLoading = instance.loaded;
	cachedLoading.levelName = "MAP02";
	Document cached = dirtyDocument(2, 22);
	ASSERT_TRUE(instance.documentCache.store("MAP02", std::move(cached),
			cachedLoading));
	instance.Project_MarkMetadataDirty();
	ASSERT_TRUE(instance.Project_HasChanges());

	ASSERT_TRUE(instance.Project_WriteAutosave());
	RecoveryStore recoveries(global::cache_dir / "recovery");
	std::optional<RecoverySnapshot> snapshot = recoveries.latest(packagePath());
	ASSERT_TRUE(snapshot);
	EXPECT_EQ(snapshot->maps.size(), 2u);

	ASSERT_TRUE(instance.M_SaveProject(true));
	EXPECT_FALSE(instance.Project_HasChanges());
	EXPECT_FALSE(instance.level.hasChanges());
	EXPECT_FALSE(instance.documentCache.isDirty("MAP02"));
	EXPECT_FALSE(recoveries.latest(packagePath()));

	std::shared_ptr<Wad_file> saved = M_OpenEditablePackage(packagePath());
	ASSERT_TRUE(saved);
	ASSERT_GE(saved->LevelFind("MAP01"), 0);
	ASSERT_GE(saved->LevelFind("MAP02"), 0);
	const Lump_c *map01 = saved->GetLump(saved->LevelHeader(saved->LevelFind("MAP01")));
	const Lump_c *map02 = saved->GetLump(saved->LevelHeader(saved->LevelFind("MAP02")));
	ASSERT_TRUE(map01);
	ASSERT_TRUE(map02);
	EXPECT_EQ(map01->getData(), (std::vector<byte>{ 11 }));
	EXPECT_EQ(map02->getData(), (std::vector<byte>{ 22 }));
	EXPECT_TRUE(saved->FindLump(EUREKA_LUMP));
}

TEST_P(ProjectSaveTest, SaveProjectKeepsUndoAndRedoHistory)
{
	std::shared_ptr<Wad_file> package = createPackage();
	instance.wad.master.ReplaceEditWad(package);
	instance.loaded.levelName = "MAP01";
	instance.loaded.levelFormat = MapFormat::doom;
	instance.level = dirtyDocument(1, 11);

	ASSERT_TRUE(instance.M_SaveProject(true));
	EXPECT_EQ(instance.level.headerData, (std::vector<byte>{11}));
	EXPECT_FALSE(instance.level.hasChanges());

	ASSERT_TRUE(instance.level.basis.undo());
	EXPECT_EQ(instance.level.headerData, (std::vector<byte>{1}));
	EXPECT_TRUE(instance.level.hasChanges());

	// Saving an accidentally undone state must not destroy the Redo future.
	ASSERT_TRUE(instance.M_SaveProject(true));
	EXPECT_FALSE(instance.level.hasChanges());
	ASSERT_TRUE(instance.level.basis.redo());
	EXPECT_EQ(instance.level.headerData, (std::vector<byte>{11}));
	EXPECT_TRUE(instance.level.hasChanges());
}

TEST_P(ProjectSaveTest, LaterAutosavesRetainUnloadedRecoveryMaps)
{
	std::shared_ptr<Wad_file> package = createPackage();
	instance.wad.master.ReplaceEditWad(package);
	instance.loaded.levelName = "MAP01";
	instance.loaded.levelFormat = MapFormat::doom;
	instance.level.markSaved();

	LoadingData cachedLoading = instance.loaded;
	cachedLoading.levelName = "MAP02";
	Document cached = dirtyDocument(2, 22);
	ASSERT_TRUE(instance.documentCache.store("MAP02", std::move(cached),
			cachedLoading));
	ASSERT_TRUE(instance.Project_WriteAutosave());

	DLG_Confirm_Override = [](const std::vector<SString> &, const char *, va_list)
	{
		return 0; // Later
	};
	EXPECT_FALSE(instance.Project_CheckRecovery());
	EXPECT_TRUE(instance.Project_HasDeferredRecovery());
	DLG_Confirm_Override = nullptr;

	// Model a clean reload followed by work in another map. The deferred MAP02
	// snapshot must be carried into the new generation instead of becoming an
	// inaccessible older generation.
	instance.documentCache.clear();
	instance.level = dirtyDocument(1, 11);
	ASSERT_TRUE(instance.Project_WriteAutosave());

	RecoveryStore recoveries(global::cache_dir / "recovery");
	std::optional<RecoverySnapshot> snapshot = recoveries.latest(packagePath());
	ASSERT_TRUE(snapshot);
	ASSERT_EQ(snapshot->maps.size(), 2u);
	std::vector<SString> names;
	for (const RecoveryMapFile &map : snapshot->maps)
		names.push_back(map.mapName);
	std::sort(names.begin(), names.end());
	EXPECT_EQ(names, (std::vector<SString>{ "MAP01", "MAP02" }));
}

TEST_P(ProjectSaveTest, RotatesWholePackageBackups)
{
	fs::create_directories(global::cache_dir / "backups");
	config::backup_max_files = 2;
	config::backup_max_space = 100;
	std::shared_ptr<Wad_file> package = createPackage();

	M_BackupWad(package.get());
	M_BackupWad(package.get());
	M_BackupWad(package.get());

	const fs::path backupDirectory = global::cache_dir / "backups" / "campaign";
	std::vector<fs::path> backups;
	for (const fs::directory_entry &entry : fs::directory_iterator(backupDirectory))
		if (entry.is_regular_file())
			backups.push_back(entry.path());
	ASSERT_EQ(backups.size(), 2u);
	for (const fs::path &backup : backups)
	{
		EXPECT_EQ(backup.extension(), packagePath().extension());
		EXPECT_TRUE(M_ValidateEditablePackage(backup));
	}
}


TEST_P(ProjectSaveTest, PersistsActiveAndNavigatorSessionBesidePackage)
{
	std::shared_ptr<Wad_file> package = createPackage();
	instance.wad.master.ReplaceEditWad(package);
	instance.loaded.levelName = "MAP02";
	instance.loaded.gameName = "doom2";
	instance.loaded.iwadName = getSubPath("portable/doom2.wad");
	instance.loaded.project.version = ProjectMetadata::CURRENT_VERSION;
	instance.loaded.project.name = "Session Test";
	instance.loaded.project.package = GetParam();
	instance.loaded.project.campaign = CampaignMode::custom;
	instance.loaded.project.mapSlots = { "MAP02", "MAP01" };
	instance.Project_SetNavigatorSelection("MAP01");

	ASSERT_TRUE(instance.M_SaveProject(true));
	std::optional<ProjectSession> session = M_LoadProjectSession(packagePath());
	ASSERT_TRUE(session);
	EXPECT_EQ(session->activeMap, "MAP02");
	EXPECT_EQ(session->navigatorMap, "MAP01");
	EXPECT_EQ(session->iwadGame, "doom2");
	EXPECT_EQ(session->iwadFile, "doom2.wad");
	EXPECT_TRUE(session->iwadRelative.is_relative());
	EXPECT_TRUE(fs::exists(M_ProjectSessionPath(packagePath())));
}

TEST_P(ProjectSaveTest, PersistsRichCampaignGraphMetadata)
{
	std::shared_ptr<Wad_file> package = createPackage();
	instance.wad.master.ReplaceEditWad(package);
	instance.loaded.levelName = "MAP01";
	instance.loaded.project.version = ProjectMetadata::CURRENT_VERSION;
	instance.loaded.project.name = "Graph Test";
	instance.loaded.project.package = GetParam();
	instance.loaded.project.campaign = CampaignMode::custom;
	instance.loaded.project.mapSlots = { "MAP01", "MAP02" };
	instance.loaded.project.mapDefinitions = {
			{ "MAP01", "The Arrival", "Episode One",
					SString("MAP02"), SString("MAP02") },
			{ "MAP02", "The End", "Episode One",
					SString{}, std::nullopt, true }
	};
	instance.Project_MarkMetadataDirty();
	ASSERT_TRUE(instance.Project_WriteAutosave());
	RecoveryStore recoveries(global::cache_dir / "recovery");
	std::optional<RecoverySnapshot> snapshot = recoveries.latest(packagePath());
	ASSERT_TRUE(snapshot);
	std::shared_ptr<Wad_file> recoveryContext = Wad_file::Open(
			snapshot->contextFile, WadOpenMode::read);
	ASSERT_TRUE(recoveryContext);
	ProjectMetadata recovered = readProjectMetadata(*recoveryContext);
	const CampaignMapDefinition *recoveredFirst =
			recovered.mapDefinition("MAP01");
	ASSERT_TRUE(recoveredFirst);
	EXPECT_EQ(recoveredFirst->title, "The Arrival");
	EXPECT_EQ(recoveredFirst->secretExit,
			std::optional<SString>("MAP02"));
	const CampaignMapDefinition *recoveredLast =
			recovered.mapDefinition("MAP02");
	ASSERT_TRUE(recoveredLast);
	EXPECT_TRUE(recoveredLast->entryPoint);

	ASSERT_TRUE(instance.M_SaveProject(true));
	EXPECT_FALSE(instance.Project_MetadataHasChanges());

	std::shared_ptr<Wad_file> reopened = M_OpenEditablePackage(packagePath());
	ASSERT_TRUE(reopened);
	ProjectMetadata parsed = readProjectMetadata(*reopened);
	EXPECT_EQ(parsed.version, ProjectMetadata::CURRENT_VERSION);
	EXPECT_EQ(parsed.package, GetParam());
	ASSERT_EQ(parsed.mapDefinitions.size(), 2u);
	const CampaignMapDefinition *first = parsed.mapDefinition("MAP01");
	ASSERT_TRUE(first);
	EXPECT_EQ(first->title, "The Arrival");
	EXPECT_EQ(first->episode, "Episode One");
	EXPECT_EQ(first->normalExit, std::optional<SString>("MAP02"));
	EXPECT_EQ(first->secretExit, std::optional<SString>("MAP02"));
	const CampaignMapDefinition *last = parsed.mapDefinition("MAP02");
	ASSERT_TRUE(last);
	ASSERT_TRUE(last->normalExit.has_value());
	EXPECT_TRUE(last->normalExit->empty());
	EXPECT_TRUE(last->entryPoint);
	EXPECT_EQ(M_CampaignEntryMaps(parsed),
			(std::vector<SString>{ "MAP01", "MAP02" }));
}

INSTANTIATE_TEST_SUITE_P(WadAndPk3, ProjectSaveTest,
		::testing::Values(ProjectPackage::wad, ProjectPackage::pk3));
