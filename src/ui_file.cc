//------------------------------------------------------------------------
//  FILE-RELATED DIALOGS
//------------------------------------------------------------------------
//
//  Eureka DOOM Editor
//
//  Copyright (C) 2012-2019 Andrew Apted
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

#include "Errors.h"
#include "Instance.h"

#include "main.h"
#include "m_config.h"
#include "m_files.h"
#include "m_package.h"
#include "m_game.h"
#include "m_project_resources.h"
#include "w_wad.h"

#include "ui_window.h"
#include "ui_file.h"

#include <algorithm>
#include <sstream>
#include <set>

#define FREE_COL  fl_rgb_color(0x33, 0xFF, 0xAA)
#define USED_COL  (config::gui_scheme == 2 ? fl_rgb_color(0xFF, 0x11, 0x11) : fl_rgb_color(0xFF, 0x88, 0x88))


// TODO: find a better home for this
bool ValidateMapName(const char *p)
{
	size_t len = strlen(p);

	if (len == 0 || len > 8)
		return false;

	if (! safe_isalpha(*p))
		return false;

	for ( ; *p ; p++)
		if (! (safe_isalnum(*p) || *p == '_'))
			return false;

	return true;
}


UI_ChooseMap::UI_ChooseMap(const char *initial_name,
						   const std::shared_ptr<const Wad_file> &_rename_wad) :
	UI_Escapable_Window(420, 385, "Choose Map"),
	rename_wad(_rename_wad)
{
	resizable(NULL);

	callback(close_callback, this);

	{
		map_name = new Fl_Input(120, 35, 120, 25, "Map slot: ");
		map_name->labelfont(FL_HELVETICA_BOLD);
	}

	map_name->when(FL_WHEN_CHANGED);
	map_name->callback(input_callback, this);
	map_name->value(initial_name);

	FLFocusOnCreation(map_name);

	map_buttons = new Fl_Group(x(), y() + 60, w(), y() + 320);
	map_buttons->end();

	{
		int bottom_y = 320;

		Fl_Group* o = new Fl_Group(0, bottom_y, 420, 65);
		o->box(FL_FLAT_BOX);
		o->color(WINDOW_BG, WINDOW_BG);

		ok_but = new Fl_Return_Button(260, bottom_y + 17, 100, 35, "OK");
		ok_but->labelfont(FL_HELVETICA_BOLD);
		ok_but->callback(ok_callback, this);

		Fl_Button *cancel = new Fl_Button(75, bottom_y + 17, 100, 35, "Cancel");
		cancel->callback(close_callback, this);

		o->end();
	}

	end();

	CheckMapName();
}

void UI_ChooseMap::PopulateButtons(char format, const Wad_file *test_wad)
{
	int but_W = 60;

	for (int col = 0 ; col < 5 ; col++)
	for (int row = 0 ; row < 8 ; row++)
	{
		int cx = x() + 30 + col * (but_W + but_W / 5);
		int cy = y() + 80 + row * 24 + (row / 2) * 10;

		char name_buf[20];

		if (format == 'E')
		{
			int epi = 1 + row / 2;
			int map = 1 + col + (row & 1) * 5;

			if (map > 9)
				continue;

			snprintf(name_buf, sizeof(name_buf), "E%dM%d", epi, map);
		}
		else
		{
			int map = 1 + col + row * 5;

			// this logic matches UI_OpenMap on the IWAD
			if (row >= 2)
				map--;
			else if (row == 1 && col == 4)
				continue;

			if (map < 1 || map > 32)
				continue;

			snprintf(name_buf, sizeof(name_buf), "MAP%02d", map);
		}

		Fl_Button * but = new Fl_Button(cx, cy, 60, 20);
		but->copy_label(name_buf);
		but->callback(button_callback, this);

		if (test_wad && test_wad->LevelFind(name_buf) >= 0)
		{
			if (rename_wad)
				but->deactivate();
			else
				but->color(USED_COL);
		}
		else
			but->color(FREE_COL);

		map_buttons->add(but);
	}
}


SString UI_ChooseMap::Run()
{
	set_modal();

	show();

	while (action == ACT_none)
	{
		Fl::wait(0.2);
	}

	if (action == ACT_CANCEL)
		return "";

	return SString(map_name->value()).asUpper();
}


void UI_ChooseMap::close_callback(Fl_Widget *w, void *data)
{
	UI_ChooseMap * that = (UI_ChooseMap *)data;

	that->action = ACT_CANCEL;
}


void UI_ChooseMap::ok_callback(Fl_Widget *w, void *data)
{
	UI_ChooseMap * that = (UI_ChooseMap *)data;

	// sanity check
	if (ValidateMapName(that->map_name->value()))
		that->action = ACT_ACCEPT;
	else
		fl_beep();
}


void UI_ChooseMap::button_callback(Fl_Widget *w, void *data)
{
	UI_ChooseMap * that = (UI_ChooseMap *)data;

	that->map_name->value(w->label());
	that->action = ACT_ACCEPT;
}


void UI_ChooseMap::input_callback(Fl_Widget *w, void *data)
{
	UI_ChooseMap * that = (UI_ChooseMap *)data;

	that->CheckMapName();
}


void UI_ChooseMap::CheckMapName()
{
	bool was_valid = ok_but->active();
	bool  is_valid = ValidateMapName(map_name->value());

	if (rename_wad && is_valid)
	{
		if (rename_wad->LevelFind(map_name->value()) >= 0)
			is_valid = false;
	}

	if (was_valid == is_valid)
		return;

	if (is_valid)
	{
		ok_but->activate();
		map_name->textcolor(FL_FOREGROUND_COLOR);
	}
	else
	{
		ok_but->deactivate();
		map_name->textcolor(FL_RED);
	}

	map_name->redraw();
}


//------------------------------------------------------------------------


UI_OpenMap::UI_OpenMap(Instance &inst) :
	UI_Escapable_Window(420, 475, "Open Map"), inst(inst)
{
	resizable(NULL);

	callback(close_callback, this);

	{
		look_where = new Fl_Choice(130, 80, 190, 25, "Find map in:  ");
		look_where->labelfont(FL_HELVETICA_BOLD);
		look_where->add("the PWAD above|the Game IWAD|the Resource wads");
		look_where->callback(look_callback, this);

		look_where->value(inst.wad.master.editWad() ? LOOK_PWad : LOOK_IWad);
	}

	{
		Fl_Box* o = new Fl_Box(15, 15, 270, 20, "PWAD file:");
		o->labelfont(FL_HELVETICA_BOLD);
		o->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
	}

	pwad_name = new Fl_Output(20, 40, 295, 26);

	Fl_Button *load_but = new Fl_Button(330, 39, 65, 28, "Load");
	load_but->callback(load_callback, this);


	map_name = new Fl_Input(99, 125, 100, 26, "Map slot: ");
	map_name->labelfont(FL_HELVETICA_BOLD);
	map_name->when(FL_WHEN_CHANGED);
	map_name->callback(input_callback, this);

	{
		Fl_Box *o = new Fl_Box(230, 125, 180, 26, "Available maps:");
		// o->labelfont(FL_HELVETICA_BOLD);
		o->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
	}


	// all the map buttons go into this group

	button_grp = new UI_Scroll(5, 165, w()-10, 230, +1 /* bar_side */);
	button_grp->align(FL_ALIGN_TOP | FL_ALIGN_INSIDE);
	button_grp->resize_horiz(false);
	button_grp->Line_size(24);
	button_grp->box(FL_FLAT_BOX);

	/* bottom buttons */

	{
		int bottom_y = h() - 70;

		Fl_Group* o = new Fl_Group(0, bottom_y, w(), 70);
		o->box(FL_FLAT_BOX);
		o->color(WINDOW_BG, WINDOW_BG);

		ok_but = new Fl_Return_Button(260, bottom_y + 20, 100, 34, "OK");
		ok_but->labelfont(FL_HELVETICA_BOLD);
		ok_but->callback(ok_callback, this);

		Fl_Button * cancel = new Fl_Button(75, bottom_y + 20, 100, 35, "Cancel");
		cancel->callback(close_callback, this);

		o->end();
	}

	end();

	CheckMapName();
}


UI_OpenMap::~UI_OpenMap()
{ }


std::shared_ptr<Wad_file> UI_OpenMap::Run(SString* map_v, bool * did_load)
{
	map_v->clear();
	*did_load = false;

	if (inst.wad.master.editWad())
		SetPWAD(inst.wad.master.editWad()->PathName().u8string());

	Populate();

	set_modal();
	show();

	while (action == Action::none)
	{
		Fl::wait(0.2);
	}

	if (action != Action::accept)
		using_wad.reset();

	if (using_wad)
	{
		*map_v = SString(map_name->value()).asUpper();

		if (using_wad == loaded_wad)
		{
			*did_load  = true;
			loaded_wad.reset();
		}
	}

	// if we are not returning a pwad which got loaded, e.g. because
	// the user cancelled or chose the game IWAD, then close it now.
	loaded_wad.reset();

	return using_wad;
}


void UI_OpenMap::CheckMapName()
{
	bool was_valid = ok_but->active();

	bool  is_valid = using_wad &&
	                 ValidateMapName(map_name->value()) &&
					 (using_wad->LevelFind(map_name->value()) >= 0);

	if (was_valid == is_valid)
		return;

	if (is_valid)
	{
		ok_but->activate();
		map_name->textcolor(FL_FOREGROUND_COLOR);
	}
	else
	{
		ok_but->deactivate();
		map_name->textcolor(FL_RED);
	}

	map_name->redraw();
}


void UI_OpenMap::Populate()
{
	button_grp->label("\n\n\n\n\nNO   MAPS   FOUND");
	button_grp->Remove_all();

	using_wad.reset();

	if (look_where->value() == LOOK_IWad)
	{
		using_wad = inst.wad.master.gameWad();
		PopulateButtons();
	}
	else if (look_where->value() >= LOOK_Resource)
	{
		// we simply use the last resource which contains levels

		// TODO: probably should collect ones with a map, add to look_where choices

		for (auto it = inst.wad.master.resourceWads().rbegin();
			 it != inst.wad.master.resourceWads().rend(); ++it)
		{
			if ((*it)->LevelCount() >= 0)
			{
				using_wad = *it;
				PopulateButtons();
				break;
			}
		}
	}
	else if (loaded_wad)
	{
		using_wad = loaded_wad;
		PopulateButtons();
	}
	else if (inst.wad.master.editWad())
	{
		using_wad = inst.wad.master.editWad();
		PopulateButtons();
	}

	button_grp->Init_sizes();
	button_grp->redraw();
}


static bool DifferentEpisode(const char *A, const char *B)
{
	if (A[0] != B[0])
		return true;

	// handle ExMx
	if (safe_toupper(A[0]) == 'E')
	{
		return A[1] != B[1];
	}

	// handle MAPxx
	if (strlen(A) < 4 && strlen(B) < 4)
		return false;

	return A[3] != B[3];
}


void UI_OpenMap::PopulateButtons()
{
	std::shared_ptr<const Wad_file> wad = using_wad;
	SYS_ASSERT(wad);

	int num_levels = wad->LevelCount();

	if (num_levels == 0)
		return;

	button_grp->label("");

	std::set<SString> level_names;

	for (int lev = 0 ; lev < num_levels ; lev++)
	{
		Lump_c *lump = wad->GetLump(wad->LevelHeader(lev));

		level_names.insert(lump->Name());
	}

	int cx_base = button_grp->x() + 25;
	int cy_base = button_grp->y() + 5;
	int but_W   = 60;

	// create them buttons!!

	int row = 0;
	int col = 0;

	SString last_name;

	for (const SString &name : level_names)
	{
		if (col > 0 && !last_name.empty() && DifferentEpisode(last_name.c_str(), name.c_str()))
		{
			col = 0;
			row++;
		}

		int cx = cx_base + col * (but_W + but_W / 5);
		int cy = cy_base + row * 24 + (row / 2) * 8;

		Fl_Button * but = new Fl_Button(cx, cy, but_W, 20);
		but->copy_label(name.c_str());
		but->color(FREE_COL);
		but->callback(button_callback, this);

		button_grp->Add(but);

		col++;
		if (col >= 5)
		{
			col = 0;
			row++;
		}

		last_name = name;
	}

	redraw();
}


void UI_OpenMap::SetPWAD(const SString &name)
{
	pwad_name->value(fl_filename_name(name.c_str()));
}


void UI_OpenMap::close_callback(Fl_Widget *w, void *data)
{
	UI_OpenMap * that = (UI_OpenMap *)data;

	that->action = Action::cancel;
}


void UI_OpenMap::ok_callback(Fl_Widget *w, void *data)
{
	UI_OpenMap * that = (UI_OpenMap *)data;

	// sanity check
	if (that->using_wad && ValidateMapName(that->map_name->value()))
		that->action = Action::accept;
	else
		fl_beep();
}


void UI_OpenMap::button_callback(Fl_Widget *w, void *data)
{
	auto that = (UI_OpenMap *)data;

	// sanity check
	if (! that->using_wad)
		return;

	that->map_name->value(w->label());
	that->action = Action::accept;
}


void UI_OpenMap::input_callback(Fl_Widget *w, void *data)
{
	UI_OpenMap * that = (UI_OpenMap *)data;

	that->CheckMapName();
}


void UI_OpenMap::look_callback(Fl_Widget *w, void *data)
{
	UI_OpenMap * that = (UI_OpenMap *)data;

	that->Populate();
	that->CheckMapName();
}


void UI_OpenMap::load_callback(Fl_Widget *w, void *data)
{
	UI_OpenMap * that = (UI_OpenMap *)data;

	that->LoadFile();
	that->CheckMapName();
}


void UI_OpenMap::LoadFile()
{
	Fl_Native_File_Chooser chooser;

	chooser.title("Pick file to open");
	chooser.type(Fl_Native_File_Chooser::BROWSE_FILE);
	chooser.filter("Map Packages\t*.{wad,pk3,zip}");
	chooser.directory(reinterpret_cast<const char *>(inst.Main_FileOpFolder().u8string().c_str()));

	// Show native chooser
	switch (chooser.show())
	{
		case -1:
			gLog.printf("Open Map: error choosing file:\n");
			gLog.printf("   %s\n", chooser.errmsg());

			DLG_Notify("Unable to open the map:\n\n%s",
					   chooser.errmsg());
			return;

		case 1:
			gLog.printf("Open Map: cancelled by user\n");
			return;

		default:
			break;  // OK
	}


	std::shared_ptr<Wad_file> wad = M_OpenEditablePackage(
			fs::path(reinterpret_cast<const char8_t *>(chooser.filename())));

	if (! wad)
	{
		// FIXME: get an error message, add it here

		DLG_Notify("Unable to open the chosen WAD or PK3 package.\n\n"
				   "Please try again.");
		return;
	}

	if (wad->LevelCount() <= 0)
	{
		DLG_Notify("The chosen package contains no editable levels.\n\n"
				   "Please try again.");
		return;
	}


	// replace existing one

	loaded_wad = wad;

	SetPWAD(loaded_wad->PathName().u8string());

	if (using_wad == loaded_wad)
		using_wad = wad;


	// change the "Find map in ..." setting
	look_where->value(LOOK_PWad);

	Populate();
}


//------------------------------------------------------------------------

#define STARTUP_MSG  "No IWADs could be found."


UI_ProjectSetup::UI_ProjectSetup(Instance &inst, bool new_project, bool is_startup) :
	UI_Escapable_Window(is_startup ? 400 : (new_project ? 620 : 650),
			is_startup ? 200 : (new_project ? 550 : 590),
			new_project ? "New Project" : "Manage Project"),
	inst(inst),
	newProject_(new_project)
{
	callback(close_callback, this);

	resizable(NULL);
	if (new_project)
	{
		BuildNewProjectWizard();
		end();
		return;
	}

	int by = 0;

	if (is_startup)
	{
		Fl_Box * message = new Fl_Box(FL_FLAT_BOX, 15, 15, 370, 46, STARTUP_MSG);
		message->align(FL_ALIGN_INSIDE);
		message->color(FL_RED, FL_RED);
		message->labelcolor(FL_YELLOW);
		message->labelsize(18);

		by += 60;
	}

	game_choice = new Fl_Choice(140, by+25, 150, 29, "Game IWAD: ");
	game_choice->labelfont(FL_HELVETICA_BOLD);
	game_choice->down_box(FL_BORDER_BOX);
	game_choice->callback((Fl_Callback*)game_callback, this);

	{
		Fl_Button* o = new Fl_Button(305, by+27, 75, 25, "Find");
		o->callback((Fl_Callback*)find_callback, this);
	}

	port_choice = new Fl_Choice(140, by+62, 150, 29, "Source Port: ");
	port_choice->labelfont(FL_HELVETICA_BOLD);
	port_choice->down_box(FL_BORDER_BOX);
	port_choice->callback((Fl_Callback*)port_callback, this);

	{
		Fl_Button* o = new Fl_Button(305, by+64, 75, 25, "Setup");
		o->callback((Fl_Callback*)setup_callback, this);

		if (is_startup)
			o->hide();
	}

	format_choice = new Fl_Choice(140, by+99, 150, 29, "Map Type: ");
	format_choice->labelfont(FL_HELVETICA_BOLD);
	format_choice->down_box(FL_BORDER_BOX);
	format_choice->callback((Fl_Callback*)format_callback, this);

	if (new_project)
	{
		package_choice = new Fl_Choice(140, by+136, 240, 29, "Package: ");
		package_choice->labelfont(FL_HELVETICA_BOLD);
		package_choice->down_box(FL_BORDER_BOX);
		package_choice->add("WAD package|PK3 package (BiasedDoom / GZDoom)");
		package_choice->value(0);
		package_choice->callback((Fl_Callback*)package_callback, this);

	}

	if (!is_startup)
	{
		const int campaignY = by + (new_project ? 173 : 136);
		campaign_choice = new Fl_Choice(140, campaignY, 340, 29, "Campaign: ");
		campaign_choice->labelfont(FL_HELVETICA_BOLD);
		campaign_choice->down_box(FL_BORDER_BOX);
		campaign_choice->add("Full IWAD replacement|Single map|Custom map order");
		campaign_choice->value(0);
		campaign_choice->callback((Fl_Callback*)campaign_callback, this);

		custom_slots = new Fl_Input(140, campaignY + 37, 340, 29, "Map order: ");
		custom_slots->tooltip("Comma- or space-separated map names, in campaign order");
		custom_slots->callback((Fl_Callback*)custom_slots_callback, this);
		custom_slots->when(FL_WHEN_CHANGED);
		custom_slots->deactivate();
	}

#if 0  // Disabled for now
	namespace_choice = new Fl_Choice(140, by+140, 150, 29, "Namespace: ");
	namespace_choice->labelfont(FL_HELVETICA_BOLD);
	namespace_choice->down_box(FL_BORDER_BOX);
	namespace_choice->callback((Fl_Callback*)namespace_callback, this);
	namespace_choice->hide();
#endif

	if (is_startup)
	{
		port_choice->hide();
		format_choice->hide();
	}

	// Resource section

	if (! is_startup)
	{
		Fl_Box *res_title = new Fl_Box(35, by + 214, 220, 30,
				"Resource Files or Folders:");
		res_title->labelfont(FL_HELVETICA_BOLD);
		res_title->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
		BuildResourceList(35, by + 244, w() - 70, 205);
	}

	// bottom buttons
	{
		by = is_startup ? by + 80 : 520;

		Fl_Group *g = new Fl_Group(0, by, w(), h() - by);
		g->box(FL_FLAT_BOX);
		g->color(WINDOW_BG, WINDOW_BG);

		const char *cancel_text = is_startup ? "Quit" : "Cancel";

		cancel = new Fl_Button(90, g->y() + 14, 80, 35, cancel_text);
		cancel->callback((Fl_Callback*)close_callback, this);

		const char *ok_text = (is_startup | new_project) ? "OK" : "Use";

		ok_but = new Fl_Button(w() - 160, g->y() + 14, 80, 35, ok_text);
		ok_but->labelfont(FL_HELVETICA_BOLD);
		ok_but->callback((Fl_Callback*)use_callback, this);

		g->end();
	}

	end();
}


void UI_ProjectSetup::BuildNewProjectWizard()
{
	wizard_step = new Fl_Box(20, 12, w() - 40, 34,
			"Step 1 of 3 - Project settings");
	wizard_step->labelfont(FL_HELVETICA_BOLD);
	wizard_step->labelsize(18);
	wizard_step->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

	wizard = new Fl_Wizard(0, 55, w(), 427);
	wizard->box(FL_FLAT_BOX);
	wizard->color(WINDOW_BG, WINDOW_BG);

	// Page 1: engine-facing project settings.
	wizard_pages[0] = new Fl_Group(0, 55, w(), 427);
	wizard_pages[0]->box(FL_FLAT_BOX);
	wizard_pages[0]->color(WINDOW_BG, WINDOW_BG);

	{
		Fl_Box *intro = new Fl_Box(35, 69, w() - 70, 34,
				"Choose the game data and compatibility profile for this project.");
		intro->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
	}

	game_choice = new Fl_Choice(170, 118, 260, 29, "Game IWAD: ");
	game_choice->labelfont(FL_HELVETICA_BOLD);
	game_choice->down_box(FL_BORDER_BOX);
	game_choice->callback((Fl_Callback*)game_callback, this);

	{
		Fl_Button *find = new Fl_Button(445, 120, 90, 25, "Find IWAD");
		find->callback((Fl_Callback*)find_callback, this);
	}

	port_choice = new Fl_Choice(170, 163, 260, 29, "Source Port: ");
	port_choice->labelfont(FL_HELVETICA_BOLD);
	port_choice->down_box(FL_BORDER_BOX);
	port_choice->callback((Fl_Callback*)port_callback, this);

	{
		Fl_Button *setup = new Fl_Button(445, 165, 90, 25, "Setup");
		setup->callback((Fl_Callback*)setup_callback, this);
	}

	format_choice = new Fl_Choice(170, 208, 260, 29, "Map Type: ");
	format_choice->labelfont(FL_HELVETICA_BOLD);
	format_choice->down_box(FL_BORDER_BOX);
	format_choice->callback((Fl_Callback*)format_callback, this);

	package_choice = new Fl_Choice(170, 253, 365, 29, "Package: ");
	package_choice->labelfont(FL_HELVETICA_BOLD);
	package_choice->down_box(FL_BORDER_BOX);
	package_choice->add("WAD package|PK3 package (BiasedDoom / GZDoom)");
	package_choice->value(0);
	package_choice->callback((Fl_Callback*)package_callback, this);

	{
		Fl_Box *help = new Fl_Box(85, 312, 450, 70,
				"The game IWAD is opened read-only. The new package stores only "
				"your maps, project metadata, and any resources you later add to it.");
		help->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
	}
	wizard_pages[0]->end();

	// Page 2: campaign shape and external resources.
	wizard_pages[1] = new Fl_Group(0, 55, w(), 427);
	wizard_pages[1]->box(FL_FLAT_BOX);
	wizard_pages[1]->color(WINDOW_BG, WINDOW_BG);

	campaign_choice = new Fl_Choice(170, 82, 365, 29, "Campaign: ");
	campaign_choice->labelfont(FL_HELVETICA_BOLD);
	campaign_choice->down_box(FL_BORDER_BOX);
	campaign_choice->add("Full IWAD replacement|Single map|Custom map order");
	campaign_choice->value(0);
	campaign_choice->callback((Fl_Callback*)campaign_callback, this);

	custom_slots = new Fl_Input(170, 121, 365, 29, "Map order: ");
	custom_slots->tooltip("Comma- or space-separated map names, in campaign order");
	custom_slots->callback((Fl_Callback*)custom_slots_callback, this);
	custom_slots->when(FL_WHEN_CHANGED);
	custom_slots->deactivate();

	{
		Fl_Box *res_title = new Fl_Box(35, 174, 240, 30,
				"Resource Files or Folders:");
		res_title->labelfont(FL_HELVETICA_BOLD);
		res_title->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
	}

	BuildResourceList(35, 208, w() - 70, 162);
	wizard_pages[1]->end();

	// Page 3: destination and an explicit review before creation.
	wizard_pages[2] = new Fl_Group(0, 55, w(), 427);
	wizard_pages[2]->box(FL_FLAT_BOX);
	wizard_pages[2]->color(WINDOW_BG, WINDOW_BG);

	destination_input = new Fl_Input(145, 82, 350, 29, "Destination: ");
	destination_input->labelfont(FL_HELVETICA_BOLD);
	destination_input->tooltip("A new .wad or .pk3 package; existing files are never overwritten");
	destination_input->callback((Fl_Callback*)destination_callback, this);
	destination_input->when(FL_WHEN_CHANGED);

	{
		Fl_Button *choose = new Fl_Button(505, 84, 80, 25, "Choose...");
		choose->callback((Fl_Callback*)choose_destination_callback, this);
	}

	review_summary = new Fl_Box(35, 128, 550, 235);
	review_summary->box(FL_DOWN_BOX);
	review_summary->color(FL_BACKGROUND2_COLOR);
	review_summary->align(FL_ALIGN_TOP | FL_ALIGN_LEFT | FL_ALIGN_INSIDE |
			FL_ALIGN_WRAP);
	review_summary->labelfont(FL_COURIER);
	review_summary->labelsize(13);

	review_status = new Fl_Box(35, 374, 550, 52);
	review_status->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
	review_status->labelfont(FL_HELVETICA_BOLD);
	wizard_pages[2]->end();
	wizard->end();

	Fl_Group *bottom = new Fl_Group(0, 482, w(), h() - 482);
	bottom->box(FL_FLAT_BOX);
	bottom->color(WINDOW_BG, WINDOW_BG);

	cancel = new Fl_Button(30, 498, 90, 35, "Cancel");
	cancel->callback((Fl_Callback*)close_callback, this);

	back_but = new Fl_Button(365, 498, 90, 35, "Back");
	back_but->callback((Fl_Callback*)back_callback, this);

	next_but = new Fl_Return_Button(500, 498, 90, 35, "Next");
	next_but->labelfont(FL_HELVETICA_BOLD);
	next_but->callback((Fl_Callback*)next_callback, this);

	ok_but = new Fl_Return_Button(500, 498, 90, 35, "Create");
	ok_but->labelfont(FL_HELVETICA_BOLD);
	ok_but->callback((Fl_Callback*)use_callback, this);
	bottom->end();

	ShowWizardPage(0);
}


namespace
{

std::string ProjectPathText(const fs::path &path)
{
	const std::u8string text = path.u8string();
	return std::string(reinterpret_cast<const char *>(text.data()), text.size());
}

const char *ProjectPackageLabel(ProjectPackage package)
{
	return package == ProjectPackage::pk3 ? "PK3" : "WAD";
}

const char *CampaignLabel(CampaignMode campaign)
{
	if (campaign == CampaignMode::singleMap)
		return "Single map";
	if (campaign == CampaignMode::custom)
		return "Custom map order";
	return "Full IWAD replacement";
}

const char *MapFormatLabel(MapFormat format)
{
	if (format == MapFormat::hexen)
		return "Hexen";
	if (format == MapFormat::udmf)
		return "UDMF";
	if (format == MapFormat::doom)
		return "Doom";
	return "Not selected";
}

} // namespace


void UI_ProjectSetup::BuildResourceList(int x, int y, int width, int height)
{
	static const int columns[] = { 42, 175, 0 };
	resource_list = new Fl_Hold_Browser(x, y, width, height);
	resource_list->column_widths(columns);
	resource_list->format_char(0);
	resource_list->tooltip(
			"Resources load from top to bottom; later entries can override earlier ones");
	resource_list->callback(resource_selection_callback, this);

	const int buttonY = y + height + 9;
#ifdef __APPLE__
	resource_add_file = new Fl_Button(x, buttonY, 165, 27,
			"Add File or Folder...");
	resource_add_file->callback((Fl_Callback *)resource_add_file_callback, this);
	resource_remove = new Fl_Button(x + 181, buttonY, 90, 27, "Remove");
	resource_up = new Fl_Button(x + 279, buttonY, 78, 27, "Move Up");
	resource_down = new Fl_Button(x + 365, buttonY, 88, 27, "Move Down");
#else
	resource_add_file = new Fl_Button(x, buttonY, 90, 27, "Add File...");
	resource_add_file->callback((Fl_Callback *)resource_add_file_callback, this);
	resource_add_folder = new Fl_Button(x + 98, buttonY, 100, 27,
			"Add Folder...");
	resource_add_folder->callback((Fl_Callback *)resource_add_folder_callback,
			this);
	resource_remove = new Fl_Button(x + 214, buttonY, 90, 27, "Remove");
	resource_up = new Fl_Button(x + 312, buttonY, 78, 27, "Move Up");
	resource_down = new Fl_Button(x + 398, buttonY, 88, 27, "Move Down");
#endif
	resource_remove->callback((Fl_Callback *)resource_remove_callback, this);
	resource_up->callback((Fl_Callback *)resource_up_callback, this);
	resource_down->callback((Fl_Callback *)resource_down_callback, this);
	UpdateResourceButtons();
}


void UI_ProjectSetup::RefreshResourceList(int selectedRow)
{
	if (!resource_list)
		return;

	if (selectedRow <= 0)
		selectedRow = resource_list->value();
	resource_list->clear();
	for (size_t index = 0; index < result.resources.size(); ++index)
	{
		const fs::path &resource = result.resources[index];
		const fs::path name = resource.has_filename() ? resource.filename() :
				resource.parent_path().filename();
		const SString row = SString::printf("%zu.\t%s\t%s", index + 1,
				ProjectPathText(name).c_str(), ProjectPathText(resource).c_str());
		resource_list->add(row.c_str());
	}

	if (result.resources.empty())
		resource_list->value(0);
	else
	{
		selectedRow = std::clamp(selectedRow, 1,
				static_cast<int>(result.resources.size()));
		resource_list->value(selectedRow);
	}
	UpdateResourceButtons();
}


void UI_ProjectSetup::UpdateResourceButtons()
{
	if (!resource_list || !resource_remove || !resource_up || !resource_down)
		return;

	const int selected = resource_list->value();
	const bool hasSelection = selected > 0 &&
			selected <= static_cast<int>(result.resources.size());
	if (hasSelection)
		resource_remove->activate();
	else
		resource_remove->deactivate();

	if (hasSelection && selected > 1)
		resource_up->activate();
	else
		resource_up->deactivate();

	if (hasSelection && selected < static_cast<int>(result.resources.size()))
		resource_down->activate();
	else
		resource_down->deactivate();
}


void UI_ProjectSetup::ShowWizardPage(int page)
{
	if (!wizard)
		return;

	wizard_page = std::clamp(page, 0, 2);
	wizard->value(wizard_pages[wizard_page]);

	static const char *stepLabels[] =
	{
		"Step 1 of 3 - Project settings",
		"Step 2 of 3 - Campaign and resources",
		"Step 3 of 3 - Review and create"
	};
	wizard_step->copy_label(stepLabels[wizard_page]);

	if (wizard_page == 0)
		back_but->deactivate();
	else
		back_but->activate();

	if (wizard_page == 2)
	{
		next_but->hide();
		ok_but->show();
		UpdateWizardReview();
	}
	else
	{
		ok_but->hide();
		next_but->show();
		next_but->activate();
	}

	redraw();
}


bool UI_ProjectSetup::ValidateBasics(bool notify)
{
	SString message;
	if (result.game.empty())
		message = "Choose a supported game IWAD before continuing.";
	else
	{
		const fs::path *iwad = global::recent.queryIWAD(result.game);
		std::error_code error;
		if (!iwad || !fs::is_regular_file(*iwad, error) || error)
			message = "The selected game IWAD is no longer available.";
	}

	if (message.empty() && result.port.empty())
		message = "Choose a source-port compatibility profile before continuing.";
	else if (message.empty() && result.mapFormat == MapFormat::invalid)
		message = "Choose a map format before continuing.";
	else if (message.empty() && result.package != ProjectPackage::wad &&
			result.package != ProjectPackage::pk3)
		message = "Choose a supported project package type before continuing.";

	if (!message.empty() && notify)
		DLG_Notify("Project settings are incomplete:\n\n%s", message.c_str());
	return message.empty();
}


bool UI_ProjectSetup::ValidateCampaignAndResources(bool notify)
{
	SString error;
	if (result.campaign == CampaignMode::custom)
	{
		std::optional<std::vector<SString>> slots = M_ParseCustomMapSlots(
				custom_slots ? custom_slots->value() : "", &error);
		if (slots)
			result.mapSlots = std::move(*slots);
	}
	else if (newProject_ && result.mapSlots.empty())
	{
		error = "The selected IWAD does not contain a valid map slot.";
	}
	if (error.empty())
	{
		const ProjectResourceValidation validation =
				M_ValidateProjectResources(result.resources);
		if (!validation.valid())
			error = validation.message;
	}

	if (!error.empty() && notify)
		DLG_Notify("Invalid campaign or resource settings:\n\n%s", error.c_str());
	return error.empty();
}


ProjectDestinationValidation UI_ProjectSetup::ValidateDestination()
{
	fs::path destination;
	if (destination_input && destination_input->value()[0])
	{
		destination = fs::path(reinterpret_cast<const char8_t *>(
				destination_input->value()));
	}

	const fs::path normalized = M_NormalizeProjectDestination(destination,
			result.package);
	const bool knownIwad = !normalized.empty() &&
			global::recent.hasIwadByPath(normalized);
	return M_ValidateProjectDestination(normalized, result.package, knownIwad);
}


void UI_ProjectSetup::UpdateWizardReview()
{
	if (!newProject_ || !review_summary || !review_status)
		return;

	const bool basicsValid = ValidateBasics(false);
	const bool campaignValid = ValidateCampaignAndResources(false);
	ProjectDestinationValidation destination = ValidateDestination();
	result.destination = destination.destination;

	const fs::path *iwad = result.game.empty() ? nullptr :
			global::recent.queryIWAD(result.game);
	std::ostringstream summary;
	summary << "Package:       " << ProjectPackageLabel(result.package) << '\n';
	summary << "Game IWAD:     ";
	if (iwad)
		summary << ProjectPathText(*iwad);
	else
		summary << "Not selected";
	summary << '\n';
	summary << "Compatibility: " << result.port.c_str() << " / "
			<< MapFormatLabel(result.mapFormat) << '\n';
	summary << "Campaign:      " << CampaignLabel(result.campaign);
	if (!campaignValid && result.campaign == CampaignMode::custom)
		summary << " (invalid map order)";
	else if (!result.mapSlots.empty())
	{
		const size_t slotCount = result.campaign == CampaignMode::singleMap ?
				1 : result.mapSlots.size();
		summary << " (" << slotCount << " map" << (slotCount == 1 ? "" : "s")
				<< ", starting at " << result.mapSlots.front().c_str() << ')';
	}
	summary << '\n';

	const size_t resourceCount = result.resources.size();
	summary << "Resources:     " << resourceCount << '\n';
	if (resourceCount > 0)
	{
		constexpr size_t maxReviewedResources = 6;
		const size_t shownResources = std::min(resourceCount,
				maxReviewedResources);
		for (size_t index = 0; index < shownResources; ++index)
		{
			summary << "  " << index + 1 << ". "
					<< ProjectPathText(result.resources[index]) << '\n';
		}
		if (shownResources < resourceCount)
			summary << "  ... and " << resourceCount - shownResources
					<< " more\n";
	}
	summary << "Destination:   ";
	if (destination.destination.empty())
		summary << "Not selected";
	else
		summary << ProjectPathText(destination.destination);
	review_summary->copy_label(summary.str().c_str());

	SString status;
	if (!basicsValid)
		status = "Return to Project settings and complete every required choice.";
	else if (!campaignValid)
		status = "Return to Campaign and resources and correct the invalid selection.";
	else if (!destination.valid())
		status = destination.message;
	else
		status = "Ready to create. The destination is new and the game IWAD will remain read-only.";

	review_status->copy_label(status.c_str());
	review_status->labelcolor(basicsValid && campaignValid && destination.valid() ?
			fl_rgb_color(0x18, 0x70, 0x32) : FL_RED);

	if (basicsValid && campaignValid && destination.valid())
		ok_but->activate();
	else
		ok_but->deactivate();
}


void UI_ProjectSetup::back_callback(Fl_Button *, void *data)
{
	UI_ProjectSetup *that = static_cast<UI_ProjectSetup *>(data);
	that->ShowWizardPage(that->wizard_page - 1);
}


void UI_ProjectSetup::next_callback(Fl_Button *, void *data)
{
	UI_ProjectSetup *that = static_cast<UI_ProjectSetup *>(data);
	if (that->wizard_page == 0 && !that->ValidateBasics(true))
		return;
	if (that->wizard_page == 1 && !that->ValidateCampaignAndResources(true))
		return;
	that->ShowWizardPage(that->wizard_page + 1);
}


void UI_ProjectSetup::destination_callback(Fl_Input *, void *data)
{
	UI_ProjectSetup *that = static_cast<UI_ProjectSetup *>(data);
	that->UpdateWizardReview();
}


void UI_ProjectSetup::choose_destination_callback(Fl_Button *, void *data)
{
	UI_ProjectSetup *that = static_cast<UI_ProjectSetup *>(data);
	Fl_Native_File_Chooser chooser;
	chooser.title("Choose new project destination");
	chooser.type(Fl_Native_File_Chooser::BROWSE_SAVE_FILE);
	chooser.filter(that->result.package == ProjectPackage::pk3 ?
			"PK3 Packages\t*.pk3" : "WAD Packages\t*.wad");
	chooser.directory(reinterpret_cast<const char *>(
			that->inst.Main_FileOpFolder().u8string().c_str()));

	switch (chooser.show())
	{
		case -1:
			DLG_Notify("Unable to choose a project destination:\n\n%s",
					chooser.errmsg());
			return;

		case 1:
			return;

		default:
			break;
	}

	fs::path destination = fs::path(reinterpret_cast<const char8_t *>(
			chooser.filename()));
	destination = M_NormalizeProjectDestination(destination,
			that->result.package);
	that->destination_input->value(ProjectPathText(destination).c_str());
	that->UpdateWizardReview();
}


std::optional<UI_ProjectSetup::Result> UI_ProjectSetup::Run()
{
	PopulateIWADs();
	PopulatePort();
	PopulateMapFormat();
	PopulateCampaign();
	PopulateResources();
	if (newProject_)
		ShowWizardPage(0);

	set_modal();

	show();

	while (action == Action::none)
	{
		Fl::wait(0.2);
	}

	return (action == Action::accept) ? result : std::optional<UI_ProjectSetup::Result>{};
}


void UI_ProjectSetup::PopulateCampaign()
{
	if (!campaign_choice || !custom_slots)
		return;

	if (!newProject_ && inst.loaded.project.isExplicit())
	{
		result.campaign = inst.loaded.project.campaign;
		result.mapSlots = inst.loaded.project.mapSlots;
	}

	if (result.mapSlots.empty())
	{
		const fs::path *iwadPath = global::recent.queryIWAD(result.game);
		std::shared_ptr<Wad_file> iwad = iwadPath ?
				Wad_file::Open(*iwadPath, WadOpenMode::read) : nullptr;
		if (iwad)
			result.mapSlots = M_ProjectMapSlots(*iwad);
	}

	int selection = 0;
	if (result.campaign == CampaignMode::singleMap)
		selection = 1;
	else if (result.campaign == CampaignMode::custom)
		selection = 2;
	campaign_choice->value(selection);
	const SString formatted = M_FormatCustomMapSlots(result.mapSlots);
	custom_slots->value(formatted.c_str());
	if (result.campaign == CampaignMode::custom)
		custom_slots->activate();
	else
		custom_slots->deactivate();
}

void UI_ProjectSetup::PopulateIWADs()
{
	// This is called (a) when dialog is first opened, or (b) when
	// the user has found a new iwad.  For the latter case, we want
	// to show the newly found game.

	SString prev_game = result.game;

	if (prev_game.empty())
		prev_game = inst.loaded.gameName;
	if (prev_game.empty())
		prev_game = "doom2";


	result.game.clear();
	game_choice->clear();


	SString menu_string;
	int menu_value = 0;

	menu_string = global::recent.collectGamesForMenu(&menu_value, prev_game.c_str());

	if (!menu_string.empty())
	{
		game_choice->add(menu_string.c_str());
		game_choice->value(menu_value);

		result.game = game_choice->mvalue()->text;
	}

	if (!result.game.empty())
		ok_but->activate();
	else
		ok_but->deactivate();
}


void UI_ProjectSetup::PopulatePort()
{
	SString prev_port;

	if (port_choice->mvalue())
		prev_port = port_choice->mvalue()->text;

	if (prev_port.empty())
		prev_port = inst.loaded.portName;
	if (prev_port.empty())
		prev_port = "vanilla";


	result.port = "vanilla";

	port_choice->clear();

	// if no game, port doesn't matter
	if (result.game.empty())
		return;


	SString base_game;

	if (game_choice->mvalue())
		base_game = M_GetBaseGame(game_choice->mvalue()->text);
	else if (!inst.loaded.gameName.empty())
		base_game = M_GetBaseGame(inst.loaded.gameName);

	if (base_game.empty())
		base_game = "doom2";


	int menu_value = 0;

	SString menu_string = M_CollectPortsForMenu(base_game.c_str(), &menu_value, prev_port.c_str());

	if (!menu_string.empty())
	{
		port_choice->add  (menu_string.c_str());
		port_choice->value(menu_value);

		result.port = port_choice->mvalue()->text;
	}
}


void UI_ProjectSetup::PopulateMapFormat()
{
	MapFormat prev_fmt = result.mapFormat;

	if (prev_fmt == MapFormat::invalid)
		prev_fmt = inst.loaded.levelFormat;


	format_choice->clear();

	// if no game, format doesn't matter
	if (result.game.empty())
	{
		result.mapFormat = MapFormat::doom;
		result.nameSpace = "";
		return;
	}


	// determine the usable formats, from current game and port
	const char *c_game = "doom2";
	const char *c_port = "vanilla";

	if (game_choice->mvalue())
		c_game = game_choice->mvalue()->text;

	if (port_choice->mvalue())
		c_port = port_choice->mvalue()->text;

	usable_formats = M_DetermineMapFormats(c_game, c_port);

	SYS_ASSERT(usable_formats != 0);


	// reconstruct the menu
	int menu_value = 0;
	int entry_id = 0;

	if (usable_formats & (1 << static_cast<int>(MapFormat::doom)))
	{
		format_choice->add("Doom Format");
		entry_id++;
	}

	if (usable_formats & (1 << static_cast<int>(MapFormat::hexen)))
	{
		if (prev_fmt == MapFormat::hexen)
			menu_value = entry_id;

		format_choice->add("Hexen Format");
		entry_id++;
	}

	if (global::udmf_testing && (usable_formats & (1 << static_cast<int>(MapFormat::udmf))))
	{
		if (prev_fmt == MapFormat::udmf)
			menu_value = entry_id;

		format_choice->add("UDMF");
		entry_id++;
	}

	format_choice->value(menu_value);

	// set map_format field based on current menu entry.
	format_callback(format_choice, (void *)this);


	// determine the UDMF namespace
	result.nameSpace = "";

	const PortInfo_c *pinfo = M_LoadPortInfo(port_choice->mvalue()->text);
	if (pinfo)
		result.nameSpace = pinfo->udmf_namespace;

	// don't leave namespace as "" when chosen format is UDMF.
	// [ this is to handle broken config files somewhat sanely ]
	if (result.nameSpace.empty() && result.mapFormat == MapFormat::udmf)
		result.nameSpace = "Hexen";
}


void UI_ProjectSetup::PopulateNamespaces()
{
#if 0  // Disabled for now

	if (map_format != MAPF_UDMF)
	{
		namespace_choice->hide();
		return;
	}

	namespace_choice->show();

	// get previous value
	const char *prev_ns = name_space.c_str();

	if (prev_ns[0] == 0)
		prev_ns = Udmf_namespace.c_str();


	namespace_choice->clear();

	if (! port_choice->mvalue())
		return;

	PortInfo_c *pinfo = M_LoadPortInfo(port_choice->mvalue()->text);
	if (! pinfo)
		return;

	int menu_value = 0;

	for (int i = 0 ; i < (int)pinfo->namespaces.size() ; i++)
	{
		const char * ns = pinfo->namespaces[i].c_str();

		namespace_choice->add(ns);

		// keep same entry as before, when possible
		if (strcmp(prev_ns, ns) == 0)
			menu_value = i;
	}

	namespace_choice->value(menu_value);

	if (menu_value < (int)pinfo->namespaces.size())
		name_space = pinfo->namespaces[menu_value];
#endif
}

void UI_ProjectSetup::PopulateResources()
{
	// Note: these resource wads may be invalid (not exist) during startup.
	//       This is probably NOT the place to validate them...
	result.resources = inst.loaded.resourceList;
	RefreshResourceList();
}


void UI_ProjectSetup::close_callback(Fl_Widget *w, void *data)
{
	UI_ProjectSetup * that = (UI_ProjectSetup *)data;

	that->action = Action::cancel;
}


void UI_ProjectSetup::use_callback(Fl_Button *w, void *data)
{
	UI_ProjectSetup * that = (UI_ProjectSetup *)data;
	if (!that->ValidateBasics(true) ||
			(that->custom_slots && !that->ValidateCampaignAndResources(true)))
		return;

	if (that->newProject_)
	{
		ProjectDestinationValidation validation = that->ValidateDestination();
		if (!validation.valid())
		{
			DLG_Notify("Cannot create the project:\n\n%s",
					validation.message.c_str());
			return;
		}
		that->result.destination = validation.destination;
		that->destination_input->value(
				ProjectPathText(validation.destination).c_str());
	}

	that->action = Action::accept;
}


void UI_ProjectSetup::game_callback(Fl_Choice *w, void *data)
{
	UI_ProjectSetup * that = (UI_ProjectSetup *)data;

	const char * name = w->mvalue()->text;

	if (global::recent.queryIWAD(name))
	{
		that->result.game = name;
		that->ok_but->activate();
	}
	else
	{
		that->result.game.clear();
		that->ok_but->deactivate();
	}

	that->PopulatePort();
	that->PopulateMapFormat();
	if (that->result.campaign != CampaignMode::custom)
	{
		that->result.mapSlots.clear();
		that->PopulateCampaign();
	}
	that->UpdateWizardReview();
}


void UI_ProjectSetup::port_callback(Fl_Choice *w, void *data)
{
	UI_ProjectSetup * that = (UI_ProjectSetup *)data;

	const char * name = w->mvalue()->text;

	that->result.port = name;

	that->PopulateMapFormat();
	that->UpdateWizardReview();
}


void UI_ProjectSetup::format_callback(Fl_Choice *w, void *data)
{
	UI_ProjectSetup * that = (UI_ProjectSetup *)data;

	const char * fmt_str = w->mvalue()->text;

	if (strstr(fmt_str, "UDMF"))
		that->result.mapFormat = MapFormat::udmf;
	else if (strstr(fmt_str, "Hexen"))
		that->result.mapFormat = MapFormat::hexen;
	else
		that->result.mapFormat = MapFormat::doom;

	that->PopulateNamespaces();
	that->UpdateWizardReview();
}

void UI_ProjectSetup::campaign_callback(Fl_Choice *w, void *data)
{
	UI_ProjectSetup *that = static_cast<UI_ProjectSetup *>(data);
	that->result.campaign = w->value() == 2 ? CampaignMode::custom :
			(w->value() == 1 ? CampaignMode::singleMap : CampaignMode::fullIwad);
	if (that->result.campaign == CampaignMode::custom)
		that->custom_slots->activate();
	else
	{
		that->custom_slots->deactivate();
		if (that->newProject_)
		{
			that->result.mapSlots.clear();
			that->PopulateCampaign();
		}
	}
	that->UpdateWizardReview();
}


void UI_ProjectSetup::custom_slots_callback(Fl_Input *input, void *data)
{
	UI_ProjectSetup *that = static_cast<UI_ProjectSetup *>(data);
	SString ignored;
	std::optional<std::vector<SString>> slots =
			M_ParseCustomMapSlots(input->value(), &ignored);
	if (slots)
		that->result.mapSlots = std::move(*slots);
	that->UpdateWizardReview();
}

void UI_ProjectSetup::package_callback(Fl_Choice *w, void *data)
{
	UI_ProjectSetup *that = static_cast<UI_ProjectSetup *>(data);
	that->result.package = (w->value() == 1) ? ProjectPackage::pk3 :
			ProjectPackage::wad;
	that->UpdateWizardReview();
}


void UI_ProjectSetup::namespace_callback(Fl_Choice *w, void *data)
{
	UI_ProjectSetup * that = (UI_ProjectSetup *)data;

	that->result.nameSpace = w->mvalue()->text;
}


void UI_ProjectSetup::find_callback(Fl_Button *w, void *data)
{
	UI_ProjectSetup * that = (UI_ProjectSetup *)data;

	Fl_Native_File_Chooser chooser;

	chooser.title("Pick file to open");
	chooser.type(Fl_Native_File_Chooser::BROWSE_FILE);
	chooser.filter("Wads\t*.wad");
	chooser.directory(reinterpret_cast<const char *>(that->inst.Main_FileOpFolder().u8string().c_str()));

	switch (chooser.show())
	{
		case -1:  // error
			DLG_Notify("Unable to open that wad:\n\n%s", chooser.errmsg());
			return;

		case 1:  // cancelled
			return;

		default:
			break;  // OK
	}

	// check that a game definition exists

	SString game = GameNameFromIWAD(fs::path(reinterpret_cast<const char8_t *>(chooser.filename())));

	if (! M_CanLoadDefinitions(global::home_dir, global::old_linux_home_and_cache_dir,
			global::install_dir, GAMES_DIR, game))
	{
		DLG_Notify("That game is not supported (no definition file).\n\n"
		           "Please try again.");
		return;
	}

	global::recent.addIWAD(fs::path(reinterpret_cast<const char8_t *>(chooser.filename())));
	global::recent.save(global::home_dir);

	that->result.game = game;

	that->PopulateIWADs();
	that->PopulatePort();
	that->PopulateMapFormat();
	that->UpdateWizardReview();
}

void UI_ProjectSetup::setup_callback(Fl_Button *w, void *data)
{
	UI_ProjectSetup * that = (UI_ProjectSetup *)data;

	// FIXME : deactivate button when this is true
	if (that->result.game.empty() || that->result.port.empty())
	{
		fl_beep();
		return;
	}

	that->inst.M_PortSetupDialog(that->result.port, that->result.game, {});
}


void UI_ProjectSetup::resource_selection_callback(Fl_Widget *, void *data)
{
	auto that = static_cast<UI_ProjectSetup *>(data);
	that->UpdateResourceButtons();
}


void UI_ProjectSetup::resource_add_file_callback(Fl_Button *, void *data)
{
	auto that = static_cast<UI_ProjectSetup *>(data);
#ifdef __APPLE__
	that->ChooseAndAddResource(true);
#else
	that->ChooseAndAddResource(false);
#endif
}


void UI_ProjectSetup::resource_add_folder_callback(Fl_Button *, void *data)
{
	auto that = static_cast<UI_ProjectSetup *>(data);
	that->ChooseAndAddResource(true);
}


void UI_ProjectSetup::ChooseAndAddResource(bool directory)
{
	const char *title = directory ? "Pick resource folder" :
			"Pick resource file";
#ifdef __APPLE__
	title = "Pick resource file or folder";
	directory = true;
#endif

	Fl_Native_File_Chooser chooser;

	chooser.title(title);
	if (directory)
	{
		chooser.type(Fl_Native_File_Chooser::BROWSE_DIRECTORY);
#ifdef __APPLE__
		chooser.filter("WAD/PK3 resources\t*.{wad,pk3,zip}\nHeresy Editor defs\t*.ugh\nDehacked files\t*.deh\nBEX files\t*.bex");
#endif
	}
	else
	{
		chooser.type(Fl_Native_File_Chooser::BROWSE_FILE);
		chooser.filter("WAD/PK3 resources\t*.{wad,pk3,zip}\nHeresy Editor defs\t*.ugh\nDehacked files\t*.deh\nBEX files\t*.bex");
	}
	chooser.directory(reinterpret_cast<const char *>(
			inst.Main_FileOpFolder().u8string().c_str()));

	switch (chooser.show())
	{
		case -1:  // error
			DLG_Notify("Unable to choose that resource:\n\n%s", chooser.errmsg());
			return;

		case 1:  // cancelled
			return;

		default:
			break;  // OK
	}

	const fs::path resource(reinterpret_cast<const char8_t *>(chooser.filename()));
	ProjectResourceValidation validation;
	if (!M_AddProjectResource(result.resources, resource, &validation))
	{
		DLG_Notify("Cannot add that resource:\n\n%s",
				validation.message.c_str());
		return;
	}

	RefreshResourceList(static_cast<int>(result.resources.size()));
	UpdateWizardReview();
}


void UI_ProjectSetup::resource_remove_callback(Fl_Button *, void *data)
{
	auto that = static_cast<UI_ProjectSetup *>(data);
	const int selected = that->resource_list->value();
	if (selected <= 0 || !M_RemoveProjectResource(that->result.resources,
			static_cast<size_t>(selected - 1)))
		return;

	that->RefreshResourceList(selected);
	that->UpdateWizardReview();
}


void UI_ProjectSetup::resource_up_callback(Fl_Button *, void *data)
{
	auto that = static_cast<UI_ProjectSetup *>(data);
	const int selected = that->resource_list->value();
	if (selected <= 0 || !M_MoveProjectResource(that->result.resources,
			static_cast<size_t>(selected - 1), -1))
		return;

	that->RefreshResourceList(selected - 1);
	that->UpdateWizardReview();
}


void UI_ProjectSetup::resource_down_callback(Fl_Button *, void *data)
{
	auto that = static_cast<UI_ProjectSetup *>(data);
	const int selected = that->resource_list->value();
	if (selected <= 0 || !M_MoveProjectResource(that->result.resources,
			static_cast<size_t>(selected - 1), 1))
		return;

	that->RefreshResourceList(selected + 1);
	that->UpdateWizardReview();
}


//--- editor settings ---
// vi:ts=4:sw=4:noexpandtab
