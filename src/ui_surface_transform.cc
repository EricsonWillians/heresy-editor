//------------------------------------------------------------------------
//  SURFACE TEXTURE TRANSFORM DIALOG
//------------------------------------------------------------------------

#include "ui_surface_transform.h"

#include "Errors.h"
#include "Instance.h"
#include "LineDef.h"
#include "main.h"
#include "m_files.h"
#include "m_package.h"
#include "m_surface_transform.h"
#include "Sector.h"
#include "SideDef.h"
#include "ui_browser.h"
#include "ui_canvas.h"
#include "ui_window.h"
#include "w_texture.h"
#include "w_wad.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <tuple>

namespace
{

struct WallTarget
{
	int linedef = -1;
	int sidedef = -1;
	WallSurfacePart part = WallSurfacePart::middle;
	SString texture;
};

struct PlaneTarget
{
	int sector = -1;
	PlaneSurfacePart part = PlaneSurfacePart::floor;
	SString texture;
};

struct BakedResourcePlan
{
	PackageResourceKind kind = PackageResourceKind::wallTexture;
	SString sourceTexture;
	SString resolvedTexture;
	int width = 0;
	int height = 0;
	bool mirrorX = false;
	bool mirrorY = false;
};

struct BakedWallAssignment
{
	int sidedef = -1;
	WallSurfacePart part = WallSurfacePart::middle;
	SString texture;
};

struct BakedPlaneAssignment
{
	int sector = -1;
	PlaneSurfacePart part = PlaneSurfacePart::floor;
	SString texture;
};

struct ResolvedTransformPlan
{
	std::vector<WallSurfacePreviewValue> walls;
	std::vector<PlaneSurfacePreviewValue> planes;
	std::vector<BakedResourcePlan> bakedResources;
	std::vector<BakedWallAssignment> bakedWalls;
	std::vector<BakedPlaneAssignment> bakedPlanes;
};

class ScopedMainWindowInputLock
{
public:
	explicit ScopedMainWindowInputLock(UI_MainWindow *window) :
		window_(window),
		restoreActivity_(window && window->active())
	{
		if (restoreActivity_)
			window_->deactivate();
	}

	~ScopedMainWindowInputLock()
	{
		if (restoreActivity_)
		{
			window_->activate();
			window_->redraw();
		}
	}

	ScopedMainWindowInputLock(const ScopedMainWindowInputLock &) = delete;
	ScopedMainWindowInputLock &operator=(
			const ScopedMainWindowInputLock &) = delete;

private:
	UI_MainWindow *window_ = nullptr;
	bool restoreActivity_ = false;
};

bool NearlyEqual(double a, double b)
{
	return std::abs(a - b) <= 1e-9 *
			std::max({1.0, std::abs(a), std::abs(b)});
}

SString FormatValue(double value)
{
	if (std::abs(value) < 1e-12)
		value = 0.0;
	return SString::printf("%.8g", value);
}

bool ParseValue(const char *text, double &result)
{
	if (!text)
		return false;

	char *end = nullptr;
	result = std::strtod(text, &end);
	if (end == text)
		return false;
	while (*end && std::isspace(static_cast<unsigned char>(*end)))
		end++;
	if (*end == '%')
	{
		end++;
		while (*end && std::isspace(static_cast<unsigned char>(*end)))
			end++;
	}
	return *end == 0 && std::isfinite(result);
}

WallSurfacePart PartForBit(int part)
{
	if (part & (PART_RT_UPPER | PART_LF_UPPER))
		return WallSurfacePart::upper;
	if (part & (PART_RT_LOWER | PART_LF_LOWER))
		return WallSurfacePart::lower;
	return WallSurfacePart::middle;
}

int PartBit(WallSurfacePart part, bool left)
{
	int result = part == WallSurfacePart::upper ? PART_RT_UPPER :
			part == WallSurfacePart::lower ? PART_RT_LOWER :
					PART_RT_RAIL;
	return left ? result << 4 : result;
}

int VisibleParts(const Instance &instance, const LineDef &line)
{
	int result = 0;
	auto addSide = [&](bool left)
	{
		const int sideIndex = left ? line.left : line.right;
		if (!instance.level.isSidedef(sideIndex))
			return;

		const SideDef &side = *instance.level.sidedefs[sideIndex];
		const int otherIndex = left ? line.right : line.left;
		if (!instance.level.isSidedef(otherIndex))
		{
			result |= PartBit(WallSurfacePart::middle, left);
			return;
		}

		const Sector &front = instance.level.getSector(side);
		const Sector &back = instance.level.getSector(
				*instance.level.sidedefs[otherIndex]);
		if (front.ceilh > back.ceilh)
			result |= PartBit(WallSurfacePart::upper, left);
		if (front.floorh < back.floorh)
			result |= PartBit(WallSurfacePart::lower, left);
		if (!is_null_tex(side.MidTex()))
			result |= PartBit(WallSurfacePart::middle, left);
	};

	addSide(false);
	addSide(true);
	return result;
}

class UI_SurfaceTransformDialog : public UI_Escapable_Window
{
	struct NumericControl
	{
		Fl_Box *label = nullptr;
		Fl_Repeat_Button *minus = nullptr;
		Fl_Float_Input *input = nullptr;
		Fl_Repeat_Button *plus = nullptr;
		double step = 1.0;
	};

public:
	explicit UI_SurfaceTransformDialog(
			Instance &instance, int objectOverride, int partsOverride) :
		UI_Escapable_Window(lastWidth_, lastHeight_,
				"Texture Fit & Alignment"),
		instance_(instance),
		capabilities_(M_SurfaceTransformCapabilities(
				instance.loaded.levelFormat, instance.conf)),
		preview_(instance.level),
		objectOverride_(objectOverride),
		partsOverride_(partsOverride)
	{
		isWall_ = instance_.edit.mode == ObjType::linedefs;
		isPlane_ = instance_.edit.mode == ObjType::sectors;
		collectObjects();
		if (const std::shared_ptr<Wad_file> package =
					instance_.wad.master.editWad())
		{
			try
			{
				for (const PackageResourceEntry &entry :
						M_ListPackageResources(package->PathName()))
					packageResourceNames_.insert(
							entry.editorName.asUpper());
			}
			catch (...)
			{
				// Apply performs authoritative package validation. Keeping the
				// dialog usable here lets it report that exact failure inline.
			}
		}

		auto makeLabel = [](const char *text, bool bold = false)
		{
			Fl_Box *label = new Fl_Box(0, 0, 10, 28, text);
			label->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
			label->labelsize(14);
			if (bold)
				label->labelfont(FL_HELVETICA_BOLD);
			return label;
		};
		auto addNumeric = [&](const char *label, double step)
		{
			NumericControl control;
			control.label = makeLabel(label);
			control.minus = new Fl_Repeat_Button(0, 0, 30, 30, "−");
			control.input = new Fl_Float_Input(0, 0, 100, 30);
			control.plus = new Fl_Repeat_Button(0, 0, 30, 30, "+");
			control.step = step;
			control.minus->labelsize(19);
			control.plus->labelsize(17);
			control.minus->tooltip(
					"Hold to decrease · Shift for coarse steps · Ctrl for fine steps");
			control.plus->tooltip(
					"Hold to increase · Shift for coarse steps · Ctrl for fine steps");
			control.minus->callback(stepCallback, this);
			control.plus->callback(stepCallback, this);
			control.input->callback(changeCallback, this);
			control.input->when(FL_WHEN_CHANGED);
			numeric_.push_back(control);
			return control.input;
		};

		title_ = new Fl_Box(0, 0, 10, 30,
				"Texture fit and alignment");
		title_->labelfont(FL_HELVETICA_BOLD);
		title_->labelsize(19);
		title_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

		subtitle_ = new Fl_Box(0, 0, 10, 24,
				"Always live on the 3D canvas. Apply records one Undo step.");
		subtitle_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
		subtitle_->labelcolor(fl_rgb_color(34, 105, 66));
		subtitle_->labelsize(14);

		targetGroup_ = new Fl_Group(0, 0, 10, 86, "Target surfaces");
		targetGroup_->box(FL_ENGRAVED_BOX);
		targetGroup_->align(FL_ALIGN_TOP_LEFT);
		targetGroup_->labelsize(14);
		targetGroup_->labelfont(FL_HELVETICA_BOLD);
		targetGroup_->begin();
		scopeLabel_ = makeLabel("Surfaces");
		scope_ = new Fl_Choice(0, 0, 100, 30);
		if (isWall_)
			scope_->add("Selected / visible|Upper|Middle|Lower|All wall parts");
		else
			scope_->add("Selected / visible|Floor|Ceiling|Floor + ceiling");
		scope_->value(0);
		scope_->callback(changeCallback, this);

		applyLabel_ = makeLabel("Mode");
		applyMode_ = new Fl_Choice(0, 0, 100, 30);
		applyMode_->add("Set exact values|Adjust current values");
		applyMode_->value(objectCount_ > 1 ? 1 : 0);
		applyMode_->callback(changeCallback, this);

		targetInfo_ = new Fl_Box(0, 0, 100, 25);
		targetInfo_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
		targetInfo_->labelsize(13);
		targetGroup_->end();

		transformGroup_ = new Fl_Group(0, 0, 10, 260,
				"Position, size, and orientation");
		transformGroup_->box(FL_ENGRAVED_BOX);
		transformGroup_->align(FL_ALIGN_TOP_LEFT);
		transformGroup_->labelsize(14);
		transformGroup_->labelfont(FL_HELVETICA_BOLD);
		transformGroup_->begin();
		offsetX_ = addNumeric("X offset", 1.0);
		offsetY_ = addNumeric("Y offset", 1.0);
		scaleX_ = addNumeric("Tile width", 1.0);
		scaleY_ = addNumeric("Tile height", 1.0);
		rotation_ = addNumeric("Rotation", 1.0);
		offsetX_->tooltip("Texture X offset in map units");
		offsetY_->tooltip("Texture Y offset in map units");
		scaleX_->tooltip("100% uses one map unit per source pixel");
		scaleY_->tooltip("100% uses one map unit per source pixel");

		sizeHeading_ = new Fl_Box(0, 0, 100, 24,
				"Rendered tile size (%)");
		sizeHeading_->labelfont(FL_HELVETICA_BOLD);
		sizeHeading_->labelsize(14);
		sizeHeading_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
		lockAspect_ = new Fl_Check_Button(
				0, 0, 168, 27, "Lock width / height");
		lockAspect_->value(1);
		lockAspect_->callback(changeCallback, this);

		mirrorX_ = new Fl_Check_Button(0, 0, 112, 27, "Mirror X");
		mirrorY_ = new Fl_Check_Button(0, 0, 112, 27, "Mirror Y");
		mirrorX_->callback(changeCallback, this);
		mirrorY_->callback(changeCallback, this);

		rotationNote_ = new Fl_Box(0, 0, 200, 27,
				"degrees clockwise · floors / ceilings");
		rotationNote_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
		rotationNote_->labelsize(13);

		fitLabel_ = new Fl_Box(0, 0, 135, 28, "Fit selected surface");
		fitLabel_->labelfont(FL_HELVETICA_BOLD);
		fitLabel_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
		fitSurface_ = new Fl_Button(0, 0, 110, 30, "Fit both");
		fitWidth_ = new Fl_Button(0, 0, 110, 30, "Fit width");
		fitHeight_ = new Fl_Button(0, 0, 110, 30, "Fit height");
		for (Fl_Button *button : {fitSurface_, fitWidth_, fitHeight_})
			button->callback(buttonCallback, this);
		transformGroup_->end();

		presetGroup_ = new Fl_Group(0, 0, 10, 58, "Tile presets");
		presetGroup_->box(FL_ENGRAVED_BOX);
		presetGroup_->align(FL_ALIGN_TOP_LEFT);
		presetGroup_->labelsize(14);
		presetGroup_->labelfont(FL_HELVETICA_BOLD);
		presetGroup_->begin();
		native_ = new Fl_Button(0, 0, 100, 30, "Native 1:1");
		size64_ = new Fl_Button(0, 0, 90, 30, "64 units");
		size128_ = new Fl_Button(0, 0, 90, 30, "128 units");
		size256_ = new Fl_Button(0, 0, 90, 30, "256 units");
		presetNote_ = new Fl_Box(0, 0, 150, 30, "longest tile side");
		presetNote_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
		presetNote_->labelsize(13);
		reset_ = new Fl_Button(0, 0, 110, 30, "Reset");
		for (Fl_Button *button :
				{native_, size64_, size128_, size256_, reset_})
			button->callback(buttonCallback, this);
		presetGroup_->end();

		tileInfo_ = new Fl_Box(0, 0, 100, 38);
		tileInfo_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
		tileInfo_->labelsize(13);

		previewStatus_ = new Fl_Box(0, 0, 100, 24);
		previewStatus_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
		previewStatus_->labelfont(FL_HELVETICA_BOLD);
		previewStatus_->labelsize(13);

		compatibility_ = new Fl_Box(0, 0, 100, 54);
		compatibility_->box(FL_THIN_DOWN_BOX);
		compatibility_->align(
				FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
		compatibility_->color(fl_rgb_color(245, 245, 245));
		compatibility_->labelsize(13);

		cancel_ = new Fl_Button(0, 0, 112, 34, "Cancel");
		apply_ = new Fl_Button(0, 0, 170, 34, "Apply to Map");
		apply_->labelfont(FL_HELVETICA_BOLD);
		apply_->shortcut(FL_Enter);
		cancel_->callback(closeCallback, this);
		apply_->callback(applyCallback, this);

		end();
		callback(closeCallback, this);
		// This is intentionally non-modal at the window-manager level.
		// GNOME/Mutter's "attach-modal-dialogs" feature can unmaximize or
		// resize the owner when an attached modal dialog is dragged.  A
		// non-modal transient remains above the editor without allowing the
		// window manager to treat both windows as one movable unit.
		set_non_modal();
		size_range(720, 700, 1200, 950);
		resizable(transformGroup_);
		initialized_ = true;
		layout();

		rebuildTargets();
		refreshControls();
	}

	~UI_SurfaceTransformDialog() override
	{
		preview_.clear();
		instance_.RedrawMap();
	}

	void run()
	{
		if ((!isWall_ && !isPlane_) || objectCount_ == 0)
			return;

		// Keep map editing disabled while the transient preview owns complete
		// sidedef/sector snapshots.  This provides application-level safety
		// without using a WM modal relationship, so moving this window can
		// never alter the main editor window's geometry.
		ScopedMainWindowInputLock inputLock(instance_.main_win);

		show();
		while (!closed_)
			Fl::wait(0.1);
		hide();
	}

	bool hasTargets() const noexcept
	{
		return objectCount_ > 0;
	}

	void resize(int X, int Y, int W, int H) override
	{
		UI_Escapable_Window::resize(X, Y, W, H);
		if (!initialized_)
			return;
		lastWidth_ = W;
		lastHeight_ = H;
		layout();
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
					 static_cast<const Fl_Widget *>(targetGroup_),
					 static_cast<const Fl_Widget *>(transformGroup_),
					 static_cast<const Fl_Widget *>(presetGroup_),
					 static_cast<const Fl_Widget *>(tileInfo_),
					 static_cast<const Fl_Widget *>(previewStatus_),
					 static_cast<const Fl_Widget *>(compatibility_),
					 static_cast<const Fl_Widget *>(cancel_),
					 static_cast<const Fl_Widget *>(apply_)})
		{
			if (!inside(widget))
				return fail("A primary transform widget is outside the window.");
		}
		for (const NumericControl &control : numeric_)
		{
			if (!inside(control.label) || !inside(control.minus) ||
					!inside(control.input) || !inside(control.plus))
				return fail("A numeric transform control is outside the window.");
			if (control.label->x() + control.label->w() >
						control.minus->x() ||
					control.minus->x() + control.minus->w() >
						control.input->x() ||
					control.input->x() + control.input->w() >
						control.plus->x())
				return fail("A numeric label, step button, or field overlaps.");
		}
		if (fitSurface_->x() + fitSurface_->w() > fitWidth_->x() ||
				fitWidth_->x() + fitWidth_->w() > fitHeight_->x())
			return fail("Surface-fit buttons overlap.");
		if (presetNote_->x() + presetNote_->w() > reset_->x())
			return fail("Tile preset text overlaps Reset.");
		if (compatibility_->y() + compatibility_->h() > cancel_->y())
			return fail("Compatibility guidance overlaps action buttons.");
		if (reason)
			reason->clear();
		return true;
	}

	bool windowPolicyValid(SString *reason = nullptr) const
	{
		if (modal())
		{
			if (reason)
				*reason =
						"The transform window must not advertise WM modal state.";
			return false;
		}
		if (!non_modal())
		{
			if (reason)
				*reason =
						"The transform window must remain an independent transient.";
			return false;
		}
		if (parent())
		{
			if (reason)
				*reason =
						"The transform window must not be a main-window subwindow.";
			return false;
		}
		if (reason)
			reason->clear();
		return true;
	}

private:
	void layoutNumeric(int index, int X, int Y, int W)
	{
		if (index < 0 || static_cast<size_t>(index) >= numeric_.size())
			return;
		NumericControl &control = numeric_[static_cast<size_t>(index)];
		const int labelWidth = 92;
		const int buttonWidth = 31;
		const int gap = 5;
		const int inputX = X + labelWidth + gap + buttonWidth;
		const int inputWidth = std::max(70,
				W - labelWidth - buttonWidth * 2 - gap * 2);
		control.label->resize(X, Y, labelWidth, 30);
		control.minus->resize(X + labelWidth + gap, Y, buttonWidth, 30);
		control.input->resize(inputX, Y, inputWidth, 30);
		control.plus->resize(inputX + inputWidth, Y, buttonWidth, 30);
	}

	void layout()
	{
		const int margin = 20;
		const int contentWidth = w() - margin * 2;
		const int gap = 18;
		const int columnWidth = (contentWidth - 36 - gap) / 2;
		const int left = margin + 18;
		const int right = left + columnWidth + gap;

		title_->resize(margin + 4, 10, contentWidth - 8, 30);
		subtitle_->resize(margin + 4, 42, contentWidth - 8, 24);

		targetGroup_->resize(margin, 78, contentWidth, 86);
		const int targetHalf = (contentWidth - 36) / 2;
		scopeLabel_->resize(left, 100, 76, 30);
		scope_->resize(left + 84, 100,
				std::max(150, targetHalf - 84), 30);
		const int modeX = margin + 18 + targetHalf + 18;
		applyLabel_->resize(modeX, 100, 58, 30);
		applyMode_->resize(modeX + 66, 100,
				std::max(150, contentWidth - (modeX - margin) - 84), 30);
		targetInfo_->resize(left + 84, 134,
				contentWidth - 120, 25);

		transformGroup_->resize(margin, 178, contentWidth, 266);
		layoutNumeric(0, left, 204, columnWidth);
		layoutNumeric(1, right, 204, columnWidth);
		sizeHeading_->resize(left, 244, columnWidth, 24);
		layoutNumeric(2, left, 274, columnWidth);
		layoutNumeric(3, right, 274, columnWidth);
		lockAspect_->resize(left + 96, 310, 176, 27);
		mirrorX_->resize(right + 16, 310, 112, 27);
		mirrorY_->resize(right + 140, 310, 112, 27);
		layoutNumeric(4, left, 348, columnWidth);
		rotationNote_->resize(right + 16, 348,
				std::max(170, columnWidth - 16), 30);

		fitLabel_->resize(left, 397, 145, 30);
		const int fitX = left + 154;
		const int fitAvailable = contentWidth - (fitX - margin) - 18;
		const int fitButtonWidth = std::max(90,
				(fitAvailable - 16) / 3);
		fitSurface_->resize(fitX, 397, fitButtonWidth, 30);
		fitWidth_->resize(fitX + fitButtonWidth + 8, 397,
				fitButtonWidth, 30);
		fitHeight_->resize(fitX + (fitButtonWidth + 8) * 2, 397,
				fitButtonWidth, 30);

		presetGroup_->resize(margin, 458, contentWidth, 58);
		int presetX = left;
		native_->resize(presetX, 478, 100, 30);
		presetX += 108;
		size64_->resize(presetX, 478, 88, 30);
		presetX += 96;
		size128_->resize(presetX, 478, 92, 30);
		presetX += 100;
		size256_->resize(presetX, 478, 92, 30);
		presetX += 100;
		presetNote_->resize(presetX, 478,
				std::max(90, w() - presetX - 154), 30);
		reset_->resize(w() - margin - 122, 478, 104, 30);

		tileInfo_->resize(margin + 4, 524, contentWidth - 8, 38);
		previewStatus_->resize(margin + 4, 564,
				contentWidth - 8, 24);
		const int buttonsY = h() - 44;
		compatibility_->resize(margin, 592, contentWidth,
				std::max(52, buttonsY - 604));
		cancel_->resize(margin + 4, buttonsY, 112, 34);
		apply_->resize(w() - margin - 174, buttonsY, 170, 34);
		redraw();
	}

	void collectObjects()
	{
		if (!isWall_ && !isPlane_)
			return;

		if (!instance_.edit.Selected->empty())
		{
			for (sel_iter_c it(*instance_.edit.Selected);
					!it.done(); it.next())
			{
				objects_.push_back({*it,
						partsOverride_ != 0 ? partsOverride_ :
								instance_.edit.Selected->get_ext(*it)});
			}
		}
		else if (objectOverride_ >= 0)
		{
			objects_.push_back({objectOverride_, partsOverride_});
		}
		else if (instance_.edit.highlight.valid() &&
				instance_.edit.highlight.type == instance_.edit.mode)
		{
			objects_.push_back({instance_.edit.highlight.num,
					instance_.edit.highlight.parts});
		}
		objectCount_ = static_cast<int>(objects_.size());
	}

	void rebuildTargets()
	{
		wallTargets_.clear();
		planeTargets_.clear();
		const int scope = scope_->value();

		if (isWall_)
		{
			std::set<std::pair<int, int>> seen;
			for (const auto &[lineIndex, selectedParts] : objects_)
			{
				if (!instance_.level.isLinedef(lineIndex))
					continue;
				const LineDef &line = *instance_.level.linedefs[lineIndex];
				int parts = selectedParts;
				if (scope == 0 && (parts <= 1 ||
						!(parts & (PART_RT_ALL | PART_LF_ALL))))
					parts = VisibleParts(instance_, line);
				else if (scope > 0)
				{
					parts = 0;
					auto addRequested = [&](bool left)
					{
						const int side = left ? line.left : line.right;
						if (!instance_.level.isSidedef(side))
							return;
						if (scope == 1 || scope == 4)
							parts |= PartBit(WallSurfacePart::upper, left);
						if (scope == 2 || scope == 4)
							parts |= PartBit(WallSurfacePart::middle, left);
						if (scope == 3 || scope == 4)
							parts |= PartBit(WallSurfacePart::lower, left);
					};
					addRequested(false);
					addRequested(true);
				}

				for (int bit : {PART_RT_LOWER, PART_RT_UPPER, PART_RT_RAIL,
						PART_LF_LOWER, PART_LF_UPPER, PART_LF_RAIL})
				{
					if (!(parts & bit))
						continue;
					const bool left = (bit & PART_LF_ALL) != 0;
					const int sidedef = left ? line.left : line.right;
					if (!instance_.level.isSidedef(sidedef))
						continue;
					// A one-sided line is selected/rendered as a lower part,
					// while its actual texture and UDMF transform are middle.
					const WallSurfacePart part = line.TwoSided() ?
							PartForBit(bit) : WallSurfacePart::middle;
					const auto key = std::pair{sidedef, static_cast<int>(part)};
					if (!seen.insert(key).second)
						continue;
					const SideDef &side = *instance_.level.sidedefs[sidedef];
					const SString texture =
							part == WallSurfacePart::upper ? side.UpperTex() :
							part == WallSurfacePart::lower ? side.LowerTex() :
									side.MidTex();
					wallTargets_.push_back(
							{lineIndex, sidedef, part, texture});
				}
			}
		}
		else
		{
			std::set<std::pair<int, int>> seen;
			for (const auto &[sector, selectedParts] : objects_)
			{
				if (!instance_.level.isSector(sector))
					continue;
				int parts = selectedParts;
				if (scope == 0 && !(parts & PART_SEC_ALL))
				{
					parts = instance_.edit.sector_render_mode == SREND_Ceiling ?
							PART_CEIL : PART_FLOOR;
				}
				else if (scope == 1)
					parts = PART_FLOOR;
				else if (scope == 2)
					parts = PART_CEIL;
				else if (scope == 3)
					parts = PART_SEC_ALL;

				for (int bit : {PART_FLOOR, PART_CEIL})
				{
					if (!(parts & bit))
						continue;
					const PlaneSurfacePart part = bit == PART_FLOOR ?
							PlaneSurfacePart::floor :
							PlaneSurfacePart::ceiling;
					if (!seen.insert({sector, static_cast<int>(part)}).second)
						continue;
					const Sector &source = *instance_.level.sectors[sector];
					planeTargets_.push_back({sector, part,
							part == PlaneSurfacePart::floor ?
									source.FloorTex() : source.CeilTex()});
				}
			}
		}

		loadFirstTarget();
	}

	void loadFirstTarget()
	{
		firstTransform_ = {};
		firstBaseX_ = firstBaseY_ = 0;
		imageWidth_ = imageHeight_ = 0;
		bool mixed = false;

		if (!wallTargets_.empty())
		{
			const WallTarget &first = wallTargets_.front();
			const SideDef &side = *instance_.level.sidedefs[first.sidedef];
			firstTransform_ = M_EffectiveWallSurfaceTransform(
					side, first.part, instance_.loaded.levelFormat,
					instance_.conf);
			firstBaseX_ = side.x_offset;
			firstBaseY_ = side.y_offset;
			const Img_c *image = instance_.wad.images.getTexture(
					instance_.conf, first.texture, true);
			if (image)
			{
				imageWidth_ = image->width();
				imageHeight_ = image->height();
			}
			for (const WallTarget &target : wallTargets_)
			{
				const SideDef &candidate =
						*instance_.level.sidedefs[target.sidedef];
				const SurfaceTransform value =
						M_EffectiveWallSurfaceTransform(candidate,
								target.part,
								instance_.loaded.levelFormat,
								instance_.conf);
				mixed |= !NearlyEqual(value.offsetX + candidate.x_offset,
							firstTransform_.offsetX + firstBaseX_) ||
						!NearlyEqual(value.offsetY + candidate.y_offset,
							firstTransform_.offsetY + firstBaseY_) ||
						!NearlyEqual(value.scaleX, firstTransform_.scaleX) ||
						!NearlyEqual(value.scaleY, firstTransform_.scaleY);
			}
		}
		else if (!planeTargets_.empty())
		{
			const PlaneTarget &first = planeTargets_.front();
			const Sector &sector = *instance_.level.sectors[first.sector];
			firstTransform_ = M_EffectivePlaneSurfaceTransform(
					sector, first.part, instance_.loaded.levelFormat,
					instance_.conf);
			const Img_c *image = instance_.wad.images.W_GetFlat(
					instance_.conf, first.texture, true);
			if (image)
			{
				imageWidth_ = image->width();
				imageHeight_ = image->height();
			}
			for (const PlaneTarget &target : planeTargets_)
			{
				const SurfaceTransform value =
						M_EffectivePlaneSurfaceTransform(
								*instance_.level.sectors[target.sector],
								target.part,
								instance_.loaded.levelFormat,
								instance_.conf);
				mixed |= !NearlyEqual(value.offsetX, firstTransform_.offsetX) ||
						!NearlyEqual(value.offsetY, firstTransform_.offsetY) ||
						!NearlyEqual(value.scaleX, firstTransform_.scaleX) ||
						!NearlyEqual(value.scaleY, firstTransform_.scaleY) ||
						!NearlyEqual(value.rotation, firstTransform_.rotation);
			}
		}

		mixed_ = mixed;
	}

	bool canBakeScaling() const noexcept
	{
		const std::shared_ptr<Wad_file> package =
				instance_.wad.master.editWad();
		return instance_.conf.features.tx_start &&
				package && !package->IsReadOnly() &&
				(M_ProjectPackageForPath(package->PathName()) ==
							ProjectPackage::wad ||
				 M_ProjectPackageForPath(package->PathName()) ==
							ProjectPackage::pk3);
	}

	void setNumericActive(size_t index, bool active)
	{
		if (index >= numeric_.size())
			return;
		NumericControl &control = numeric_[index];
		for (Fl_Widget *widget :
				{static_cast<Fl_Widget *>(control.minus),
					 static_cast<Fl_Widget *>(control.input),
					 static_cast<Fl_Widget *>(control.plus)})
		{
			active ? widget->activate() : widget->deactivate();
		}
		active ? control.label->activate() : control.label->deactivate();
	}

	void refreshControls()
	{
		const bool relative = applyMode_->value() == 1;
		const int targetCount = isWall_ ?
				static_cast<int>(wallTargets_.size()) :
				static_cast<int>(planeTargets_.size());
		targetInfo_->copy_label(SString::printf(
				"%d map object%s, %d surface%s%s",
				objectCount_, objectCount_ == 1 ? "" : "s",
				targetCount, targetCount == 1 ? "" : "s",
				mixed_ ? " — mixed existing transforms" : "").c_str());

		if (relative)
		{
			offsetX_->value("0");
			offsetY_->value("0");
			scaleX_->value("100");
			scaleY_->value("100");
			rotation_->value("0");
			mirrorX_->value(0);
			mirrorY_->value(0);
		}
		else
		{
			offsetX_->value(FormatValue(
					firstTransform_.offsetX + firstBaseX_).c_str());
			offsetY_->value(FormatValue(
					firstTransform_.offsetY + firstBaseY_).c_str());
			scaleX_->value(FormatValue(
					100.0 / std::abs(firstTransform_.scaleX)).c_str());
			scaleY_->value(FormatValue(
					100.0 / std::abs(firstTransform_.scaleY)).c_str());
			rotation_->value(FormatValue(firstTransform_.rotation).c_str());
			mirrorX_->value(firstTransform_.scaleX < 0.0);
			mirrorY_->value(firstTransform_.scaleY < 0.0);
		}

		const bool udmfWalls = isWall_ && capabilities_.wallScale;
		const bool udmfPlanes = isPlane_ && capabilities_.planeScale;
		const bool nativeScale = udmfWalls || udmfPlanes;
		const bool bakedScale = !nativeScale && canBakeScaling();
		const bool canScale = nativeScale || bakedScale;
		const bool canRotate = isPlane_ && capabilities_.planeRotation;
		const bool canPan = isWall_ || capabilities_.planeOffsets;

		setNumericActive(0, canPan);
		setNumericActive(1, canPan);
		setNumericActive(2, canScale);
		setNumericActive(3, canScale);
		setNumericActive(4, canRotate);
		const std::array<Fl_Widget *, 11> scaleWidgets = {
				lockAspect_, mirrorX_, mirrorY_,
				fitSurface_, fitWidth_, fitHeight_,
				native_, size64_, size128_, size256_,
				sizeHeading_};
		for (Fl_Widget *widget : scaleWidgets)
			canScale ? widget->activate() : widget->deactivate();
		canRotate ? rotationNote_->activate() : rotationNote_->deactivate();

		if (instance_.loaded.levelFormat != MapFormat::udmf)
		{
			if (bakedScale)
			{
				compatibility_->copy_label(isWall_ ?
						"Classic Doom/Hexen map: X/Y offsets stay native map "
						"fields. Size, Fit, and Mirror create a safely renamed "
						"project texture copy at the reviewed dimensions and "
						"assign it to this wall; the source is preserved." :
						"Classic Doom/Hexen map: Size, Fit, and Mirror create a "
						"safely renamed project flat at the reviewed dimensions. "
						"Plane panning and rotation still require UDMF.");
			}
			else
			{
				compatibility_->copy_label(isWall_ ?
						"Classic Doom/Hexen cannot store wall scale. Add a "
						"writable project package and use a modern port with "
						"standalone texture namespaces to enable fitted copies." :
						"Classic Doom/Hexen cannot store plane transforms. Add "
						"a writable modern-port project to enable fitted copies, "
						"or use UDMF for panning and rotation.");
			}
		}
		else if (!nativeScale && bakedScale)
		{
			compatibility_->copy_label(
					"This UDMF namespace does not declare native transforms. "
					"Size, Fit, and Mirror will use a safely renamed fitted "
					"project resource; unsupported pan/rotation fields are not "
					"written.");
		}
		else if (!nativeScale)
		{
			compatibility_->copy_label(isWall_ ?
					"The active UDMF port profile does not declare per-part "
					"wall transforms. Shared integer X/Y offsets remain "
					"available; unsupported scale/mirror fields are not written." :
					"The active UDMF port profile does not declare plane "
					"transforms. Unsupported properties will not be written.");
		}
		else
		{
			compatibility_->copy_label(
					"UDMF: values are stored per wall part or plane. Negative "
					"scale mirrors the image. Native dimensions and non-power-"
					"of-two textures are preserved.");
		}

		updateTileInfo();
	}

	void updateTileInfo()
	{
		double offsetX = 0.0;
		double offsetY = 0.0;
		double rotation = 0.0;
		double percentX = 100.0;
		double percentY = 100.0;
		if (!ParseValue(scaleX_->value(), percentX) ||
				!ParseValue(scaleY_->value(), percentY) ||
				!ParseValue(offsetX_->value(), offsetX) ||
				!ParseValue(offsetY_->value(), offsetY) ||
				!ParseValue(rotation_->value(), rotation) ||
				percentX <= 0.0 || percentY <= 0.0)
		{
			tileInfo_->copy_label("Enter finite offsets/rotation and positive "
					"rendered-size percentages.");
			updateLivePreview();
			return;
		}

		if (imageWidth_ > 0 && imageHeight_ > 0)
		{
			SurfaceDimensions dimensions;
			if (!wallTargets_.empty())
			{
				const WallTarget &target = wallTargets_.front();
				dimensions = M_WallSurfaceDimensions(instance_.level,
						target.linedef, target.sidedef, target.part);
			}
			else if (!planeTargets_.empty())
				dimensions = M_PlaneSurfaceDimensions(instance_.level,
						planeTargets_.front().sector);
			if (dimensions.valid())
			{
				tileInfo_->copy_label(SString::printf(
						"Source %d × %d px · tile %.3g × %.3g map units · "
						"selected surface %.3g × %.3g units.",
						imageWidth_, imageHeight_,
						imageWidth_ * percentX / 100.0,
						imageHeight_ * percentY / 100.0,
						dimensions.width, dimensions.height).c_str());
			}
			else
			{
				tileInfo_->copy_label(SString::printf(
						"Source %d × %d px · tile %.3g × %.3g map units · "
						"100%% is native 1:1.",
						imageWidth_, imageHeight_,
						imageWidth_ * percentX / 100.0,
						imageHeight_ * percentY / 100.0).c_str());
			}
		}
		else
		{
			tileInfo_->copy_label(
					"Texture dimensions are unavailable; scale is still "
					"stored exactly for the target port.");
		}

		updateLivePreview();
	}

	bool readTransform(SurfaceTransform &transform, SString &reason) const
	{
		double percentX, percentY;
		if (!ParseValue(offsetX_->value(), transform.offsetX) ||
				!ParseValue(offsetY_->value(), transform.offsetY) ||
				!ParseValue(scaleX_->value(), percentX) ||
				!ParseValue(scaleY_->value(), percentY) ||
				!ParseValue(rotation_->value(), transform.rotation) ||
				percentX <= 0.0 || percentY <= 0.0)
		{
			reason = "Enter finite positions/rotation and positive size percentages.";
			return false;
		}

		transform.scaleX = 100.0 / percentX;
		transform.scaleY = 100.0 / percentY;
		if (mirrorX_->value())
			transform.scaleX = -transform.scaleX;
		if (mirrorY_->value())
			transform.scaleY = -transform.scaleY;
		transform = M_NormalizeSurfaceTransform(transform);

		if (!M_SurfaceTransformValid(transform,
				isPlane_ && capabilities_.planeRotation, &reason))
			return false;
		reason.clear();
		return true;
	}

	SurfaceTransform resolveSizePreset(SurfaceTransform transform,
			const SString &texture, bool wall,
			const SurfaceDimensions &dimensions) const
	{
		const Img_c *image = wall ?
				instance_.wad.images.getTexture(
						instance_.conf, texture, true) :
				instance_.wad.images.W_GetFlat(
						instance_.conf, texture, true);
		if (fitMode_ != SurfaceFitMode::none)
		{
			return M_FitSurfaceTransform(transform,
					image ? image->width() : 0,
					image ? image->height() : 0,
					dimensions, fitMode_);
		}
		if (!fixedSizeUnits_)
			return transform;

		double magnitude = 1.0;
		if (*fixedSizeUnits_ > 0 && image)
		{
			magnitude = static_cast<double>(
					std::max(image->width(), image->height())) /
					*fixedSizeUnits_;
		}
		transform.scaleX = transform.scaleX < 0.0 ?
				-magnitude : magnitude;
		transform.scaleY = transform.scaleY < 0.0 ?
				-magnitude : magnitude;
		return transform;
	}

	std::set<SString> occupiedSurfaceNames() const
	{
		std::set<SString> result = packageResourceNames_;
		for (const auto &[name, image] :
				instance_.wad.images.getWallSurfaceImages(instance_.conf))
		{
			(void)image;
			result.insert(name.asUpper());
		}
		for (const auto &[name, image] :
				instance_.wad.images.getPlaneSurfaceImages(instance_.conf))
		{
			(void)image;
			result.insert(name.asUpper());
		}
		return result;
	}

	SString nextFittedName(const SString &source,
			std::set<SString> &occupied) const
	{
		SString prefix;
		for (char character : source.asUpper())
		{
			if (std::isalnum(static_cast<unsigned char>(character)) ||
					character == '_')
				prefix.push_back(character);
			if (prefix.length() == 5)
				break;
		}
		if (prefix.empty())
			prefix = "FIT";
		static const char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
		for (int ordinal = 1; ordinal < 36 * 36; ++ordinal)
		{
			const SString candidate = SString::printf("%sF%c%c",
					prefix.c_str(), digits[(ordinal / 36) % 36],
					digits[ordinal % 36]);
			if (occupied.insert(candidate).second)
				return candidate;
		}
		return {};
	}

	bool resolvePlan(const SurfaceTransform &entered,
			ResolvedTransformPlan &plan, SString &reason) const
	{
		plan = {};
		const bool relative = applyMode_->value() == 1;
		const bool bakedScaling = canBakeScaling();
		std::set<SString> occupied = occupiedSurfaceNames();
		using BakeKey = std::tuple<int, SString, int, int, bool, bool>;
		std::map<BakeKey, SString> bakedNames;
		auto addBaked = [&](PackageResourceKind kind,
				const SString &source, int width, int height,
				bool mirrorX, bool mirrorY, SString &resolved)
		{
			if (width <= 0 || height <= 0 || width > 8192 || height > 8192)
			{
				reason = "Fitted texture dimensions must be between 1 and 8192 pixels.";
				return false;
			}
			if (static_cast<uint64_t>(width) *
						static_cast<uint64_t>(height) * 4 + 18 >
					256ull * 1024ull * 1024ull)
			{
				reason = "The fitted texture would exceed the 256 MiB resource limit.";
				return false;
			}
			const BakeKey key{static_cast<int>(kind), source.asUpper(),
					width, height, mirrorX, mirrorY};
			if (const auto found = bakedNames.find(key);
					found != bakedNames.end())
			{
				resolved = found->second;
				return true;
			}
			resolved = nextFittedName(source, occupied);
			if (resolved.empty())
			{
				reason = "Could not generate a unique fitted texture name.";
				return false;
			}
			bakedNames.emplace(key, resolved);
			plan.bakedResources.push_back({kind, source, resolved,
					width, height, mirrorX, mirrorY});
			return true;
		};

		if (isWall_)
		{
			if (wallTargets_.empty())
			{
				reason = "No visible wall surfaces match the selected scope.";
				return false;
			}
			std::set<int> changedSharedOffsets;
			for (const WallTarget &target : wallTargets_)
			{
				if (!instance_.level.isSidedef(target.sidedef))
				{
					reason = "A target sidedef no longer exists.";
					return false;
				}
				const SideDef &side =
						*instance_.level.sidedefs[target.sidedef];
				const SurfaceDimensions dimensions =
						M_WallSurfaceDimensions(instance_.level,
								target.linedef, target.sidedef,
								target.part);
				const Img_c *targetImage =
						instance_.wad.images.getTexture(instance_.conf,
								target.texture, true);
				if ((fitMode_ == SurfaceFitMode::fitWidth ||
							fitMode_ == SurfaceFitMode::fitHeight ||
							fitMode_ == SurfaceFitMode::fitSurface) &&
						(!dimensions.valid() || !targetImage))
				{
					reason = !targetImage ?
							"The selected texture cannot be decoded for fitting." :
							"The selected wall surface has no usable width or height.";
					return false;
				}
				if (fixedSizeUnits_ && !targetImage)
				{
					reason = "The selected texture dimensions are unavailable.";
					return false;
				}
				if (!capabilities_.wallPartOffsets)
				{
					WallSurfacePreviewValue change;
					change.sidedef = target.sidedef;
					change.part = target.part;
					if (changedSharedOffsets.insert(target.sidedef).second)
					{
						const double x = relative ?
								side.x_offset + entered.offsetX :
								entered.offsetX;
						const double y = relative ?
								side.y_offset + entered.offsetY :
								entered.offsetY;
						const double minimum =
								instance_.loaded.levelFormat == MapFormat::udmf ?
								std::numeric_limits<int>::min() : -32768.0;
						const double maximum =
								instance_.loaded.levelFormat == MapFormat::udmf ?
								std::numeric_limits<int>::max() : 32767.0;
						if (!NearlyEqual(x, std::round(x)) ||
								!NearlyEqual(y, std::round(y)))
						{
							reason =
									"Shared wall positions must be whole map units.";
							return false;
						}
						if (x < minimum || x > maximum ||
								y < minimum || y > maximum)
						{
							reason = instance_.loaded.levelFormat ==
									MapFormat::udmf ?
									"Shared UDMF wall positions exceed signed 32-bit range." :
									"Classic wall positions exceed signed 16-bit range.";
							return false;
						}
						change.setSharedOffsets = true;
						change.sharedOffsetX =
								static_cast<int>(std::round(x));
						change.sharedOffsetY =
								static_cast<int>(std::round(y));
					}

					if (bakedScaling)
					{
						const Img_c *image = targetImage;
						SurfaceTransform result;
						if (relative)
						{
							result.scaleX *= entered.scaleX;
							result.scaleY *= entered.scaleY;
						}
						else
							result = entered;
						result.offsetX = result.offsetY = 0.0;
						result.rotation = 0.0;
						result = resolveSizePreset(result,
								target.texture, true, dimensions);
						if (!M_SurfaceTransformValid(result, false, &reason))
							return false;
						const bool changedSize =
								!NearlyEqual(std::abs(result.scaleX), 1.0) ||
								!NearlyEqual(std::abs(result.scaleY), 1.0);
						const bool mirrored =
								result.scaleX < 0.0 || result.scaleY < 0.0;
						if ((changedSize || mirrored) && !image)
						{
							reason = "The selected texture cannot be decoded for fitting.";
							return false;
						}
						if (changedSize || mirrored)
						{
							const int width = static_cast<int>(std::lround(
									image->width() / std::abs(result.scaleX)));
							const int height = static_cast<int>(std::lround(
									image->height() / std::abs(result.scaleY)));
							SString fitted;
							if (!addBaked(PackageResourceKind::wallTexture,
									target.texture, width, height,
									result.scaleX < 0.0,
									result.scaleY < 0.0, fitted))
								return false;
							plan.bakedWalls.push_back(
									{target.sidedef, target.part, fitted});
						}
						change.transform = result;
						change.setPartTransform = true;
						change.forceUnsupportedPreview = true;
					}
					if (change.setSharedOffsets ||
							change.setPartTransform)
						plan.walls.push_back(change);
					continue;
				}

				SurfaceTransform result =
						M_WallSurfaceTransform(side, target.part);
				if (relative)
				{
					result.offsetX += entered.offsetX;
					result.offsetY += entered.offsetY;
					result.scaleX *= entered.scaleX;
					result.scaleY *= entered.scaleY;
				}
				else
				{
					result = resolveSizePreset(entered,
							target.texture, true, dimensions);
					result.offsetX -= side.x_offset;
					result.offsetY -= side.y_offset;
					result.rotation = 0.0;
				}
				if (!M_SurfaceTransformValid(
						result, false, &reason))
				{
					return false;
				}
				WallSurfacePreviewValue change;
				change.sidedef = target.sidedef;
				change.part = target.part;
				change.transform = result;
				change.setPartTransform = true;
				plan.walls.push_back(change);
			}
		}
		else
		{
			if (!capabilities_.anyPlaneTransform() && !bakedScaling)
			{
				reason =
						"The active map format cannot store or bake plane transforms.";
				return false;
			}
			if (planeTargets_.empty())
			{
				reason = "No floor or ceiling surfaces match the selected scope.";
				return false;
			}
			for (const PlaneTarget &target : planeTargets_)
			{
				if (!instance_.level.isSector(target.sector))
				{
					reason = "A target sector no longer exists.";
					return false;
				}
				const SurfaceDimensions dimensions =
						M_PlaneSurfaceDimensions(instance_.level,
								target.sector);
				const Img_c *targetImage =
						instance_.wad.images.W_GetFlat(instance_.conf,
								target.texture, true);
				if ((fitMode_ == SurfaceFitMode::fitWidth ||
							fitMode_ == SurfaceFitMode::fitHeight ||
							fitMode_ == SurfaceFitMode::fitSurface) &&
						(!dimensions.valid() || !targetImage))
				{
					reason = !targetImage ?
							"The selected flat cannot be decoded for fitting." :
							"The selected sector has no usable plane bounds.";
					return false;
				}
				if (fixedSizeUnits_ && !targetImage)
				{
					reason = "The selected flat dimensions are unavailable.";
					return false;
				}
				const bool native = capabilities_.planeScale;
				SurfaceTransform result = native ?
						M_PlaneSurfaceTransform(
								*instance_.level.sectors[target.sector],
								target.part) : SurfaceTransform{};
				if (relative)
				{
					result.offsetX += entered.offsetX;
					result.offsetY += entered.offsetY;
					result.scaleX *= entered.scaleX;
					result.scaleY *= entered.scaleY;
					result.rotation += entered.rotation;
				}
				else
					result = resolveSizePreset(entered,
							target.texture, false,
							dimensions);
				result = M_NormalizeSurfaceTransform(result);
				if (!M_SurfaceTransformValid(result, true, &reason))
					return false;
				if (!native)
				{
					if (!NearlyEqual(result.offsetX, 0.0) ||
							!NearlyEqual(result.offsetY, 0.0) ||
							!NearlyEqual(result.rotation, 0.0))
					{
						reason = "Classic fitted flats cannot pan or rotate; use UDMF for those fields.";
						return false;
					}
					const Img_c *image = targetImage;
					const bool changedSize =
							!NearlyEqual(std::abs(result.scaleX), 1.0) ||
							!NearlyEqual(std::abs(result.scaleY), 1.0);
					const bool mirrored =
							result.scaleX < 0.0 || result.scaleY < 0.0;
					if ((changedSize || mirrored) && !image)
					{
						reason = "The selected flat cannot be decoded for fitting.";
						return false;
					}
					if (changedSize || mirrored)
					{
						const int width = static_cast<int>(std::lround(
								image->width() / std::abs(result.scaleX)));
						const int height = static_cast<int>(std::lround(
								image->height() / std::abs(result.scaleY)));
						SString fitted;
						if (!addBaked(PackageResourceKind::flat,
								target.texture, width, height,
								result.scaleX < 0.0,
								result.scaleY < 0.0, fitted))
							return false;
						plan.bakedPlanes.push_back(
								{target.sector, target.part, fitted});
					}
					plan.planes.push_back({target.sector, target.part,
							result, true});
				}
				else
					plan.planes.push_back(
							{target.sector, target.part, result, false});
			}
		}
		reason.clear();
		return !plan.walls.empty() || !plan.planes.empty();
	}

	void updateLivePreview()
	{
		preview_.clear();

		SurfaceTransform entered;
		ResolvedTransformPlan plan;
		SString reason;
		const bool valid =
				readTransform(entered, reason) &&
				resolvePlan(entered, plan, reason);
		if (!valid)
		{
			apply_->deactivate();
			previewStatus_->copy_label(SString::printf(
					"Cannot preview: %s", reason.c_str()).c_str());
			previewStatus_->labelcolor(fl_rgb_color(170, 35, 35));
			instance_.RedrawMap();
			return;
		}

		apply_->activate();
		const int count = static_cast<int>(
				plan.walls.size() + plan.planes.size());

		if (!preview_.apply(plan.walls, plan.planes))
		{
			apply_->deactivate();
			previewStatus_->copy_label(
					"Cannot preview: target geometry changed.");
			previewStatus_->labelcolor(fl_rgb_color(170, 35, 35));
			instance_.RedrawMap();
			return;
		}

		previewStatus_->copy_label(plan.bakedResources.empty() ?
				SString::printf(
						"● LIVE on canvas — previewing %d surface%s.",
						count, count == 1 ? "" : "s").c_str() :
				SString::printf(
						"● LIVE — Apply creates %zu fitted project resource%s "
						"for %d surface%s.",
						plan.bakedResources.size(),
						plan.bakedResources.size() == 1 ? "" : "s",
						count, count == 1 ? "" : "s").c_str());
		previewStatus_->labelcolor(fl_rgb_color(24, 122, 65));
		instance_.RedrawMap();
	}

	void writeBakedResources(const ResolvedTransformPlan &plan)
	{
		if (plan.bakedResources.empty())
			return;
		std::shared_ptr<Wad_file> package =
				instance_.wad.master.editWad();
		if (!package || package->IsReadOnly())
			throw std::runtime_error(
					"Fitting in this map format requires a writable project package.");

		std::vector<PackageResourceWrite> writes;
		writes.reserve(plan.bakedResources.size());
		for (const BakedResourcePlan &resource : plan.bakedResources)
		{
			const Img_c *source =
					resource.kind == PackageResourceKind::wallTexture ?
					instance_.wad.images.getTexture(instance_.conf,
							resource.sourceTexture, true) :
					instance_.wad.images.W_GetFlat(instance_.conf,
							resource.sourceTexture, true);
			if (!source)
				throw std::runtime_error(
						"A source surface image is no longer available.");
			writes.push_back({resource.kind, resource.resolvedTexture,
					".tga", M_BakeSurfaceTextureTGA(*source,
							instance_.wad.palette, resource.width,
							resource.height, resource.mirrorX,
							resource.mirrorY), std::nullopt});
		}

		const fs::path packagePath = package->PathName();
		const std::vector<PackageResourceEntry> currentEntries =
				M_ListPackageResources(packagePath);
		for (const PackageResourceWrite &write : writes)
			for (const PackageResourceEntry &entry : currentEntries)
				if (entry.editorName.noCaseEqual(write.editorName))
					throw std::runtime_error(
							"The project gained a conflicting fitted texture name; review the fit again.");
		M_BackupWad(package.get());
		M_WritePackageResources(packagePath, writes);
		std::shared_ptr<Wad_file> reopened =
				M_OpenEditablePackage(packagePath);
		if (!reopened)
			throw std::runtime_error(
					"The fitted resources were written, but the package could not be reopened.");

		WadData refreshed = instance_.wad;
		refreshed.master.ReplaceEditWad(reopened);
		refreshed.reloadSurfaceImages(refreshed.master.gameWad(),
				instance_.conf, refreshed.master.resourceWads());
		instance_.wad = std::move(refreshed);
		if (instance_.main_win)
		{
			instance_.main_win->canvas->DeleteContext();
			instance_.main_win->browser->Populate();
		}
	}

	void apply()
	{
		preview_.clear();
		SurfaceTransform entered;
		ResolvedTransformPlan plan;
		SString reason;
		if (!readTransform(entered, reason) ||
				!resolvePlan(entered, plan, reason))
		{
			instance_.Beep("%s", reason.c_str());
			updateLivePreview();
			return;
		}

		try
		{
			writeBakedResources(plan);
		}
		catch (const std::exception &error)
		{
			DLG_ShowError(false, "Could not create fitted project textures: %s",
					error.what());
			updateLivePreview();
			return;
		}

		{
			EditOperation operation(instance_.level.basis);
			const int count = static_cast<int>(
					plan.walls.size() + plan.planes.size());
			operation.setMessage("%s %d surface%s",
					plan.bakedResources.empty() ?
							"transformed" : "fitted",
					count, count == 1 ? "" : "s");

			for (const WallSurfacePreviewValue &change : plan.walls)
			{
				if (change.setSharedOffsets)
				{
					operation.changeSidedef(change.sidedef,
							SideDef::F_X_OFFSET,
							change.sharedOffsetX);
					operation.changeSidedef(change.sidedef,
							SideDef::F_Y_OFFSET,
							change.sharedOffsetY);
				}
				if (change.setPartTransform &&
						!change.forceUnsupportedPreview)
					M_ChangeWallSurfaceTransform(operation,
							change.sidedef, change.part,
							change.transform);
			}
			for (const PlaneSurfacePreviewValue &change : plan.planes)
			{
				if (!change.forceUnsupportedPreview)
					M_ChangePlaneSurfaceTransform(operation,
							change.sector, change.part,
							change.transform);
			}
			for (const BakedWallAssignment &assignment : plan.bakedWalls)
			{
				const SideDef::StringIDAddress field =
						assignment.part == WallSurfacePart::upper ?
								SideDef::F_UPPER_TEX :
						assignment.part == WallSurfacePart::lower ?
								SideDef::F_LOWER_TEX :
								SideDef::F_MID_TEX;
				operation.changeSidedef(assignment.sidedef, field,
						BA_InternaliseString(assignment.texture));
			}
			for (const BakedPlaneAssignment &assignment : plan.bakedPlanes)
			{
				const Sector::StringIDAddress field =
						assignment.part == PlaneSurfacePart::floor ?
								Sector::F_FLOOR_TEX :
								Sector::F_CEIL_TEX;
				operation.changeSector(assignment.sector, field,
						BA_InternaliseString(assignment.texture));
			}
		}

		instance_.RedrawMap();
		closed_ = true;
	}

	void cancel()
	{
		preview_.clear();
		closed_ = true;
		instance_.RedrawMap();
	}

	void quickSize(int units)
	{
		fitMode_ = units <= 0 ?
				SurfaceFitMode::native : SurfaceFitMode::none;
		fixedSizeUnits_ = units > 0 ?
				std::optional<int>(units) : std::nullopt;
		if (units <= 0 || imageWidth_ <= 0 || imageHeight_ <= 0)
		{
			scaleX_->value("100");
			scaleY_->value("100");
		}
		else
		{
			const double percent = 100.0 * units /
					std::max(imageWidth_, imageHeight_);
			scaleX_->value(FormatValue(percent).c_str());
			scaleY_->value(FormatValue(percent).c_str());
		}
		updateTileInfo();
	}

	void quickFit(SurfaceFitMode mode)
	{
		fitMode_ = mode;
		fixedSizeUnits_.reset();
		SurfaceDimensions dimensions;
		if (!wallTargets_.empty())
		{
			const WallTarget &target = wallTargets_.front();
			dimensions = M_WallSurfaceDimensions(instance_.level,
					target.linedef, target.sidedef, target.part);
		}
		else if (!planeTargets_.empty())
		{
			dimensions = M_PlaneSurfaceDimensions(
					instance_.level, planeTargets_.front().sector);
		}
		if (!dimensions.valid() || imageWidth_ <= 0 || imageHeight_ <= 0)
		{
			updateTileInfo();
			return;
		}

		double percentX = 100.0;
		double percentY = 100.0;
		ParseValue(scaleX_->value(), percentX);
		ParseValue(scaleY_->value(), percentY);
		if (mode == SurfaceFitMode::fitWidth ||
				mode == SurfaceFitMode::fitSurface)
		{
			percentX = 100.0 * dimensions.width / imageWidth_;
		}
		if (mode == SurfaceFitMode::fitHeight ||
				mode == SurfaceFitMode::fitSurface)
		{
			percentY = 100.0 * dimensions.height / imageHeight_;
		}
		scaleX_->value(FormatValue(percentX).c_str());
		scaleY_->value(FormatValue(percentY).c_str());
		updateTileInfo();
	}

	static void closeCallback(Fl_Widget *, void *data)
	{
		static_cast<UI_SurfaceTransformDialog *>(data)->cancel();
	}

	static void applyCallback(Fl_Widget *, void *data)
	{
		static_cast<UI_SurfaceTransformDialog *>(data)->apply();
	}

	static void stepCallback(Fl_Widget *widget, void *data)
	{
		auto *dialog = static_cast<UI_SurfaceTransformDialog *>(data);
		for (size_t index = 0; index < dialog->numeric_.size(); ++index)
		{
			NumericControl &control = dialog->numeric_[index];
			if (widget != control.minus && widget != control.plus)
				continue;

			double value = 0.0;
			if (!ParseValue(control.input->value(), value))
				value = index == 2 || index == 3 ? 100.0 : 0.0;
			double step = control.step;
			if (Fl::event_state(FL_SHIFT))
				step *= index < 2 ? 8.0 : 10.0;
			if (Fl::event_state(FL_CTRL) &&
					(index >= 2 || dialog->capabilities_.wallPartOffsets ||
							dialog->capabilities_.planeOffsets))
			{
				step *= 0.1;
			}
			value += widget == control.plus ? step : -step;
			if ((index == 2 || index == 3) && value <= 0.0)
				value = std::max(0.1, step);
			control.input->value(FormatValue(value).c_str());
			changeCallback(control.input, dialog);
			return;
		}
	}

	static void changeCallback(Fl_Widget *widget, void *data)
	{
		auto *dialog = static_cast<UI_SurfaceTransformDialog *>(data);
		if (widget == dialog->scope_)
		{
			dialog->preview_.clear();
			dialog->fitMode_ = SurfaceFitMode::none;
			dialog->fixedSizeUnits_.reset();
			dialog->rebuildTargets();
			dialog->refreshControls();
		}
		else if (widget == dialog->applyMode_)
		{
			dialog->fitMode_ = SurfaceFitMode::none;
			dialog->fixedSizeUnits_.reset();
			dialog->refreshControls();
		}
		else
		{
			if (widget == dialog->scaleX_ || widget == dialog->scaleY_)
			{
				dialog->fitMode_ = SurfaceFitMode::none;
				dialog->fixedSizeUnits_.reset();
			}
			if (!dialog->syncing_ && dialog->lockAspect_->value())
			{
				dialog->syncing_ = true;
				if (widget == dialog->scaleX_)
					dialog->scaleY_->value(dialog->scaleX_->value());
				else if (widget == dialog->scaleY_)
					dialog->scaleX_->value(dialog->scaleY_->value());
				dialog->syncing_ = false;
			}
			dialog->updateTileInfo();
		}
	}

	static void buttonCallback(Fl_Widget *widget, void *data)
	{
		auto *dialog = static_cast<UI_SurfaceTransformDialog *>(data);
		dialog->applyMode_->value(0);
		dialog->refreshControls();
		if (widget == dialog->fitSurface_)
			dialog->quickFit(SurfaceFitMode::fitSurface);
		else if (widget == dialog->fitWidth_)
			dialog->quickFit(SurfaceFitMode::fitWidth);
		else if (widget == dialog->fitHeight_)
			dialog->quickFit(SurfaceFitMode::fitHeight);
		else if (widget == dialog->native_)
		{
			dialog->quickSize(0);
		}
		else if (widget == dialog->size64_)
		{
			dialog->quickSize(64);
		}
		else if (widget == dialog->size128_)
		{
			dialog->quickSize(128);
		}
		else if (widget == dialog->size256_)
		{
			dialog->quickSize(256);
		}
		else
		{
			dialog->offsetX_->value("0");
			dialog->offsetY_->value("0");
			dialog->rotation_->value("0");
			dialog->mirrorX_->value(0);
			dialog->mirrorY_->value(0);
			dialog->fitMode_ = SurfaceFitMode::native;
			dialog->fixedSizeUnits_.reset();
			dialog->quickSize(0);
		}
	}

	Instance &instance_;
	SurfaceTransformCapabilities capabilities_;
	SurfaceTransformPreview preview_;
	bool isWall_ = false;
	bool isPlane_ = false;
	bool closed_ = false;
	bool mixed_ = false;
	bool syncing_ = false;
	int objectCount_ = 0;
	int objectOverride_ = -1;
	int partsOverride_ = 0;
	std::vector<std::pair<int, int>> objects_;
	std::vector<WallTarget> wallTargets_;
	std::vector<PlaneTarget> planeTargets_;

	SurfaceTransform firstTransform_;
	int firstBaseX_ = 0;
	int firstBaseY_ = 0;
	int imageWidth_ = 0;
	int imageHeight_ = 0;
	SurfaceFitMode fitMode_ = SurfaceFitMode::none;
	std::optional<int> fixedSizeUnits_;
	std::vector<NumericControl> numeric_;
	std::set<SString> packageResourceNames_;
	bool initialized_ = false;

	Fl_Box *title_ = nullptr;
	Fl_Box *subtitle_ = nullptr;
	Fl_Group *targetGroup_ = nullptr;
	Fl_Box *scopeLabel_ = nullptr;
	Fl_Choice *scope_ = nullptr;
	Fl_Box *applyLabel_ = nullptr;
	Fl_Choice *applyMode_ = nullptr;
	Fl_Box *targetInfo_ = nullptr;
	Fl_Group *transformGroup_ = nullptr;
	Fl_Box *sizeHeading_ = nullptr;
	Fl_Float_Input *offsetX_ = nullptr;
	Fl_Float_Input *offsetY_ = nullptr;
	Fl_Float_Input *scaleX_ = nullptr;
	Fl_Float_Input *scaleY_ = nullptr;
	Fl_Check_Button *lockAspect_ = nullptr;
	Fl_Check_Button *mirrorX_ = nullptr;
	Fl_Check_Button *mirrorY_ = nullptr;
	Fl_Float_Input *rotation_ = nullptr;
	Fl_Box *rotationNote_ = nullptr;
	Fl_Box *fitLabel_ = nullptr;
	Fl_Button *fitSurface_ = nullptr;
	Fl_Button *fitWidth_ = nullptr;
	Fl_Button *fitHeight_ = nullptr;
	Fl_Group *presetGroup_ = nullptr;
	Fl_Button *native_ = nullptr;
	Fl_Button *size64_ = nullptr;
	Fl_Button *size128_ = nullptr;
	Fl_Button *size256_ = nullptr;
	Fl_Box *presetNote_ = nullptr;
	Fl_Button *reset_ = nullptr;
	Fl_Box *tileInfo_ = nullptr;
	Fl_Box *previewStatus_ = nullptr;
	Fl_Box *compatibility_ = nullptr;
	Fl_Button *cancel_ = nullptr;
	Fl_Button *apply_ = nullptr;

	static int lastWidth_;
	static int lastHeight_;
};

int UI_SurfaceTransformDialog::lastWidth_ = 760;
int UI_SurfaceTransformDialog::lastHeight_ = 700;

} // namespace

void UI_RunSurfaceTransform(
		Instance &instance, int objectOverride, int partsOverride)
{
	if (instance.edit.mode != ObjType::linedefs &&
			instance.edit.mode != ObjType::sectors)
	{
		instance.Beep(
				"Surface Transform requires linedef or sector mode");
		return;
	}

	UI_SurfaceTransformDialog dialog(
			instance, objectOverride, partsOverride);
	if (!dialog.hasTargets())
	{
		instance.Beep("Select or highlight a wall, floor, or ceiling");
		return;
	}
	dialog.run();
}

bool UI_VerifySurfaceTransformLayout(Instance &instance,
		int width, int height, SString *reason)
{
	UI_SurfaceTransformDialog dialog(instance, -1, 0);
	if (!dialog.hasTargets())
	{
		if (reason)
			*reason = "The layout verifier requires one selected surface.";
		return false;
	}
	dialog.resize(0, 0, width, height);
	return dialog.layoutValid(reason);
}

bool UI_VerifySurfaceTransformWindowPolicy(
		Instance &instance, SString *reason)
{
	UI_SurfaceTransformDialog dialog(instance, -1, 0);
	if (!dialog.hasTargets())
	{
		if (reason)
			*reason =
					"The window-policy verifier requires one selected surface.";
		return false;
	}
	return dialog.windowPolicyValid(reason);
}
