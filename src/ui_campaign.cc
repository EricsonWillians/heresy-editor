//------------------------------------------------------------------------
//  CAMPAIGN NAVIGATOR
//------------------------------------------------------------------------

#include "Instance.h"
#include "main.h"

#include "ui_campaign.h"

#include <FL/Fl_Hold_Browser.H>

namespace
{

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

} // namespace

UI_CampaignNavigator::UI_CampaignNavigator(Instance &instance) :
		UI_Escapable_Window(600, 440, "Campaign Navigator"),
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
	}

	SString heading = "Project maps";
	if (instance_.loaded.project.isExplicit())
	{
		heading = SString::printf("%s — %d configured slot%s",
				instance_.loaded.project.name.c_str(),
				static_cast<int>(instance_.loaded.project.mapSlots.size()),
				instance_.loaded.project.mapSlots.size() == 1 ? "" : "s");
	}
	if (instance_.Project_HasChanges())
		heading += " — modified";
	Fl_Box *title = new Fl_Box(20, 15, 560, 30);
	title->copy_label(heading.c_str());
	title->labelfont(FL_HELVETICA_BOLD);
	title->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

	maps_ = new Fl_Hold_Browser(20, 50, 560, 300);
	static const int columns[] = { 120, 0 };
	maps_->column_widths(columns);
	maps_->callback(selectionCallback, this);
	for (const CampaignMapStatus &status : statuses_)
	{
		SString row = SString::printf("%s\t%s", status.name.c_str(),
				StatusText(status).c_str());
		maps_->add(row.c_str());
	}
	if (!statuses_.empty())
	{
		int current = 1;
		for (size_t index = 0; index < statuses_.size(); ++index)
			if (statuses_[index].current)
				current = static_cast<int>(index + 1);
		maps_->value(current);
	}

	open_ = new Fl_Button(20, 375, 90, 32, "Open");
	open_->callback(openCallback, this);
	create_ = new Fl_Button(120, 375, 90, 32, "Create");
	create_->callback(createCallback, this);
	duplicate_ = new Fl_Button(220, 375, 100, 32, "Duplicate");
	duplicate_->callback(duplicateCallback, this);
	rename_ = new Fl_Button(330, 375, 90, 32, "Rename");
	rename_->callback(renameCallback, this);
	delete_ = new Fl_Button(430, 375, 70, 32, "Delete");
	delete_->callback(deleteCallback, this);
	Fl_Button *close = new Fl_Button(510, 375, 70, 32, "Close");
	close->callback(closeCallback, this);

	end();
	updateButtons();
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
