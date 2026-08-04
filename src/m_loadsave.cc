//------------------------------------------------------------------------
//  LEVEL LOAD / SAVE / NEW
//------------------------------------------------------------------------
//
//  Eureka DOOM Editor
//
//  Copyright (C) 2001-2019 Andrew Apted
//  Copyright (C)      2015 Ioan Chera
//  Copyright (C) 1997-2003 André Majorel et al
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 2
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//------------------------------------------------------------------------
//
//  Based on Yadex which incorporated code from DEU 5.21 that was put
//  in the public domain in 1994 by Raphaël Quinet and Brendon Wyber.
//
//------------------------------------------------------------------------

#include "Errors.h"
#include "Instance.h"
#include "main.h"

#include "lib_adler.h"

#include "e_basis.h"
#include "e_checks.h"
#include "e_main.h"  // CalculateLevelBounds()
#include "LineDef.h"
#include "m_config.h"
#include "m_files.h"
#include "m_mapinfo.h"
#include "m_package.h"
#include "m_recovery.h"
#include "m_session.h"
#include "m_loadsave.h"
#include "m_testmap.h"
#include "r_subdiv.h"
#include "Sector.h"
#include "SideDef.h"
#include "Thing.h"
#include "Vertex.h"
#include "w_rawdef.h"
#include "w_wad.h"

#include "ui_window.h"
#include "ui_campaign.h"
#include "ui_file.h"
#include "ui_mapinfo.h"
#include "ui_menu.h"
#include "ui_package.h"

#include <memory>
#include <ctime>
#include <set>

static const char overwrite_message[] =
	"The %s PWAD already contains this map.  "
	"This operation will destroy that map (overwrite it)."
	"\n\n"
	"Are you sure you want to continue?";


std::optional<ProjectSession> Instance::Project_LoadSession(
		const fs::path &packagePath, LoadingData &loading,
		bool preserveExplicitIWAD)
{
	std::optional<ProjectSession> session = M_LoadProjectSession(packagePath);
	if (!session)
		return {};

	const fs::path *known = global::recent.queryIWAD(session->iwadGame);
	const std::optional<fs::path> knownPath = known ?
			std::optional<fs::path>(*known) : std::nullopt;
	const std::optional<fs::path> resolved = M_ResolveProjectIWAD(packagePath,
			*session, knownPath, M_SystemIWADSearchLocations(global::home_dir,
					global::old_linux_home_and_cache_dir));
	if (resolved && (!preserveExplicitIWAD || loading.iwadName.empty()))
	{
		loading.iwadName = *resolved;
		global::recent.addIWAD(*resolved);
		if (!global::home_dir.empty())
			global::recent.save(global::home_dir);
	}
	return session;
}


void Instance::Project_AdoptSession(
		const std::optional<ProjectSession> &session)
{
	projectSession_ = session;
	navigatorSelection_ = session ? session->navigatorMap : SString{};
}


void Instance::Project_SaveSession() noexcept
{
	const std::shared_ptr<Wad_file> package = wad.master.editWad();
	if (!package || package->PathName().empty() || !loaded.project.isExplicit())
		return;
	try
	{
		ProjectSession session = M_MakeProjectSession(package->PathName(),
				loaded.iwadName, loaded.gameName, loaded.levelName,
				navigatorSelection_);
		M_SaveProjectSession(package->PathName(), session);
		projectSession_ = std::move(session);
		global::recent.addRecentProject(package->PathName(), loaded.levelName,
				global::home_dir);
	}
	catch (const std::exception &error)
	{
		gLog.printf("WARNING: could not save project session sidecar: %s\n",
				error.what());
	}
}


void Instance::Project_SetNavigatorSelection(const SString &mapName) noexcept
{
	if (mapName.good() && M_IsValidProjectMapName(mapName))
		navigatorSelection_ = mapName.asUpper();
	else
		navigatorSelection_.clear();
	Project_SaveSession();
}

static Document makeFreshDocument(Instance &inst, const ConfigData &config, MapFormat levelFormat)
{
	Document doc(inst);
	auto sec = std::make_unique<Sector>();

	sec->SetDefaults(config);
	doc.sectors.push_back(std::move(sec));

	for (int i = 0 ; i < 4 ; i++)
	{
		auto v = std::make_shared<Vertex>();

		v->SetRawX(levelFormat, (i >= 2) ? 256 : -256);
		v->SetRawY(levelFormat, (i==1 || i==2) ? 256 :-256);
		doc.vertices.push_back(std::move(v));

		auto sd = std::make_shared<SideDef>();
		sd->SetDefaults(config, false);
		doc.sidedefs.push_back(std::move(sd));

		auto ld = std::make_shared<LineDef>();
		ld->start = i;
		ld->end   = (i+1) % 4;
		ld->flags = MLF_Blocking;
		ld->right = i;
		doc.linedefs.push_back(std::move(ld));
	}

	for (int pl = 1 ; pl <= 4 ; pl++)
	{
		auto th = std::make_unique<Thing>();

		th->type  = pl;
		th->angle = 90;

		th->SetRawX(levelFormat, (pl == 1) ? 0 : (pl - 3) * 48);
		th->SetRawY(levelFormat, (pl == 1) ? 48 : (pl == 3) ? -48 : 0);
		doc.things.push_back(std::move(th));
	}

	doc.CalculateLevelBounds();

	return doc;
}

void Instance::FreshLevel()
{
	level.clear();
	level = makeFreshDocument(*this, conf, loaded.levelFormat);

	Subdiv_InvalidateAll();

	// reset various editor state, so nothing from the previous
	// map is still rendered or shown in the panels
	Editor_ClearAction();
	Selection_InvalidateLast();
	edit.Selected->clear_all();
	edit.highlight.clear();

	if (main_win)
	{
		main_win->UpdateTotals(level);
		main_win->InvalidatePanelObj();
	}

	// place the 3D camera at the new map's player start,
	// so entering 3D view does not leave it in the void
	Render3D_Setup();

	ZoomWholeMap();

	edit.defaultState();
}


static void updateLoading(const UI_ProjectSetup::Result &result, LoadingData &loading)
{
	loading.gameName = result.game;
	loading.portName = result.port;
	const fs::path *iwad = global::recent.queryIWAD(result.game);
	SYS_ASSERT(iwad);
	loading.iwadName = *iwad;
	loading.levelFormat = result.mapFormat;
	loading.udmfNamespace = result.nameSpace;
	loading.resourceList = result.resources;

	if (loading.project.isExplicit())
	{
		loading.project.campaign = result.campaign;
		if (result.campaign == CampaignMode::custom)
			loading.project.mapSlots = result.mapSlots;
		M_ReconcileCampaignMetadata(loading.project);
	}
}

void Instance::Project_ApplyChanges(const UI_ProjectSetup::Result &result) noexcept(false)
{
	LoadingData loading = loaded;
	updateLoading(result, loading);
	Fl::wait(0.1);
	Main_LoadResources(loading);
	if (loaded.project.isExplicit() && loaded.project.campaign != CampaignMode::custom &&
			wad.master.gameWad())
	{
		M_RefreshProjectMapSlots(loaded.project, *wad.master.gameWad());
	}
	documentCache.updateLoadingContext(loaded);
	Project_MarkMetadataDirty();
	Fl::wait(0.1);
}


void Instance::CMD_ManageProject()
{
	try
	{
		UI_ProjectSetup dialog(*this, false /* new_project */, false /* is_startup */);
		std::optional<UI_ProjectSetup::Result> result = dialog.Run();

		if (result)
		{
			Project_ApplyChanges(*result);
		}
	}
	catch(const std::runtime_error &e)
	{
		DLG_ShowError(false, "Error managing project: %s", e.what());
	}
}


void Instance::CMD_NewProject()
{
	NewResources newres{};
	ConfigData backupConfig = conf;
	LoadingData backupLoading = loaded;
	WadData backupWadData = wad;
	const bool discardOldRecovery = Project_HasChanges() && !recoveryDeferred_;
	const fs::path oldPackagePath = wad.master.editWad() ?
			wad.master.editWad()->PathName() : fs::path{};
	std::optional<Document> backupDoc;
	try
	{
		if (!Project_ConfirmClose("create a new project"))
			return;

		/* Collect and review all project settings before creating anything. */
		// TODO: new instance
		UI_ProjectSetup dialog(*this, true /* new_project */, false /* is_startup */);

		std::optional<UI_ProjectSetup::Result> result = dialog.Run();

		if (!result)
		{
			return;
		}

		const fs::path normalizedDestination = M_NormalizeProjectDestination(
				result->destination, result->package);
		ProjectDestinationValidation destination = M_ValidateProjectDestination(
				normalizedDestination, result->package,
				global::recent.hasIwadByPath(normalizedDestination));
		if (!destination.valid())
		{
			DLG_Notify("Cannot create the project:\n\n%s",
					destination.message.c_str());
			return;
		}
		const fs::path &filename = destination.destination;


		LoadingData loading = loaded;
		updateLoading(*result, loading);
		newres = loadResources(loading, wad, nullptr);

		const std::shared_ptr<Wad_file> &gameWad = newres.waddata.master.gameWad();
		if (!gameWad || !gameWad->IsReadOnly())
			ThrowException("The selected IWAD could not be opened read-only.");

		newres.loading.project = M_NewProjectMetadata(filename,
				result->package, result->campaign, *gameWad);
		if (result->campaign == CampaignMode::custom)
		{
			newres.loading.project.mapSlots = result->mapSlots;
			M_ReconcileCampaignMetadata(newres.loading.project);
		}
		if (newres.loading.project.mapSlots.empty())
			ThrowException("The selected IWAD does not contain a valid map slot.");

		const SString map_name = newres.loading.project.mapSlots.front();

		gLog.printf("Creating New File : %s in %s\n", map_name.c_str(),
				reinterpret_cast<const char *>(filename.u8string().c_str()));


		std::shared_ptr<PackageBackend> backend =
				M_CreatePackageBackend(filename, result->package);
		std::shared_ptr<Wad_file> wad = backend ? backend->openEditable() : nullptr;

		if (!wad)
		{
			DLG_Notify("Unable to create the new project package.");
			return;
		}

		backupDoc = std::move(level);
		level = makeFreshDocument(*this, newres.config, newres.loading.levelFormat);
		conf = std::move(newres.config);
		loaded = std::move(newres.loading);
		if(main_win)
			testmap::updateMenuName(main_win->menu_bar, loaded);

		this->wad = std::move(newres.waddata);

		SaveLevel(loaded, map_name, *wad, false);
		this->wad.master.ReplaceEditWad(wad);
		Project_ClearDocumentCache();
		ConfirmLevelSaveSuccess(loaded, *wad);
		Project_SaveSession();
		UpdateViewOnResources();
		if (discardOldRecovery && !oldPackagePath.empty())
			RecoveryStore(global::cache_dir / "recovery").discard(oldPackagePath);
		Project_ResetAutosaveTimer();

		RedrawMap();
	}
	catch(const std::runtime_error &e)
	{
		conf = std::move(backupConfig);
		loaded = std::move(backupLoading);
		wad = std::move(backupWadData);
		if(backupDoc)
			level = std::move(backupDoc.value());
		if(main_win)
			testmap::updateMenuName(main_win->menu_bar, loaded);

		DLG_ShowError(false, "Could not create new project: %s", e.what());
	}
}


bool Instance::MissingIWAD_Dialog()
{
	UI_ProjectSetup dialog(*this, false /* new_project */, true /* is_startup */);

	std::optional<UI_ProjectSetup::Result> result = dialog.Run();

	if (result)
	{
		loaded.gameName = result->game;
		SYS_ASSERT(!loaded.gameName.empty());

		const fs::path *iwad = global::recent.queryIWAD(loaded.gameName);
		SYS_ASSERT(!!iwad);
		loaded.iwadName = *iwad;

		if(main_win)
			testmap::updateMenuName(main_win->menu_bar, loaded);
	}

	return result.has_value();
}


void Instance::CMD_FreshMap()
{
	std::optional<Document> backupDoc;
	try
	{
		if (!wad.master.editWad())
		{
			DLG_Notify("Cannot create a fresh map unless editing a PWAD.");
			return;
		}

		if (wad.master.editWad()->IsReadOnly())
		{
			DLG_Notify("Cannot create a fresh map: file is read-only.");
			return;
		}

		if (!level.Main_ConfirmQuit("create a fresh map"))
			return;

		SString map_name;
		{
			UI_ChooseMap dialog(loaded.levelName.c_str());

			dialog.PopulateButtons(static_cast<char>(toupper(loaded.levelName[0])), wad.master.editWad().get());

			map_name = dialog.Run();
		}

		// cancelled?
		if (map_name.empty())
			return;

		// would this replace an existing map?
		if (wad.master.editWad()->LevelFind(map_name) >= 0)
		{
			if (DLG_Confirm({ "Cancel", "&Overwrite" },
				overwrite_message, "current") <= 0)
			{
				return;
			}
		}

		M_BackupWad(wad.master.editWad().get());
		documentCache.erase(map_name);

		gLog.printf("Created NEW map : %s\n", map_name.c_str());

		// TODO: make this allow running another level
		backupDoc = std::move(level);
		FreshLevel();

		// save it now : sets Level_name and window title
		SaveLevelAndUpdateWindow(loaded, map_name, *wad.master.editWad(), false);
		Project_SaveSession();
		Project_SynchronizeRecoveryAfterSave();
	}
	catch (const std::runtime_error& e)
	{
		if(backupDoc)
			level = std::move(*backupDoc);
		DLG_ShowError(false, "Could not create fresh map: %s", e.what());
	}

}


void Instance::CMD_CreateNextMap()
{
	if (!loaded.project.isExplicit())
	{
		DLG_Notify("Create Next Map requires an explicit Heresy Editor project.\n\n"
				"Use Fresh Map for an ordinary WAD.");
		return;
	}

	std::shared_ptr<Wad_file> editWad = wad.master.editWad();
	if (!editWad || editWad->IsReadOnly())
	{
		DLG_Notify("The project WAD is not available for writing.");
		return;
	}

	std::optional<SString> nextMap = M_NextProjectMap(loaded.project,
			loaded.levelName);
	if (!nextMap)
	{
		const CampaignMapDefinition *definition =
				loaded.project.mapDefinition(loaded.levelName);
		if (definition && definition->normalExit &&
				definition->normalExit->empty())
		{
			DLG_Notify("The normal route from %s ends the campaign.",
					loaded.levelName.c_str());
		}
		else
		{
			DLG_Notify("There is no normal-route map after %s in this campaign.",
					loaded.levelName.c_str());
		}
		return;
	}

	if (editWad->LevelFind(*nextMap) >= 0)
	{
		try
		{
			if (Project_SwitchMap(editWad, *nextMap))
				Status_Set("Opened existing %s", nextMap->c_str());
		}
		catch (const std::runtime_error &error)
		{
			DLG_ShowError(false, "Could not open %s: %s", nextMap->c_str(),
					error.what());
		}
		return;
	}

	Project_CreateMap(*nextMap);
}


//------------------------------------------------------------------------
//  LOADING CODE
//------------------------------------------------------------------------

static void UpperCaseShortStr(char *buf, int max_len)
{
	for (int i = 0 ; (i < max_len) && buf[i] ; i++)
	{
		buf[i] = static_cast<char>(toupper(buf[i]));
	}
}


const Lump_c *Load_LookupAndSeek(int loading_level, const Wad_file *load_wad, const char *name)
{
	int idx = load_wad->LevelLookupLump(loading_level, name);

	if (idx < 0)
		return NULL;

	const Lump_c *lump = load_wad->GetLump(idx);

	return lump;
}


void Document::LoadVertices(int loading_level, const Wad_file *load_wad)
{
	const Lump_c *lump = Load_LookupAndSeek(loading_level, load_wad, "VERTEXES");
	if (! lump)
		ThrowException("No vertex lump!\n");

	int count = lump->Length() / sizeof(raw_vertex_t);

# if DEBUG_LOAD
	PrintDebug("GetVertices: num = %d\n", count);
# endif

	vertices.reserve(count);
	LumpInputStream stream(*lump);

	for (int i = 0 ; i < count ; i++)
	{
		raw_vertex_t raw;

		if (! stream.read(&raw, sizeof(raw)))
			ThrowException("Error reading vertices.\n");

		auto vert = std::make_unique<Vertex>();

		vert->xf = LE_S16(raw.x);
		vert->yf = LE_S16(raw.y);

		vertices.push_back(std::move(vert));
	}
}


void Document::LoadSectors(int loading_level, const Wad_file *load_wad)
{
	const Lump_c *lump = Load_LookupAndSeek(loading_level, load_wad, "SECTORS");
	if (! lump)
		ThrowException("No sector lump!\n");

	int count = lump->Length() / sizeof(raw_sector_t);

# if DEBUG_LOAD
	PrintDebug("GetSectors: num = %d\n", count);
# endif

	sectors.reserve(count);
	LumpInputStream stream(*lump);

	for (int i = 0 ; i < count ; i++)
	{
		raw_sector_t raw;

		if (! stream.read(&raw, sizeof(raw)))
			ThrowException("Error reading sectors.\n");

		auto sec = std::make_shared<Sector>();

		sec->floorh = LE_S16(raw.floorh);
		sec->ceilh  = LE_S16(raw.ceilh);

		UpperCaseShortStr(raw.floor_tex, 8);
		UpperCaseShortStr(raw. ceil_tex, 8);

		sec->floor_tex = BA_InternaliseString(SString(raw.floor_tex, 8));
		sec->ceil_tex  = BA_InternaliseString(SString(raw.ceil_tex,  8));

		sec->light = LE_U16(raw.light);
		sec->type  = LE_U16(raw.type);
		sec->tag   = LE_S16(raw.tag);

		sectors.push_back(std::move(sec));
	}
}


void Document::CreateFallbackSector(const ConfigData &config)
{
	gLog.printf("Creating a fallback sector.\n");

	auto sec = std::make_shared<Sector>();

	sec->SetDefaults(config);

	sectors.push_back(std::move(sec));
}

void Document::CreateFallbackSideDef(const ConfigData &config)
{
	// we need a valid sector too!
	if (numSectors() == 0)
		CreateFallbackSector(config);

	gLog.printf("Creating a fallback sidedef.\n");

	auto sd = std::make_shared<SideDef>();

	sd->SetDefaults(config, false);

	sidedefs.push_back(std::move(sd));
}

void Document::CreateFallbackVertices()
{
	gLog.printf("Creating two fallback vertices.\n");

	auto v1 = std::make_unique<Vertex>();
	auto v2 = std::make_unique<Vertex>();

	v1->xf = -777;
	v1->yf = -777;

	v2->xf = 555;
	v2->yf = 555;

	vertices.push_back(std::move(v1));
	vertices.push_back(std::move(v2));
}

void Document::ValidateSidedefRefs(LineDef & ld, int num, const ConfigData &config, BadCount &bad)
{
	if (ld.right >= numSidedefs() || ld.left >= numSidedefs())
	{
		gLog.printf("WARNING: linedef #%d has invalid sidedefs (%d / %d)\n",
				  num, ld.right, ld.left);

		bad.sidedef_refs++;

		// ensure we have a usable sidedef
		if (numSidedefs() == 0)
			CreateFallbackSideDef(config);

		if (ld.right >= numSidedefs())
			ld.right = 0;

		if (ld.left >= numSidedefs())
			ld.left = 0;
	}
}

void Document::ValidateVertexRefs(LineDef &ld, int num, BadCount &bad)
{
	if (ld.start >= numVertices() || ld.end >= numVertices() ||
	    ld.start == ld.end)
	{
		gLog.printf("WARNING: linedef #%d has invalid vertices (%d -> %d)\n",
		          num, ld.start, ld.end);

		bad.linedef_count++;

		// ensure we have a valid vertex
		if (numVertices() < 2)
			CreateFallbackVertices();

		ld.start = 0;
		ld.end   = numVertices() - 1;
	}
}

void Document::ValidateSectorRef(SideDef &sd, int num, const ConfigData &config, BadCount &bad)
{
	if (sd.sector >= numSectors())
	{
		gLog.printf("WARNING: sidedef #%d has invalid sector (%d)\n",
		          num, sd.sector);

		bad.sector_refs++;

		// ensure we have a valid sector
		if (numSectors() == 0)
			CreateFallbackSector(config);

		sd.sector = 0;
	}
}


void Document::LoadHeader(int loading_level, const Wad_file &load_wad)
{
	const Lump_c *lump = load_wad.GetLump(load_wad.LevelHeader(loading_level));
	headerData = lump->getData();
}


void Document::LoadBehavior(int loading_level, const Wad_file *load_wad)
{
	// IOANCH 9/2015: support Hexen maps
	const Lump_c *lump = Load_LookupAndSeek(loading_level, load_wad, "BEHAVIOR");
	if (! lump)
		ThrowException("No BEHAVIOR lump!\n");

	behaviorData = lump->getData();
}


void Document::LoadScripts(int loading_level, const Wad_file *load_wad)
{
	// the SCRIPTS lump is usually absent
	const Lump_c *lump = Load_LookupAndSeek(loading_level, load_wad, "SCRIPTS");
	if (! lump)
		return;

	scriptsData = lump->getData();
}


void Document::LoadThings(int loading_level, const Wad_file *load_wad)
{
	const Lump_c *lump = Load_LookupAndSeek(loading_level, load_wad, "THINGS");
	if (! lump)
		ThrowException("No things lump!\n");

	int count = lump->Length() / sizeof(raw_thing_t);

# if DEBUG_LOAD
	PrintDebug("GetThings: num = %d\n", count);
# endif

	LumpInputStream stream(*lump);

	for (int i = 0 ; i < count ; i++)
	{
		raw_thing_t raw;

		if (! stream.read(&raw, sizeof(raw)))
			ThrowException("Error reading things.\n");

		auto th = std::make_unique<Thing>();

		th->xf = LE_S16(raw.x);
		th->yf = LE_S16(raw.y);

		th->angle   = LE_U16(raw.angle);
		th->type    = LE_U16(raw.type);
		th->options = LE_U16(raw.options);

		things.push_back(std::move(th));
	}
}


// IOANCH 9/2015
void Document::LoadThings_Hexen(int loading_level, const Wad_file *load_wad)
{
	const Lump_c *lump = Load_LookupAndSeek(loading_level, load_wad, "THINGS");
	if (! lump)
		ThrowException("No things lump!\n");

	int count = lump->Length() / sizeof(raw_hexen_thing_t);

# if DEBUG_LOAD
	PrintDebug("GetThings: num = %d\n", count);
# endif

	LumpInputStream stream(*lump);

	for (int i = 0; i < count; ++i)
	{
		raw_hexen_thing_t raw;

		if (! stream.read(&raw, sizeof(raw)))
			ThrowException("Error reading things.\n");

		auto th = std::make_unique<Thing>();

		th->tid = LE_S16(raw.tid);
		th->xf = LE_S16(raw.x);
		th->yf = LE_S16(raw.y);
		th->hf = LE_S16(raw.height);

		th->angle = LE_U16(raw.angle);
		th->type = LE_U16(raw.type);
		th->options = LE_U16(raw.options);

		th->special = raw.special;
		th->arg1 = raw.args[0];
		th->arg2 = raw.args[1];
		th->arg3 = raw.args[2];
		th->arg4 = raw.args[3];
		th->arg5 = raw.args[4];

		things.push_back(std::move(th));
	}
}


void Document::LoadSideDefs(int loading_level, const Wad_file *load_wad, const ConfigData &config, BadCount &bad)
{
	const Lump_c *lump = Load_LookupAndSeek(loading_level, load_wad, "SIDEDEFS");
	if(!lump)
		ThrowException("No sidedefs lump!\n");

	int count = lump->Length() / sizeof(raw_sidedef_t);

# if DEBUG_LOAD
	PrintDebug("GetSidedefs: num = %d\n", count);
# endif

	LumpInputStream stream(*lump);

	for (int i = 0 ; i < count ; i++)
	{
		raw_sidedef_t raw;

		if (! stream.read(&raw, sizeof(raw)))
			ThrowException("Error reading sidedefs.\n");

		auto sd = std::make_shared<SideDef>();

		sd->x_offset = LE_S16(raw.x_offset);
		sd->y_offset = LE_S16(raw.y_offset);

		UpperCaseShortStr(raw.upper_tex, 8);
		UpperCaseShortStr(raw.lower_tex, 8);
		UpperCaseShortStr(raw.  mid_tex, 8);

		sd->upper_tex = BA_InternaliseString(SString(raw.upper_tex, 8));
		sd->lower_tex = BA_InternaliseString(SString(raw.lower_tex, 8));
		sd->  mid_tex = BA_InternaliseString(SString(raw.  mid_tex, 8));

		sd->sector = LE_U16(raw.sector);

		ValidateSectorRef(*sd, i, config, bad);

		sidedefs.push_back(std::move(sd));
	}
}


void Document::LoadLineDefs(int loading_level, const Wad_file *load_wad, const ConfigData &config, BadCount &bad)
{
	const Lump_c *lump = Load_LookupAndSeek(loading_level, load_wad, "LINEDEFS");
	if (! lump)
		ThrowException("No linedefs lump!\n");

	int count = lump->Length() / sizeof(raw_linedef_t);

# if DEBUG_LOAD
	PrintDebug("GetLinedefs: num = %d\n", count);
# endif

	if (count == 0)
		return;

	LumpInputStream stream(*lump);

	for (int i = 0 ; i < count ; i++)
	{
		raw_linedef_t raw;

		if (! stream.read(&raw, sizeof(raw)))
			ThrowException("Error reading linedefs.\n");

		auto ld = std::make_shared<LineDef>();

		ld->start = LE_U16(raw.start);
		ld->end   = LE_U16(raw.end);

		ld->flags = LE_U16(raw.flags);
		ld->type  = LE_U16(raw.type);
		ld->arg1   = LE_S16(raw.tag);

		ld->right = LE_U16(raw.right);
		ld->left  = LE_U16(raw.left);

		if (ld->right == 0xFFFF) ld->right = -1;
		if (ld-> left == 0xFFFF) ld-> left = -1;

		ValidateVertexRefs(*ld, i, bad);
		ValidateSidedefRefs(*ld, i, config, bad);

		linedefs.push_back(std::move(ld));
	}
}


// IOANCH 9/2015
void Document::LoadLineDefs_Hexen(int loading_level, const Wad_file *load_wad, const ConfigData &config, BadCount &bad)
{
	const Lump_c *lump = Load_LookupAndSeek(loading_level, load_wad, "LINEDEFS");
	if (! lump)
		ThrowException("No linedefs lump!\n");

	int count = lump->Length() / sizeof(raw_hexen_linedef_t);

# if DEBUG_LOAD
	PrintDebug("GetLinedefs: num = %d\n", count);
# endif

	if (count == 0)
		return;

	LumpInputStream stream(*lump);

	for (int i = 0 ; i < count ; i++)
	{
		raw_hexen_linedef_t raw;

		if (! stream.read(&raw, sizeof(raw)))
			ThrowException("Error reading linedefs.\n");

		auto ld = std::make_shared<LineDef>();

		ld->start = LE_U16(raw.start);
		ld->end   = LE_U16(raw.end);

		ld->flags = LE_U16(raw.flags);
		ld->type = raw.type;
		ld->arg1  = raw.args[0];
		ld->arg2 = raw.args[1];
		ld->arg3 = raw.args[2];
		ld->arg4 = raw.args[3];
		ld->arg5 = raw.args[4];

		ld->right = LE_U16(raw.right);
		ld->left  = LE_U16(raw.left);

		if (ld->right == 0xFFFF) ld->right = -1;
		if (ld-> left == 0xFFFF) ld-> left = -1;

		ValidateVertexRefs(*ld, i, bad);
		ValidateSidedefRefs(*ld, i, config, bad);

		linedefs.push_back(std::move(ld));
	}
}


void Document::RemoveUnusedVerticesAtEnd()
{
	if (numVertices() == 0)
		return;

	bitvec_c used_verts(numVertices());

	for (const auto &linedef : linedefs)
	{
		used_verts.set(linedef->start);
		used_verts.set(linedef->end);
	}

	int new_count = numVertices();

	while (new_count > 2 && !used_verts.get(new_count-1))
		new_count--;

	// we directly modify the vertex array here (which is not
	// normally kosher, but level loading is a special case).
	if (new_count < numVertices())
	{
		gLog.printf("Removing %d unused vertices at end\n", numVertices() - new_count);

		vertices.resize(new_count);
	}
}


static void ShowLoadProblem(const BadCount &bad)
{
	gLog.printf("Map load problems:\n");
	gLog.printf("   %d linedefs with bad vertex refs\n", bad.linedef_count);
	gLog.printf("   %d linedefs with bad sidedef refs\n", bad.sidedef_refs);
	gLog.printf("   %d sidedefs with bad sector refs\n", bad.sector_refs);

	SString message;

	if (bad.linedef_count > 0)
	{
		message = SString::printf("Found %d linedefs with bad vertex references.\n"
			"These references have been replaced.",
			bad.linedef_count);
	}
	else
	{
		message = SString::printf("Found %d bad sector refs, %d bad sidedef refs.\n"
			"These references have been replaced.",
			bad.sector_refs, bad.sidedef_refs);
	}

	DLG_Notify("Map validation report:\n\n%s", message.c_str());
}

std::vector<SString> Instance::Project_DirtyMapNames() const
{
	std::vector<SString> result = documentCache.dirtyMapNames();
	if (level.hasChanges() && loaded.levelName.good())
		result.push_back(loaded.levelName.asUpper());
	std::sort(result.begin(), result.end());
	result.erase(std::unique(result.begin(), result.end()), result.end());
	return result;
}

bool Instance::Project_ConfirmClose(const char *action) const
{
	const std::vector<SString> dirtyMaps = Project_DirtyMapNames();
	if (dirtyMaps.empty() && !projectMetadataDirty_)
		return true;

	SString secondButton = SString::printf("&%s", action);
	if (secondButton.size() >= 2)
	{
		secondButton[1] = static_cast<char>(safe_toupper(secondButton[1]));
		size_t space = secondButton.find(' ');
		if (space != SString::npos)
			secondButton.erase(space, SString::npos);
	}

	if (dirtyMaps.empty())
	{
		return DLG_Confirm({ "Cancel", secondButton },
				"You have unsaved project settings.  Do you really want to %s?",
				action) == 1;
	}

	return DLG_Confirm({ "Cancel", secondButton },
			"You have unsaved changes in %d project map%s%s.  "
			"Do you really want to %s?",
			static_cast<int>(dirtyMaps.size()),
			dirtyMaps.size() == 1 ? "" : "s",
			projectMetadataDirty_ ? " and the project settings" : "", action) == 1;
}

void Instance::Project_ResetAutosaveTimer() noexcept
{
	autosaveInterval_ = std::clamp(config::autosave_interval, 0, 1440);
	if (autosaveInterval_ <= 0)
	{
		autosaveDeadline_ = {};
		return;
	}
	autosaveDeadline_ = std::chrono::steady_clock::now() +
			std::chrono::minutes(autosaveInterval_);
}

void Instance::Project_AutosaveTick() noexcept
{
	const int configured = std::clamp(config::autosave_interval, 0, 1440);
	if (configured != autosaveInterval_ || autosaveDeadline_ ==
			std::chrono::steady_clock::time_point{})
	{
		Project_ResetAutosaveTimer();
		return;
	}
	if (configured <= 0 || std::chrono::steady_clock::now() < autosaveDeadline_)
		return;

	// Schedule the next deadline before doing I/O so an error cannot create a
	// tight retry loop in the UI thread.
	Project_ResetAutosaveTimer();
	if (Project_HasChanges())
		Project_WriteAutosave(true);
}

void Instance::Project_DiscardRecovery() noexcept
{
	const std::shared_ptr<Wad_file> package = wad.master.editWad();
	if (package && !package->PathName().empty())
		RecoveryStore(global::cache_dir / "recovery").discard(package->PathName());
	recoveryDeferred_ = false;
}

void Instance::Project_SynchronizeRecoveryAfterSave() noexcept
{
	if (Project_HasChanges())
		Project_WriteAutosave();
	else if (!recoveryDeferred_)
		Project_DiscardRecovery();
}

bool Instance::Project_WriteAutosave(bool notify) noexcept
{
	const std::shared_ptr<Wad_file> package = wad.master.editWad();
	if (!package || package->PathName().empty())
		return false;
	if (!Project_HasChanges())
	{
		if (!recoveryDeferred_)
			Project_DiscardRecovery();
		return true;
	}

	fs::path staging;
	try
	{
		RecoveryStore store(global::cache_dir / "recovery");
		const std::optional<RecoverySnapshot> previous = recoveryDeferred_ ?
				store.latest(package->PathName()) : std::nullopt;
		staging = store.beginSnapshot(package->PathName());

		if (previous && !projectMetadataDirty_)
		{
			std::error_code error;
			fs::copy_file(previous->contextFile, staging / "context.wad",
					fs::copy_options::none, error);
			if (error)
				throw fs::filesystem_error("Could not retain deferred recovery context",
						previous->contextFile, staging / "context.wad", error);
		}
		else
		{
			std::shared_ptr<Wad_file> context = Wad_file::Open(
					staging / "context.wad", WadOpenMode::write);
			if (!context)
				ThrowException("Could not create the recovery project context.");
			loaded.writeEurekaLump(*context, true);
			context->writeToDisk();
		}

		std::vector<CachedMapDocument *> cached = documentCache.dirtyDocuments();
		std::sort(cached.begin(), cached.end(),
				[](const CachedMapDocument *left, const CachedMapDocument *right)
				{
					return left->mapName < right->mapName;
				});

		std::vector<RecoveryMapFile> maps;
		std::set<SString> currentMaps;
		auto writeMap = [this, &staging, &maps](const SString &mapName,
				const LoadingData &mapContext, const Document &document)
		{
			const fs::path fileName = SString::printf("map-%zu.wad", maps.size()).c_str();
			std::shared_ptr<Wad_file> recoveryWad = Wad_file::Open(
					staging / fileName, WadOpenMode::write);
			if (!recoveryWad)
				ThrowException("Could not create a recovery map package.");
			LoadingData recoveryLoading = mapContext;
			StoreDocumentInWad(recoveryLoading, mapName, *recoveryWad, document,
					true /* autosave never blocks on node building */);
			recoveryWad->writeToDisk();
			maps.push_back({ mapName.asUpper(), fileName });
		};

		for (const CachedMapDocument *entry : cached)
		{
			writeMap(entry->mapName, entry->loading, entry->document);
			currentMaps.insert(entry->mapName.asUpper());
		}
		if (level.hasChanges())
		{
			writeMap(loaded.levelName, loaded, level);
			currentMaps.insert(loaded.levelName.asUpper());
		}

		// "Later" must not make an older recovery generation inaccessible.
		// Carry its still-unloaded maps forward, while current dirty documents
		// with the same names take precedence.
		if (previous)
		{
			for (const RecoveryMapFile &map : previous->maps)
			{
				if (currentMaps.count(map.mapName.asUpper()) > 0)
					continue;
				const fs::path fileName =
						SString::printf("map-%zu.wad", maps.size()).c_str();
				std::error_code error;
				fs::copy_file(map.fileName, staging / fileName,
						fs::copy_options::none, error);
				if (error)
					throw fs::filesystem_error("Could not retain deferred recovery map",
							map.fileName, staging / fileName, error);
				maps.push_back({ map.mapName.asUpper(), fileName });
			}
		}

		store.commitSnapshot(package->PathName(), staging, loaded.levelName, maps);
		gLog.printf("Autosaved project recovery: %zu changed map%s\n",
				maps.size(), maps.size() == 1 ? "" : "s");
		if (notify)
			Status_Set("Autosaved project recovery (%zu map%s)", maps.size(),
					maps.size() == 1 ? "" : "s");
		return true;
	}
	catch (const std::exception &error)
	{
		if (!staging.empty())
		{
			std::error_code ignored;
			fs::remove_all(staging, ignored);
		}
		gLog.printf("WARNING: could not autosave project recovery: %s\n",
				error.what());
		if (notify)
			Status_Set("Could not autosave recovery; see the log");
		return false;
	}
}

bool Instance::Project_CheckRecovery()
{
	const std::shared_ptr<Wad_file> package = wad.master.editWad();
	if (!package || package->PathName().empty())
		return false;

	RecoveryStore store(global::cache_dir / "recovery");
	std::optional<RecoverySnapshot> snapshot;
	try
	{
		snapshot = store.latest(package->PathName());
	}
	catch (const std::exception &error)
	{
		gLog.printf("WARNING: could not inspect project recovery: %s\n",
				error.what());
		return false;
	}
	if (!snapshot)
	{
		recoveryDeferred_ = false;
		return false;
	}

	char timestamp[64] = "an unknown time";
	const std::time_t created = static_cast<std::time_t>(snapshot->createdAt);
	if (const std::tm *local = std::localtime(&created))
		std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", local);

	const int choice = DLG_Confirm({ "&Later", "&Recover", "&Discard" },
			"Heresy Editor found recovery data from %s containing %d changed "
			"map%s.%s\n\nRecover it now?",
			timestamp, static_cast<int>(snapshot->maps.size()),
			snapshot->maps.size() == 1 ? "" : "s",
			snapshot->packageChanged ?
					"\n\nThe project package has changed since that autosave. "
					"Review recovered maps before saving." : "");
	if (choice == 0)
	{
		recoveryDeferred_ = true;
		Status_Set("Recovery kept for later; new autosaves will retain it");
		return false;
	}
	if (choice == 2)
	{
		Project_DiscardRecovery();
		Status_Set("Discarded project recovery");
		return false;
	}
	recoveryDeferred_ = true;

	try
	{
		std::shared_ptr<Wad_file> contextWad = Wad_file::Open(
				snapshot->contextFile, WadOpenMode::read);
		if (!contextWad)
			ThrowException("Could not open the recovery project context.");

		LoadingData recoveryContext = loaded;
		if (!recoveryContext.parseEurekaLump(global::home_dir,
				global::old_linux_home_and_cache_dir, global::install_dir,
				global::recent, contextWad.get()))
		{
			return false;
		}
		recoveryContext.levelName = loaded.levelName;

		NewResources recoveryResources = loadResources(recoveryContext, wad, package);
		const ConfigData previousConfig = conf;
		const LoadingData previousLoading = loaded;
		const WadData previousWads = wad;
		std::vector<std::pair<SString, NewDocument>> documents;
		int activeIndex = -1;
		try
		{
			conf = std::move(recoveryResources.config);
			loaded = std::move(recoveryResources.loading);
			wad = std::move(recoveryResources.waddata);
			wad.master.ReplaceEditWad(package);

			for (const RecoveryMapFile &map : snapshot->maps)
			{
				std::shared_ptr<Wad_file> mapWad = Wad_file::Open(map.fileName,
						WadOpenMode::read);
				if (!mapWad)
					ThrowException("Could not open recovered map %s.", map.mapName.c_str());
				int levelNumber = mapWad->LevelFind(map.mapName);
				if (levelNumber < 0)
					ThrowException("Recovered map %s is missing.", map.mapName.c_str());
				NewDocument recovered = openDocument(loaded, *mapWad, levelNumber);
				recovered.loading.levelName = map.mapName.asUpper();
				recovered.doc.markRecovered();
				documents.emplace_back(map.mapName.asUpper(), std::move(recovered));
			}
			for (size_t index = 0; index < documents.size(); ++index)
			{
				if (documents[index].first.noCaseEqual(snapshot->activeMap))
				{
					activeIndex = static_cast<int>(index);
					break;
				}
			}
			if (activeIndex < 0 && documents.size() > documentCache.capacity())
				activeIndex = 0;
			const size_t cachedCount = documents.size() -
					(activeIndex >= 0 ? 1u : 0u);
			if (cachedCount > documentCache.capacity())
				ThrowException("Too many recovered maps are resident at once.");
		}
		catch (...)
		{
			conf = previousConfig;
			loaded = previousLoading;
			wad = previousWads;
			throw;
		}

		documentCache.clear();
		for (size_t index = 0; index < documents.size(); ++index)
		{
			if (static_cast<int>(index) == activeIndex)
				continue;
			NewDocument &recovered = documents[index].second;
			const bool stored = documentCache.store(documents[index].first,
					std::move(recovered.doc), recovered.loading);
			SYS_ASSERT(stored);
			(void)stored;
		}

		projectMetadataDirty_ = true;
		UpdateViewOnResources();
		if (activeIndex >= 0)
		{
			NewDocument &active = documents[static_cast<size_t>(activeIndex)].second;
			level = std::move(active.doc);
			loaded = std::move(active.loading);
			if (main_win)
			{
				testmap::updateMenuName(main_win->menu_bar, loaded);
				menu::setUndoDetail(main_win->menu_bar, level.basis.undoMenuName());
				menu::setRedoDetail(main_win->menu_bar, level.basis.redoMenuName());
			}
			refreshViewAfterLoad(active.bad, package.get(), loaded.levelName, false);
		}
		else if (main_win)
		{
			main_win->SetTitle(package->PathName().u8string(), loaded.levelName,
					false);
		}

		Status_Set("Recovered project (%zu changed map%s); use Save Project",
				documents.size(), documents.size() == 1 ? "" : "s");
		recoveryDeferred_ = false;
		Project_ResetAutosaveTimer();
		return true;
	}
	catch (const std::exception &error)
	{
		DLG_ShowError(false, "Could not recover the project: %s\n\nThe recovery "
				"data has been kept for another attempt.", error.what());
		return false;
	}
}

bool Instance::Project_SwitchMap(const std::shared_ptr<Wad_file> &package,
		const SString &mapName)
{
	if (!package)
		return false;
	if (loaded.levelName.noCaseEqual(mapName))
		return true;

	const int levelNumber = package->LevelFind(mapName);
	if (levelNumber < 0)
		ThrowException("No such map: %s\n", mapName.c_str());

	std::optional<CachedMapDocument> cached = documentCache.take(mapName);
	std::optional<NewDocument> opened;
	if (!cached)
		opened = openDocument(loaded, *package, levelNumber);

	const SString previousName = loaded.levelName;
	if (previousName.good() &&
			!documentCache.store(previousName, std::move(level), loaded) &&
			level.hasChanges())
	{
		if (cached)
			documentCache.store(cached->mapName, std::move(cached->document),
					cached->loading);
		DLG_Notify("The project already has eight resident maps with unsaved "
				"changes.\n\nSave one of those maps before opening another.");
		return false;
	}

	BadCount bad{};
	if (cached)
	{
		level = std::move(cached->document);
		loaded = std::move(cached->loading);
	}
	else
	{
		bad = opened->bad;
		level = std::move(opened->doc);
		loaded = std::move(opened->loading);
	}

	if (main_win)
	{
		testmap::updateMenuName(main_win->menu_bar, loaded);
		menu::setUndoDetail(main_win->menu_bar, level.basis.undoMenuName());
		menu::setRedoDetail(main_win->menu_bar, level.basis.redoMenuName());
	}
	refreshViewAfterLoad(bad, package.get(), mapName, false);
	Project_SaveSession();
	return true;
}

bool Instance::Project_CreateMap(const SString &mapName)
{
	std::shared_ptr<Wad_file> package = wad.master.editWad();
	if (!package || package->IsReadOnly())
	{
		DLG_Notify("The current project package is not available for writing.");
		return false;
	}
	if (package->LevelFind(mapName) >= 0)
		return Project_SwitchMap(package, mapName);

	const SString previousName = loaded.levelName;
	std::optional<Document> uncachedPrevious;
	if (previousName.good() &&
			!documentCache.store(previousName, std::move(level), loaded))
	{
		if (level.hasChanges())
		{
			DLG_Notify("The project already has eight resident maps with unsaved "
					"changes.\n\nSave one of those maps before creating another.");
			return false;
		}
		uncachedPrevious.emplace(std::move(level));
	}

	const fs::path packagePath = package->PathName();
	try
	{
		M_BackupWad(package.get());
		FreshLevel();
		documentCache.erase(mapName);
		SaveLevelAndUpdateWindow(loaded, mapName, *package, false);
		Status_Set("Created %s", mapName.c_str());
		Project_SaveSession();
		Project_SynchronizeRecoveryAfterSave();
		RedrawMap();
		return true;
	}
	catch (const std::runtime_error &error)
	{
		if (uncachedPrevious)
		{
			level = std::move(*uncachedPrevious);
		}
		else if (previousName.good())
		{
			std::optional<CachedMapDocument> previous =
					documentCache.take(previousName);
			if (previous)
			{
				level = std::move(previous->document);
				loaded = std::move(previous->loading);
			}
		}

		std::shared_ptr<Wad_file> restored = M_OpenEditablePackage(packagePath);
		if (restored)
			wad.master.ReplaceEditWad(restored);
		if (main_win)
			testmap::updateMenuName(main_win->menu_bar, loaded);
		RedrawMap();
		DLG_ShowError(false, "%s could not be written, but the original project "
				"package was preserved: %s", mapName.c_str(), error.what());
		return false;
	}
}

void Instance::CMD_CampaignNavigator()
{
	std::shared_ptr<Wad_file> package = wad.master.editWad();
	if (!package)
	{
		DLG_Notify("Open or create a project package before using the "
				"Campaign Navigator.");
		return;
	}

	for (;;)
	{
		CampaignNavigatorResult result = UI_CampaignNavigator(*this).Run();
		if (result.action == CampaignNavigatorAction::none)
			return;

		if (result.action == CampaignNavigatorAction::editMetadata)
		{
			const auto before = loaded.project.serializedFields();
			SString error;
			if (!M_SetCampaignMapDefinition(loaded.project,
					result.mapDefinition, &error))
			{
				DLG_Notify("Invalid campaign map details:\n\n%s", error.c_str());
				continue;
			}
			if (before != loaded.project.serializedFields())
			{
				documentCache.updateLoadingContext(loaded);
				Project_MarkMetadataDirty();
				Status_Set("Updated campaign details for %s",
						result.mapName.c_str());
			}
			continue;
		}

		try
		{
			switch (result.action)
			{
				case CampaignNavigatorAction::open:
					Project_SwitchMap(package, result.mapName);
					break;

				case CampaignNavigatorAction::create:
					Project_CreateMap(result.mapName);
					break;

				case CampaignNavigatorAction::duplicate:
					if (Project_SwitchMap(package, result.mapName))
						CMD_CopyMap();
					break;

				case CampaignNavigatorAction::rename:
					if (Project_SwitchMap(package, result.mapName))
						CMD_RenameMap();
					break;

				case CampaignNavigatorAction::remove:
					if (Project_SwitchMap(package, result.mapName))
						CMD_DeleteMap();
					break;

				case CampaignNavigatorAction::none:
				case CampaignNavigatorAction::editMetadata:
					break;
			}
		}
		catch (const std::runtime_error &error)
		{
			DLG_ShowError(false, "Campaign operation failed: %s", error.what());
		}
		return;
	}
}

bool Instance::Project_GenerateRuntimeMapInfo()
{
	std::shared_ptr<Wad_file> package = wad.master.editWad();
	if (!package || !loaded.project.isExplicit())
	{
		DLG_Notify("Open or create an explicit WAD or PK3 project before generating "
				"runtime MAPINFO.");
		return false;
	}
	if (package->IsReadOnly())
	{
		DLG_Notify("The current project package is read-only. Runtime MAPINFO was "
				"not generated.");
		return false;
	}

	SString error;
	std::optional<GeneratedRuntimeMapInfo> generated = M_GenerateRuntimeMapInfo(
			loaded.project, loaded.portName, loaded.gameName, &error);
	if (!generated)
	{
		DLG_Notify("Runtime MAPINFO cannot be generated:\n\n%s", error.c_str());
		return false;
	}

	const fs::path packagePath = package->PathName();
	const ProjectPackage packageType = M_ProjectPackageForPath(packagePath);
	try
	{
		RuntimeMapInfoInspection inspection = M_InspectRuntimeMapInfo(packagePath,
				packageType, *package);
		if (!inspection.canWrite())
		{
			DLG_Notify("%s", inspection.detail.c_str());
			return false;
		}

		if (!UI_ConfirmRuntimeMapInfoPreview(*generated, inspection, packagePath,
				loaded.project.mapSlots.size(), Project_HasChanges()))
		{
			return false;
		}

		// The preview may use dirty in-memory campaign metadata. Save it and all
		// resident maps first, then inspect the just-saved package again so the
		// conflict decision is adjacent to the write.
		if (Project_HasChanges() && !M_SaveProject(false))
			return false;
		package = wad.master.editWad();
		if (!package)
			throw std::runtime_error("The editable project package is no longer open.");
		inspection = M_InspectRuntimeMapInfo(packagePath, packageType, *package);
		if (!inspection.canWrite())
		{
			DLG_Notify("The package changed before generation.\n\n%s",
					inspection.detail.c_str());
			return false;
		}

		M_BackupWad(package.get());
		M_StoreManagedRuntimeMapInfo(*package, generated->text);
		package->writeToDisk();
		Status_Set("%s runtime ZMAPINFO for %zu map%s",
				inspection.state == RuntimeMapInfoState::managed ? "Updated" : "Generated",
				loaded.project.mapSlots.size(),
				loaded.project.mapSlots.size() == 1 ? "" : "s");
		Project_SaveSession();
		return true;
	}
	catch (const std::runtime_error &runtimeError)
	{
		// The in-memory aggregate may have received the generated lump before an
		// atomic disk replacement failed. Reload it to keep later saves honest.
		std::shared_ptr<Wad_file> restored = M_OpenEditablePackage(packagePath);
		if (restored)
			wad.master.ReplaceEditWad(restored);
		DLG_ShowError(false, "Runtime MAPINFO was not written; the original package "
				"was preserved: %s", runtimeError.what());
		return false;
	}
}

void Instance::CMD_GenerateRuntimeMapInfo()
{
	Project_GenerateRuntimeMapInfo();
}

void Instance::CMD_PackageMetadata()
{
	std::shared_ptr<Wad_file> package = wad.master.editWad();
	if (!package)
	{
		DLG_Notify("Open a PK3 project before inspecting package metadata.");
		return;
	}

	const fs::path &path = package->PathName();
	if (M_ProjectPackageForPath(path) != ProjectPackage::pk3)
	{
		DLG_Notify("PK3 Metadata is available for PK3 or ZIP projects.\n\n"
				"The current project is a WAD.");
		return;
	}

	SString error;
	std::optional<Pk3PackageInventory> inventory =
			M_InspectPk3Package(path, &error);
	if (!inventory)
	{
		DLG_ShowError(false, "Could not inspect PK3 metadata: %s", error.c_str());
		return;
	}

	const ResourceDiagnostics diagnostics =
			M_AnalyzeResourceConflicts(*inventory, wad.master);
	UI_Pk3Metadata(*inventory, diagnostics).Run();
}

//
// Read in the level data
//

void Instance::LoadLevel(const Wad_file *wad, const SString &level) noexcept(false)
{
	int lev_num = wad->LevelFind(level);

	if (lev_num < 0)
		ThrowException("No such map: %s\n", level.c_str());

	LoadLevelNum(wad, lev_num);

	// reset various editor state
	Editor_ClearAction();
	Selection_InvalidateLast();

	edit.Selected->clear_all();
	edit.highlight.clear();

	if (main_win)
	{
		main_win->UpdateTotals(this->level);
		main_win->UpdateGameInfo(loaded, conf);
		main_win->InvalidatePanelObj();
		main_win->redraw();

		main_win->SetTitle(wad->PathName().u8string(), level, wad->IsReadOnly());

		// load the user state associated with this map
		if (! M_LoadUserState())
		{
			M_DefaultUserState();
		}
	}

	loaded.levelName = level.asUpper();

	Status_Set("Loaded %s", loaded.levelName.c_str());

	RedrawMap();
}

NewDocument Instance::openDocument(const LoadingData &inLoading, const Wad_file &wad, int level)
{
	assert(level >= 0 && level < wad.LevelCount());

	NewDocument newdoc = { Document(*this), inLoading, BadCount() };

	Document& doc = newdoc.doc;
	LoadingData& loading = newdoc.loading;
	BadCount& bad = newdoc.bad;

	loading.levelFormat = wad.LevelFormat(level);
	doc.LoadHeader(level, wad);
	if(loading.levelFormat == MapFormat::udmf)
	{
		UDMF_LoadLevel(level, &wad, doc, loading, bad);
	}
	else try
	{
		if(loading.levelFormat == MapFormat::hexen)
			doc.LoadThings_Hexen(level, &wad);
		else
			doc.LoadThings(level, &wad);
		doc.LoadVertices(level, &wad);
		doc.LoadSectors(level, &wad);
		doc.LoadSideDefs(level, &wad, conf, bad);
		if(loading.levelFormat == MapFormat::hexen)
		{
			doc.LoadLineDefs_Hexen(level, &wad, conf, bad);
			doc.LoadBehavior(level, &wad);
			doc.LoadScripts(level, &wad);
		}
		else
			doc.LoadLineDefs(level, &wad, conf, bad);
	}
	catch(const std::runtime_error &e)
	{
		gLog.printf("%s\n", e.what());
		throw;
	}
	doc.RemoveUnusedVerticesAtEnd();
	doc.checks.sidedefsUnpack(true);
	doc.CalculateLevelBounds();
	doc.markSaved();

	return newdoc;
}

void Instance::LoadLevelNum(const Wad_file *wad, int lev_num) noexcept(false)
{
	NewDocument newdoc = openDocument(loaded, *wad, lev_num);
	if (newdoc.bad.linedef_count || newdoc.bad.sector_refs || newdoc.bad.sidedef_refs)
	{
		ShowLoadProblem(newdoc.bad);
	}
	loaded = newdoc.loading;
	level = std::move(newdoc.doc);
	if(main_win)
		testmap::updateMenuName(main_win->menu_bar, loaded);
	Subdiv_InvalidateAll();
}

void Instance::refreshViewAfterLoad(const BadCount& bad, const Wad_file *wad, const SString &map_name, bool new_resources)
{
	if (bad.exists())
		ShowLoadProblem(bad);

	Subdiv_InvalidateAll();

	// reset various editor state
	Editor_ClearAction();
	Selection_InvalidateLast();
	edit.Selected->clear_all();
	edit.highlight.clear();

	if (main_win)
	{
		main_win->UpdateTotals(level);
		main_win->UpdateGameInfo(loaded, conf);
		main_win->InvalidatePanelObj();
		main_win->redraw();

		main_win->SetTitle(wad->PathName().u8string(), map_name, wad->IsReadOnly());

		// load the user state associated with this map
		if (!M_LoadUserState())
			M_DefaultUserState();
	}
	loaded.levelName = map_name.asUpper();
	Status_Set("Loaded %s", loaded.levelName.c_str());
	RedrawMap();

	if (new_resources && main_win)
	{
		if (main_win->canvas)
			main_win->canvas->DeleteContext();

		main_win->browser->Populate();

		// TODO: only call this when the IWAD has changed
		main_win->propsLoadValues();
	}
}


//
// open a new wad file.
// when 'map_name' is not NULL, try to open that map.
//
void OpenFileMap(const fs::path &filename, const SString &map_namem) noexcept(false)
{
	// TODO: change this to start a new instance
	SString map_name = map_namem;
	const bool discardOldRecovery = gInstance->Project_HasChanges() &&
			!gInstance->Project_HasDeferredRecovery();
	const fs::path oldPackagePath = gInstance->wad.master.editWad() ?
			gInstance->wad.master.editWad()->PathName() : fs::path{};
	if (!gInstance->Project_ConfirmClose("open another map"))
		return;


	std::shared_ptr<Wad_file> wad;

	// make sure file exists, as Open() with 'a' would create it otherwise
	if (FileExists(filename))
	{
		wad = M_OpenEditablePackage(filename);
	}

	if (! wad)
	{
		// FIXME: get an error message, add it here

		DLG_Notify("Unable to open that WAD or PK3 package.");
		return;
	}

	LoadingData loading = gInstance->loaded;
	// Opening another package must not leak explicit project state from the
	// previously active package. Legacy WADs remain implicit projects.
	loading.project.clear();
	const std::optional<ProjectSession> openedSession =
			gInstance->Project_LoadSession(wad->PathName(), loading, false);
	if (map_name.empty() && openedSession &&
			wad->LevelFind(openedSession->activeMap) >= 0)
	{
		map_name = openedSession->activeMap;
	}


	// determine which level to use
	int lev_num = -1;

	if (map_name.good())
	{
		lev_num = wad->LevelFind(map_name);
	}

	if (lev_num < 0)
	{
		lev_num = wad->LevelFindFirst();
	}

	if (lev_num < 0)
	{
		DLG_Notify("No editable levels were found in that package.");

		return;
	}

	if (wad->FindLump(EUREKA_LUMP))
	{
		if (! loading.parseEurekaLump(global::home_dir, global::old_linux_home_and_cache_dir,
				global::install_dir, global::recent, wad.get()))
		{
			return;
		}
	}


	/* OK, open it */


	// this wad replaces the current PWAD

	// always grab map_name from the actual level
	{
		int idx = wad->LevelHeader(lev_num);
		map_name  = wad->GetLump(idx)->Name();
	}

	gLog.printf("Loading Map : %s of %s\n", map_name.c_str(), reinterpret_cast<const char *>(wad->PathName().u8string().c_str()));

	// These 2 may throw, but it's safe here
	NewDocument newdoc = gInstance->openDocument(loading, *wad, lev_num);
	NewResources newres = loadResources(newdoc.loading, gInstance->wad, wad);

	gInstance->level = std::move(newdoc.doc);
	gInstance->conf = std::move(newres.config);
	gInstance->loaded = std::move(newres.loading);
	gInstance->wad = std::move(newres.waddata);
	gInstance->wad.master.ReplaceEditWad(wad);
	gInstance->Project_ClearDocumentCache();
	gInstance->Project_AdoptSession(openedSession);

	if(gInstance->main_win)
		testmap::updateMenuName(gInstance->main_win->menu_bar, gInstance->loaded);

	gInstance->refreshViewAfterLoad(newdoc.bad, wad.get(), map_name, true);
	if (discardOldRecovery && !oldPackagePath.empty())
		RecoveryStore(global::cache_dir / "recovery").discard(oldPackagePath);
	gInstance->Project_ResetAutosaveTimer();
	gInstance->Project_SaveSession();
	gInstance->Project_CheckRecovery();
}


void Instance::CMD_OpenMap()
{
	SString map_name;
	bool did_load = false;

	std::shared_ptr<Wad_file> wad = UI_OpenMap(*this).Run(&map_name, &did_load);

	if (! wad)	// cancelled
		return;

	// this shouldn't happen -- but just in case...
	int lev_num = wad->LevelFind(map_name);
	if (lev_num < 0)
	{
		DLG_Notify("Hmmmm, cannot find that map !?!");

		return;
	}

	if (!did_load && wad == this->wad.master.editWad())
	{
		try
		{
			Project_SwitchMap(wad, map_name);
		}
		catch (const std::runtime_error &error)
		{
			DLG_ShowError(false, "Could not open %s: %s", map_name.c_str(),
					error.what());
		}
		return;
	}

	if (!Project_ConfirmClose("open another map"))
		return;
	const bool discardOldRecovery = Project_HasChanges() && !recoveryDeferred_;
	const fs::path oldPackagePath = this->wad.master.editWad() ?
			this->wad.master.editWad()->PathName() : fs::path{};

	LoadingData loading = loaded;
	if (did_load || (this->wad.master.editWad() &&
			wad != this->wad.master.editWad()))
	{
		loading.project.clear();
	}
	std::optional<ProjectSession> openedSession;
	if (did_load)
		openedSession = Project_LoadSession(wad->PathName(), loading, false);
	if (did_load && wad->FindLump(EUREKA_LUMP) && !loading.parseEurekaLump(global::home_dir,
		global::old_linux_home_and_cache_dir, global::install_dir, global::recent, wad.get()))
	{
		return;
	}

	// does this wad replace the currently edited wad?
	bool new_resources = false;
	std::shared_ptr<Wad_file> newEditWad;
	bool removeEditWad = false;

	if (did_load)
	{
		SYS_ASSERT(wad != this->wad.master.editWad());
		SYS_ASSERT(wad != this->wad.master.gameWad());

		newEditWad = wad;

		new_resources = true;
	}
	// ...or does it remove the edit_wad? (e.g. wad == game_wad)
	else if (this->wad.master.editWad() && wad != this->wad.master.editWad())
	{
		removeEditWad = true;
		new_resources = true;
	}

	gLog.printf("Loading Map : %s of %s\n", map_name.c_str(), reinterpret_cast<const char *>(wad->PathName().u8string().c_str()));

	NewDocument newdoc = { Document(*this), LoadingData(), BadCount() };
	try
	{
		newdoc = openDocument(loading, *wad, lev_num);
	}
	catch (const std::runtime_error& e)
	{
		DLG_ShowError(false, "Could not open %s of %s. %s", map_name.c_str(),
					  reinterpret_cast<const char *>(wad->PathName().u8string().c_str()), e.what());
		return;
	}


	if (new_resources)
	{
		// TODO: call a safe version of Main_LoadResources
		NewResources newres = {};
		try
		{
			newres = loadResources(newdoc.loading, this->wad, newEditWad);
		}
		catch (const std::runtime_error& e)
		{
			DLG_ShowError(false, "Could not reload resources: %s", e.what());
			return;
		}

		// success loading resources
		conf = std::move(newres.config);
		loaded = std::move(newres.loading);
		this->wad = std::move(newres.waddata);

		if(main_win)
			testmap::updateMenuName(main_win->menu_bar, loaded);

		gLog.printf("--- DONE ---\n");
		gLog.printf("\n");
	}

	// on success

	if (removeEditWad)
		this->wad.master.RemoveEditWad();
	else if (newEditWad)
		this->wad.master.ReplaceEditWad(newEditWad);
	Project_ClearDocumentCache();
	if (newEditWad)
		Project_AdoptSession(openedSession);

	level = std::move(newdoc.doc);
	if(!new_resources)	// we already updated loaded with resources
		loaded = std::move(newdoc.loading);
	if(main_win)
		testmap::updateMenuName(main_win->menu_bar, loaded);

	refreshViewAfterLoad(newdoc.bad, wad.get(), map_name, new_resources);
	if (discardOldRecovery && !oldPackagePath.empty())
		RecoveryStore(global::cache_dir / "recovery").discard(oldPackagePath);
	Project_ResetAutosaveTimer();
	if (newEditWad)
	{
		Project_SaveSession();
		Project_CheckRecovery();
	}
}


void Instance::CMD_GivenFile()
{
	SString mode = EXEC_Param[0];

	int index = last_given_file;

	if (mode.empty() || mode.noCaseEqual("current"))
	{
		// index = index + 0;
	}
	else if (mode.noCaseEqual("next"))
	{
		index = index + 1;
	}
	else if (mode.noCaseEqual("prev"))
	{
		index = index - 1;
	}
	else if (mode.noCaseEqual("first"))
	{
		index = 0;
	}
	else if (mode.noCaseEqual("last"))
	{
		index = (int)global::Pwad_list.size() - 1;
	}
	else
	{
		Beep("GivenFile: unknown keyword: %s", mode.c_str());
		return;
	}

	if (index < 0 || index >= (int)global::Pwad_list.size())
	{
		Beep("No more files");
		return;
	}

	last_given_file = index;

	// TODO: remember last map visited in this wad

	try
	{
		OpenFileMap(global::Pwad_list[index], NULL);
	}
	catch (const std::runtime_error& e)
	{
		gLog.printf("%s\n", e.what());
		DLG_ShowError(false, "Cannot load given file %s: %s", reinterpret_cast<const char *>(global::Pwad_list[index].u8string().c_str()), e.what());
	}
}


void Instance::CMD_FlipMap()
{
	SString mode = EXEC_Param[0];

	if (mode.empty())
	{
		Beep("FlipMap: missing keyword");
		return;
	}


	std::shared_ptr<Wad_file> wad = this->wad.master.activeWad();

	// the level might not be found (lev_num < 0) -- that is OK
	int lev_idx = wad->LevelFind(loaded.levelName);
	int max_idx = wad->LevelCount() - 1;

	if (max_idx < 0)
	{
		Beep("No maps ?!?");
		return;
	}

	SYS_ASSERT(lev_idx <= max_idx);


	if (mode.noCaseEqual("next"))
	{
		if (lev_idx < 0)
			lev_idx = 0;
		else if (lev_idx < max_idx)
			lev_idx++;
		else
		{
			Beep("No more maps");
			return;
		}
	}
	else if (mode.noCaseEqual("prev"))
	{
		if (lev_idx < 0)
			lev_idx = max_idx;
		else if (lev_idx > 0)
			lev_idx--;
		else
		{
			Beep("No more maps");
			return;
		}
	}
	else if (mode.noCaseEqual("first"))
	{
		lev_idx = 0;
	}
	else if (mode.noCaseEqual("last"))
	{
		lev_idx = max_idx;
	}
	else
	{
		Beep("FlipMap: unknown keyword: %s", mode.c_str());
		return;
	}

	SYS_ASSERT(lev_idx >= 0);
	SYS_ASSERT(lev_idx <= max_idx);


	int lump_idx = wad->LevelHeader(lev_idx);
	Lump_c * lump  = wad->GetLump(lump_idx);
	const SString &map_name = lump->Name();

	gLog.printf("Flipping Map to : %s\n", map_name.c_str());

	try
	{
		if (wad == this->wad.master.editWad())
			Project_SwitchMap(wad, map_name);
		else
		{
			const bool discardOldRecovery = Project_HasChanges() &&
					!recoveryDeferred_;
			const fs::path oldPackagePath = this->wad.master.editWad() ?
					this->wad.master.editWad()->PathName() : fs::path{};
			if (!Project_ConfirmClose("open another map"))
				return;
			LoadLevel(wad.get(), map_name);
			Project_ClearDocumentCache();
			if (discardOldRecovery && !oldPackagePath.empty())
				RecoveryStore(global::cache_dir / "recovery").discard(oldPackagePath);
			Project_ResetAutosaveTimer();
		}
	}
	catch (const std::runtime_error &error)
	{
		DLG_ShowError(false, "Could not open %s: %s", map_name.c_str(),
				error.what());
	}
}


//------------------------------------------------------------------------
//  SAVING CODE
//------------------------------------------------------------------------

int Document::SaveHeader(Wad_file& wad, const SString &level) const
{
	int size = (int)headerData.size();

	int saving_level;
	Lump_c *lump = wad.AddLevel(level, &saving_level);

	if (size > 0)
	{
		lump->Write(&headerData[0], size);
	}

	return saving_level;
}


void Document::SaveBehavior(Wad_file &wad) const
{
	int size = (int)behaviorData.size();

	Lump_c &lump = wad.AddLump("BEHAVIOR");

	if (size > 0)
	{
		lump.Write(&behaviorData[0], size);
	}
}


void Document::SaveScripts(Wad_file& wad) const
{
	int size = (int)scriptsData.size();

	if (size > 0)
	{
		Lump_c &lump = wad.AddLump("SCRIPTS");

		lump.Write(&scriptsData[0], size);
	}
}


void Document::SaveVertices(Wad_file &wad) const
{
	Lump_c &lump = wad.AddLump("VERTEXES");

	for (const auto &vert : vertices)
	{
		raw_vertex_t raw{};

		raw.x = LE_S16(static_cast<int>(round(vert->xf)));
		raw.y = LE_S16(static_cast<int>(round(vert->yf)));

		lump.Write(&raw, sizeof(raw));
	}
}


void Document::SaveSectors(Wad_file &wad) const
{
	Lump_c &lump = wad.AddLump("SECTORS");

	// The binary format stores heights as int16. Out-of-range values would
	// silently wrap, so count them and warn instead of corrupting quietly.
	int numBadHeights = 0;

	for (const auto& sec : sectors)
	{
		raw_sector_t raw{};

		if (sec->floorh < -32767 || sec->floorh > 32767 ||
			sec->ceilh  < -32767 || sec->ceilh  > 32767)
			numBadHeights++;

		raw.floorh = LE_S16(sec->floorh);
		raw.ceilh  = LE_S16(sec->ceilh);

		W_StoreString(raw.floor_tex, sec->FloorTex(), sizeof(raw.floor_tex));
		W_StoreString(raw.ceil_tex,  sec->CeilTex(),  sizeof(raw.ceil_tex));

		raw.light = LE_U16(sec->light);
		raw.type  = LE_U16(sec->type);
		raw.tag   = LE_U16(sec->tag);

		lump.Write(&raw, sizeof(raw));
	}

	if (numBadHeights > 0)
	{
		gLog.printf("WARNING: %d sector(s) have heights outside the int16 range "
					"of the binary map format; they were truncated on save.\n",
					numBadHeights);
		DLG_Notify("%d sector(s) have floor/ceiling heights outside the\n"
				   "-32767..32767 range of the binary map format.\n\n"
				   "They were truncated on save. Use the UDMF format\n"
				   "for heights beyond that range.",
				   numBadHeights);
	}
}


void Document::SaveThings(Wad_file &wad) const
{
	Lump_c &lump = wad.AddLump("THINGS");

	for (const auto &th : things)
	{
		raw_thing_t raw{};

		raw.x = LE_S16(static_cast<int>(round(th->xf)));
		raw.y = LE_S16(static_cast<int>(round(th->yf)));

		raw.angle   = LE_U16(th->angle);
		raw.type    = LE_U16(th->type);
		raw.options = LE_U16(th->options);

		lump.Write(&raw, sizeof(raw));
	}
}


// IOANCH 9/2015
void Document::SaveThings_Hexen(Wad_file& wad) const
{
	Lump_c &lump = wad.AddLump("THINGS");

	for (const auto &th : things)
	{
		raw_hexen_thing_t raw{};

		raw.tid = LE_S16(th->tid);

		raw.x = LE_S16(static_cast<int>(round(th->xf)));
		raw.y = LE_S16(static_cast<int>(round(th->yf)));
		raw.height = LE_S16(static_cast<int>(round(th->hf)));

		raw.angle   = LE_U16(th->angle);
		raw.type    = LE_U16(th->type);
		raw.options = LE_U16(th->options);

		raw.special = static_cast<uint8_t>(th->special);
		raw.args[0] = static_cast<uint8_t>(th->arg1);
		raw.args[1] = static_cast<uint8_t>(th->arg2);
		raw.args[2] = static_cast<uint8_t>(th->arg3);
		raw.args[3] = static_cast<uint8_t>(th->arg4);
		raw.args[4] = static_cast<uint8_t>(th->arg5);

		lump.Write(&raw, sizeof(raw));
	}
}


void Document::SaveSideDefs(Wad_file &wad) const
{
	Lump_c &lump = wad.AddLump("SIDEDEFS");

	for (const auto &side : sidedefs)
	{
		raw_sidedef_t raw{};

		raw.x_offset = LE_S16(side->x_offset);
		raw.y_offset = LE_S16(side->y_offset);

		W_StoreString(raw.upper_tex, side->UpperTex(), sizeof(raw.upper_tex));
		W_StoreString(raw.lower_tex, side->LowerTex(), sizeof(raw.lower_tex));
		W_StoreString(raw.mid_tex,   side->MidTex(),   sizeof(raw.mid_tex));

		raw.sector = LE_U16(side->sector);

		lump.Write(&raw, sizeof(raw));
	}
}


void Document::SaveLineDefs(Wad_file &wad) const
{
	Lump_c &lump = wad.AddLump("LINEDEFS");

	for (const auto &ld : linedefs)
	{
		raw_linedef_t raw{};

		raw.start = LE_U16(ld->start);
		raw.end   = LE_U16(ld->end);

		raw.flags = LE_U16(ld->flags);
		raw.type  = LE_U16(ld->type);
		raw.tag   = LE_S16(ld->arg1);

		raw.right = (ld->right >= 0) ? LE_U16(ld->right) : 0xFFFF;
		raw.left  = (ld->left  >= 0) ? LE_U16(ld->left)  : 0xFFFF;

		lump.Write(&raw, sizeof(raw));
	}
}


// IOANCH 9/2015
void Document::SaveLineDefs_Hexen(Wad_file &wad) const
{
	Lump_c &lump = wad.AddLump("LINEDEFS");

	for (const auto &ld : linedefs)
	{
		raw_hexen_linedef_t raw{};

		raw.start = LE_U16(ld->start);
		raw.end   = LE_U16(ld->end);

		raw.flags = LE_U16(ld->flags);
		raw.type  = static_cast<uint8_t>(ld->type);

		raw.args[0] = static_cast<uint8_t>(ld->arg1);
		raw.args[1] = static_cast<uint8_t>(ld->arg2);
		raw.args[2] = static_cast<uint8_t>(ld->arg3);
		raw.args[3] = static_cast<uint8_t>(ld->arg4);
		raw.args[4] = static_cast<uint8_t>(ld->arg5);

		raw.right = (ld->right >= 0) ? LE_U16(ld->right) : 0xFFFF;
		raw.left  = (ld->left  >= 0) ? LE_U16(ld->left)  : 0xFFFF;

		lump.Write(&raw, sizeof(raw));
	}
}


static void EmptyLump(Wad_file& wad, const char *name)
{
	wad.AddLump(name);
}

void Instance::StoreDocumentInWad(LoadingData &loading,
		const SString &mapName, Wad_file &wad, const Document &document,
		bool inhibitNodeBuild)
{
	// set global level name now (for debugging code)
	loading.levelName = mapName.asUpper();

	// remove previous version of level (if it exists)
	int lev_num = wad.LevelFind(mapName);
	int level_lump = -1;

	if (lev_num >= 0)
	{
		level_lump = wad.LevelHeader(lev_num);

		wad.RemoveLevel(lev_num);
	}

	wad.InsertPoint(level_lump);

	int saving_level = document.SaveHeader(wad, mapName);

	if (loading.levelFormat == MapFormat::udmf)
	{
		UDMF_SaveLevel(loading, wad, document);
	}
	else
	{
		// IOANCH 9/2015: save Hexen format maps
		if (loading.levelFormat == MapFormat::hexen)
		{
			document.SaveThings_Hexen(wad);
			document.SaveLineDefs_Hexen(wad);
		}
		else
		{
			document.SaveThings(wad);
			document.SaveLineDefs(wad);
		}

		document.SaveSideDefs(wad);
		document.SaveVertices(wad);

		EmptyLump(wad, "SEGS");
		EmptyLump(wad, "SSECTORS");
		EmptyLump(wad, "NODES");

		document.SaveSectors(wad);

		EmptyLump(wad, "REJECT");
		EmptyLump(wad, "BLOCKMAP");

		if (loading.levelFormat == MapFormat::hexen)
		{
			document.SaveBehavior(wad);
			document.SaveScripts(wad);
		}
	}

	// build the nodes
	if (config::bsp_on_save && !inhibitNodeBuild)
	{
		BuildNodesAfterSave(saving_level, loading, wad, document);
	}

	// this is mainly for Next/Prev-map commands
	// [ it doesn't change the on-disk wad file at all ]
	wad.SortLevels();

}

void Instance::SaveLevel(LoadingData &loading, const SString &mapName,
		Wad_file &wad, bool inhibitNodeBuild)
{
	StoreDocumentInWad(loading, mapName, wad, level, inhibitNodeBuild);
	loading.writeEurekaLump(wad);
	wad.writeToDisk();
}

void Instance::ConfirmLevelSaveSuccess(const LoadingData &loading, const Wad_file &wad)
{
	if (loading.project.isExplicit())
		global::recent.addRecentProject(wad.PathName(), loading.levelName,
				global::home_dir);
	else
		global::recent.addRecent(wad.PathName(), loading.levelName, global::home_dir);

	Status_Set("Saved %s", loading.levelName.c_str());

	if (main_win)
	{
		main_win->SetTitle(wad.PathName().u8string(), loading.levelName, false);

		// save the user state associated with this map
		M_SaveUserState();
	}

	level.markSaved();
	projectMetadataDirty_ = false;
}

//
// Write out the level data
//
void Instance::SaveLevelAndUpdateWindow(LoadingData& loading, const SString &level, Wad_file &wad, bool inhibit_node_build)
{
	SaveLevel(loading, level, wad, inhibit_node_build);
	ConfirmLevelSaveSuccess(loading, wad);
}

// these return false if user cancelled
bool Instance::M_SaveMap(bool inhibit_node_build)
{
	// we require a wad file to save into.
	// if there is none, then need to create one via Export function.

	if (!wad.master.editWad())
	{
		return M_ExportMap(inhibit_node_build);
	}

	if (wad.master.editWad()->IsReadOnly())
	{
		if (DLG_Confirm({ "Cancel", "&Export" },
		                "The current pwad is a READ-ONLY file. "
						"Do you want to export this map into a new file?") <= 0)
		{
			return false;
		}

		return M_ExportMap(inhibit_node_build);
	}


	M_BackupWad(wad.master.editWad().get());

	gLog.printf("Saving Map : %s in %s\n", loaded.levelName.c_str(), reinterpret_cast<const char *>(wad.master.editWad()->PathName().u8string().c_str()));

	try
	{
		SaveLevelAndUpdateWindow(loaded, loaded.levelName, *wad.master.editWad(), inhibit_node_build);
	}
	catch(const std::runtime_error &e)
	{
		DLG_ShowError(false, "Could not save map: %s", e.what());
		return false;
	}
	Project_SaveSession();
	Project_SynchronizeRecoveryAfterSave();

	return true;
}

bool Instance::M_SaveProject(bool inhibit_node_build)
{
	std::shared_ptr<Wad_file> package = wad.master.editWad();
	if (!package)
	{
		DLG_Notify("Save Project requires an editable project package.\n\n"
				"Use Export Map to create one first.");
		return false;
	}
	if (package->IsReadOnly())
	{
		DLG_Notify("The current project package is read-only.\n\n"
				"Use Export Map to create a writable package first.");
		return false;
	}
	if (recoveryDeferred_)
	{
		// Merge any work made since "Later" into the retained snapshot before
		// asking again. Recovering then produces one complete resident set.
		if (Project_HasChanges() && !Project_WriteAutosave())
		{
			DLG_Notify("The retained recovery could not be updated.  Save Project "
					"has stopped to avoid losing either version of your work.");
			return false;
		}
		Project_CheckRecovery();
		if (recoveryDeferred_)
			return false;
	}
	if (!Project_HasChanges())
	{
		Project_SaveSession();
		Status_Set("Project is already saved");
		return true;
	}

	const fs::path packagePath = package->PathName();
	std::vector<CachedMapDocument *> cached = documentCache.dirtyDocuments();
	const bool activeDirty = level.hasChanges();
	const size_t mapCount = cached.size() + (activeDirty ? 1 : 0);

	M_BackupWad(package.get());
	gLog.printf("Saving Project : %s (%zu changed map%s)\n",
			reinterpret_cast<const char *>(packagePath.u8string().c_str()),
			mapCount, mapCount == 1 ? "" : "s");

	try
	{
		for (CachedMapDocument *entry : cached)
		{
			StoreDocumentInWad(entry->loading, entry->mapName, *package,
					entry->document, inhibit_node_build);
		}
		if (activeDirty)
		{
			StoreDocumentInWad(loaded, loaded.levelName, *package, level,
					inhibit_node_build);
		}

		// Metadata and every changed resident map share this single validated,
		// atomic package replacement.
		loaded.writeEurekaLump(*package);
		package->writeToDisk();
	}
	catch (const std::runtime_error &error)
	{
		// Serialization mutates the in-memory aggregate before the atomic disk
		// commit. Reload it so a failed save cannot poison a later attempt.
		std::shared_ptr<Wad_file> restored = M_OpenEditablePackage(packagePath);
		if (restored)
			wad.master.ReplaceEditWad(restored);
		DLG_ShowError(false, "Could not save project; the original package was "
				"preserved: %s", error.what());
		return false;
	}

	for (CachedMapDocument *entry : cached)
		entry->document.markSaved();
	if (activeDirty)
		level.markSaved();
	projectMetadataDirty_ = false;

	Status_Set("Saved project (%zu map%s)", mapCount,
			mapCount == 1 ? "" : "s");
	if (main_win)
	{
		main_win->SetTitle(packagePath.u8string(), loaded.levelName, false);
		M_SaveUserState();
	}
	Project_SaveSession();
	Project_DiscardRecovery();
	Project_ResetAutosaveTimer();
	return true;
}


bool Instance::M_ExportMap(bool inhibit_node_build)
{
	Fl_Native_File_Chooser chooser;

	chooser.title("Pick file to export to");
	chooser.type(Fl_Native_File_Chooser::BROWSE_SAVE_FILE);
	chooser.filter("Wads\t*.wad");
	chooser.directory(reinterpret_cast<const char *>(Main_FileOpFolder().u8string().c_str()));

	// Show native chooser
	switch (chooser.show())
	{
		case -1:
			gLog.printf("Export Map: error choosing file:\n");
			gLog.printf("   %s\n", chooser.errmsg());

			DLG_Notify("Unable to export the map:\n\n%s", chooser.errmsg());
			return false;

		case 1:
			gLog.printf("Export Map: cancelled by user\n");
			return false;

		default:
			break;  // OK
	}

	// if extension is missing then add ".wad"
	fs::path filename = fs::path(reinterpret_cast<const char8_t *>(chooser.filename()));

	fs::path extension = filename.extension();
	if(extension.empty())
		filename = fs::path(filename.u8string() + u8".wad");

	// don't export into a file we currently have open
	if (wad.master.MasterDir_HaveFilename(filename.u8string()))
	{
		DLG_Notify("Unable to export the map:\n\nFile already in use");
		return false;
	}

	std::shared_ptr<Wad_file> wad = Wad_file::Open(filename, WadOpenMode::append);
	if (!wad)
	{
		DLG_Notify("Unable to export the map:\n\n%s",
			"Error creating output file");
		return false;
	}
	if (wad->IsReadOnly())
	{
		DLG_Notify("Cannot export the map into a READ-ONLY file.");

		return false;
	}
	// adopt iwad/port/resources of the target wad
	LoadingData loading = loaded;
	loading.project.clear();
	if (wad->FindLump(EUREKA_LUMP))
	{
		if (!loading.parseEurekaLump(global::home_dir, global::old_linux_home_and_cache_dir,
				global::install_dir, global::recent, wad.get()))
		{
			return false;
		}
	}

	// ask user for map name

	SString map_name;
	{
		UI_ChooseMap dialog(loading.levelName.c_str());

		dialog.PopulateButtons(static_cast<char>(toupper(loading.levelName[0])),
								wad.get());

		map_name = dialog.Run();
	}

	// cancelled?
	if (map_name.empty())
	{
		return false;
	}


	// we will write into the chosen wad.
	// however if the level already exists, get confirmation first

	if (wad->LevelFind(map_name) >= 0)
	{
		if (DLG_Confirm({ "Cancel", "&Overwrite" },
		                overwrite_message, "selected") <= 0)
		{
			return false;
		}
	}

	const size_t cachedDirtyMaps = documentCache.dirtyCount();
	if (cachedDirtyMaps > 0 &&
			DLG_Confirm({ "Cancel", "&Export" },
					"You have unsaved changes in %d other project map%s.  "
					"Exporting will close the current project package and discard "
					"those changes.\n\nDo you really want to export?",
					static_cast<int>(cachedDirtyMaps),
					cachedDirtyMaps == 1 ? "" : "s") <= 0)
	{
		return false;
	}
	const bool discardOldRecovery = Project_HasChanges() && !recoveryDeferred_;
	const fs::path oldPackagePath = this->wad.master.editWad() ?
			this->wad.master.editWad()->PathName() : fs::path{};

	// back-up an existing wad
	if (wad->NumLumps() > 0)
	{
		M_BackupWad(wad.get());
	}


	gLog.printf("Exporting Map : %s in %s\n", map_name.c_str(), reinterpret_cast<const char *>(wad->PathName().u8string().c_str()));

	try
	{

		SaveLevel(loading, map_name, *wad, inhibit_node_build);
	}
	catch(const std::runtime_error &e)
	{
		DLG_ShowError(false, "Could not export map: %s", e.what());
		return false;
	}

	try
	{
		// do this after the save (in case it fatal errors)
		Main_LoadResources(loading, wad);
	}
	catch(const std::runtime_error &e)
	{
		DLG_ShowError(false, "Successfully exported map, but could not load the new resources: %s", e.what());
		return false;
	}

	// the new wad replaces the current PWAD
	this->wad.master.ReplaceEditWad(wad);
	Project_ClearDocumentCache();
	ConfirmLevelSaveSuccess(loaded, *wad);
	if (discardOldRecovery && !oldPackagePath.empty())
		RecoveryStore(global::cache_dir / "recovery").discard(oldPackagePath);
	Project_ResetAutosaveTimer();

	return true;
}


void Instance::CMD_SaveMap()
{
	M_SaveMap(false);
}

void Instance::CMD_SaveProject()
{
	M_SaveProject(false);
}

void Instance::CMD_SaveAll()
{
	// The current application model owns one project Instance. Keeping this
	// command distinct makes its application-wide intent explicit and leaves
	// room for multiple project windows without changing bindings or menus.
	M_SaveProject(false);
}


void Instance::CMD_ExportMap()
{
	M_ExportMap(false);
}


//------------------------------------------------------------------------
//  COPY, RENAME and DELETE MAP
//------------------------------------------------------------------------

void Instance::CMD_CopyMap()
{
	try
	{
		if (!wad.master.editWad())
		{
			DLG_Notify("Cannot copy a map unless editing a PWAD.");
			return;
		}

		if (wad.master.editWad()->IsReadOnly())
		{
			DLG_Notify("Cannot copy map: file is read-only.");
			return;
		}

		// ask user for map name

		SString new_name;
		{
			auto dialog = std::make_unique<UI_ChooseMap>(loaded.levelName.c_str(),
				wad.master.editWad());

			dialog->PopulateButtons(static_cast<char>(toupper(loaded.levelName[0])), wad.master.editWad().get());

			new_name = dialog->Run();
		}

		// cancelled?
		if (new_name.empty())
			return;

		// sanity check that the name is different
		// (should be prevented by the choose-map dialog)
		if (y_stricmp(new_name.c_str(), loaded.levelName.c_str()) == 0)
		{
			Beep("Name is same!?!");
			return;
		}

		// perform the copy (just a save)
		gLog.printf("Copying Map : %s --> %s\n", loaded.levelName.c_str(), new_name.c_str());

		documentCache.erase(new_name);
		SaveLevelAndUpdateWindow(loaded, new_name, *wad.master.editWad(), false);

		Status_Set("Copied to %s", loaded.levelName.c_str());
		Project_SaveSession();
		Project_SynchronizeRecoveryAfterSave();
	}
	catch (const std::runtime_error& e)
	{
		DLG_ShowError(false, "Could not copy map: %s", e.what());
	}

}


void Instance::CMD_RenameMap()
{
	const LoadingData backupLoading = loaded;
	const bool backupMetadataDirty = projectMetadataDirty_;
	fs::path packagePath;
	try
	{
		if(!wad.master.gameWad())
		{
			gLog.printf("No IWAD file!\n");
			return;
		}
		if (!wad.master.editWad())
		{
			DLG_Notify("Cannot rename a map unless editing a PWAD.");
			return;
		}

		if (wad.master.editWad()->IsReadOnly())
		{
			DLG_Notify("Cannot rename map : file is read-only.");
			return;
		}


		// ask user for map name
		SString new_name;
		{
			auto dialog = std::make_unique<UI_ChooseMap>(loaded.levelName.c_str(),
				wad.master.editWad() /* rename_wad */);

			// pick level format from the IWAD
			// [ user may be trying to rename map after changing the IWAD ]
			char format = 'M';
			{
				int idx = wad.master.gameWad()->LevelFindFirst();

				if (idx >= 0)
				{
					idx = wad.master.gameWad()->LevelHeader(idx);
					const SString& name = wad.master.gameWad()->GetLump(idx)->Name();
					format = static_cast<char>(toupper(name[0]));
				}
			}

			dialog->PopulateButtons(format, wad.master.editWad().get());

			new_name = dialog->Run();
		}

		// cancelled?
		if (new_name.empty())
			return;

		// sanity check that the name is different
		// (should be prevented by the choose-map dialog)
		if (y_stricmp(new_name.c_str(), loaded.levelName.c_str()) == 0)
		{
			Beep("Name is same!?!");
			return;
		}

		// perform the rename
		const SString old_name = loaded.levelName.asUpper();
		const int lev_num = wad.master.editWad()->LevelFind(old_name);
		if (lev_num < 0)
			ThrowException("The current map is not present in the editable package.");

		packagePath = wad.master.editWad()->PathName();
		const int level_lump = wad.master.editWad()->LevelHeader(lev_num);
		wad.master.editWad()->RenameLump(level_lump, new_name.c_str());
		if (loaded.project.isExplicit())
		{
			M_RenameProjectMapMetadata(loaded.project, old_name, new_name);
			loaded.writeEurekaLump(*wad.master.editWad());
		}
		wad.master.editWad()->writeToDisk();

		loaded.levelName = new_name.asUpper();
		documentCache.erase(new_name);
		documentCache.rename(old_name, new_name);
		if (loaded.project.isExplicit())
		{
			documentCache.updateLoadingContext(loaded);
			projectMetadataDirty_ = false;
		}
		if (navigatorSelection_.noCaseEqual(old_name))
			navigatorSelection_ = loaded.levelName;

		if (main_win)
			main_win->SetTitle(wad.master.editWad()->PathName().u8string(),
					loaded.levelName, false);

		Status_Set("Renamed to %s", loaded.levelName.c_str());
		Project_SaveSession();
		Project_SynchronizeRecoveryAfterSave();
	}
	catch (const std::runtime_error& e)
	{
		loaded = backupLoading;
		projectMetadataDirty_ = backupMetadataDirty;
		if (!packagePath.empty())
		{
			std::shared_ptr<Wad_file> restored = M_OpenEditablePackage(packagePath);
			if (restored)
				wad.master.ReplaceEditWad(restored);
		}
		DLG_ShowError(false, "Could not rename map: %s", e.what());
	}

}


void Instance::CMD_DeleteMap()
{
	if (!wad.master.editWad())
	{
		DLG_Notify("Cannot delete a map unless editing a PWAD.");
		return;
	}

	if (wad.master.editWad()->IsReadOnly())
	{
		DLG_Notify("Cannot delete map : file is read-only.");
		return;
	}

	if (wad.master.editWad()->LevelCount() < 2)
	{
		// perhaps ask either to Rename map, or Delete the file (and Eureka will shut down)

		DLG_Notify("Cannot delete the last map in a PWAD.");
		return;
	}

	if (DLG_Confirm({ "Cancel", "&Delete" },
		"Are you sure you want to delete this map? "
		"It will be permanently removed from the current PWAD.") <= 0)
	{
		return;
	}

	gLog.printf("Deleting Map : %s...\n", loaded.levelName.c_str());

	int lev_num = wad.master.editWad()->LevelFind(loaded.levelName);

	if (lev_num < 0)
	{
		Beep("No such map ?!?");
		return;
	}

	M_BackupWad(wad.master.editWad().get());
	documentCache.erase(loaded.levelName);

	// kick it to the curb
	int backupPoint = wad.master.editWad()->LevelHeader(lev_num);
	std::vector<Lump_c> backupLumps = wad.master.editWad()->RemoveLevel(lev_num);
	try
	{
		wad.master.editWad()->writeToDisk();
	}
	catch (const std::runtime_error& e)
	{
		// Restore deleted lumps
		wad.master.editWad()->InsertPoint(backupPoint);
		for (const Lump_c& backup : backupLumps)
		{
			if(&backup == &backupLumps[0])
				wad.master.editWad()->AddLevel(backup.Name())->Write(backup.getData().data(), (int)backup.getData().size());
			else
				wad.master.editWad()->AddLump(backup.Name()).Write(backup.getData().data(), (int)backup.getData().size());
		}
		wad.master.editWad()->SortLevels();
		DLG_ShowError(false, "Cannot delete map: %s", e.what());
		return;
	}


	// choose a new level to load
	try
	{
		if (lev_num >= wad.master.editWad()->LevelCount())
			lev_num = wad.master.editWad()->LevelCount() - 1;

		int lump_idx = wad.master.editWad()->LevelHeader(lev_num);
		const Lump_c* lump = wad.master.editWad()->GetLump(lump_idx);
		const SString& map_name = lump->Name();

		gLog.printf("OK.  Loading : %s....\n", map_name.c_str());

		// TODO: overhaul the interface to NOT go back to the IWAD
		loaded.levelName.clear();
		Project_SwitchMap(wad.master.editWad(), map_name);
	}
	catch (const std::runtime_error& e)
	{
		DLG_ShowError(false, "Failed changing map after deleting a level: %s\n\nThe PWAD will be closed.", e.what());
		if (!wad.master.gameWad())
			throw;

		int lump_idx = wad.master.gameWad()->LevelHeader(0);
		const Lump_c* lump = wad.master.gameWad()->GetLump(lump_idx);
		if (!lump)
			throw;

		wad.master.RemoveEditWad();
		Project_ClearDocumentCache();
		const SString& map_name = lump->Name();
		LoadLevel(wad.master.gameWad().get(), map_name);
	}
	Project_SynchronizeRecoveryAfterSave();
}

//--- editor settings ---
// vi:ts=4:sw=4:noexpandtab
