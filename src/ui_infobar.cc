//------------------------------------------------------------------------
//  Information Bar (bottom of window)
//------------------------------------------------------------------------
//
//  Eureka DOOM Editor
//
//  Copyright (C) 2007-2019 Andrew Apted
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

#include "Instance.h"
#include "main.h"
#include "ui_window.h"

#include "e_main.h"
#include "e_linedef.h"
#include "LineDef.h"
#include "m_config.h"
#include "m_game.h"
#include "m_grid_theme.h"
#include "m_units.h"
#include "m_vector.h"
#include "r_grid.h"
#include "r_render.h"
#include "SideDef.h"
#include "ui_grid.h"
#include "Vertex.h"

#include <cstdint>


#define FREE_COLOR  (config::gui_scheme == 2 ? fl_rgb_color(0,192,0) : fl_rgb_color(128, 255, 128))

#define RATIO_COLOR  FL_YELLOW


const char *UI_InfoBar::scale_options_str =
	"0.2%|0.4%|0.8%|1.6%|  3%|  6%| 12%| 25%| 33%| 50%|100%|200%|400%|800%";

const double UI_InfoBar::scale_amounts[14] =
{
	1.0 / 512.0, 1.0 / 256.0, 1.0 / 128.0, 1.0 / 64.0, 1.0 / 32.0,
	0.0625, 0.125, 0.25, 0.33333, 0.5, 1.0, 2.0, 4.0, 8.0
};

//
// UI_InfoBar Constructor
//
UI_InfoBar::UI_InfoBar(Instance &inst, int X, int Y, int W, int H, const char *label) :
    Fl_Group(X, Y, W, H, label), inst(inst)
{
	box(FL_FLAT_BOX);


	// Fitts' law : keep buttons flush with bottom of window
	Y += 4;
	H -= 4;


	Fl_Box *mode_lab = new Fl_Box(FL_NO_BOX, X, Y, 56, H, "Mode:");
	mode_lab->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
	mode_lab->labelsize(16);

	mode = new Fl_Menu_Button(X+58, Y, 96, H, "Things");
	mode->align(FL_ALIGN_INSIDE);
	mode->add("Things|Linedefs|Sectors|Vertices");
	mode->callback(mode_callback, this);
	mode->labelsize(16);

	X = mode->x() + mode->w() + 10;


	Fl_Box *scale_lab = new Fl_Box(FL_NO_BOX, X, Y, 58, H, "Scale:");
	scale_lab->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
	scale_lab->labelsize(16);

	scale = new Fl_Menu_Button(X+60+26, Y, 78, H, "100%");
	scale->align(FL_ALIGN_INSIDE);
	scale->add(scale_options_str);
	scale->callback(scale_callback, this);
	scale->labelsize(16);

	Fl_Button *sc_minus, *sc_plus;

	sc_minus = new Fl_Button(X+60, Y+1, 24, H-2, "-");
	sc_minus->callback(sc_minus_callback, this);
	sc_minus->labelfont(FL_HELVETICA_BOLD);
	sc_minus->labelsize(16);

	sc_plus = new Fl_Button(X+60+26+80, Y+1, 24, H-2, "+");
	sc_plus->callback(sc_plus_callback, this);
	sc_plus->labelfont(FL_HELVETICA_BOLD);
	sc_plus->labelsize(16);

	X = sc_plus->x() + sc_plus->w() + 12;


	Fl_Box *gs_lab = new Fl_Box(FL_NO_BOX, X, Y, 42, H, "Grid:");
	gs_lab->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
	gs_lab->labelsize(16);

	grid_size = new Fl_Menu_Button(X+44, Y, 72, H, "OFF");

	grid_size->align(FL_ALIGN_INSIDE);
	for (const grid::StepPreset &preset : grid::StepPresets())
	{
		const SString path = SString(preset.group) + "/" + preset.label;
		grid_size->add(path.c_str(), 0, nullptr,
				reinterpret_cast<void *>(
						static_cast<intptr_t>(preset.step)));
	}
	grid_size->add("Actions/Mathematical Grid...", 0, nullptr,
			reinterpret_cast<void *>(static_cast<intptr_t>(-2)),
			FL_MENU_DIVIDER);
	grid_size->add("Actions/Origin at pointer", 0, nullptr,
			reinterpret_cast<void *>(static_cast<intptr_t>(-3)));
	grid_size->add("Actions/Origin at world 0,0", 0, nullptr,
			reinterpret_cast<void *>(static_cast<intptr_t>(-4)));
	grid_size->add("Actions/Grid off", 0, nullptr,
			reinterpret_cast<void *>(static_cast<intptr_t>(-1)));
	grid_size->callback(grid_callback, this);
	grid_size->labelsize(16);
	grid_size->tooltip(
			"Choose grouped grid sequences or open Mathematical Grid (Alt+G)");

	// Size the button for its longest "step  (real)" label (see SetGrid),
	// otherwise the real-world unit suffix spills over the SNAP toggle.
	{
		fl_font(FL_HELVETICA, 16);

		int best_w = 72;
		for (const grid::StepPreset &preset : grid::StepPresets())
		{
			const SString realSize = units::FormatLength(preset.step);
			const SString text = realSize.good()
				? SString::printf("%d  (%s)", preset.step, realSize.c_str())
				: SString::printf("%d", preset.step);

			best_w = std::max(best_w, static_cast<int>(fl_width(text.c_str())) + 34);
		}
		grid_size->size(best_w, H);
	}

	X = grid_size->x() + grid_size->w() + 12;


	grid_snap = new Fl_Toggle_Button(X+4, Y, 72, H);
	grid_snap->value(inst.grid.snaps() ? 1 : 0);
	grid_snap->color(FREE_COLOR);
	grid_snap->selection_color(
			grid::ActiveVisualPalette().snapTarget);
	grid_snap->callback(snap_callback, this);
	grid_snap->labelsize(16);

	UpdateSnapText();

	X = grid_snap->x() + grid_snap->w() + 12;


	Fl_Box *ratio_lab = new Fl_Box(FL_NO_BOX, X, Y, 52, H, "Ratio:");
	ratio_lab->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
	ratio_lab->labelsize(16);

	ratio_lock = new Fl_Menu_Button(X+54, Y, 106, H, "UNLOCK");
	ratio_lock->align(FL_ALIGN_INSIDE);
	ratio_lock->add("UNLOCK|1:1|2:1|4:1|8:1|5:4|7:4|User Value");
	ratio_lock->callback(ratio_callback, this);
	ratio_lock->labelsize(16);

	X = ratio_lock->x() + ratio_lock->w() + 12;


	Fl_Box *rend_lab = new Fl_Box(FL_FLAT_BOX, X, Y, 56, H, "Rend:");
	rend_lab->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
	rend_lab->labelsize(16);

	sec_rend = new Fl_Menu_Button(X+58, Y, 96, H, "PLAIN");
	sec_rend->align(FL_ALIGN_INSIDE);
	sec_rend->add("PLAIN|Floor|Ceiling|Lighting|Floor Bright|Ceil Bright|Sound|3D VIEW");
	sec_rend->callback(rend_callback, this);
	sec_rend->labelsize(16);
	sec_rend->tooltip(
			"F8 opens render modes · Shift+F8 / Ctrl/Cmd+F8 cycle · "
			"Shift+Tab inspects every linedef in 3D");

	X = sec_rend->x() + rend_lab->w() + 10;


	resizable(NULL);

	end();
}

//
// UI_InfoBar Destructor
//
UI_InfoBar::~UI_InfoBar()
{ }


int UI_InfoBar::handle(int event)
{
	return Fl_Group::handle(event);
}


void UI_InfoBar::mode_callback(Fl_Widget *w, void *data)
{
	Fl_Menu_Button *mode = (Fl_Menu_Button *)w;

	static const char *mode_keys = "tlsvr";
	auto bar = static_cast<const UI_InfoBar *>(data);

	bar->inst.Editor_ChangeMode(mode_keys[mode->value()]);
}


void UI_InfoBar::rend_callback(Fl_Widget *w, void *data)
{
	Fl_Menu_Button *sec_rend = (Fl_Menu_Button *)w;

	auto bar = static_cast<const UI_InfoBar *>(data);

	// last option is 3D mode
	if (sec_rend->value() > SREND_SoundProp)
	{
		Render3D_Enable(bar->inst, true);
		return;
	}

	switch (sec_rend->value())
	{
	case 1: bar->inst.edit.sector_render_mode = SREND_Floor; break;
	case 2: bar->inst.edit.sector_render_mode = SREND_Ceiling; break;
	case 3: bar->inst.edit.sector_render_mode = SREND_Lighting; break;
	case 4: bar->inst.edit.sector_render_mode = SREND_FloorBright; break;
	case 5: bar->inst.edit.sector_render_mode = SREND_CeilBright; break;
	case 6: bar->inst.edit.sector_render_mode = SREND_SoundProp; break;
	default: bar->inst.edit.sector_render_mode = SREND_Nothing; break;
	}

	if (bar->inst.edit.render3d)
		Render3D_Enable(bar->inst, false);

	// need sectors mode for sound propagation display
	if (bar->inst.edit.sector_render_mode == SREND_SoundProp && bar->inst.edit.mode != ObjType::sectors)
		bar->inst.Editor_ChangeMode('s');

	bar->inst.RedrawMap();
}


void UI_InfoBar::scale_callback(Fl_Widget *w, void *data)
{
	auto bar = static_cast<UI_InfoBar *>(data);
	Fl_Menu_Button *scale = (Fl_Menu_Button *)w;

	double new_scale = scale_amounts[scale->value()];

	bar->inst.grid.NearestScale(new_scale);
}


void UI_InfoBar::sc_minus_callback(Fl_Widget *w, void *data)
{
	auto bar = static_cast<UI_InfoBar *>(data);
	bar->inst.ExecuteCommand("Zoom", "-1", "/center");
}

void UI_InfoBar::sc_plus_callback(Fl_Widget *w, void *data)
{
	auto bar = static_cast<UI_InfoBar *>(data);
	bar->inst.ExecuteCommand("Zoom", "+1", "/center");
}


void UI_InfoBar::grid_callback(Fl_Widget *w, void *data)
{
	auto bar = static_cast<UI_InfoBar *>(data);
	Fl_Menu_Button *gsize = (Fl_Menu_Button *)w;

	if (!gsize->mvalue())
		return;
	const int new_step = static_cast<int>(
			gsize->mvalue()->argument());

	if (new_step == -2)
		UI_RunMathematicalGrid(bar->inst);
	else if (new_step == -3)
		bar->inst.grid.SetOrigin(bar->inst.edit.map.xy);
	else if (new_step == -4)
		bar->inst.grid.SetOrigin({});
	else if (new_step < 0)
		bar->inst.grid.SetShown(false);
	else
		bar->inst.grid.ForceStep(new_step);
}


void UI_InfoBar::snap_callback(Fl_Widget *w, void *data)
{
	auto bar = static_cast<UI_InfoBar *>(data);
	Fl_Toggle_Button *grid_snap = (Fl_Toggle_Button *)w;

	// update editor state
	bar->inst.grid.SetSnap(grid_snap->value() ? true : false);
}


void UI_InfoBar::ratio_callback(Fl_Widget *w, void *data)
{
	Fl_Menu_Button *ratio_lock = (Fl_Menu_Button *)w;
	auto bar = static_cast<const UI_InfoBar *>(data);

	bar->inst.grid.configureRatio(ratio_lock->value(), false);
}


//------------------------------------------------------------------------

void UI_InfoBar::NewEditMode(ObjType new_mode)
{
	switch (new_mode)
	{
		case ObjType::things:   mode->value(0); break;
		case ObjType::linedefs: mode->value(1); break;
		case ObjType::sectors:  mode->value(2); break;
		case ObjType::vertices: mode->value(3); break;

		default: break;
	}

	UpdateModeColor();
}


void UI_InfoBar::SetMouse()
{
	// TODO this method should go away

	inst.main_win->status_bar->redraw();
}


void UI_InfoBar::SetScale(double new_scale)
{
	double perc = new_scale * 100.0;

	char buffer[64];

	if (perc < 10.0)
		snprintf(buffer, sizeof(buffer), "%1.1f%%", perc);
	else
		snprintf(buffer, sizeof(buffer), "%3d%%", (int)perc);

	scale->copy_label(buffer);
}

void UI_InfoBar::SetGrid(int new_step)
{
	if (new_step < 0)
	{
		grid_size->label("OFF");
	}
	else
	{
		char buffer[64];
		const SString realSize = units::FormatLength(new_step);
		if (realSize.good())
			snprintf(buffer, sizeof(buffer), "%d  (%s)", new_step, realSize.c_str());
		else
			snprintf(buffer, sizeof(buffer), "%d", new_step);
		grid_size->copy_label(buffer);
	}
}


void UI_InfoBar::UpdateSnap()
{
   grid_snap->value(inst.grid.snaps() ? 1 : 0);

   UpdateSnapText();
}


void UI_InfoBar::UpdateSecRend()
{
	if (inst.edit.render3d)
	{
		sec_rend->label("3D VIEW");
		return;
	}

	switch (inst.edit.sector_render_mode)
	{
	case SREND_Floor:       sec_rend->label("Floor");   break;
	case SREND_Ceiling:     sec_rend->label("Ceiling"); break;
	case SREND_Lighting:    sec_rend->label("Lighting"); break;
	case SREND_FloorBright: sec_rend->label("Floor Brt"); break;
	case SREND_CeilBright:  sec_rend->label("Ceil Brt"); break;
	case SREND_SoundProp:   sec_rend->label("Sound");    break;
	default:                sec_rend->label("PLAIN");    break;
	}
}


void UI_InfoBar::UpdateRatio()
{
	if (inst.grid.getRatio() == 0)
		ratio_lock->color(FL_BACKGROUND_COLOR);
	else
		ratio_lock->color(RATIO_COLOR);

	if (inst.grid.getRatio() == 7)
	{
		char buffer[256];
		snprintf(buffer, sizeof(buffer), "Usr %d:%d", config::grid_ratio_high, config::grid_ratio_low);

		// drop the "Usr" part when overly long
		if (strlen(buffer) > 9)
			ratio_lock->copy_label(buffer+4);
		else
			ratio_lock->copy_label(buffer);
	}
	else
	{
		ratio_lock->copy_label(ratio_lock->text(inst.grid.getRatio()));
	}
}


void UI_InfoBar::UpdateModeColor()
{
	switch (mode->value())
	{
		case 0: mode->label("Things");   mode->color(THING_MODE_COL);  break;
		case 1: mode->label("Linedefs"); mode->color(LINE_MODE_COL);   break;
		case 2: mode->label("Sectors");  mode->color(SECTOR_MODE_COL); break;
		case 3: mode->label("Vertices"); mode->color(VERTEX_MODE_COL); break;
	}
}


void UI_InfoBar::UpdateSnapText()
{
	grid_snap->selection_color(
			grid::ActiveVisualPalette().snapTarget);
	if (grid_snap->value())
	{
		grid_snap->label("SNAP ON");
		grid_snap->tooltip(
				"Snapping is ON — edits use the visible reticle target "
				"(click to switch to free positioning)");
	}
	else
	{
		grid_snap->label("FREE");
		grid_snap->tooltip(
				"Snapping is OFF — edits use free positioning "
				"(click to snap to grid intersections)");
	}

	grid_snap->redraw();
}


//------------------------------------------------------------------------


#define INFO_TEXT_COL	fl_rgb_color(192, 192, 192)
#define INFO_DIM_COL	fl_rgb_color(128, 128, 128)


UI_StatusBar::UI_StatusBar(Instance &inst, int X, int Y, int W, int H, const char *label) :
    Fl_Widget(X, Y, W, H, label),
	status(), inst(inst)
{
	box(FL_NO_BOX);
}

UI_StatusBar::~UI_StatusBar()
{ }


int UI_StatusBar::handle(int event)
{
	// this never handles any events
	return 0;
}

void UI_StatusBar::draw()
{
	fl_color(fl_rgb_color(64, 64, 64));
	fl_rectf(x(), y(), w(), h());

	fl_color(fl_rgb_color(96, 96, 96));
	fl_rectf(x(), y() + h() - 1, w(), 1);

	fl_push_clip(x(), y(), w(), h());

	fl_font(FL_COURIER, 16);

	int cx = x() + 10;
	int cy = y() + 20;

	if (inst.edit.render3d)
	{
		IB_Number(cx, cy, "x", iround(inst.r_view.x), 5);
		IB_Number(cx, cy, "y", iround(inst.r_view.y), 5);
		IB_Number(cx, cy, "z", iround(inst.r_view.z) - inst.conf.miscInfo.view_height, 4);

		// use less space when an action is occurring
		if (inst.edit.action == EditorAction::nothing)
		{
			int ang = iround(inst.r_view.angle * 180 / M_PI);
			if (ang < 0) ang += 360;

			IB_Number(cx, cy, "ang", ang, 3);
			cx += 2;

			IB_Flag(cx, cy, inst.r_view.gravity, "GRAV", "grav");
#if 0
			IB_Number(cx, cy, "gamma", usegamma, 1);
#endif
		}

		cx += 4;
	}
	else  // 2D view
	{
		const v2double_t snapped = inst.grid.Snap(inst.edit.map.xy);
		IB_Coord(cx, cy, "x", snapped.x);
		IB_Coord(cx, cy, "y", snapped.y);
		cx += 10;
#if 0
		IB_Number(cx, cy, "gamma", usegamma, 1);
		cx += 10;
#endif
	}

	/* status message */

	IB_Flag(cx, cy, true, "|", "|");

	fl_color(INFO_TEXT_COL);

	switch (inst.edit.action)
	{
	case EditorAction::drag:
		IB_ShowDrag(cx, cy);
		break;

	case EditorAction::transform:
		IB_ShowTransform(cx, cy);
		break;

	case EditorAction::adjustOfs:
		IB_ShowOffsets(cx, cy);
		break;

	case EditorAction::drawLine:
		IB_ShowDrawLine(cx, cy);
		break;

	case EditorAction::selbox:
		IB_ShowSelboxSize(cx, cy);
		break;

	default:
		fl_draw(status.c_str(), cx, cy);
		cx += static_cast<int>(fl_width(status.c_str())) + 12;
		IB_ShowSelectionSize(cx, cy);
		break;
	}

	// the map's real-world dimensions stay visible at the right edge of the
	// status bar, so the scale of the level is always known without any
	// on-canvas label getting in the way of the drawing itself
	IB_ShowMapSize(cy);

	fl_pop_clip();
}


void UI_StatusBar::IB_ShowDrag(int cx, int cy)
{
	if (inst.edit.render3d && inst.edit.mode == ObjType::sectors)
	{
		IB_Number(cx, cy, "raise delta", iround(inst.edit.drag_sector_dz), 4);
		return;
	}
	if (inst.edit.render3d && inst.edit.mode == ObjType::things && inst.edit.drag_thing_up_down)
	{
		double dz = inst.edit.drag_cur.z - inst.edit.drag_start.z;
		IB_Number(cx, cy, "raise delta", iround(dz), 4);
		return;
	}

	v2double_t delta;

	if (inst.edit.render3d)
	{
		delta.x = inst.edit.drag_cur.x - inst.edit.drag_start.x;
		delta.y = inst.edit.drag_cur.y - inst.edit.drag_start.y;
	}
	else
	{
		delta = inst.main_win->canvas->DragDelta();
	}

	IB_Coord(cx, cy, "dragging delta x", static_cast<float>(delta.x));
	IB_Coord(cx, cy,                "y", static_cast<float>(delta.y));

	const SString dist = units::FormatLength(delta.hypot());
	if (dist.good())
		IB_String(cx, cy, SString::printf("dist %s", dist.c_str()).c_str());
}


void UI_StatusBar::IB_ShowTransform(int cx, int cy)
{
	int rot_degrees;

	switch (inst.edit.trans_mode)
	{
	case TRANS_K_Scale:
		IB_Coord(cx, cy, "scale by", static_cast<float>(inst.edit.trans_param.scale.x));
		break;

	case TRANS_K_Stretch:
		IB_Coord(cx, cy, "stretch x", static_cast<float>(inst.edit.trans_param.scale.x));
		IB_Coord(cx, cy,         "y", static_cast<float>(inst.edit.trans_param.scale.y));
		break;

	case TRANS_K_Rotate:
	case TRANS_K_RotScale:
		rot_degrees = static_cast<int>(round(inst.edit.trans_param.rotate * (180 / M_PI)));
		IB_Number(cx, cy, "rotate by", rot_degrees, 3);
		break;

	case TRANS_K_Skew:
		IB_Coord(cx, cy, "skew x", static_cast<float>(inst.edit.trans_param.skew.x));
		IB_Coord(cx, cy,      "y", static_cast<float>(inst.edit.trans_param.skew.y));
		break;
	}

	if (inst.edit.trans_mode == TRANS_K_RotScale)
		IB_Coord(cx, cy, "scale", static_cast<float>(inst.edit.trans_param.scale.x));
}


void UI_StatusBar::IB_ShowOffsets(int cx, int cy)
{
	int dx = iround(inst.edit.adjust_dx);
	int dy = iround(inst.edit.adjust_dy);

	Objid hl = inst.edit.highlight;

	if (! inst.edit.Selected->empty())
	{
		if (inst.edit.Selected->count_obj() == 1)
		{
			int first = inst.edit.Selected->find_first();
			int parts = inst.edit.Selected->get_ext(first);

			hl = Objid(inst.edit.mode, first, parts);
		}
		else
		{
			hl.clear();
		}
	}

	if (hl.valid() && hl.parts >= 2)
	{
		const auto L = inst.level.linedefs[hl.num];

		int x_offset = 0;
		int y_offset = 0;

		const SideDef *SD = NULL;

		if (hl.parts & PART_LF_ALL)
			SD = inst.level.getLeft(*L);
		else
			SD = inst.level.getRight(*L);

		if (SD != NULL)
		{
			x_offset = SD->x_offset;
			y_offset = SD->y_offset;

			IB_Number(cx, cy, "new ofs x", x_offset + dx, 4);
			IB_Number(cx, cy,         "y", y_offset + dy, 4);
		}
	}

	IB_Number(cx, cy, "delta x", dx, 4);
	IB_Number(cx, cy,       "y", dy, 4);
}


void UI_StatusBar::IB_ShowDrawLine(int cx, int cy)
{
	if (! inst.edit.drawLine.from.valid())
		return;

	const auto V = inst.level.vertices[inst.edit.drawLine.from.num];

	v2double_t dv = inst.edit.drawLine.to - V->xy();

	// show a ratio value
	FFixedPoint fdx = FFixedPoint(dv.x);
	FFixedPoint fdy = FFixedPoint(dv.y);

	SString ratio_name = LD_RatioName(fdx, fdy, false);

	int old_cx = cx;
	IB_String(cx, cy, ratio_name.c_str());

	cx = std::max(cx+12, old_cx + 170);

	IB_Coord(cx, cy, "delta x", static_cast<float>(dv.x));
	IB_Coord(cx, cy,       "y", static_cast<float>(dv.y));

	const SString dist = units::FormatLength(dv.hypot());
	if (dist.good())
		IB_String(cx, cy, SString::printf("dist %s", dist.c_str()).c_str());
}


//  While a selection box is being outlined, show its live dimensions in map
//  units plus real-world measurements, so the mapper can size selections
//  precisely without any label overlapping the canvas.
void UI_StatusBar::IB_ShowSelboxSize(int cx, int cy)
{
	const double w = fabs(inst.edit.selbox2.x - inst.edit.selbox1.x);
	const double h = fabs(inst.edit.selbox2.y - inst.edit.selbox1.y);

	IB_Coord(cx, cy, "sel w", w);
	IB_Coord(cx, cy,       "h", h);

	if (! units::Enabled())
		return;

	const SString realW = units::FormatLength(w);
	const SString realH = units::FormatLength(h);
	if (realW.good() && realH.good())
		IB_String(cx, cy, SString::printf("size %s x %s",
				realW.c_str(), realH.c_str()).c_str());
}


//  Shows the overall dimensions of the map's bounding box in real-world
//  measurements (e.g. "map 512.0 m x 512.0 m"), right-aligned so it never
//  collides with the left-to-right status fields.
void UI_StatusBar::IB_ShowMapSize(int cy)
{
	if (! units::Enabled() || inst.edit.render3d)
		return;

	if (inst.level.numVertices() == 0)
		return;

	const double w = inst.level.Map_bound2.x - inst.level.Map_bound1.x;
	const double h = inst.level.Map_bound2.y - inst.level.Map_bound1.y;

	const SString realW = units::FormatLength(w);
	const SString realH = units::FormatLength(h);
	if (! realW.good() || ! realH.good())
		return;

	const SString msg = SString::printf("map %s x %s", realW.c_str(), realH.c_str());

	fl_color(INFO_TEXT_COL);
	fl_draw(msg.c_str(), x() + UI_StatusBar::w() - static_cast<int>(fl_width(msg.c_str())) - 10, cy);
}


//  When a selection exists, show its bounding-box dimensions as real-world
//  measurements (e.g. "size 16.0 m x 4.0 m") so the mapper can reason about
//  the true scale of what is selected.  Appended after the status message.
void UI_StatusBar::IB_ShowSelectionSize(int cx, int cy)
{
	if (! units::Enabled() || inst.edit.render3d)
		return;

	if (! inst.edit.Selected || inst.edit.Selected->empty())
		return;

	v2double_t pos1, pos2;
	inst.level.objects.calcBBox(*inst.edit.Selected, pos1, pos2);

	const double w = pos2.x - pos1.x;
	const double h = pos2.y - pos1.y;

	const SString realW = units::FormatLength(w);
	const SString realH = units::FormatLength(h);
	if (realW.good() && realH.good())
		IB_String(cx, cy, SString::printf("size %s x %s",
				realW.c_str(), realH.c_str()).c_str());
}


void UI_StatusBar::IB_Number(int& cx, int& cy, const char *label, int value, int size)
{
	char buffer[256];

	// negative size means we require a sign
	if (size < 0)
		snprintf(buffer, sizeof(buffer), "%s:%-+*d ", label, -size + 1, value);
	else
		snprintf(buffer, sizeof(buffer), "%s:%-*d ", label, size, value);

	fl_color(INFO_TEXT_COL);
	fl_draw(buffer, cx, cy);

	cx = static_cast<int>(cx + fl_width(buffer));
}


void UI_StatusBar::IB_Coord(int& cx, int& cy, const char *label, double value)
{
	char buffer[256];
	snprintf(buffer, sizeof(buffer), "%s:%-8.2f ", label, value);

	fl_color(INFO_TEXT_COL);
	fl_draw(buffer, cx, cy);

	cx = static_cast<int>(cx + fl_width(buffer));
}


void UI_StatusBar::IB_String(int& cx, int& cy, const char *str)
{
	fl_draw(str, cx, cy);

	cx = static_cast<int>(cx + fl_width(str));
}


void UI_StatusBar::IB_Flag(int& cx, int& cy, bool value, const char *label_on, const char *label_off)
{
	const char *label = value ? label_on : label_off;

	fl_color(value ? INFO_TEXT_COL : INFO_DIM_COL);

	fl_draw(label, cx, cy);

	cx = static_cast<int>(cx + fl_width(label) + 20);
}


void UI_StatusBar::SetStatus(const char *str)
{
	if (status == str)
		return;

	status = str;

	redraw();
}


void Instance::Status_Set(EUR_FORMAT_STRING(const char *fmt), ...) const
{
	if (!main_win)
		return;

	va_list arg_ptr;

	va_start(arg_ptr, fmt);
	SString text = SString::vprintf(fmt, arg_ptr);;
	va_end(arg_ptr);

	main_win->status_bar->SetStatus(text.c_str());
}


void Instance::Status_Clear() const
{
	if (!main_win)
		return;

	main_win->status_bar->SetStatus("");
}

//--- editor settings ---
// vi:ts=4:sw=4:noexpandtab
