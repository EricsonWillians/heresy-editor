//------------------------------------------------------------------------
//  READ-ONLY PK3 METADATA INVENTORY
//------------------------------------------------------------------------

#include "ui_package.h"

#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Text_Display.H>
#include <FL/fl_utf8.h>

#include <string>

namespace
{

SString SafeDisplayText(const SString &input)
{
	std::string result(input.c_str(), input.size());
	for (char &character : result)
	{
		const unsigned char value = static_cast<unsigned char>(character);
		if (value < 0x20 || value == 0x7f)
			character = '?';
	}
	if (fl_utf8test(result.data(), static_cast<unsigned>(result.size())) == 0)
	{
		for (char &character : result)
			if (static_cast<unsigned char>(character) >= 0x80)
				character = '?';
	}
	return result;
}

SString FormatSize(uint64_t bytes)
{
	if (bytes < 1024)
		return SString::printf("%llu B", static_cast<unsigned long long>(bytes));
	if (bytes < 1024 * 1024)
		return SString::printf("%.1f KiB", static_cast<double>(bytes) / 1024.0);
	if (bytes < 1024ULL * 1024 * 1024)
		return SString::printf("%.1f MiB",
				static_cast<double>(bytes) / (1024.0 * 1024.0));
	return SString::printf("%.1f GiB",
			static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
}

const char *PreviewLabel(Pk3PreviewState state)
{
	switch (state)
	{
		case Pk3PreviewState::text: return "raw text";
		case Pk3PreviewState::empty: return "empty";
		case Pk3PreviewState::binary: return "binary";
		case Pk3PreviewState::tooLarge: return "preview limited";
		case Pk3PreviewState::unavailable: return "unavailable";
	}
	return "unavailable";
}

SString FileCount(size_t count)
{
	return SString::printf("%llu file%s",
			static_cast<unsigned long long>(count), count == 1 ? "" : "s");
}

SString ConflictScope(const ResourceConflict &conflict)
{
	if (conflict.kind == ResourceConflictKind::loadOrderOverride)
	{
		return SString::printf("%llu source%s",
				static_cast<unsigned long long>(conflict.participants.size()),
				conflict.participants.size() == 1 ? "" : "s");
	}

	size_t entries = 0;
	for (const ResourceConflictParticipant &participant : conflict.participants)
		entries += participant.occurrences;
	return SString::printf("%llu entr%s", static_cast<unsigned long long>(entries),
			entries == 1 ? "y" : "ies");
}

SString ConflictHandling(const ResourceConflict &conflict)
{
	if (conflict.kind == ResourceConflictKind::duplicateWithinSource)
	{
		return conflict.nameSpace == ResourceNamespaceKind::sprite ?
				"ambiguous" : "later entry nominal";
	}
	if (!conflict.nominalWinner)
		return "ambiguous";
	const ResourceConflictParticipant &winner =
			conflict.participants[*conflict.nominalWinner];
	if (winner.archivePath.good())
		return "later entry nominal";
	return "nominal: " + winner.label;
}

} // namespace

UI_Pk3Metadata::UI_Pk3Metadata(const Pk3PackageInventory &inventory,
		const ResourceDiagnostics &diagnostics) :
		UI_Escapable_Window(920, 660, "PK3 Metadata and Resources"),
		inventory_(inventory),
		diagnostics_(diagnostics)
{
	callback(closeCallback, this);
	set_modal();
	resizable(nullptr);

	SString packageName = SafeDisplayText(inventory_.path.filename().u8string());
	SString heading = SString::printf("%s — read-only package inventory",
			packageName.c_str());
	Fl_Box *title = new Fl_Box(20, 14, 880, 28);
	title->copy_label(heading.c_str());
	title->labelfont(FL_HELVETICA_BOLD);
	title->labelsize(18);
	title->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

	SString summary = SString::printf(
			"%llu files • %llu maps • %llu declarations/sources • "
			"%llu assets • %llu warnings • %llu overrides • %s unpacked",
			static_cast<unsigned long long>(inventory_.totalFiles),
			static_cast<unsigned long long>(inventory_.mapFiles),
			static_cast<unsigned long long>(inventory_.metadata.size()),
			static_cast<unsigned long long>(inventory_.resourceFiles),
			static_cast<unsigned long long>(diagnostics_.warningCount()),
			static_cast<unsigned long long>(diagnostics_.overrideCount()),
			FormatSize(inventory_.totalSize).c_str());
	Fl_Box *summaryBox = new Fl_Box(20, 45, 880, 24);
	summaryBox->copy_label(summary.c_str());
	summaryBox->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

	static const int columns[] = { 155, 455, 115, 0 };
	const char *headings[] = { "Type", "Archive path or group", "Size", "Handling" };
	int headingX = 25;
	for (int column = 0; column < 4; ++column)
	{
		const int width = column < 3 ? columns[column] : 170;
		Fl_Box *columnHeading = new Fl_Box(headingX, 76, width, 23,
				headings[column]);
		columnHeading->labelfont(FL_HELVETICA_BOLD);
		columnHeading->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
		headingX += width;
	}

	entries_ = new Fl_Hold_Browser(20, 99, 880, 225);
	entries_->column_widths(columns);
	entries_->format_char(0);
	entries_->callback(selectionCallback, this);
	addRows();

	Fl_Box *previewHeading = new Fl_Box(20, 333, 880, 24,
			"Selection details / verbatim preview");
	previewHeading->labelfont(FL_HELVETICA_BOLD);
	previewHeading->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

	previewBuffer_ = new Fl_Text_Buffer();
	preview_ = new Fl_Text_Display(20, 357, 880, 225);
	preview_->buffer(previewBuffer_);
	preview_->textfont(FL_COURIER);
	preview_->textsize(14);
	preview_->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);

	Fl_Box *policy = new Fl_Box(20, 591, 700, 50,
			"Inspection performs no writes. Normal saves update only managed map "
			"and Heresy project entries; declarations, runtime sources, resources, "
			"and other records remain untouched.");
	policy->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);

	Fl_Button *close = new Fl_Button(790, 603, 110, 32, "Close");
	close->callback(closeCallback, this);

	end();
	if (!rows_.empty())
	{
		entries_->value(1);
		updatePreview();
	}
	else
	{
		setPreview("The PK3 contains no files to display.");
	}
}

UI_Pk3Metadata::~UI_Pk3Metadata()
{
	if (preview_)
		preview_->buffer(nullptr);
	delete previewBuffer_;
}

void UI_Pk3Metadata::addRows()
{
	for (size_t index = 0; index < diagnostics_.conflicts.size(); ++index)
	{
		const ResourceConflict &conflict = diagnostics_.conflicts[index];
		SString subject = SString::printf("%s: %s",
				M_ResourceNamespaceName(conflict.nameSpace),
				conflict.editorName.c_str());
		SString row = SString::printf("%s\t%s\t%s\t%s",
				conflict.kind == ResourceConflictKind::loadOrderOverride ?
						"Override" : "Warning",
				subject.c_str(), ConflictScope(conflict).c_str(),
				ConflictHandling(conflict).c_str());
		entries_->add(row.c_str());
		rows_.push_back({ RowKind::conflict, index });
	}
	if (diagnostics_.conflicts.empty())
	{
		entries_->add("Diagnostics\tNo projected-name or loaded-source conflicts\t—\tclean");
		rows_.push_back({ RowKind::diagnosticSummary, 0 });
	}

	for (size_t index = 0; index < inventory_.metadata.size(); ++index)
	{
		const Pk3MetadataEntry &entry = inventory_.metadata[index];
		SString row = SString::printf("%s\t%s\t%s\t%s",
				M_Pk3MetadataKindName(entry.kind),
				SafeDisplayText(entry.path).c_str(), FormatSize(entry.size).c_str(),
				PreviewLabel(entry.previewState));
		entries_->add(row.c_str());
		rows_.push_back({ RowKind::metadata, index });
	}

	for (size_t index = 0; index < inventory_.resources.size(); ++index)
	{
		const Pk3ResourceGroup &group = inventory_.resources[index];
		SString path = SafeDisplayText(group.pathPrefix) + "/ — " + group.label;
		SString row = SString::printf("Resource group\t%s\t%s\t%s",
				path.c_str(), FormatSize(group.size).c_str(),
				group.projectedByEditor ? "editor namespace" : "preserved/listed");
		entries_->add(row.c_str());
		rows_.push_back({ RowKind::resource, index });
	}

	auto addSummary = [this](RowKind kind, const char *label, size_t count,
			const char *handling)
	{
		if (count == 0)
			return;
		SString row = SString::printf("Archive summary\t%s\t%s\t%s", label,
				FileCount(count).c_str(), handling);
		entries_->add(row.c_str());
		rows_.push_back({ kind, 0 });
	};
	addSummary(RowKind::maps, "Embedded map WADs", inventory_.mapFiles,
			"managed map entries");
	addSummary(RowKind::editorMetadata, "Heresy project metadata",
			inventory_.editorFiles, "managed project entry");
	addSummary(RowKind::directories, "Directory records",
			inventory_.directoryEntries, "preserved");
	addSummary(RowKind::other, "Other preserved files", inventory_.otherFiles,
			"untouched");
}

void UI_Pk3Metadata::setPreview(const SString &text)
{
	previewBuffer_->text(text.c_str());
	preview_->scroll(0, 0);
}

void UI_Pk3Metadata::updatePreview()
{
	const int selected = entries_->value();
	if (selected <= 0 || selected > static_cast<int>(rows_.size()))
	{
		setPreview("Select an inventory row to see its details.");
		return;
	}

	const Row &row = rows_[selected - 1];
	if (row.kind == RowKind::conflict)
	{
		const ResourceConflict &conflict = diagnostics_.conflicts[row.index];
		SString details = SString::printf(
				"Diagnostic: %s\nNamespace: %s\nEditor name: %s\n"
				"Resolution: %s\n\nSources in resolution order:\n",
				M_ResourceConflictKindName(conflict.kind),
				M_ResourceNamespaceName(conflict.nameSpace),
				conflict.editorName.c_str(),
				SafeDisplayText(conflict.resolution).c_str());
		for (size_t index = 0; index < conflict.participants.size(); ++index)
		{
			const ResourceConflictParticipant &participant =
					conflict.participants[index];
			SString source;
			if (participant.archivePath.good())
			{
				source = SafeDisplayText(participant.archivePath);
			}
			else
			{
				SString filename = SafeDisplayText(participant.packagePath.u8string());
				source = SString::printf("%s — %s (load #%llu)",
						SafeDisplayText(participant.label).c_str(), filename.c_str(),
						static_cast<unsigned long long>(participant.loadOrder));
				if (participant.occurrences > 1)
				{
					source += SString::printf(" — %llu occurrences",
							static_cast<unsigned long long>(participant.occurrences));
				}
			}
			details += SString::printf("  %llu. %s%s\n",
					static_cast<unsigned long long>(index + 1), source.c_str(),
					conflict.nominalWinner && *conflict.nominalWinner == index ?
							"  [nominal winner]" : "");
		}
		details += "\nNo archive path or load order has been changed.";
		setPreview(details);
		return;
	}
	if (row.kind == RowKind::diagnosticSummary)
	{
		setPreview("No duplicate projected editor names or cross-source "
				"flat/sprite/texture overrides were detected in the currently "
				"loaded resource stack.");
		return;
	}
	if (row.kind == RowKind::metadata)
	{
		const Pk3MetadataEntry &entry = inventory_.metadata[row.index];
		SString details = SString::printf(
				"Archive path: %s\nType: %s\nSize: %s\nPolicy: %s\n",
				SafeDisplayText(entry.path).c_str(),
				M_Pk3MetadataKindName(entry.kind), FormatSize(entry.size).c_str(),
				SafeDisplayText(entry.detail).c_str());
		if (entry.previewState == Pk3PreviewState::text)
			details += "\n--- Verbatim read-only content ---\n" + entry.preview;
		setPreview(details);
		return;
	}

	if (row.kind == RowKind::resource)
	{
		const Pk3ResourceGroup &group = inventory_.resources[row.index];
		SString details = SString::printf(
				"Archive group: %s/\nCategory: %s\nFiles: %llu\n"
				"Unpacked size: %s\n\n",
				SafeDisplayText(group.pathPrefix).c_str(), group.label.c_str(),
				static_cast<unsigned long long>(group.entries),
				FormatSize(group.size).c_str());
		if (group.projectedByEditor)
		{
			details += "Eligible files in this conventional namespace are exposed "
					"to the editor by basename. Their original archive paths remain "
					"unchanged; embedded WADs and unreadable entries are skipped.";
		}
		else
		{
			details += "This resource group is inventoried but not interpreted by "
					"the current map editor. Its archive records are preserved.";
		}
		setPreview(details);
		return;
	}

	if (row.kind == RowKind::maps)
	{
		setPreview(SString::printf(
				"Embedded maps: %llu\n\nRecognized maps use maps/<slot>.wad and "
				"are the only gameplay entries managed by normal map editing.",
				static_cast<unsigned long long>(inventory_.mapFiles)));
		return;
	}
	if (row.kind == RowKind::editorMetadata)
	{
		setPreview(SString::printf(
				"Heresy project metadata entries: %llu\n\nThe managed "
				"heresy/project.txt entry stores editor project settings and the "
				"campaign graph.",
				static_cast<unsigned long long>(inventory_.editorFiles)));
		return;
	}
	if (row.kind == RowKind::directories)
	{
		setPreview(SString::printf(
				"Directory records: %llu\n\nExplicit ZIP directory records are "
				"preserved and require no content preview.",
				static_cast<unsigned long long>(inventory_.directoryEntries)));
		return;
	}
	setPreview(SString::printf(
			"Other preserved files: %llu\n\nThese entries are not interpreted or "
			"rewritten by the package inventory or normal map/project saves.",
			static_cast<unsigned long long>(inventory_.otherFiles)));
}

void UI_Pk3Metadata::selectionCallback(Fl_Widget *, void *data)
{
	static_cast<UI_Pk3Metadata *>(data)->updatePreview();
}

void UI_Pk3Metadata::closeCallback(Fl_Widget *, void *data)
{
	static_cast<UI_Pk3Metadata *>(data)->finished_ = true;
}

void UI_Pk3Metadata::Run()
{
	show();
	while (!finished_)
		Fl::wait(0.2);
	hide();
}
