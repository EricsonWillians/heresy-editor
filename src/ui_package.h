//------------------------------------------------------------------------
//  READ-ONLY PK3 METADATA INVENTORY
//------------------------------------------------------------------------

#ifndef HERESY_UI_PACKAGE_H
#define HERESY_UI_PACKAGE_H

#include "m_package.h"
#include "m_resource_diagnostics.h"
#include "ui_window.h"

#include <vector>

class Fl_Hold_Browser;
class Fl_Text_Buffer;
class Fl_Text_Display;

class UI_Pk3Metadata : public UI_Escapable_Window
{
public:
	UI_Pk3Metadata(const Pk3PackageInventory &inventory,
			const ResourceDiagnostics &diagnostics);
	~UI_Pk3Metadata() override;

	void Run();

private:
	enum class RowKind
	{
		conflict,
		diagnosticSummary,
		metadata,
		resource,
		maps,
		editorMetadata,
		directories,
		other
	};

	struct Row
	{
		RowKind kind;
		size_t index = 0;
	};

	void addRows();
	void updatePreview();
	void setPreview(const SString &text);

	static void selectionCallback(Fl_Widget *, void *data);
	static void closeCallback(Fl_Widget *, void *data);

	const Pk3PackageInventory &inventory_;
	const ResourceDiagnostics &diagnostics_;
	std::vector<Row> rows_;
	Fl_Hold_Browser *entries_ = nullptr;
	Fl_Text_Display *preview_ = nullptr;
	Fl_Text_Buffer *previewBuffer_ = nullptr;
	bool finished_ = false;
};

#endif // HERESY_UI_PACKAGE_H
