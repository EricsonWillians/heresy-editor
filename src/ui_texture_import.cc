//------------------------------------------------------------------------
//  SURFACE TEXTURE IMPORT USER INTERFACE
//------------------------------------------------------------------------

#include "ui_texture_import.h"

#include "Instance.h"
#include "hdr_fltk.h"
#include "m_files.h"
#include "m_texture_import.h"
#include "ui_browser.h"
#include "ui_window.h"
#include "WadData.h"

#include <algorithm>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace
{

fs::path lastImportDirectory;

const char *ResourceKindName(PackageResourceKind kind) noexcept
{
	return kind == PackageResourceKind::flat ?
			"flat" : "wall texture";
}

SString DisplayPath(const fs::path &path)
{
	return SString(path.u8string());
}

bool ChooseImportFiles(Instance &instance, std::vector<fs::path> &files)
{
	Fl_Native_File_Chooser chooser;
	chooser.title("Choose surface textures to import");
	chooser.type(Fl_Native_File_Chooser::BROWSE_MULTI_FILE);
	chooser.filter("Surface images\t*.{png,jpg,jpeg,tga,lmp}\n"
			"PNG images\t*.png\n"
			"JPEG images\t*.{jpg,jpeg}\n"
			"TGA images\t*.tga\n"
			"Doom images\t*.lmp");
	const fs::path directory = lastImportDirectory.empty() ?
			instance.Main_FileOpFolder() : lastImportDirectory;
	if (!directory.empty())
		chooser.directory(reinterpret_cast<const char *>(
				directory.u8string().c_str()));
	const int result = chooser.show();
	if (result < 0)
	{
		DLG_Notify("Unable to choose surface textures:\n\n%s",
				chooser.errmsg());
		return false;
	}
	if (result > 0)
		return false;
	for (int index = 0; index < chooser.count(); ++index)
	{
		const char *selected = chooser.filename(index);
		if (!selected || !selected[0])
			continue;
		fs::path path(reinterpret_cast<const char8_t *>(selected));
		files.push_back(path);
		lastImportDirectory = path.parent_path();
	}
	return !files.empty();
}

class ImportPreview final : public Fl_Widget
{
public:
	ImportPreview(int X, int Y, int W, int H, const Palette &palette) :
			Fl_Widget(X, Y, W, H), palette_(palette)
	{
		box(FL_BORDER_BOX);
	}

	void image(const Img_c *image)
	{
		image_ = image;
		redraw();
	}

private:
	void draw() override
	{
		fl_push_clip(x(), y(), w(), h());
		fl_color(FL_BLACK);
		fl_rectf(x(), y(), w(), h());
		for (int yy = 0; yy < h(); yy += 8)
			for (int xx = 0; xx < w(); xx += 8)
			{
				fl_color(((xx / 8 + yy / 8) & 1) ? 48 : 64);
				fl_rectf(x() + xx, y() + yy, std::min(8, w() - xx),
						std::min(8, h() - yy));
		}
		if (image_ && !image_->is_null())
		{
			const double scale = std::min(
					static_cast<double>(w() - 8) / image_->width(),
					static_cast<double>(h() - 8) / image_->height());
			const int targetWidth = std::max(1,
					static_cast<int>(image_->width() * scale));
			const int targetHeight = std::max(1,
					static_cast<int>(image_->height() * scale));
			const int originX = x() + (w() - targetWidth) / 2;
			const int originY = y() + (h() - targetHeight) / 2;
			for (int py = 0; py < targetHeight; ++py)
			{
				const int sourceY = std::min(image_->height() - 1,
						py * image_->height() / targetHeight);
				for (int px = 0; px < targetWidth; ++px)
				{
					const int sourceX = std::min(image_->width() - 1,
							px * image_->width() / targetWidth);
					const img_pixel_t pixel =
							image_->buf()[sourceY * image_->width() + sourceX];
					if (pixel == TRANS_PIXEL)
						continue;
					byte red;
					byte green;
					byte blue;
					palette_.decodePixel(pixel, red, green, blue);
					fl_color(fl_rgb_color(red, green, blue));
					fl_point(originX + px, originY + py);
				}
			}
		}
		fl_color(FL_DARK2);
		fl_rect(x(), y(), w(), h());
		fl_pop_clip();
	}

	const Palette &palette_;
	const Img_c *image_ = nullptr;
};

class UI_SurfaceTextureImport final : public UI_Escapable_Window
{
public:
	UI_SurfaceTextureImport(Instance &instance,
			std::vector<TextureImportRequestItem> requests) :
			UI_Escapable_Window(900, 650, "Import Surface Textures"),
			instance_(instance), requests_(std::move(requests))
	{
		Fl_Box *heading = new Fl_Box(20, 12, 860, 28,
				"Review surface texture import");
		heading->labelfont(FL_HELVETICA_BOLD);
		heading->labelsize(17);
		heading->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

		Fl_Box *help = new Fl_Box(20, 42, 860, 38,
				"Names are portable map identifiers. Safe Rename never shadows "
				"an IWAD, resource, or existing project image.");
		help->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);

		items_ = new Fl_Hold_Browser(20, 88, 535, 295);
		items_->column_char('\t');
		items_->column_widths(itemColumns_);
		items_->callback(selectCallback, this);

		preview_ = new ImportPreview(600, 96, 192, 192,
				instance_.wad.palette);
		previewLabel_ = new Fl_Box(568, 294, 255, 70);
		previewLabel_->align(FL_ALIGN_TOP | FL_ALIGN_CENTER |
				FL_ALIGN_INSIDE | FL_ALIGN_WRAP);

		name_ = new Fl_Input(112, 402, 130, 26, "Map name:");
		name_->when(FL_WHEN_CHANGED);
		name_->callback(editCallback, this);

		usage_ = new Fl_Choice(370, 402, 185, 26, "Use on:");
		usage_->add("Walls|Floors / Ceilings|All surfaces");
		usage_->callback(editCallback, this);

		policy_ = new Fl_Choice(690, 402, 190, 26, "Conflict:");
		policy_->add("Rename imported|Override loaded|Replace project|Skip");
		policy_->callback(editCallback, this);

		destination_ = new Fl_Box(20, 438, 860, 38);
		destination_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);

		issues_ = new Fl_Hold_Browser(20, 482, 860, 82);

		addMore_ = new Fl_Button(20, 584, 110, 30, "Add More...");
		addMore_->callback(addMoreCallback, this);
		remove_ = new Fl_Button(140, 584, 90, 30, "Remove");
		remove_->callback(removeCallback, this);
		summary_ = new Fl_Box(252, 582, 340, 36);
		summary_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
		cancel_ = new Fl_Button(660, 584, 90, 30, "Cancel");
		cancel_->callback(cancelCallback, this);
		import_ = new Fl_Return_Button(760, 584, 120, 30, "Import");
		import_->callback(importCallback, this);

		callback(closeCallback, this);
		end();
		set_modal();
		selected_ = requests_.empty() ? -1 : 0;
		recompute();
	}

	bool run(std::vector<TextureImportRequestItem> &requests,
			TextureImportPlan &plan)
	{
		show();
		while (!closed_)
			Fl::wait(0.1);
		hide();
		if (!accepted_)
			return false;
		requests = requests_;
		plan = plan_;
		return true;
	}

private:
	void recompute()
	{
		plan_ = M_PlanTextureImport(instance_.wad, instance_.conf,
				instance_.wad.master.editWad()->PathName(), requests_,
				&instance_.level);
		updating_ = true;
		items_->clear();
		for (size_t index = 0; index < plan_.items.size(); ++index)
		{
			const TextureImportPlanItem &item = plan_.items[index];
			size_t errors = 0;
			for (const TextureImportIssue &issue : plan_.issues)
				if (issue.item == static_cast<int>(index) &&
						issue.severity == TextureImportSeverity::error)
					++errors;
			items_->add(SString::printf("%s\t%s\t%s\t%s",
					item.resolvedName.empty() ? "(invalid)" :
							item.resolvedName.c_str(),
					M_TextureSurfaceUsageName(item.usage),
					M_TextureImportFormatName(item.format),
					errors ? "ERROR" :
							item.skipped ? "SKIP" :
							item.conflicts.empty() ? "READY" : "REVIEW").
					c_str());
		}
		if (selected_ >= static_cast<int>(plan_.items.size()))
			selected_ = static_cast<int>(plan_.items.size()) - 1;
		if (selected_ >= 0)
			items_->select(selected_ + 1);
		refreshSelected();
		issues_->clear();
		for (const TextureImportIssue &issue : plan_.issues)
			issues_->add(SString::printf("%s%s%s",
					issue.severity == TextureImportSeverity::error ?
							"ERROR: " : "Warning: ",
					issue.item >= 0 ?
							SString::printf("Item %d — ", issue.item + 1).c_str() :
							"",
					issue.explanation.c_str()).c_str());
		for (size_t itemIndex = 0; itemIndex < plan_.items.size();
				++itemIndex)
		{
			const TextureImportPlanItem &item = plan_.items[itemIndex];
			for (const TextureResourceOwner &owner : item.conflicts)
			{
				const SString location = owner.entryPath.empty() ?
						DisplayPath(owner.packagePath) :
						SString::printf("%s — %s",
								DisplayPath(owner.packagePath).c_str(),
								owner.entryPath.c_str());
				issues_->add(SString::printf(
						"Conflict: Item %zu — %s %s, load order %zu, "
						"%zu definition%s — %s",
						itemIndex + 1, owner.label.c_str(),
						ResourceKindName(owner.kind), owner.loadOrder,
						owner.occurrences,
						owner.occurrences == 1 ? "" : "s",
						location.c_str()).c_str());
			}
		}
		summary_->copy_label(SString::printf(
				"%zu file%s, %llu KiB; %zu replacement%s",
				plan_.importCount(), plan_.importCount() == 1 ? "" : "s",
				static_cast<unsigned long long>(
						(plan_.totalBytes + 1023) / 1024),
				plan_.replacementCount(),
				plan_.replacementCount() == 1 ? "" : "s").c_str());
		if (plan_.valid())
			import_->activate();
		else
			import_->deactivate();
		updating_ = false;
		redraw();
	}

	void refreshSelected()
	{
		const bool available = selected_ >= 0 &&
				selected_ < static_cast<int>(plan_.items.size());
		if (!available)
		{
			name_->value("");
			name_->deactivate();
			usage_->deactivate();
			policy_->deactivate();
			remove_->deactivate();
			preview_->image(nullptr);
			previewLabel_->label("");
			destination_->label("");
			return;
		}
		name_->activate();
		usage_->activate();
		policy_->activate();
		remove_->activate();
		const TextureImportPlanItem &item = plan_.items[selected_];
		name_->value(requests_[selected_].requestedName.empty() ?
				item.requestedName.c_str() :
				requests_[selected_].requestedName.c_str());
		switch (item.usage)
		{
			case TextureSurfaceUsage::walls: usage_->value(0); break;
			case TextureSurfaceUsage::planes: usage_->value(1); break;
			case TextureSurfaceUsage::allSurfaces: usage_->value(2); break;
		}
		usage_->mode(0, 0);
		usage_->mode(1, 0);
		usage_->mode(2, 0);
		if (item.format == TextureImportFormat::doomPatch)
		{
			usage_->mode(1, FL_MENU_INACTIVE);
			usage_->mode(2, FL_MENU_INACTIVE);
		}
		else if (item.format == TextureImportFormat::rawFlat)
		{
			usage_->mode(0, FL_MENU_INACTIVE);
			usage_->mode(2, FL_MENU_INACTIVE);
		}
		if (item.format != TextureImportFormat::rawFlat &&
				!instance_.conf.features.tx_start)
		{
			usage_->mode(0, FL_MENU_INACTIVE);
			usage_->mode(1, FL_MENU_INACTIVE);
			usage_->mode(2, FL_MENU_INACTIVE);
		}
		switch (item.conflictPolicy)
		{
			case TextureConflictPolicy::renameImported: policy_->value(0); break;
			case TextureConflictPolicy::overrideLoaded: policy_->value(1); break;
			case TextureConflictPolicy::replaceProject: policy_->value(2); break;
			case TextureConflictPolicy::skip: policy_->value(3); break;
		}
		policy_->mode(0, 0);
		policy_->mode(3, 0);
		const bool hasProjectConflict = std::any_of(
				item.conflicts.begin(), item.conflicts.end(),
				[](const TextureResourceOwner &owner)
				{
					return owner.project;
				});
		policy_->mode(1,
				hasProjectConflict ? FL_MENU_INACTIVE : 0);
		std::vector<PackageResourceKind> selectedKinds;
		if (item.usage != TextureSurfaceUsage::planes)
			selectedKinds.push_back(PackageResourceKind::wallTexture);
		if (item.usage != TextureSurfaceUsage::walls)
			selectedKinds.push_back(PackageResourceKind::flat);
		bool replaceable = !selectedKinds.empty();
		for (PackageResourceKind kind : selectedKinds)
		{
			const size_t matches = static_cast<size_t>(std::count_if(
					item.conflicts.begin(), item.conflicts.end(),
					[kind](const TextureResourceOwner &owner)
					{
						return owner.project &&
								owner.kind == kind &&
								owner.occurrences == 1 &&
								owner.replaceableProjectEntry;
					}));
			if (matches != 1)
				replaceable = false;
		}
		policy_->mode(2, replaceable ? 0 : FL_MENU_INACTIVE);
		preview_->image(item.preview.get());
		const std::u8string sourceName =
				item.source.filename().u8string();
		previewLabel_->copy_label(SString::printf(
				"%s\n%d x %d%s\n%llu KiB",
				reinterpret_cast<const char *>(sourceName.c_str()),
				item.width, item.height,
				item.hasAlpha ? " • alpha" : "",
				static_cast<unsigned long long>(
						(item.byteSize + 1023) / 1024)).c_str());
		SString destinations = "Destination: ";
		for (size_t index = 0; index < item.destinations.size(); ++index)
		{
			if (index)
				destinations += ", ";
			destinations += item.destinations[index].packagePath;
		}
		if (!item.conflicts.empty())
			destinations += SString::printf(
					" • %zu loaded conflict%s • %zu active-map use%s",
					item.conflicts.size(),
					item.conflicts.size() == 1 ? "" : "s",
					item.activeMapUsages,
					item.activeMapUsages == 1 ? "" : "s");
		destination_->copy_label(destinations.c_str());
	}

	void readControls()
	{
		if (updating_ || selected_ < 0 ||
				selected_ >= static_cast<int>(requests_.size()))
			return;
		TextureImportRequestItem &request = requests_[selected_];
		request.requestedName = name_->value();
		request.automaticUsage = false;
		request.usage = usage_->value() == 0 ?
				TextureSurfaceUsage::walls :
				usage_->value() == 1 ? TextureSurfaceUsage::planes :
						TextureSurfaceUsage::allSurfaces;
		switch (policy_->value())
		{
			case 1:
				request.conflictPolicy =
						TextureConflictPolicy::overrideLoaded;
				break;
			case 2:
				request.conflictPolicy =
						TextureConflictPolicy::replaceProject;
				break;
			case 3:
				request.conflictPolicy = TextureConflictPolicy::skip;
				break;
			default:
				request.conflictPolicy =
						TextureConflictPolicy::renameImported;
				break;
		}
		recompute();
	}

	static void selectCallback(Fl_Widget *, void *data)
	{
		auto *dialog = static_cast<UI_SurfaceTextureImport *>(data);
		dialog->selected_ = dialog->items_->value() - 1;
		dialog->updating_ = true;
		dialog->refreshSelected();
		dialog->updating_ = false;
	}

	static void editCallback(Fl_Widget *, void *data)
	{
		static_cast<UI_SurfaceTextureImport *>(data)->readControls();
	}

	static void addMoreCallback(Fl_Widget *, void *data)
	{
		auto *dialog = static_cast<UI_SurfaceTextureImport *>(data);
		std::vector<fs::path> paths;
		if (!ChooseImportFiles(dialog->instance_, paths))
			return;
		for (const fs::path &path : paths)
			dialog->requests_.push_back({ path });
		dialog->selected_ = static_cast<int>(dialog->requests_.size()) - 1;
		dialog->recompute();
	}

	static void removeCallback(Fl_Widget *, void *data)
	{
		auto *dialog = static_cast<UI_SurfaceTextureImport *>(data);
		if (dialog->selected_ < 0 ||
				dialog->selected_ >=
						static_cast<int>(dialog->requests_.size()))
			return;
		dialog->requests_.erase(
				dialog->requests_.begin() + dialog->selected_);
		if (dialog->selected_ >=
				static_cast<int>(dialog->requests_.size()))
			dialog->selected_ =
					static_cast<int>(dialog->requests_.size()) - 1;
		dialog->recompute();
	}

	static void importCallback(Fl_Widget *, void *data)
	{
		auto *dialog = static_cast<UI_SurfaceTextureImport *>(data);
		if (!dialog->plan_.valid())
			return;
		if (dialog->plan_.destructive())
		{
			SString impact =
					"This import can change how existing map references look:\n\n";
			for (const TextureImportPlanItem &item : dialog->plan_.items)
			{
				if (item.skipped ||
						(item.conflictPolicy !=
								TextureConflictPolicy::overrideLoaded &&
						 item.conflictPolicy !=
								TextureConflictPolicy::replaceProject))
				{
					continue;
				}
				impact += SString::printf(
						"%s — %s — %zu active-map use%s\n",
						item.resolvedName.c_str(),
						M_TextureConflictPolicyName(item.conflictPolicy),
						item.activeMapUsages,
						item.activeMapUsages == 1 ? "" : "s");
				for (const TextureImportDestination &destination :
						item.destinations)
				{
					impact += SString::printf(
							"  %s: %s%s%s\n",
							ResourceKindName(destination.kind),
							destination.replaceEntryPath ?
									"replace " : "write ",
							destination.replaceEntryPath ?
									destination.replaceEntryPath->c_str() :
									"",
							destination.replaceEntryPath ?
									SString::printf(" with %s",
											destination.packagePath.c_str()).
											c_str() :
									destination.packagePath.c_str());
				}
				for (const TextureResourceOwner &owner : item.conflicts)
				{
					impact += SString::printf(
							"  shadows %s %s (load order %zu): %s%s%s\n",
							owner.label.c_str(),
							ResourceKindName(owner.kind), owner.loadOrder,
							DisplayPath(owner.packagePath).c_str(),
							owner.entryPath.empty() ? "" : " — ",
							owner.entryPath.c_str());
				}
			}
			impact += "\nIWADs and external resources remain unchanged. "
					"Continue?";
			const int response = DLG_Confirm({ "Cancel", "&Import" },
					"%s", impact.c_str());
			if (response != 1)
				return;
		}
		dialog->accepted_ = true;
		dialog->closed_ = true;
	}

	static void cancelCallback(Fl_Widget *, void *data)
	{
		static_cast<UI_SurfaceTextureImport *>(data)->closed_ = true;
	}

	static void closeCallback(Fl_Widget *, void *data)
	{
		static_cast<UI_SurfaceTextureImport *>(data)->closed_ = true;
	}

	static constexpr int itemColumns_[] = { 100, 145, 105, 0 };

	Instance &instance_;
	std::vector<TextureImportRequestItem> requests_;
	TextureImportPlan plan_;
	int selected_ = -1;
	bool updating_ = false;
	bool closed_ = false;
	bool accepted_ = false;
	Fl_Hold_Browser *items_ = nullptr;
	ImportPreview *preview_ = nullptr;
	Fl_Box *previewLabel_ = nullptr;
	Fl_Input *name_ = nullptr;
	Fl_Choice *usage_ = nullptr;
	Fl_Choice *policy_ = nullptr;
	Fl_Box *destination_ = nullptr;
	Fl_Hold_Browser *issues_ = nullptr;
	Fl_Button *addMore_ = nullptr;
	Fl_Button *remove_ = nullptr;
	Fl_Box *summary_ = nullptr;
	Fl_Button *cancel_ = nullptr;
	Fl_Return_Button *import_ = nullptr;
};

constexpr int UI_SurfaceTextureImport::itemColumns_[];

} // namespace

void UI_ImportSurfaceTextures(Instance &instance)
{
	std::shared_ptr<Wad_file> package = instance.wad.master.editWad();
	if (!package || package->IsReadOnly())
		return;

	std::vector<fs::path> paths;
	if (!ChooseImportFiles(instance, paths))
		return;
	std::vector<TextureImportRequestItem> requests;
	for (const fs::path &path : paths)
		requests.push_back({ path });

	TextureImportPlan reviewed;
	UI_SurfaceTextureImport dialog(instance, requests);
	if (!dialog.run(requests, reviewed))
		return;

	bool packageUpdated = false;
	try
	{
		M_ApplyTextureImport(instance.wad, instance.conf, requests, reviewed,
				&instance.level, [&package]()
				{
					M_BackupWad(package.get());
				});
		packageUpdated = true;

		std::shared_ptr<Wad_file> reopened =
				M_OpenEditablePackage(package->PathName());
		if (!reopened)
			throw std::runtime_error("The saved package could not be reopened.");

		WadData refreshed = instance.wad;
		refreshed.master.ReplaceEditWad(reopened);
		refreshed.reloadSurfaceImages(
				refreshed.master.gameWad(), instance.conf,
				refreshed.master.resourceWads());
		instance.wad = std::move(refreshed);

		if (instance.main_win)
		{
			instance.main_win->canvas->DeleteContext();
			instance.main_win->browser->Populate();
			const TextureImportPlanItem *first = nullptr;
			for (const TextureImportPlanItem &item : reviewed.items)
				if (!item.skipped)
				{
					first = &item;
					break;
				}
			if (first)
			{
				const BrowserMode mode =
						first->usage == TextureSurfaceUsage::planes ?
								BrowserMode::flats : BrowserMode::textures;
				instance.main_win->BrowserMode(mode);
				instance.main_win->browser->JumpToTex(
						first->resolvedName.c_str());
			}
		}
		instance.RedrawMap();
		instance.Status_Set("Imported %zu surface texture%s (%zu replacement%s)",
				reviewed.importCount(),
				reviewed.importCount() == 1 ? "" : "s",
				reviewed.replacementCount(),
				reviewed.replacementCount() == 1 ? "" : "s");
	}
	catch (const std::exception &error)
	{
		if (packageUpdated)
			DLG_ShowError(false, "The package was updated, but the editor could "
					"not refresh its surface catalogs: %s\n\nReopen the "
					"project to load the imported resources.", error.what());
		else
			DLG_ShowError(false, "Surface textures were not imported: %s",
					error.what());
	}
}

void Instance::CMD_ImportSurfaceTextures()
{
	std::shared_ptr<Wad_file> package = wad.master.editWad();
	if (!package || package->IsReadOnly())
	{
		if (DLG_Confirm({ "Cancel", "&Create PWAD..." },
				"Surface texture import requires a writable WAD or PK3.\n\n"
				"Create or export the current map to a writable PWAD now?") != 1)
			return;
		if (!M_ExportMap(false))
			return;
		package = wad.master.editWad();
		if (!package || package->IsReadOnly())
			return;
	}
	UI_ImportSurfaceTextures(*this);
}
