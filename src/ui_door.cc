//------------------------------------------------------------------------
//  SMART DOOR REVIEW DIALOG
//------------------------------------------------------------------------

#include "ui_door.h"

#include "Instance.h"
#include "WadData.h"
#include "ui_pic.h"
#include "ui_window.h"

#include "FL/Fl_Box.H"
#include "FL/Fl_Button.H"
#include "FL/Fl_Choice.H"
#include "FL/Fl_Hold_Browser.H"
#include "FL/Fl_Input.H"
#include "FL/Fl_Toggle_Button.H"

SmartDoorDialogOverride UI_SmartDoorDialog_Override;
LoadedImageChooserOverride UI_LoadedImageChooser_Override;

std::vector<SString> UI_FilterDoorTextures(
		const ImageSet &images, const ConfigData &config,
		const SString &filter)
{
	std::vector<SString> result;
	for (const auto &[name, image] : images.getWallSurfaceImages(config))
		if (filter.empty() || name.findNoCase(filter.c_str()) != SString::npos)
			result.push_back(name);
	return result;
}

std::vector<SString> UI_FilterDoorTextures(
		const ImageSet &images, const SString &filter)
{
	std::vector<SString> result;
	for (const auto &[name, image] : images.getTextures())
		if (filter.empty() || name.findNoCase(filter.c_str()) != SString::npos)
			result.push_back(name);
	return result;
}

namespace
{

SString FormatDoorTargets(const selection_c &selection)
{
	const std::vector<int> sectors = selection.asArray();
	if (sectors.empty())
		return "TARGET: no sectors selected";
	if (sectors.size() == 1)
		return SString::printf(
				"TARGET: Sector #%d will become the door", sectors.front());

	SString result = SString::printf(
			"TARGET: %d selected sectors will each become a door — ",
			static_cast<int>(sectors.size()));
	const size_t shown = std::min<size_t>(sectors.size(), 6);
	for (size_t index = 0; index < shown; ++index)
	{
		if (index > 0)
			result += ", ";
		result += SString::printf("#%d", sectors[index]);
	}
	if (shown < sectors.size())
		result += ", ...";
	return result;
}

class UI_DoorTextureChooser : public UI_Escapable_Window
{
public:
	UI_DoorTextureChooser(Instance &inst, const SString &purpose,
						  const SString &inferred,
						  const SString &current,
						  UI_ImageSelectionKind kind) :
		UI_Escapable_Window(620, 510,
				kind == UI_ImageSelectionKind::flat ?
					"Choose Flat" : "Choose Wall Texture"),
		inst(inst), purpose(purpose), inferred(inferred),
		selected(current.empty() ? inferred : current), kind(kind)
	{
		copy_label(SString::printf("Choose %s", purpose.c_str()).c_str());
		Fl_Box *title = new Fl_Box(20, 12, 580, 28);
		title->copy_label(
				SString::printf("Choose %s", purpose.c_str()).c_str());
		title->labelfont(FL_HELVETICA_BOLD);
		title->labelsize(16);
		title->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

		Fl_Box *help = new Fl_Box(
				20, 42, 580, 40,
				kind == UI_ImageSelectionKind::flat ?
					"Choose from loaded flats. Search is case-insensitive; "
					"Use Auto returns to contextual inheritance." :
					"Choose from loaded wall textures. Search is "
					"case-insensitive; Use Auto returns to contextual "
					"inheritance.");
		help->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);

		search = new Fl_Input(92, 92, 288, 26, "Search:");
		search->when(FL_WHEN_CHANGED);
		search->callback(searchCallback, this);

		textureList = new Fl_Hold_Browser(20, 128, 360, 286);
		textureList->column_char('\t');
		textureList->column_widths(textureColumns);
		textureList->when(FL_WHEN_CHANGED | FL_WHEN_ENTER_KEY_ALWAYS);
		textureList->callback(selectionCallback, this);

		preview = new UI_Pic(inst, 426, 142, 128, 128);
		preview->box(FL_BORDER_BOX);
		preview->tooltip("Preview of the texture that will be used.");

		selectedLabel = new Fl_Box(400, 282, 190, 76);
		selectedLabel->align(
				FL_ALIGN_TOP | FL_ALIGN_CENTER | FL_ALIGN_WRAP |
				FL_ALIGN_INSIDE);

		Fl_Box *autoDetail = new Fl_Box(400, 360, 190, 50);
		autoDetail->copy_label(inferred.empty() ?
				"Auto has no inferred texture yet." :
				SString::printf("Auto currently resolves to %s.",
								inferred.c_str()).c_str());
		autoDetail->align(
				FL_ALIGN_TOP | FL_ALIGN_CENTER | FL_ALIGN_WRAP |
				FL_ALIGN_INSIDE);
		autoDetail->labelsize(11);

		Fl_Box *footer = new Fl_Box(0, 428, 620, 82);
		footer->box(FL_FLAT_BOX);
		footer->color(WINDOW_BG);

		autoButton = new Fl_Button(20, 454, 112, 32, "Use Auto");
		autoButton->tooltip("Clear the override and infer this texture.");
		autoButton->callback(autoCallback, this);

		importButton = new Fl_Button(144, 454, 150, 32, "Import...");
		importButton->tooltip(
				"Import one or more wall, floor, or all-surface images.");
		importButton->callback(importCallback, this);

		cancelButton = new Fl_Button(374, 454, 100, 32, "Cancel");
		cancelButton->callback(cancelCallback, this);

		useButton = new Fl_Button(486, 454, 114, 32, "Use Texture");
		useButton->labelfont(FL_HELVETICA_BOLD);
		useButton->callback(useCallback, this);

		end();
		resizable(textureList);
		callback(cancelCallback, this);
		populate();
	}

	bool run(SString &result)
	{
		set_modal();
		show();
		Fl::focus(search);
		while (!closed)
			Fl::wait(0.1);
		hide();
		if (accepted)
			result = selected;
		return accepted;
	}

private:
	void populate()
	{
		visible.clear();
		const ImageSet::SurfaceImageCatalog images =
				kind == UI_ImageSelectionKind::flat ?
				inst.wad.images.getPlaneSurfaceImages(inst.conf) :
				inst.wad.images.getWallSurfaceImages(inst.conf);
		for (const auto &[name, image] : images)
			if (SString(search->value()).empty() ||
				name.findNoCase(search->value()) != SString::npos)
				visible.push_back(name);
		textureList->clear();
		int selectedRow = 0;
		for (size_t index = 0; index < visible.size(); ++index)
		{
			const Img_c *image = kind == UI_ImageSelectionKind::flat ?
					inst.wad.images.W_GetFlat(inst.conf, visible[index]) :
					inst.wad.images.getTexture(inst.conf, visible[index]);
			textureList->add(SString::printf("%s\t%dx%d",
					visible[index].c_str(), image ? image->width() : 0,
					image ? image->height() : 0).c_str());
			if (visible[index].noCaseEqual(selected))
				selectedRow = static_cast<int>(index) + 1;
		}
		if (selectedRow == 0 && !visible.empty())
			selectedRow = 1;
		if (selectedRow > 0)
		{
			textureList->select(selectedRow);
			textureList->middleline(selectedRow);
		}
		else
			selected.clear();
		refreshSelection();
	}

	void refreshSelection()
	{
		const int row = textureList->value();
		if (row > 0 && row <= static_cast<int>(visible.size()))
			selected = visible[row - 1];

		if (selected.empty())
		{
			preview->Clear();
			selectedLabel->copy_label(visible.empty() ?
					"No loaded textures match." : "Select a texture.");
			useButton->deactivate();
			return;
		}
		if (kind == UI_ImageSelectionKind::flat)
			preview->GetFlat(selected);
		else
			preview->GetTex(selected);
		selectedLabel->copy_label(SString::printf(
				"%s\n%s", selected.c_str(), purpose.c_str()).c_str());
		useButton->activate();
	}

	static void searchCallback(Fl_Widget *, void *data)
	{
		static_cast<UI_DoorTextureChooser *>(data)->populate();
	}

	static void selectionCallback(Fl_Widget *, void *data)
	{
		UI_DoorTextureChooser *chooser =
				static_cast<UI_DoorTextureChooser *>(data);
		chooser->refreshSelection();
		if (Fl::event_clicks() && chooser->useButton->active())
			useCallback(nullptr, data);
	}

	static void autoCallback(Fl_Widget *, void *data)
	{
		UI_DoorTextureChooser *chooser =
				static_cast<UI_DoorTextureChooser *>(data);
		chooser->selected.clear();
		chooser->accepted = true;
		chooser->closed = true;
	}

	static void cancelCallback(Fl_Widget *, void *data)
	{
		UI_DoorTextureChooser *chooser =
				static_cast<UI_DoorTextureChooser *>(data);
		chooser->accepted = false;
		chooser->closed = true;
	}

	static void importCallback(Fl_Widget *, void *data)
	{
		UI_DoorTextureChooser *chooser =
				static_cast<UI_DoorTextureChooser *>(data);
		chooser->inst.ExecuteCommand("ImportSurfaceTextures");
		chooser->populate();
	}

	static void useCallback(Fl_Widget *, void *data)
	{
		UI_DoorTextureChooser *chooser =
				static_cast<UI_DoorTextureChooser *>(data);
		if (chooser->selected.empty())
			return;
		chooser->accepted = true;
		chooser->closed = true;
	}

	static constexpr int textureColumns[] = {220, 100, 0};

	Instance &inst;
	SString purpose;
	SString inferred;
	SString selected;
	UI_ImageSelectionKind kind;
	std::vector<SString> visible;
	Fl_Input *search = nullptr;
	Fl_Hold_Browser *textureList = nullptr;
	UI_Pic *preview = nullptr;
	Fl_Box *selectedLabel = nullptr;
	Fl_Button *autoButton = nullptr;
	Fl_Button *importButton = nullptr;
	Fl_Button *cancelButton = nullptr;
	Fl_Button *useButton = nullptr;
	bool closed = false;
	bool accepted = false;
};

constexpr int UI_DoorTextureChooser::textureColumns[];

class UI_SmartDoorDialog : public UI_Escapable_Window
{
public:
	UI_SmartDoorDialog(Instance &inst, const selection_c &selection,
					   const DoorOptions &initial) :
		UI_Escapable_Window(760, 640, "Make Smart Door"),
		inst(inst), selection(selection), options(initial),
		presets(M_AvailableDoorPresets(inst.conf, inst.loaded.levelFormat))
	{
		Fl_Box *title = new Fl_Box(
				20, 10, 720, 28,
				"Make doors from these target sectors");
		title->labelsize(16);
		title->labelfont(FL_HELVETICA_BOLD);
		title->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

		target = new Fl_Box(20, 43, 720, 58);
		target->box(FL_BORDER_BOX);
		target->color(DarkerColor(WINDOW_BG));
		target->labelcolor(fl_rgb_color(255, 156, 48));
		target->labelfont(FL_HELVETICA_BOLD);
		target->labelsize(14);
		target->align(
				FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
		target->copy_label(FormatDoorTargets(selection).c_str());

		Fl_Box *targetHelp = new Fl_Box(
				20, 104, 720, 36,
				"The orange hatched sector area on the map is the exact geometry "
				"that will be collapsed into a door. Nothing changes until Make Door.");
		targetHelp->align(
				FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
		targetHelp->labelsize(11);

		presetChoice = new Fl_Choice(
				180, 145, 350, 28, "Door behavior:");
		for (const DoorPreset &preset : presets)
		{
			SString label = M_DoorPresetLabel(inst.conf, preset);
			for (size_t index = 0; index < label.size(); index++)
				if (label[index] == '/' || label[index] == '|')
					label[index] = '-';
			presetChoice->add(label.c_str());
		}
		presetChoice->callback(optionCallback, this);

		int selectedPreset = 0;
		for (int index = 0; index < static_cast<int>(presets.size()); index++)
			if (presets[index].id.noCaseEqual(options.presetId))
				selectedPreset = index;
		if (!presets.empty())
		{
			presetChoice->value(selectedPreset);
			options.presetId = presets[selectedPreset].id;
		}

		faceInput = new UI_DynInput(
				180, 188, 290, 28, "Door face texture:");
		static_cast<Fl_Input *>(faceInput)->value(options.faceTexture.c_str());
		faceInput->callback(optionCallback, this);
		faceInput->callback2(optionCallback, this);
		faceInput->tooltip(
				"Texture visible on the moving door slab. Empty means Auto; "
				"custom texture names can still be typed.");
		faceBrowse = new Fl_Button(480, 188, 84, 28, "Browse...");
		faceBrowse->callback(browseCallback, this);
		faceAuto = new Fl_Toggle_Button(572, 188, 62, 28, "Auto");
		faceAuto->selection_color(fl_rgb_color(64, 152, 88));
		faceAuto->tooltip(
				"When lit, the face texture is inferred from the doorway.");
		faceAuto->callback(autoCallback, this);
		facePreview = new UI_Pic(inst, 652, 178, 72, 72);
		facePreview->box(FL_BORDER_BOX);
		facePreview->callback(browseCallback, this);
		facePreview->tooltip(
				"Door face preview. Click to choose a loaded texture.");
		faceDetail = new Fl_Box(180, 218, 450, 32);
		faceDetail->align(
				FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_WRAP |
				FL_ALIGN_INSIDE);
		faceDetail->labelsize(11);

		trackInput = new UI_DynInput(
				180, 268, 290, 28, "Track wall texture:");
		static_cast<Fl_Input *>(trackInput)->value(options.trackTexture.c_str());
		trackInput->callback(optionCallback, this);
		trackInput->callback2(optionCallback, this);
		trackInput->tooltip(
				"Texture on the narrow side walls inside the doorway. Empty "
				"means Auto; custom texture names can still be typed.");
		trackBrowse = new Fl_Button(480, 268, 84, 28, "Browse...");
		trackBrowse->callback(browseCallback, this);
		trackAuto = new Fl_Toggle_Button(572, 268, 62, 28, "Auto");
		trackAuto->selection_color(fl_rgb_color(64, 152, 88));
		trackAuto->tooltip(
				"When lit, the track texture is inferred from the doorway.");
		trackAuto->callback(autoCallback, this);
		trackPreview = new UI_Pic(inst, 652, 258, 72, 72);
		trackPreview->box(FL_BORDER_BOX);
		trackPreview->callback(browseCallback, this);
		trackPreview->tooltip(
				"Track wall preview. Click to choose a loaded texture.");
		trackDetail = new Fl_Box(180, 298, 450, 32);
		trackDetail->align(
				FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_WRAP |
				FL_ALIGN_INSIDE);
		trackDetail->labelsize(11);

		summary = new Fl_Box(20, 345, 720, 28);
		summary->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
		summary->labelfont(FL_HELVETICA_BOLD);

		Fl_Box *legend = new Fl_Box(
				20, 373, 720, 25,
				"Map preview: ORANGE HATCH = door target   "
				"ORANGE LINES = activating portals   GREEN = track walls");
		legend->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
		legend->labelsize(11);

		issues = new Fl_Hold_Browser(20, 402, 720, 154);
		issues->column_char('\t');
		issues->column_widths(issueColumns);
		issues->callback(issueCallback, this);

		Fl_Box *footer = new Fl_Box(0, 568, 760, 72);
		footer->box(FL_FLAT_BOX);
		footer->color(WINDOW_BG);

		cancelButton = new Fl_Button(25, 588, 105, 32, "Cancel");
		cancelButton->callback(cancelCallback, this);

		makeButton = new Fl_Button(585, 588, 150, 32, "Make Door");
		makeButton->labelfont(FL_HELVETICA_BOLD);
		makeButton->callback(makeCallback, this);
		const int count = static_cast<int>(selection.asArray().size());
		makeButton->copy_label(SString::printf(
				"Make %d Door%s", count, count == 1 ? "" : "s").c_str());

		end();
		resizable(issues);
		callback(cancelCallback, this);
		if (inst.main_win)
			position(inst.main_win->x() +
					 std::max(10, inst.main_win->w() - w() - 24),
					 inst.main_win->y() + 42);
		recompute();
	}

	~UI_SmartDoorDialog() override
	{
		UI_ClearDesignAssistPreview(inst);
	}

	bool run(DoorOptions &result)
	{
		set_modal();
		show();
		while (!closed)
			Fl::wait(0.1);
		hide();
		UI_ClearDesignAssistPreview(inst);
		if (accepted)
			result = options;
		return accepted;
	}

private:
	void clearPreview()
	{
		UI_ClearDesignAssistPreview(inst);
	}

	void recompute()
	{
		if (!presets.empty() && presetChoice->value() >= 0)
			options.presetId = presets[presetChoice->value()].id;
		options.faceTexture = faceInput->value();
		options.trackTexture = trackInput->value();
		faceAuto->value(options.faceTexture.empty() ? 1 : 0);
		trackAuto->value(options.trackTexture.empty() ? 1 : 0);

		plan = M_PlanSmartDoors(inst.level, inst.conf, &inst.wad.images,
								inst.loaded.levelFormat, selection, options);

		UI_SetSmartDoorPreview(inst, plan);

		summary->copy_label(SString::printf(
				"%d sector%s  |  %d activating portal%s  |  %d track wall%s",
				static_cast<int>(plan.sectors.size()),
				plan.sectors.size() == 1 ? "" : "s",
				static_cast<int>(plan.portalLines.size()),
				plan.portalLines.size() == 1 ? "" : "s",
				static_cast<int>(plan.trackLines.size()),
				plan.trackLines.size() == 1 ? "" : "s").c_str());

		facePreview->GetTex(plan.faceTexture);
		trackPreview->GetTex(plan.trackTexture);
		faceDetail->copy_label(options.faceTexture.empty() ?
				SString::printf(
					"Auto selected %s for the moving door slab.",
					plan.faceTexture.c_str()).c_str() :
				SString::printf(
					"Override: %s will be used on the moving door slab.",
					plan.faceTexture.c_str()).c_str());
		trackDetail->copy_label(options.trackTexture.empty() ?
				SString::printf(
					"Auto selected %s for the narrow doorway side walls.",
					plan.trackTexture.c_str()).c_str() :
				SString::printf(
					"Override: %s will be used on the doorway side walls.",
					plan.trackTexture.c_str()).c_str());

		issues->clear();
		issueRows.clear();
		if (plan.issues.empty())
		{
			issues->add("Ready\tNo warnings");
			issueRows.push_back(-1);
		}
		for (size_t index = 0; index < plan.issues.size(); ++index)
		{
			const DoorIssue &issue = plan.issues[index];
			SString location;
			if (issue.sector >= 0 && issue.line >= 0)
				location = SString::printf("Sector #%d, line #%d",
										   issue.sector, issue.line);
			else if (issue.sector >= 0)
				location = SString::printf("Sector #%d", issue.sector);
			else if (issue.line >= 0)
				location = SString::printf("Line #%d", issue.line);
			else
				location = "Selection";

			issues->add(SString::printf("%s\t%s: %s",
					issue.severity == DoorIssueSeverity::error ?
						"ERROR" : "Warning",
					location.c_str(), issue.message.c_str()).c_str());
			issueRows.push_back(static_cast<int>(index));
		}

		if (plan.valid())
			makeButton->activate();
		else
			makeButton->deactivate();
	}

	static void optionCallback(Fl_Widget *, void *data)
	{
		static_cast<UI_SmartDoorDialog *>(data)->recompute();
	}

	static void browseCallback(Fl_Widget *widget, void *data)
	{
		UI_SmartDoorDialog *dialog =
				static_cast<UI_SmartDoorDialog *>(data);
		const bool face = widget == dialog->faceBrowse ||
				widget == dialog->facePreview;
		UI_DynInput *input = face ?
				dialog->faceInput : dialog->trackInput;
		SString selected = input->value();
		const SString inferred = face ?
				dialog->plan.inferredFaceTexture :
				dialog->plan.inferredTrackTexture;
		if (!UI_ChooseSmartDoorTexture(dialog->inst,
				face ? "Door Face Texture" : "Track Wall Texture",
				inferred, selected))
			return;
		static_cast<Fl_Input *>(input)->value(selected.c_str());
		dialog->recompute();
	}

	static void autoCallback(Fl_Widget *widget, void *data)
	{
		UI_SmartDoorDialog *dialog =
				static_cast<UI_SmartDoorDialog *>(data);
		UI_DynInput *input = widget == dialog->faceAuto ?
				dialog->faceInput : dialog->trackInput;
		if (widget == dialog->faceAuto)
			dialog->options.useAutoFaceTexture();
		else
			dialog->options.useAutoTrackTexture();
		static_cast<Fl_Input *>(input)->value("");
		dialog->recompute();
	}

	static void issueCallback(Fl_Widget *, void *data)
	{
		UI_SmartDoorDialog *dialog =
				static_cast<UI_SmartDoorDialog *>(data);
		const int row = dialog->issues->value() - 1;
		if (row < 0 || row >= static_cast<int>(dialog->issueRows.size()))
			return;
		const int issueIndex = dialog->issueRows[row];
		if (issueIndex < 0 ||
			issueIndex >= static_cast<int>(dialog->plan.issues.size()))
			return;
		const DoorIssue &issue = dialog->plan.issues[issueIndex];
		if (issue.sector >= 0)
			dialog->inst.GoToObject(
					Objid(ObjType::sectors, issue.sector));
		else if (issue.line >= 0)
			dialog->inst.GoToObject(
					Objid(ObjType::linedefs, issue.line));
	}

	static void cancelCallback(Fl_Widget *, void *data)
	{
		UI_SmartDoorDialog *dialog =
				static_cast<UI_SmartDoorDialog *>(data);
		dialog->accepted = false;
		dialog->closed = true;
		dialog->clearPreview();
	}

	static void makeCallback(Fl_Widget *, void *data)
	{
		UI_SmartDoorDialog *dialog =
				static_cast<UI_SmartDoorDialog *>(data);
		if (!dialog->plan.valid())
			return;
		dialog->accepted = true;
		dialog->closed = true;
		dialog->clearPreview();
	}

	static constexpr int issueColumns[] = {80, 620, 0};

	Instance &inst;
	const selection_c &selection;
	DoorOptions options;
	DoorPlan plan;
	std::vector<DoorPreset> presets;

	Fl_Choice *presetChoice = nullptr;
	UI_DynInput *faceInput = nullptr;
	UI_DynInput *trackInput = nullptr;
	Fl_Button *faceBrowse = nullptr;
	Fl_Toggle_Button *faceAuto = nullptr;
	Fl_Button *trackBrowse = nullptr;
	Fl_Toggle_Button *trackAuto = nullptr;
	UI_Pic *facePreview = nullptr;
	UI_Pic *trackPreview = nullptr;
	Fl_Box *target = nullptr;
	Fl_Box *faceDetail = nullptr;
	Fl_Box *trackDetail = nullptr;
	Fl_Box *summary = nullptr;
	Fl_Hold_Browser *issues = nullptr;
	std::vector<int> issueRows;
	Fl_Button *cancelButton = nullptr;
	Fl_Button *makeButton = nullptr;
	bool closed = false;
	bool accepted = false;
};

constexpr int UI_SmartDoorDialog::issueColumns[];

} // namespace

bool UI_ChooseLoadedImage(Instance &inst, UI_ImageSelectionKind kind,
		const SString &purpose, const SString &inferred,
		SString &imageOverride)
{
	if (UI_LoadedImageChooser_Override)
		return UI_LoadedImageChooser_Override(
				inst, kind, purpose, inferred, imageOverride);
	UI_DoorTextureChooser chooser(
			inst, purpose, inferred, imageOverride, kind);
	return chooser.run(imageOverride);
}

bool UI_ChooseSmartDoorTexture(Instance &inst, const SString &purpose,
		const SString &inferred, SString &textureOverride)
{
	return UI_ChooseLoadedImage(inst,
			UI_ImageSelectionKind::wallTexture, purpose, inferred,
			textureOverride);
}

bool UI_RunSmartDoorDialog(Instance &inst, const selection_c &selection,
						   DoorOptions &options)
{
	if (UI_SmartDoorDialog_Override)
		return UI_SmartDoorDialog_Override(inst, selection, options);

	UI_SmartDoorDialog dialog(inst, selection, options);
	return dialog.run(options);
}

void UI_SetSmartDoorPreview(Instance &inst, const DoorPlan &plan)
{
	DesignAssistPreview preview;
	for (int sector : plan.sectors)
		preview.sectors.set(sector);
	for (int line : plan.portalLines)
		preview.activatingLines.set(line);
	for (int line : plan.trackLines)
		preview.trackLines.set(line);
	preview.emphasizeSectors = true;
	inst.edit.designAssistPreview.emplace(std::move(preview));
	inst.RedrawMap();
}

void UI_ClearDesignAssistPreview(Instance &inst)
{
	if (!inst.edit.designAssistPreview)
		return;
	inst.edit.designAssistPreview.reset();
	inst.RedrawMap();
}
