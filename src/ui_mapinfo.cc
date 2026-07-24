//------------------------------------------------------------------------
//  RUNTIME MAPINFO PREVIEW
//------------------------------------------------------------------------

#include "ui_mapinfo.h"

#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Text_Display.H>

RuntimeMapInfoPreviewOverride UI_RuntimeMapInfoPreview_Override;

UI_RuntimeMapInfoPreview::UI_RuntimeMapInfoPreview(
		const GeneratedRuntimeMapInfo &generated,
		const RuntimeMapInfoInspection &inspection,
		const fs::path &packagePath, size_t mapCount, bool projectModified) :
		UI_Escapable_Window(880, 680, "Generate Runtime MAPINFO")
{
	callback(cancelCallback, this);
	set_modal();
	resizable(nullptr);

	SString heading = packagePath.filename().u8string();
	heading += inspection.state == RuntimeMapInfoState::managed ?
			" — update managed ZMAPINFO" : " — add managed ZMAPINFO";
	Fl_Box *title = new Fl_Box(20, 14, 840, 28);
	title->copy_label(heading.c_str());
	title->labelfont(FL_HELVETICA_BOLD);
	title->labelsize(18);
	title->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

	SString summary = SString::printf(
			"%llu configured map%s • %llu title fallback warning%s%s",
			static_cast<unsigned long long>(mapCount), mapCount == 1 ? "" : "s",
			static_cast<unsigned long long>(generated.warnings.size()),
			generated.warnings.size() == 1 ? "" : "s",
			projectModified ? " • project changes will be saved first" : "");
	Fl_Box *summaryBox = new Fl_Box(20, 46, 840, 24);
	summaryBox->copy_label(summary.c_str());
	summaryBox->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

	SString warnings;
	if (generated.warnings.empty())
		warnings = "All configured maps have explicit runtime titles.";
	else
	{
		warnings = SString::printf(
				"%llu configured map%s use map-slot title fallbacks. Every fallback "
				"is listed as a comment in the exact generated preview below.",
				static_cast<unsigned long long>(generated.warnings.size()),
				generated.warnings.size() == 1 ? "" : "s");
	}
	Fl_Box *warningBox = new Fl_Box(20, 76, 840, 64);
	warningBox->copy_label(warnings.c_str());
	warningBox->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);

	Fl_Box *previewHeading = new Fl_Box(20, 144, 840, 24,
			"Exact root ZMAPINFO content to write");
	previewHeading->labelfont(FL_HELVETICA_BOLD);
	previewHeading->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

	previewBuffer_ = new Fl_Text_Buffer();
	previewBuffer_->text(generated.text.c_str());
	preview_ = new Fl_Text_Display(20, 168, 840, 420);
	preview_->buffer(previewBuffer_);
	preview_->textfont(FL_COURIER);
	preview_->textsize(14);
	preview_->wrap_mode(Fl_Text_Display::WRAP_NONE, 0);

	Fl_Box *policy = new Fl_Box(20, 596, 610, 62,
			"Only this marker-owned declaration may be regenerated. Any user-authored "
			"MAPINFO-family declaration stops the operation. Missing titles use their "
			"map slot as shown above.");
	policy->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);

	Fl_Button *cancel = new Fl_Button(650, 614, 95, 32, "Cancel");
	cancel->callback(cancelCallback, this);
	Fl_Button *generate = new Fl_Button(755, 614, 105, 32,
			inspection.state == RuntimeMapInfoState::managed ? "Update" : "Generate");
	generate->callback(generateCallback, this);

	end();
}

UI_RuntimeMapInfoPreview::~UI_RuntimeMapInfoPreview()
{
	if (preview_)
		preview_->buffer(nullptr);
	delete previewBuffer_;
}

bool UI_RuntimeMapInfoPreview::Run()
{
	show();
	while (!finished_)
		Fl::wait(0.2);
	hide();
	return accepted_;
}

void UI_RuntimeMapInfoPreview::cancelCallback(Fl_Widget *, void *data)
{
	auto *dialog = static_cast<UI_RuntimeMapInfoPreview *>(data);
	dialog->accepted_ = false;
	dialog->finished_ = true;
}

void UI_RuntimeMapInfoPreview::generateCallback(Fl_Widget *, void *data)
{
	auto *dialog = static_cast<UI_RuntimeMapInfoPreview *>(data);
	dialog->accepted_ = true;
	dialog->finished_ = true;
}

bool UI_ConfirmRuntimeMapInfoPreview(const GeneratedRuntimeMapInfo &generated,
		const RuntimeMapInfoInspection &inspection, const fs::path &packagePath,
		size_t mapCount, bool projectModified)
{
	if (UI_RuntimeMapInfoPreview_Override)
	{
		return UI_RuntimeMapInfoPreview_Override(generated, inspection,
				packagePath, mapCount, projectModified);
	}
	return UI_RuntimeMapInfoPreview(generated, inspection, packagePath,
			mapCount, projectModified).Run();
}
