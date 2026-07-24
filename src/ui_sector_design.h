//------------------------------------------------------------------------
//  SMART SECTOR DESIGNER PANEL
//------------------------------------------------------------------------

#ifndef __EUREKA_UI_SECTOR_DESIGN_H__
#define __EUREKA_UI_SECTOR_DESIGN_H__

#include "e_sector_design.h"
#include "hdr_fltk.h"
#include "m_keys.h"

#include <functional>

class Instance;
class UI_Pic;
enum class BrowserMode;

class UI_SectorDesigner : public Fl_Group
{
public:
	UI_SectorDesigner(Instance &inst, int X, int Y, int W, int H);
	~UI_SectorDesigner() override;
	void resize(int X, int Y, int W, int H) override;

	void Open(SectorDesignMode mode);
	void Close();
	bool Active() const;

	void CanvasClick(const v2double_t &point, keycode_t modifiers);
	void CanvasMove(const v2double_t &point, keycode_t modifiers,
					bool primaryButtonDown = false);
	void CanvasRelease(const v2double_t &point, keycode_t modifiers);
	bool CanvasKey(keycode_t key);
	bool CanvasWheel(int deltaY);
	void BrowsedItem(BrowserMode kind, int number, const char *name,
					 int e_state);
	void RemoveLastAnchor();
	void Escape();
	void Commit();
	void CycleRoute();
	void Flip();
	void Refresh();

	const SectorDesignPlan &Plan() const
	{
		return plan_;
	}
	SString ReviewText() const;
	Fl_Double_Window *ReviewDetailsWindow() const
	{
		return reviewWindow_;
	}

private:
	enum class PropertyTexture
	{
		floor,
		ceiling,
		wall
	};

	SectorDesignRequest CurrentRequest() const;
	void LoadMemory(SectorDesignMode mode);
	void SaveMemory();
	void ReadControls();
	void WriteControls();
	void RefreshTargets();
	void Recompute();
	void PresentPlan(bool planningException = false);
	void RefreshModeUI();
	void ClearGesture();
	void NavigateToIssue();
	void ReadLiftTriggers();
	void ReadDoorSegments();
	void ChooseDoorTexture(bool face);
	void UseAutoDoorTexture(bool face);
	void ChoosePropertyTexture(PropertyTexture texture);
	void UseAutoPropertyTexture(PropertyTexture texture);
	SString InferredPropertyTexture(PropertyTexture texture) const;
	void UpdatePropertyTexturePreviews();
	void ChooseSectorSpecial();
	void UpdateSectorSpecialDescription();
	void ChangePropertyTarget();
	void CopyReview();
	void ShowReviewDetails();
	void UpdateReviewDetails();
	bool WaitingForGesture() const;
	SString WaitingPrompt() const;
	SString WaitingSummary() const;
	void ChangeMode(SectorDesignMode mode);
	bool HandleTargetSectorClick(const v2double_t &point,
			keycode_t modifiers);

	static void optionCallback(Fl_Widget *, void *);
	static void connectionCallback(Fl_Widget *, void *);
	static void modeCallback(Fl_Widget *, void *);
	static void propertyTargetCallback(Fl_Widget *, void *);
	static void issueCallback(Fl_Widget *, void *);
	static void liftTriggerCallback(Fl_Widget *, void *);
	static void doorSegmentCallback(Fl_Widget *, void *);
	static void doorTextureCallback(Fl_Widget *, void *);
	static void doorTextureAutoCallback(Fl_Widget *, void *);
	static void propertyTextureCallback(Fl_Widget *, void *);
	static void propertyTextureAutoCallback(Fl_Widget *, void *);
	static void sectorSpecialCallback(Fl_Widget *, void *);
	static void copyReviewCallback(Fl_Widget *, void *);
	static void expandReviewCallback(Fl_Widget *, void *);
	static void reviewWindowCallback(Fl_Widget *, void *);
	static void commitCallback(Fl_Widget *, void *);
	static void closeCallback(Fl_Widget *, void *);

	Instance &inst_;
	SectorDesignRequest request_;
	SectorDesignPlan plan_;
	std::vector<v2double_t> fixedAnchors_;
	std::vector<int> endpointLines_;
	v2double_t pointer_;
	keycode_t pointerModifiers_ = 0;
	bool pointerActive_ = false;
	bool anchorPressArmed_ = false;
	bool anchorDragMoved_ = false;
	v2double_t anchorPressPoint_;
	bool extrudePressArmed_ = false;
	bool extrudeDragMoved_ = false;
	v2double_t extrudePressPoint_;
	int extrudePressSector_ = -1;
	int extrudeSourceSector_ = -1;
	bool committing_ = false;
	bool updating_ = false;
	bool planningException_ = false;
	int propertyTargetIndex_ = 0;
	std::vector<int> rankedRoutes_;
	int routeCyclePosition_ = 0;

	Fl_Choice *modeChoice_ = nullptr;
	Fl_Box *instructions_ = nullptr;
	Fl_Scroll *scroll_ = nullptr;

	Fl_Input *widthInput_ = nullptr;
	Fl_Input *depthInput_ = nullptr;
	Fl_Check_Button *extrudeUseDragCheck_ = nullptr;
	Fl_Check_Button *extrudeOppositeCheck_ = nullptr;
	Fl_Input *offsetInput_ = nullptr;
	Fl_Input *sidesInput_ = nullptr;
	Fl_Choice *polygonProfile_ = nullptr;
	Fl_Input *rotationInput_ = nullptr;
	Fl_Input *polygonInnerInput_ = nullptr;
	Fl_Choice *architectureStyle_ = nullptr;
	Fl_Choice *architectureElement_ = nullptr;
	Fl_Input *architectureBays_ = nullptr;
	Fl_Input *architectureSize_ = nullptr;
	Fl_Input *architectureMargin_ = nullptr;
	Fl_Choice *joinChoice_ = nullptr;
	Fl_Input *stepCountInput_ = nullptr;
	Fl_Input *stepRiseInput_ = nullptr;
	Fl_Input *stepTreadInput_ = nullptr;
	Fl_Check_Button *fitTargetCheck_ = nullptr;
	Fl_Input *targetFloorInput_ = nullptr;
	Fl_Check_Button *headroomCheck_ = nullptr;
	Fl_Check_Button *replaceCheck_ = nullptr;

	Fl_Choice *floorMode_ = nullptr;
	Fl_Choice *propertyTarget_ = nullptr;
	Fl_Input *floorValue_ = nullptr;
	Fl_Choice *ceilingMode_ = nullptr;
	Fl_Input *ceilingValue_ = nullptr;
	Fl_Choice *lightMode_ = nullptr;
	Fl_Input *lightValue_ = nullptr;
	Fl_Input *floorTexture_ = nullptr;
	Fl_Input *ceilingTexture_ = nullptr;
	Fl_Input *wallTexture_ = nullptr;
	Fl_Button *floorTextureButton_ = nullptr;
	Fl_Button *ceilingTextureButton_ = nullptr;
	Fl_Button *wallTextureButton_ = nullptr;
	Fl_Toggle_Button *floorTextureAuto_ = nullptr;
	Fl_Toggle_Button *ceilingTextureAuto_ = nullptr;
	Fl_Toggle_Button *wallTextureAuto_ = nullptr;
	UI_Pic *floorTexturePreview_ = nullptr;
	UI_Pic *ceilingTexturePreview_ = nullptr;
	UI_Pic *wallTexturePreview_ = nullptr;
	Fl_Input *sectorType_ = nullptr;
	Fl_Button *sectorTypeButton_ = nullptr;
	Fl_Output *sectorTypeDescription_ = nullptr;
	Fl_Input *sectorTag_ = nullptr;

	Fl_Choice *startConnection_ = nullptr;
	Fl_Choice *endConnection_ = nullptr;
	Fl_Choice *doorPreset_ = nullptr;
	Fl_Input *doorDepth_ = nullptr;
	Fl_Input *doorWidth_ = nullptr;
	Fl_Input *doorOffset_ = nullptr;
	Fl_Choice *doorPlacement_ = nullptr;
	Fl_Multi_Browser *doorSegments_ = nullptr;
	std::vector<int> availableDoorLines_;
	Fl_Input *faceTexture_ = nullptr;
	Fl_Input *trackTexture_ = nullptr;
	Fl_Button *faceTextureButton_ = nullptr;
	Fl_Button *trackTextureButton_ = nullptr;
	Fl_Toggle_Button *faceAutoButton_ = nullptr;
	Fl_Toggle_Button *trackAutoButton_ = nullptr;
	UI_Pic *facePreview_ = nullptr;
	UI_Pic *trackPreview_ = nullptr;
	Fl_Choice *liftPreset_ = nullptr;
	Fl_Multi_Browser *liftTriggers_ = nullptr;
	Fl_Box *liftGuide_ = nullptr;
	Fl_Output *liftStatus_ = nullptr;
	std::vector<int> availableLiftTriggerLines_;

	Fl_Box *summary_ = nullptr;
	Fl_Hold_Browser *issues_ = nullptr;
	Fl_Button *copyReviewButton_ = nullptr;
	Fl_Button *expandReviewButton_ = nullptr;
	Fl_Button *commitButton_ = nullptr;
	Fl_Button *closeButton_ = nullptr;
	Fl_Double_Window *reviewWindow_ = nullptr;
	Fl_Text_Buffer *reviewBuffer_ = nullptr;
	Fl_Text_Display *reviewDisplay_ = nullptr;

	std::vector<DoorPreset> doorPresets_;
	std::vector<SectorActionPreset> liftPresets_;
};

using SmartSectorOpenOverride =
		std::function<void(Instance &, SectorDesignMode)>;

extern SmartSectorOpenOverride UI_SmartSectorOpen_Override;

void UI_OpenSectorDesigner(Instance &inst, SectorDesignMode mode);
void UI_CloseSectorDesigner(Instance &inst);
bool UI_SectorDesignerActive(const Instance &inst);
bool UI_SectorDesignerOwnsCanvas(const Instance &inst);
void UI_SectorDesignerCanvasClick(Instance &inst, const v2double_t &point,
								  keycode_t modifiers);
void UI_SectorDesignerCanvasMove(Instance &inst, const v2double_t &point,
								 keycode_t modifiers,
								 bool primaryButtonDown = false);
void UI_SectorDesignerCanvasRelease(Instance &inst,
								 const v2double_t &point,
								 keycode_t modifiers);
bool UI_SectorDesignerCanvasKey(Instance &inst, keycode_t key);
bool UI_SectorDesignerCanvasWheel(Instance &inst, int deltaY);
void UI_SectorDesignerRemoveLastAnchor(Instance &inst);
void UI_SectorDesignerEscape(Instance &inst);
void UI_SectorDesignerCommit(Instance &inst);
void UI_SectorDesignerCycleRoute(Instance &inst);
void UI_SectorDesignerFlip(Instance &inst);
void UI_SectorDesignerRefresh(Instance &inst);
void UI_SetSectorDesignPreview(Instance &inst,
							   const SectorDesignPlan &plan,
							   const std::vector<int> &retainedSectors = {});

#endif
