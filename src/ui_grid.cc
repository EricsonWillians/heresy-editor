//------------------------------------------------------------------------
//  MATHEMATICAL GRID AND SNAPPING DIALOG
//------------------------------------------------------------------------

#include "ui_grid.h"

#include "Instance.h"
#include "m_config.h"
#include "m_grid_theme.h"
#include "r_grid.h"
#include "ui_window.h"

#include "FL/Fl_Box.H"
#include "FL/Fl_Button.H"
#include "FL/Fl_Check_Button.H"
#include "FL/Fl_Choice.H"
#include "FL/Fl_Float_Input.H"
#include "FL/Fl_Int_Input.H"
#include "FL/Fl_Return_Button.H"
#include "FL/Fl_Value_Slider.H"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace
{

SString FormatDouble(double value)
{
	if (std::abs(value) < 1e-12)
		value = 0.0;
	return SString::printf("%.10g", value);
}

bool ParseDouble(const char *text, double &value)
{
	if (!text)
		return false;
	char *end = nullptr;
	value = std::strtod(text, &end);
	if (end == text)
		return false;
	while (*end && std::isspace(static_cast<unsigned char>(*end)))
		++end;
	return *end == 0 && std::isfinite(value);
}

class UI_MathematicalGridDialog : public UI_Escapable_Window
{
public:
	explicit UI_MathematicalGridDialog(Instance &instance) :
		UI_Escapable_Window(720, 676, "Mathematical Grid & Snapping"),
		instance_(instance),
		originalStep_(instance.grid.getStep()),
		originalSettings_(instance.grid.getMathSettings()),
		originalShown_(instance.grid.isShown()),
		originalSnap_(instance.grid.snaps()),
		originalDefaultSnap_(config::grid_default_snap),
		originalVisualTheme_(config::grid_visual_theme),
		originalOpacity_(config::grid_opacity)
	{
		title_ = new Fl_Box(24, 14, 672, 30,
				"Mathematical grid and snapping");
		title_->labelfont(FL_HELVETICA_BOLD);
		title_->labelsize(19);
		title_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

		subtitle_ = new Fl_Box(24, 45, 672, 36,
				"Configure Cartesian, rotated, oblique, triangular, "
				"hexagonal, or polar construction grids. Changes preview live.");
		subtitle_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
		subtitle_->labelsize(13);

		presetLabel_ = makeLabel(24, 93, "Preset");
		preset_ = new Fl_Choice(154, 91, 542, 30);
		preset_->add("Custom");
		for (const grid::GeometryPreset &preset :
				grid::GeometryPresets())
			preset_->add(preset.label);
		preset_->callback(presetCallback, this);

		patternLabel_ = makeLabel(24, 135, "Geometry");
		pattern_ = new Fl_Choice(154, 133, 220, 30);
		pattern_->add("Orthogonal|Oblique lattice|Polar / radial");
		pattern_->callback(changeCallback, this);

		roundingLabel_ = makeLabel(386, 135, "Snap direction");
		rounding_ = new Fl_Choice(516, 133, 180, 30);
		rounding_->add(
				"Nearest|Lower basis cell|Upper basis cell|"
				"Toward origin|Away from origin");
		rounding_->callback(changeCallback, this);

		primaryLabel_ = makeLabel(24, 177, "Primary spacing");
		primary_ = new Fl_Int_Input(154, 175, 220, 30);
		primary_->tooltip("1 to 65536 map units");
		primary_->when(FL_WHEN_CHANGED);
		primary_->callback(changeCallback, this);

		secondaryLabel_ = makeLabel(386, 177, "Secondary spacing");
		secondary_ = new Fl_Float_Input(516, 175, 180, 30);
		secondary_->tooltip(
				"Independent spacing along the second lattice axis");
		secondary_->when(FL_WHEN_CHANGED);
		secondary_->callback(changeCallback, this);

		rotationLabel_ = makeLabel(24, 219, "Rotation");
		rotation_ = new Fl_Float_Input(154, 217, 220, 30);
		rotation_->tooltip("Primary axis rotation in degrees");
		rotation_->when(FL_WHEN_CHANGED);
		rotation_->callback(changeCallback, this);

		axisLabel_ = makeLabel(386, 219, "Between axes");
		axisAngle_ = new Fl_Float_Input(516, 217, 180, 30);
		axisAngle_->tooltip(
				"Angle between oblique lattice axes, 1 to 179 degrees");
		axisAngle_->when(FL_WHEN_CHANGED);
		axisAngle_->callback(changeCallback, this);

		divisionsLabel_ = makeLabel(24, 261, "Polar divisions");
		divisions_ = new Fl_Int_Input(154, 259, 220, 30);
		divisions_->tooltip("Number of angular spokes, 3 to 360");
		divisions_->when(FL_WHEN_CHANGED);
		divisions_->callback(changeCallback, this);

		majorLabel_ = makeLabel(386, 261, "Major interval");
		majorEvery_ = new Fl_Int_Input(516, 259, 180, 30);
		majorEvery_->tooltip(
				"Emphasize every Nth ring or lattice line, 2 to 64");
		majorEvery_->when(FL_WHEN_CHANGED);
		majorEvery_->callback(changeCallback, this);

		originHeading_ = new Fl_Box(24, 306, 672, 26,
				"SNAP ORIGIN / PIVOT");
		originHeading_->labelfont(FL_HELVETICA_BOLD);
		originHeading_->labelsize(14);
		originHeading_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

		originXLabel_ = makeLabel(24, 340, "Origin X");
		originX_ = new Fl_Float_Input(154, 338, 220, 30);
		originX_->when(FL_WHEN_CHANGED);
		originX_->callback(changeCallback, this);
		originYLabel_ = makeLabel(386, 340, "Origin Y");
		originY_ = new Fl_Float_Input(516, 338, 180, 30);
		originY_->when(FL_WHEN_CHANGED);
		originY_->callback(changeCallback, this);

		worldOrigin_ = new Fl_Button(24, 380, 132, 30, "World 0, 0");
		pointerOrigin_ = new Fl_Button(166, 380, 132, 30, "Pointer");
		selectionOrigin_ = new Fl_Button(308, 380, 132, 30, "Selection center");
		cameraOrigin_ = new Fl_Button(450, 380, 132, 30, "3D camera");
		for (Fl_Button *button : {worldOrigin_, pointerOrigin_,
				selectionOrigin_, cameraOrigin_})
			button->callback(originCallback, this);

		showGrid_ = new Fl_Check_Button(24, 428, 160, 28,
				"Show construction grid");
		snap_ = new Fl_Check_Button(204, 428, 182, 28,
				"Snap to intersections");
		showGrid_->callback(changeCallback, this);
		snap_->callback(changeCallback, this);
		themeLabel_ = makeLabel(386, 429, "Visibility");
		theme_ = new Fl_Choice(516, 427, 180, 30);
		for (const grid::VisualThemeDescriptor &theme :
				grid::VisualThemes())
			theme_->add(theme.label);
		theme_->value(grid::ConfiguredVisualTheme());
		theme_->callback(themeCallback, this);
		theme_->tooltip(
				"Live grid, axis, and snap-reticle contrast theme");

		opacityLabel_ = makeLabel(24, 467, "Grid opacity");
		opacity_ = new Fl_Value_Slider(154, 465, 300, 30);
		opacity_->type(FL_HOR_NICE_SLIDER);
		opacity_->range(20.0, 100.0);
		opacity_->step(5.0);
		opacity_->value(config::grid_opacity);
		opacity_->callback(opacityCallback, this);
		opacity_->tooltip(
				"Fade grid lines toward the canvas "
				"(lower percent = more transparent)");

		description_ = new Fl_Box(24, 501, 672, 44);
		description_->box(FL_THIN_DOWN_BOX);
		description_->color(fl_rgb_color(245, 245, 245));
		description_->align(
				FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
		description_->labelsize(13);

		status_ = new Fl_Box(24, 551, 672, 42);
		status_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
		status_->labelsize(13);
		status_->labelfont(FL_HELVETICA_BOLD);

		reset_ = new Fl_Button(24, 620, 118, 34, "Reset square");
		cancel_ = new Fl_Button(438, 620, 112, 34, "Cancel");
		apply_ = new Fl_Return_Button(560, 620, 136, 34, "Use Grid");
		reset_->callback(resetCallback, this);
		cancel_->callback(cancelCallback, this);
		apply_->callback(applyCallback, this);

		end();
		callback(cancelCallback, this);
		set_non_modal();
		size_range(720, 676, 720, 676);

		loadSettings(originalSettings_);
		showGrid_->value(originalShown_);
		snap_->value(originalSnap_);
		findMatchingPreset();
		replan();
	}

	~UI_MathematicalGridDialog() override
	{
		if (!committed_)
			restoreOriginal();
	}

	void run()
	{
		UI_MainWindow *mainWindow = instance_.main_win;
		const bool restoreActivity =
				mainWindow && mainWindow->active();
		if (restoreActivity)
			mainWindow->deactivate();
		show();
		while (!closed_)
			Fl::wait(0.1);
		hide();
		if (restoreActivity)
		{
			mainWindow->activate();
			mainWindow->redraw();
		}
	}

	bool layoutValid(SString *reason = nullptr) const
	{
		auto fail = [reason](const char *message)
		{
			if (reason)
				*reason = message;
			return false;
		};
		auto inside = [this](const Fl_Widget *widget)
		{
			return widget && widget->x() >= 0 && widget->y() >= 0 &&
					widget->x() + widget->w() <= w() &&
					widget->y() + widget->h() <= h();
		};
		for (const Fl_Widget *widget :
				{static_cast<const Fl_Widget *>(title_),
				 static_cast<const Fl_Widget *>(subtitle_),
				 static_cast<const Fl_Widget *>(preset_),
				 static_cast<const Fl_Widget *>(pattern_),
				 static_cast<const Fl_Widget *>(rounding_),
				 static_cast<const Fl_Widget *>(primary_),
				 static_cast<const Fl_Widget *>(secondary_),
				 static_cast<const Fl_Widget *>(rotation_),
				 static_cast<const Fl_Widget *>(axisAngle_),
				 static_cast<const Fl_Widget *>(divisions_),
				 static_cast<const Fl_Widget *>(majorEvery_),
				 static_cast<const Fl_Widget *>(originX_),
				 static_cast<const Fl_Widget *>(originY_),
				 static_cast<const Fl_Widget *>(worldOrigin_),
				 static_cast<const Fl_Widget *>(pointerOrigin_),
				 static_cast<const Fl_Widget *>(selectionOrigin_),
				 static_cast<const Fl_Widget *>(cameraOrigin_),
				 static_cast<const Fl_Widget *>(showGrid_),
				 static_cast<const Fl_Widget *>(snap_),
				 static_cast<const Fl_Widget *>(theme_),
				 static_cast<const Fl_Widget *>(description_),
				 static_cast<const Fl_Widget *>(status_),
				 static_cast<const Fl_Widget *>(reset_),
				 static_cast<const Fl_Widget *>(cancel_),
				 static_cast<const Fl_Widget *>(apply_)})
		{
			if (!inside(widget))
				return fail("A mathematical-grid control is outside the window.");
		}
		if (description_->y() + description_->h() > status_->y() ||
				status_->y() + status_->h() > reset_->y())
			return fail("Grid guidance overlaps status or action controls.");
		if (worldOrigin_->x() + worldOrigin_->w() > pointerOrigin_->x() ||
				pointerOrigin_->x() + pointerOrigin_->w() >
						selectionOrigin_->x() ||
				selectionOrigin_->x() + selectionOrigin_->w() >
						cameraOrigin_->x())
			return fail("Grid-origin shortcuts overlap.");
		if (reason)
			reason->clear();
		return true;
	}

	bool windowPolicyValid(SString *reason = nullptr) const
	{
		if (modal() || !non_modal() || parent())
		{
			if (reason)
				*reason =
						"The mathematical-grid window must be independent and non-modal.";
			return false;
		}
		if (reason)
			reason->clear();
		return true;
	}

private:
	Fl_Box *makeLabel(int x, int y, const char *text)
	{
		Fl_Box *result = new Fl_Box(x, y, 120, 26, text);
		result->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
		result->labelsize(14);
		return result;
	}

	void loadSettings(const grid::MathSettings &settings)
	{
		pattern_->value(static_cast<int>(settings.pattern));
		rounding_->value(static_cast<int>(settings.rounding));
		primary_->value(SString::printf(
				"%d", instance_.grid.getStep()).c_str());
		secondary_->value(FormatDouble(instance_.grid.getStep() *
				settings.secondaryRatio).c_str());
		rotation_->value(FormatDouble(settings.rotation).c_str());
		axisAngle_->value(FormatDouble(settings.axisAngle).c_str());
		divisions_->value(SString::printf(
				"%d", settings.angularDivisions).c_str());
		majorEvery_->value(SString::printf(
				"%d", settings.majorEvery).c_str());
		originX_->value(FormatDouble(settings.origin.x).c_str());
		originY_->value(FormatDouble(settings.origin.y).c_str());
	}

	bool readSettings(int &step, grid::MathSettings &settings,
			SString &reason) const
	{
		char *end = nullptr;
		const long parsedStep = std::strtol(primary_->value(), &end, 10);
		if (end == primary_->value() || *end ||
				parsedStep < grid::kMinimumStep ||
				parsedStep > grid::kMaximumStep)
		{
			reason = "Primary spacing must be a whole number from 1 to 65536.";
			return false;
		}
		step = static_cast<int>(parsedStep);

		double secondary = 0.0;
		if (!ParseDouble(secondary_->value(), secondary) ||
				secondary <= 0.0)
		{
			reason = "Secondary spacing must be a positive finite number.";
			return false;
		}
		settings.pattern =
				static_cast<grid::Pattern>(pattern_->value());
		settings.rounding =
				static_cast<grid::Rounding>(rounding_->value());
		settings.secondaryRatio = secondary / step;
		if (!ParseDouble(rotation_->value(), settings.rotation) ||
				!ParseDouble(axisAngle_->value(), settings.axisAngle) ||
				!ParseDouble(originX_->value(), settings.origin.x) ||
				!ParseDouble(originY_->value(), settings.origin.y))
		{
			reason = "Rotation, axis angle, and origin must be finite numbers.";
			return false;
		}
		settings.angularDivisions = std::atoi(divisions_->value());
		settings.majorEvery = std::atoi(majorEvery_->value());
		return grid::MathSettingsValid(settings, &reason);
	}

	void replan()
	{
		grid::MathSettings settings;
		int step = 0;
		SString reason;
		if (!readSettings(step, settings, reason))
		{
			status_->copy_label(
					(SString("Cannot preview: ") + reason).c_str());
			status_->labelcolor(fl_rgb_color(170, 35, 35));
			apply_->deactivate();
			updateControlAvailability();
			return;
		}

		instance_.grid.ForceStep(step);
		instance_.grid.SetMathSettings(settings);
		instance_.grid.SetShown(showGrid_->value());
		instance_.grid.SetSnap(snap_->value());
		config::grid_visual_theme =
				grid::NormalizeVisualTheme(theme_->value());
		apply_->activate();

		const char *patternName =
				settings.pattern == grid::Pattern::orthogonal ?
						"orthogonal" :
				settings.pattern == grid::Pattern::oblique ?
						"oblique" : "polar";
		status_->copy_label(SString::printf(
				"● LIVE — %s grid, primary %d, secondary %.4g, "
				"origin %.4g, %.4g; snapping %s; %s visibility.",
				patternName, step, step * settings.secondaryRatio,
				settings.origin.x, settings.origin.y,
				snap_->value() ? "enabled" : "disabled",
				grid::VisualThemeInfo(
						config::grid_visual_theme).label).c_str());
		status_->labelcolor(fl_rgb_color(24, 122, 65));
		updateControlAvailability();
		instance_.gridUpdateSnap();
		instance_.RedrawMap();
	}

	void updateControlAvailability()
	{
		const grid::Pattern pattern =
				static_cast<grid::Pattern>(pattern_->value());
		if (pattern == grid::Pattern::polar)
		{
			divisions_->activate();
			divisionsLabel_->activate();
			secondary_->deactivate();
			secondaryLabel_->deactivate();
			axisAngle_->deactivate();
			axisLabel_->deactivate();
		}
		else
		{
			divisions_->deactivate();
			divisionsLabel_->deactivate();
			secondary_->activate();
			secondaryLabel_->activate();
			const bool oblique = pattern == grid::Pattern::oblique;
			oblique ? axisAngle_->activate() : axisAngle_->deactivate();
			oblique ? axisLabel_->activate() : axisLabel_->deactivate();
		}
	}

	void findMatchingPreset()
	{
		preset_->value(0);
		const grid::MathSettings current =
				instance_.grid.getMathSettings();
		for (size_t index = 0;
				index < grid::GeometryPresets().size(); ++index)
		{
			const grid::MathSettings &candidate =
					grid::GeometryPresets()[index].settings;
			if (candidate.pattern == current.pattern &&
					std::abs(candidate.rotation - current.rotation) < 1e-8 &&
					std::abs(candidate.secondaryRatio -
							current.secondaryRatio) < 1e-8 &&
					std::abs(candidate.axisAngle -
							current.axisAngle) < 1e-8 &&
					candidate.angularDivisions ==
							current.angularDivisions)
			{
				preset_->value(static_cast<int>(index + 1));
				description_->copy_label(
						grid::GeometryPresets()[index].description);
				return;
			}
		}
		description_->copy_label(
				"Custom mathematical grid. Every field remains editable.");
	}

	void selectOrigin(const v2double_t &origin)
	{
		originX_->value(FormatDouble(origin.x).c_str());
		originY_->value(FormatDouble(origin.y).c_str());
		preset_->value(0);
		replan();
	}

	void restoreOriginal()
	{
		instance_.grid.ForceStep(originalStep_);
		instance_.grid.SetMathSettings(originalSettings_);
		instance_.grid.SetShown(originalShown_);
		instance_.grid.SetSnap(originalSnap_);
		config::grid_default_snap = originalDefaultSnap_;
		config::grid_visual_theme = originalVisualTheme_;
		config::grid_opacity = originalOpacity_;
		instance_.gridUpdateSnap();
		instance_.RedrawMap();
	}

	static void changeCallback(Fl_Widget *, void *data)
	{
		auto *dialog =
				static_cast<UI_MathematicalGridDialog *>(data);
		dialog->preset_->value(0);
		dialog->description_->copy_label(
				"Custom mathematical grid. Every field remains editable.");
		dialog->replan();
	}

	static void presetCallback(Fl_Widget *, void *data)
	{
		auto *dialog =
				static_cast<UI_MathematicalGridDialog *>(data);
		const int selected = dialog->preset_->value();
		if (selected <= 0)
			return;
		grid::MathSettings settings =
				grid::GeometryPresets()[selected - 1].settings;
		settings.origin = dialog->instance_.grid
				.getMathSettings().origin;
		settings.rounding = static_cast<grid::Rounding>(
				dialog->rounding_->value());
		settings.majorEvery =
				std::max(2, std::atoi(dialog->majorEvery_->value()));
		dialog->loadSettings(settings);
		dialog->description_->copy_label(
				grid::GeometryPresets()[selected - 1].description);
		dialog->replan();
	}

	static void themeCallback(Fl_Widget *, void *data)
	{
		auto *dialog =
				static_cast<UI_MathematicalGridDialog *>(data);
		config::grid_visual_theme =
				grid::NormalizeVisualTheme(dialog->theme_->value());
		dialog->replan();
	}

	static void opacityCallback(Fl_Widget *, void *data)
	{
		auto *dialog =
				static_cast<UI_MathematicalGridDialog *>(data);
		config::grid_opacity =
				static_cast<int>(dialog->opacity_->value());
		dialog->replan();
	}

	static void originCallback(Fl_Widget *widget, void *data)
	{
		auto *dialog =
				static_cast<UI_MathematicalGridDialog *>(data);
		if (widget == dialog->worldOrigin_)
			dialog->selectOrigin({});
		else if (widget == dialog->pointerOrigin_)
			dialog->selectOrigin(dialog->instance_.edit.map.xy);
		else if (widget == dialog->cameraOrigin_)
			dialog->selectOrigin({dialog->instance_.r_view.x,
					dialog->instance_.r_view.y});
		else if (dialog->instance_.edit.Selected->notempty())
			dialog->selectOrigin(dialog->instance_.level.objects.calcMiddle(
					*dialog->instance_.edit.Selected));
		else
		{
			dialog->instance_.Beep(
					"Select map objects before using Selection center");
			dialog->selectOrigin(dialog->instance_.edit.map.xy);
		}
	}

	static void resetCallback(Fl_Widget *, void *data)
	{
		auto *dialog =
				static_cast<UI_MathematicalGridDialog *>(data);
		dialog->instance_.grid.ForceStep(64);
		dialog->loadSettings(grid::MathSettings{});
		dialog->showGrid_->value(1);
		dialog->snap_->value(1);
		dialog->findMatchingPreset();
		dialog->replan();
	}

	static void cancelCallback(Fl_Widget *, void *data)
	{
		auto *dialog =
				static_cast<UI_MathematicalGridDialog *>(data);
		dialog->closed_ = true;
	}

	static void applyCallback(Fl_Widget *, void *data)
	{
		auto *dialog =
				static_cast<UI_MathematicalGridDialog *>(data);
		dialog->committed_ = true;
		dialog->closed_ = true;
	}

	Instance &instance_;
	int originalStep_ = 64;
	grid::MathSettings originalSettings_;
	bool originalShown_ = true;
	bool originalSnap_ = true;
	bool originalDefaultSnap_ = true;
	int originalVisualTheme_ =
			static_cast<int>(grid::VisualTheme::highContrastDark);
	int originalOpacity_ = 75;
	bool committed_ = false;
	bool closed_ = false;

	Fl_Box *title_ = nullptr;
	Fl_Box *subtitle_ = nullptr;
	Fl_Box *presetLabel_ = nullptr;
	Fl_Choice *preset_ = nullptr;
	Fl_Box *patternLabel_ = nullptr;
	Fl_Choice *pattern_ = nullptr;
	Fl_Box *roundingLabel_ = nullptr;
	Fl_Choice *rounding_ = nullptr;
	Fl_Box *primaryLabel_ = nullptr;
	Fl_Int_Input *primary_ = nullptr;
	Fl_Box *secondaryLabel_ = nullptr;
	Fl_Float_Input *secondary_ = nullptr;
	Fl_Box *rotationLabel_ = nullptr;
	Fl_Float_Input *rotation_ = nullptr;
	Fl_Box *axisLabel_ = nullptr;
	Fl_Float_Input *axisAngle_ = nullptr;
	Fl_Box *divisionsLabel_ = nullptr;
	Fl_Int_Input *divisions_ = nullptr;
	Fl_Box *majorLabel_ = nullptr;
	Fl_Int_Input *majorEvery_ = nullptr;
	Fl_Box *originHeading_ = nullptr;
	Fl_Box *originXLabel_ = nullptr;
	Fl_Float_Input *originX_ = nullptr;
	Fl_Box *originYLabel_ = nullptr;
	Fl_Float_Input *originY_ = nullptr;
	Fl_Button *worldOrigin_ = nullptr;
	Fl_Button *pointerOrigin_ = nullptr;
	Fl_Button *selectionOrigin_ = nullptr;
	Fl_Button *cameraOrigin_ = nullptr;
	Fl_Check_Button *showGrid_ = nullptr;
	Fl_Check_Button *snap_ = nullptr;
	Fl_Box *themeLabel_ = nullptr;
	Fl_Choice *theme_ = nullptr;
	Fl_Box *opacityLabel_ = nullptr;
	Fl_Value_Slider *opacity_ = nullptr;
	Fl_Box *description_ = nullptr;
	Fl_Box *status_ = nullptr;
	Fl_Button *reset_ = nullptr;
	Fl_Button *cancel_ = nullptr;
	Fl_Return_Button *apply_ = nullptr;
};

} // namespace

void UI_RunMathematicalGrid(Instance &instance)
{
	UI_MathematicalGridDialog dialog(instance);
	dialog.run();
}

bool UI_VerifyMathematicalGridLayout(
		Instance &instance, SString *reason)
{
	UI_MathematicalGridDialog dialog(instance);
	return dialog.layoutValid(reason);
}

bool UI_VerifyMathematicalGridWindowPolicy(
		Instance &instance, SString *reason)
{
	UI_MathematicalGridDialog dialog(instance);
	return dialog.windowPolicyValid(reason);
}
