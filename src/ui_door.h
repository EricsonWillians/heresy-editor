//------------------------------------------------------------------------
//  SMART DOOR REVIEW DIALOG
//------------------------------------------------------------------------

#ifndef __EUREKA_UI_DOOR_H__
#define __EUREKA_UI_DOOR_H__

#include "e_door.h"

#include <functional>
#include <vector>

class ImageSet;
class Instance;
class UI_Pic;
class UI_DynInput;
class Fl_Button;
class Fl_Choice;
class Fl_Hold_Browser;
class Fl_Box;

using SmartDoorDialogOverride =
		std::function<bool(Instance &, const selection_c &, DoorOptions &)>;

extern SmartDoorDialogOverride UI_SmartDoorDialog_Override;

enum class UI_ImageSelectionKind
{
	wallTexture,
	flat
};

using LoadedImageChooserOverride = std::function<bool(
		Instance &, UI_ImageSelectionKind, const SString &,
		const SString &, SString &)>;

extern LoadedImageChooserOverride UI_LoadedImageChooser_Override;

bool UI_RunSmartDoorDialog(Instance &inst, const selection_c &selection,
						   DoorOptions &options);
std::vector<SString> UI_FilterDoorTextures(
		const ImageSet &images, const SString &filter);
bool UI_ChooseLoadedImage(Instance &inst, UI_ImageSelectionKind kind,
		const SString &purpose, const SString &inferred,
		SString &imageOverride);
bool UI_ChooseSmartDoorTexture(Instance &inst, const SString &purpose,
		const SString &inferred, SString &textureOverride);
void UI_SetSmartDoorPreview(Instance &inst, const DoorPlan &plan);
void UI_ClearDesignAssistPreview(Instance &inst);

#endif
