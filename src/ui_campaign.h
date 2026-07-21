//------------------------------------------------------------------------
//  CAMPAIGN NAVIGATOR
//------------------------------------------------------------------------

#ifndef HERESY_UI_CAMPAIGN_H
#define HERESY_UI_CAMPAIGN_H

#include "m_project.h"
#include "ui_window.h"

class Fl_Hold_Browser;
class Instance;

enum class CampaignNavigatorAction
{
	none,
	open,
	create,
	duplicate,
	rename,
	remove
};

struct CampaignNavigatorResult
{
	CampaignNavigatorAction action = CampaignNavigatorAction::none;
	SString mapName;
};

class UI_CampaignNavigator : public UI_Escapable_Window
{
public:
	explicit UI_CampaignNavigator(Instance &instance);
	CampaignNavigatorResult Run();

private:
	void updateButtons();
	void choose(CampaignNavigatorAction action);
	const CampaignMapStatus *selectedStatus() const;

	static void closeCallback(Fl_Widget *, void *data);
	static void selectionCallback(Fl_Widget *, void *data);
	static void openCallback(Fl_Widget *, void *data);
	static void createCallback(Fl_Widget *, void *data);
	static void duplicateCallback(Fl_Widget *, void *data);
	static void renameCallback(Fl_Widget *, void *data);
	static void deleteCallback(Fl_Widget *, void *data);

	Instance &instance_;
	std::vector<CampaignMapStatus> statuses_;
	CampaignNavigatorResult result_;
	bool finished_ = false;

	Fl_Hold_Browser *maps_ = nullptr;
	Fl_Button *open_ = nullptr;
	Fl_Button *create_ = nullptr;
	Fl_Button *duplicate_ = nullptr;
	Fl_Button *rename_ = nullptr;
	Fl_Button *delete_ = nullptr;
};

#endif // HERESY_UI_CAMPAIGN_H
