//------------------------------------------------------------------------
//  CAMPAIGN NAVIGATOR
//------------------------------------------------------------------------

#include "Instance.h"
#include "main.h"

#include "m_mapinfo.h"
#include "m_package.h"
#include "ui_campaign.h"

#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Text_Display.H>

#include <optional>
#include <vector>

namespace
{

SString RuntimeMapInfoStatus(const Instance &instance, const Wad_file &package)
{
	if (!instance.loaded.project.isExplicit() ||
			!M_RuntimeMapInfoPortSupported(instance.loaded.portName))
	{
		return {};
	}

	const std::optional<GeneratedRuntimeMapInfo> generated =
			M_GenerateRuntimeMapInfo(instance.loaded.project,
					instance.loaded.portName, instance.loaded.gameName);
	if (!generated)
		return "runtime unavailable";
	try
	{
		const RuntimeMapInfoInspection inspection = M_InspectRuntimeMapInfo(
				package.PathName(), M_ProjectPackageForPath(package.PathName()), package);
		return "runtime " + SString(M_RuntimeMapInfoFreshnessName(
				M_RuntimeMapInfoFreshness(inspection, generated->text)));
	}
	catch (const std::runtime_error &)
	{
		return "runtime unavailable";
	}
}

SString StatusText(const CampaignMapStatus &status)
{
	SString result;
	auto append = [&result](const char *text)
	{
		if (result.good())
			result += ", ";
		result += text;
	};

	if (status.current)
		append("current");
	if (status.campaignEntry)
		append("entry");
	if (status.dirty)
		append("dirty");
	if (status.missing())
		append("missing");
	else
		append("existing");
	if (status.configured)
		append("configured");
	else
		append("additional");

	return result;
}

SString ExitText(const ProjectMetadata &project,
		const CampaignMapStatus &status)
{
	if (!status.configured)
		return "not configured";
	SString result = "normal: ";
	if (status.normalExit)
		result += status.normalExit->good() ? *status.normalExit : "end";
	else
	{
		std::optional<SString> target = M_ProjectExitTarget(project, status.name,
				CampaignExit::normal);
		result += target ? *target + " (order)" : "end (order)";
	}
	if (status.secretExit && status.secretExit->good())
		result += " / secret: " + *status.secretExit;
	return result;
}

SString MapList(const std::vector<SString> &maps)
{
	SString result;
	for (const SString &map : maps)
	{
		if (result.good())
			result += ", ";
		result += map;
	}
	return result;
}

SString RouteName(const CampaignRoute &route)
{
	if (route.exit == CampaignExit::secret)
		return "Secret route";
	return route.followsCampaignOrder ? "Ordered normal route" : "Normal route";
}

SString RouteList(const std::vector<CampaignRoute> &routes)
{
	SString result;
	for (const CampaignRoute &route : routes)
	{
		if (result.good())
			result += "; ";
		result += SString::printf("%s -%s-> %s", route.source.c_str(),
				route.exit == CampaignExit::secret ? "secret" :
						(route.followsCampaignOrder ? "order" : "normal"),
				route.target.c_str());
	}
	return result;
}

SString DiagnosticTags(const CampaignGraphAnalysis &graph,
		const SString &mapName)
{
	size_t missing = 0;
	size_t cycles = 0;
	bool unreachable = false;
	for (const CampaignGraphDiagnostic &diagnostic : graph.diagnostics)
	{
		if (!diagnostic.involves(mapName))
			continue;
		switch (diagnostic.kind)
		{
			case CampaignGraphDiagnosticKind::missingRouteTarget:
				++missing;
				break;
			case CampaignGraphDiagnosticKind::unreachableMap:
				unreachable = true;
				break;
			case CampaignGraphDiagnosticKind::potentialCycle:
				++cycles;
				break;
		}
	}

	SString result;
	auto append = [&result](const SString &text)
	{
		if (result.good())
			result += ", ";
		result += text;
	};
	if (unreachable)
		append("unreachable");
	if (missing > 0)
		append(missing == 1 ? "missing route target" :
				SString::printf("%llu missing-route warnings",
						static_cast<unsigned long long>(missing)));
	if (cycles > 0)
		append(cycles == 1 ? "potential cycle" :
				SString::printf("%llu potential cycles",
						static_cast<unsigned long long>(cycles)));
	return result.good() ? result : "clean";
}

SString DiagnosticDetail(const CampaignGraphDiagnostic &diagnostic,
		const CampaignGraphAnalysis &graph)
{
	switch (diagnostic.kind)
	{
		case CampaignGraphDiagnosticKind::missingRouteTarget:
		{
			if (diagnostic.routes.empty())
				return "A configured route target is missing from the package.";
			const CampaignRoute &route = diagnostic.routes.front();
			return SString::printf(
					"Missing route target: %s %s -> %s points to a configured map "
					"that is not in the package. Create %s or change the route in "
					"Map Details.",
					RouteName(route).c_str(), route.source.c_str(),
					route.target.c_str(), route.target.c_str());
		}
		case CampaignGraphDiagnosticKind::unreachableMap:
			return SString::printf(
					"Unreachable map: %s cannot be reached from campaign %s %s "
					"through any configured normal or secret route. Connect it in "
					"Map Details or remove it from the campaign order.",
					diagnostic.maps.front().c_str(),
					graph.entryMaps.size() == 1 ? "entry" : "entries",
					MapList(graph.entryMaps).c_str());
		case CampaignGraphDiagnosticKind::potentialCycle:
			return SString::printf(
					"Potential cycle: routes among %s can repeat indefinitely (%s). "
					"Confirm that the loop is intentional or change an exit in Map Details.",
					MapList(diagnostic.maps).c_str(),
					RouteList(diagnostic.routes).c_str());
	}
	return "Campaign graph diagnostic.";
}

class UI_CampaignMapEditor : public UI_Escapable_Window
{
public:
	UI_CampaignMapEditor(const ProjectMetadata &project, const SString &mapName) :
			UI_Escapable_Window(570, 405, "Campaign Map Details"),
			project_(project)
	{
		callback(closeCallback, this);
		set_modal();
		resizable(nullptr);

		definition_.mapName = mapName.asUpper();
		if (const CampaignMapDefinition *existing =
				project_.mapDefinition(mapName))
		{
			definition_ = *existing;
		}

		SString heading = SString::printf("Campaign details for %s",
				definition_.mapName.c_str());
		Fl_Box *title = new Fl_Box(25, 15, 520, 30);
		title->copy_label(heading.c_str());
		title->labelfont(FL_HELVETICA_BOLD);
		title->labelsize(18);
		title->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

		titleInput_ = new Fl_Input(155, 62, 370, 29, "Display title: ");
		titleInput_->labelfont(FL_HELVETICA_BOLD);
		titleInput_->maximum_size(80);
		titleInput_->value(definition_.title.c_str());

		episodeInput_ = new Fl_Input(155, 105, 370, 29, "Episode group: ");
		episodeInput_->labelfont(FL_HELVETICA_BOLD);
		episodeInput_->maximum_size(80);
		episodeInput_->value(definition_.episode.c_str());

		normalChoice_ = new Fl_Choice(155, 157, 370, 29, "Normal exit: ");
		normalChoice_->labelfont(FL_HELVETICA_BOLD);
		normalChoice_->down_box(FL_BORDER_BOX);
		normalChoice_->add("Follow campaign order|End campaign here");
		normalTargets_.push_back(std::nullopt);
		normalTargets_.push_back(SString{});

		secretChoice_ = new Fl_Choice(155, 200, 370, 29, "Secret exit: ");
		secretChoice_->labelfont(FL_HELVETICA_BOLD);
		secretChoice_->down_box(FL_BORDER_BOX);
		secretChoice_->add("No secret exit");
		secretTargets_.push_back(std::nullopt);

		for (const SString &slot : project_.mapSlots)
		{
			normalChoice_->add(slot.c_str());
			normalTargets_.push_back(slot.asUpper());
			secretChoice_->add(slot.c_str());
			secretTargets_.push_back(slot.asUpper());
		}

		normalChoice_->value(FindTarget(normalTargets_, definition_.normalExit));
		secretChoice_->value(FindTarget(secretTargets_, definition_.secretExit));
		const bool routesApply = project_.campaign != CampaignMode::singleMap;
		if (!routesApply)
		{
			normalChoice_->deactivate();
			secretChoice_->deactivate();
			normalChoice_->tooltip("Single-map campaigns have no outgoing routes.");
			secretChoice_->tooltip("Single-map campaigns have no outgoing routes.");
		}

		implicitFirstEntry_ = !project_.mapSlots.empty() &&
				project_.mapSlots.front().noCaseEqual(definition_.mapName);
		entryPoint_ = new Fl_Check_Button(155, 238, 370, 26,
				"Campaign entry point (for example, an episode start)");
		entryPoint_->value(implicitFirstEntry_ || definition_.entryPoint ? 1 : 0);
		if (implicitFirstEntry_ || !routesApply)
		{
			entryPoint_->deactivate();
			entryPoint_->tooltip(implicitFirstEntry_ ?
					"The first configured slot is always an entry point." :
					"Single-map campaigns have one implicit entry point.");
		}

		const char *helpText = routesApply ?
				"Entry points and routes describe the editor campaign graph. Use File / "
				"Generate Runtime MAPINFO to publish them; the header reports managed "
				"runtime freshness." :
				"Single-map campaigns have no outgoing routes. Titles and episodes are "
				"published only when Generate Runtime MAPINFO is explicitly run.";
		Fl_Box *help = new Fl_Box(45, 275, 480, 54, helpText);
		help->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);

		Fl_Button *cancel = new Fl_Button(95, 360, 100, 30, "Cancel");
		cancel->callback(closeCallback, this);
		Fl_Return_Button *use = new Fl_Return_Button(375, 360, 100, 30, "Use");
		use->labelfont(FL_HELVETICA_BOLD);
		use->callback(useCallback, this);

		end();
	}

	std::optional<CampaignMapDefinition> Run()
	{
		show();
		while (!finished_)
			Fl::wait(0.2);
		hide();
		return accepted_ ? std::optional<CampaignMapDefinition>(definition_) :
				std::nullopt;
	}

private:
	static int FindTarget(const std::vector<std::optional<SString>> &targets,
			const std::optional<SString> &target)
	{
		for (size_t index = 0; index < targets.size(); ++index)
		{
			if (!targets[index] && !target)
				return static_cast<int>(index);
			if (targets[index] && target &&
					targets[index]->noCaseEqual(*target))
			{
				return static_cast<int>(index);
			}
		}
		return 0;
	}

	static void closeCallback(Fl_Widget *, void *data)
	{
		auto editor = static_cast<UI_CampaignMapEditor *>(data);
		editor->accepted_ = false;
		editor->finished_ = true;
	}

	static void useCallback(Fl_Widget *, void *data)
	{
		auto editor = static_cast<UI_CampaignMapEditor *>(data);
		editor->definition_.title = editor->titleInput_->value();
		editor->definition_.episode = editor->episodeInput_->value();
		const int normal = editor->normalChoice_->value();
		const int secret = editor->secretChoice_->value();
		if (normal < 0 || normal >= static_cast<int>(editor->normalTargets_.size()) ||
				secret < 0 || secret >= static_cast<int>(editor->secretTargets_.size()))
		{
			DLG_Notify("Choose valid campaign exit targets.");
			return;
		}
		editor->definition_.normalExit = editor->normalTargets_[normal];
		editor->definition_.secretExit = editor->secretTargets_[secret];
		editor->definition_.entryPoint = !editor->implicitFirstEntry_ &&
				editor->project_.campaign != CampaignMode::singleMap &&
				editor->entryPoint_->value() != 0;

		SString error;
		if (!M_ValidateCampaignMapDefinition(editor->project_,
				editor->definition_, &error))
		{
			DLG_Notify("Invalid campaign map details:\n\n%s", error.c_str());
			return;
		}
		editor->accepted_ = true;
		editor->finished_ = true;
	}

	const ProjectMetadata &project_;
	CampaignMapDefinition definition_;
	std::vector<std::optional<SString>> normalTargets_;
	std::vector<std::optional<SString>> secretTargets_;
	Fl_Input *titleInput_ = nullptr;
	Fl_Input *episodeInput_ = nullptr;
	Fl_Choice *normalChoice_ = nullptr;
	Fl_Choice *secretChoice_ = nullptr;
	Fl_Check_Button *entryPoint_ = nullptr;
	bool implicitFirstEntry_ = false;
	bool accepted_ = false;
	bool finished_ = false;
};

} // namespace

UI_CampaignNavigator::UI_CampaignNavigator(Instance &instance) :
		UI_Escapable_Window(1000, 660, "Campaign Navigator"),
		instance_(instance)
{
	callback(closeCallback, this);
	set_modal();
	resizable(nullptr);

	std::shared_ptr<Wad_file> package = instance_.wad.master.editWad();
	if (package)
	{
		statuses_ = M_CampaignMapStatuses(instance_.loaded.project, *package,
				instance_.loaded.levelName, instance_.Project_DirtyMapNames());
		graph_ = M_AnalyzeCampaignGraph(instance_.loaded.project, *package);
	}

	SString heading = "Project maps";
	if (instance_.loaded.project.isExplicit())
	{
		heading = SString::printf("%s — %d configured slot%s",
				instance_.loaded.project.name.c_str(),
				static_cast<int>(instance_.loaded.project.mapSlots.size()),
				instance_.loaded.project.mapSlots.size() == 1 ? "" : "s");
		heading += SString::printf(" — %llu entr%s",
				static_cast<unsigned long long>(graph_.entryMaps.size()),
				graph_.entryMaps.size() == 1 ? "y" : "ies");
		if (graph_.diagnostics.empty())
			heading += " — graph clean";
		else
		{
			heading += SString::printf(" — %llu graph warning%s",
					static_cast<unsigned long long>(graph_.diagnostics.size()),
					graph_.diagnostics.size() == 1 ? "" : "s");
		}
		if (package)
		{
			const SString runtime = RuntimeMapInfoStatus(instance_, *package);
			if (runtime.good())
				heading += " — " + runtime;
		}
	}
	if (instance_.Project_HasChanges())
		heading += " — modified";
	Fl_Box *title = new Fl_Box(20, 15, 960, 30);
	title->copy_label(heading.c_str());
	title->labelfont(FL_HELVETICA_BOLD);
	title->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

	static const int columns[] = { 65, 135, 100, 230, 140, 0 };
	const char *headings[] = {
		"Map", "Title", "Episode", "Routes", "State", "Diagnostics"
	};
	int headingX = 25;
	for (int column = 0; column < 6; ++column)
	{
		const int width = column < 5 ? columns[column] : 290;
		Fl_Box *heading = new Fl_Box(headingX, 50, width, 24, headings[column]);
		heading->labelfont(FL_HELVETICA_BOLD);
		heading->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
		headingX += width;
	}

	maps_ = new Fl_Hold_Browser(20, 75, 960, 335);
	maps_->column_widths(columns);
	maps_->format_char(0);
	maps_->callback(selectionCallback, this);
	for (const CampaignMapStatus &status : statuses_)
	{
		const SString diagnosticTags = status.configured ?
				DiagnosticTags(graph_, status.name) : "not configured";
		SString row = SString::printf("%s\t%s\t%s\t%s\t%s\t%s",
				status.name.c_str(), status.title.c_str(), status.episode.c_str(),
				ExitText(instance_.loaded.project, status).c_str(),
				StatusText(status).c_str(),
				diagnosticTags.c_str());
		maps_->add(row.c_str());
	}
	if (!statuses_.empty())
	{
		int current = 1;
		for (size_t index = 0; index < statuses_.size(); ++index)
			if (statuses_[index].current)
				current = static_cast<int>(index + 1);
		for (size_t index = 0; index < statuses_.size(); ++index)
		{
			if (statuses_[index].name.noCaseEqual(
					instance_.Project_NavigatorSelection()))
			{
				current = static_cast<int>(index + 1);
				break;
			}
		}
		maps_->value(current);
	}

	Fl_Box *diagnosticHeading = new Fl_Box(20, 420, 960, 24,
			"Selected map diagnostics");
	diagnosticHeading->labelfont(FL_HELVETICA_BOLD);
	diagnosticHeading->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
	diagnosticsBuffer_ = new Fl_Text_Buffer();
	diagnostics_ = new Fl_Text_Display(20, 444, 960, 115);
	diagnostics_->buffer(diagnosticsBuffer_);
	diagnostics_->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);

	open_ = new Fl_Button(20, 598, 75, 32, "Open");
	open_->callback(openCallback, this);
	create_ = new Fl_Button(105, 598, 75, 32, "Create");
	create_->callback(createCallback, this);
	duplicate_ = new Fl_Button(190, 598, 90, 32, "Duplicate");
	duplicate_->callback(duplicateCallback, this);
	rename_ = new Fl_Button(290, 598, 80, 32, "Rename");
	rename_->callback(renameCallback, this);
	delete_ = new Fl_Button(380, 598, 75, 32, "Delete");
	delete_->callback(deleteCallback, this);
	details_ = new Fl_Button(485, 598, 125, 32, "Map Details...");
	details_->callback(detailsCallback, this);
	Fl_Button *close = new Fl_Button(900, 598, 80, 32, "Close");
	close->callback(closeCallback, this);

	end();
	updateButtons();
}

UI_CampaignNavigator::~UI_CampaignNavigator()
{
	if (diagnostics_)
		diagnostics_->buffer(nullptr);
	delete diagnosticsBuffer_;
}

const CampaignMapStatus *UI_CampaignNavigator::selectedStatus() const
{
	const int selected = maps_->value();
	if (selected <= 0 || selected > static_cast<int>(statuses_.size()))
		return nullptr;
	return &statuses_[selected - 1];
}

void UI_CampaignNavigator::updateButtons()
{
	const CampaignMapStatus *status = selectedStatus();
	std::shared_ptr<Wad_file> package = instance_.wad.master.editWad();
	const bool writable = package && !package->IsReadOnly();
	const bool existing = status && status->exists;

	if (existing)
		open_->activate();
	else
		open_->deactivate();
	if (status && status->missing() && writable)
		create_->activate();
	else
		create_->deactivate();
	if (existing && writable)
	{
		duplicate_->activate();
		rename_->activate();
	}
	else
	{
		duplicate_->deactivate();
		rename_->deactivate();
	}
	if (existing && writable && package->LevelCount() > 1)
		delete_->activate();
	else
		delete_->deactivate();
	if (status && status->configured &&
			instance_.loaded.project.isExplicit() && writable)
	{
		details_->activate();
	}
	else
	{
		details_->deactivate();
	}
	updateDiagnosticDetails();
}

void UI_CampaignNavigator::updateDiagnosticDetails()
{
	auto display = [this](const SString &text)
	{
		diagnosticsBuffer_->text(text.c_str());
		diagnostics_->scroll(0, 0);
	};
	const CampaignMapStatus *status = selectedStatus();
	if (!status)
	{
		display("Select a map to inspect its campaign-graph state.");
		return;
	}
	if (!status->configured)
	{
		display(SString::printf(
				"%s is an additional package map and is not part of the configured "
				"campaign graph.", status->name.c_str()));
		return;
	}

	SString details;
	size_t issueNumber = 0;
	for (const CampaignGraphDiagnostic &diagnostic : graph_.diagnostics)
	{
		if (!diagnostic.involves(status->name))
			continue;
		++issueNumber;
		if (details.good())
			details += "\n";
		details += SString::printf("%llu. %s",
				static_cast<unsigned long long>(issueNumber),
				DiagnosticDetail(diagnostic, graph_).c_str());
	}
	if (!details.good())
	{
		details = SString::printf(
				"No graph diagnostics involve %s. Campaign %s: %s. Analysis "
				"uses effective normal routes and configured secret routes.",
				status->name.c_str(),
				graph_.entryMaps.size() == 1 ? "entry" : "entries",
				MapList(graph_.entryMaps).c_str());
	}
	display(details);
}

void UI_CampaignNavigator::choose(CampaignNavigatorAction action)
{
	const CampaignMapStatus *status = selectedStatus();
	if (!status)
		return;
	result_.action = action;
	result_.mapName = status->name;
	finished_ = true;
}

CampaignNavigatorResult UI_CampaignNavigator::Run()
{
	show();
	while (!finished_)
		Fl::wait(0.2);
	if (const CampaignMapStatus *selected = selectedStatus())
		instance_.Project_SetNavigatorSelection(selected->name);
	hide();
	return result_;
}

void UI_CampaignNavigator::closeCallback(Fl_Widget *, void *data)
{
	auto navigator = static_cast<UI_CampaignNavigator *>(data);
	navigator->result_ = {};
	navigator->finished_ = true;
}

void UI_CampaignNavigator::selectionCallback(Fl_Widget *, void *data)
{
	static_cast<UI_CampaignNavigator *>(data)->updateButtons();
}

void UI_CampaignNavigator::openCallback(Fl_Widget *, void *data)
{
	static_cast<UI_CampaignNavigator *>(data)->choose(
			CampaignNavigatorAction::open);
}

void UI_CampaignNavigator::createCallback(Fl_Widget *, void *data)
{
	static_cast<UI_CampaignNavigator *>(data)->choose(
			CampaignNavigatorAction::create);
}

void UI_CampaignNavigator::duplicateCallback(Fl_Widget *, void *data)
{
	static_cast<UI_CampaignNavigator *>(data)->choose(
			CampaignNavigatorAction::duplicate);
}

void UI_CampaignNavigator::renameCallback(Fl_Widget *, void *data)
{
	static_cast<UI_CampaignNavigator *>(data)->choose(
			CampaignNavigatorAction::rename);
}

void UI_CampaignNavigator::deleteCallback(Fl_Widget *, void *data)
{
	static_cast<UI_CampaignNavigator *>(data)->choose(
			CampaignNavigatorAction::remove);
}

void UI_CampaignNavigator::detailsCallback(Fl_Widget *, void *data)
{
	auto navigator = static_cast<UI_CampaignNavigator *>(data);
	const CampaignMapStatus *status = navigator->selectedStatus();
	if (!status || !status->configured)
		return;

	UI_CampaignMapEditor editor(navigator->instance_.loaded.project,
			status->name);
	std::optional<CampaignMapDefinition> definition = editor.Run();
	if (!definition)
		return;
	navigator->result_.action = CampaignNavigatorAction::editMetadata;
	navigator->result_.mapName = status->name;
	navigator->result_.mapDefinition = std::move(*definition);
	navigator->finished_ = true;
}
