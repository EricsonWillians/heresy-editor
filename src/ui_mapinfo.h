//------------------------------------------------------------------------
//  RUNTIME MAPINFO PREVIEW
//------------------------------------------------------------------------

#ifndef HERESY_UI_MAPINFO_H
#define HERESY_UI_MAPINFO_H

#include "m_mapinfo.h"
#include "ui_window.h"

#include <functional>

class Fl_Text_Buffer;
class Fl_Text_Display;

class UI_RuntimeMapInfoPreview : public UI_Escapable_Window
{
public:
	UI_RuntimeMapInfoPreview(const GeneratedRuntimeMapInfo &generated,
			const RuntimeMapInfoInspection &inspection,
			const fs::path &packagePath, size_t mapCount, bool projectModified);
	~UI_RuntimeMapInfoPreview() override;

	bool Run();

private:
	static void cancelCallback(Fl_Widget *, void *data);
	static void generateCallback(Fl_Widget *, void *data);

	Fl_Text_Display *preview_ = nullptr;
	Fl_Text_Buffer *previewBuffer_ = nullptr;
	bool accepted_ = false;
	bool finished_ = false;
};

using RuntimeMapInfoPreviewOverride = std::function<bool(
		const GeneratedRuntimeMapInfo &, const RuntimeMapInfoInspection &,
		const fs::path &, size_t, bool)>;

// Test and alternate-frontend seam matching the established dialog overrides.
extern RuntimeMapInfoPreviewOverride UI_RuntimeMapInfoPreview_Override;

bool UI_ConfirmRuntimeMapInfoPreview(const GeneratedRuntimeMapInfo &generated,
		const RuntimeMapInfoInspection &inspection, const fs::path &packagePath,
		size_t mapCount, bool projectModified);

#endif // HERESY_UI_MAPINFO_H
