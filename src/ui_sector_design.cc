//------------------------------------------------------------------------
//  SMART SECTOR DESIGNER PANEL
//------------------------------------------------------------------------

#include "ui_sector_design.h"

#include "Instance.h"
#include "e_basis.h"
#include "e_main.h"
#include "m_config.h"
#include "ui_door.h"
#include "ui_pic.h"
#include "ui_window.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <numbers>
#include <tuple>

SmartSectorOpenOverride UI_SmartSectorOpen_Override;

namespace
{

constexpr const char *MODE_LABELS[] =
{
	"Room", "Polygon", "Freeform", "Extrude",
	"Inset / Ring", "Corridor", "Stairs", "Lift", "Architecture"
};
constexpr int MODE_COUNT =
		static_cast<int>(sizeof(MODE_LABELS) / sizeof(MODE_LABELS[0]));
static_assert(MODE_COUNT ==
		static_cast<int>(SectorDesignMode::architecture) + 1);

constexpr const char *MODE_HELP[] =
{
	"Drag two corners, or click them. Shift makes a square; Ctrl draws from center.",
	"Drag center-to-radius, or click both points. Shift constrains rotation.",
	"Click a concave outline; click its first point or press Enter to finish.",
	"Drag a wall or sector interior toward the new space. No selection needed.",
	"Click a sector; Shift-click toggles more. Wheel changes ring thickness by one grid step.",
	"Drag or click endpoints; extra clicks add waypoints. Tab cycles routes; F reverses.",
	"Drag a new run, or click/Shift-click sectors. Click selected sectors for branch endpoints.",
	"Click a platform sector, Shift-click a batch, or drag a wall for a new lift alcove.",
	"Choose a Family and Structure, then drag its complete footprint inside a room or in clear void."
};
static_assert(std::size(MODE_HELP) == static_cast<size_t>(MODE_COUNT));

constexpr const char *POLYGON_PROFILE_LABELS[] =
{
	"Regular - Custom (3-64)",
	"Primitive - Triangle (3)",
	"Primitive - Square (4)",
	"Primitive - Pentagon (5)",
	"Primitive - Hexagon (6)",
	"Primitive - Octagon (8)",
	"Primitive - Decagon (10)",
	"Primitive - Dodecagon (12)",
	"Round - Circle draft (16)",
	"Round - Circle fine (32)",
	"Round - Circle ultra (64)",
	"Round - Circle precision (96)",
	"Star - Five point (10)",
	"Star - Eight point (16)",
	"Star - Twelve point (24)",
	"Star - Sixteen point (32)",
	"Cross - Greek (12)",
	"Cross - Maltese (16)",
	"Mechanical - Gear 8 teeth (32)",
	"Mechanical - Gear 16 teeth (64)",
	"Mechanical - Gear 24 teeth (96)",
	"Mechanical - Sawblade 32 teeth (128)",
	"Foil - Trefoil (36)",
	"Foil - Quatrefoil (48)",
	"Foil - Cinquefoil (60)",
	"Foil - Octofoil (96)",
	"Rosette - 8 lobes (48)",
	"Rosette - 16 lobes (96)",
	"Rosette - 24 lobes (144)",
	"Tracery - Cathedral 32 lobes (192)",
	"Tracery - Grand 48 lobes (288)"
};
static_assert(std::size(POLYGON_PROFILE_LABELS) ==
		static_cast<size_t>(
				SectorPolygonProfile::cathedralTracery48) + 1);

constexpr const char *ARCHITECTURE_STYLE_LABELS[] =
{
	"Functional",
	"Classical",
	"Romanesque",
	"Gothic",
	"Industrial",
	"Art Deco",
	"Infernal"
};
static_assert(std::size(ARCHITECTURE_STYLE_LABELS) ==
		static_cast<size_t>(SectorArchitectureStyle::infernal) + 1);

constexpr int ARCHITECTURE_FAMILY_COUNT =
		static_cast<int>(SectorArchitectureFamily::ceilingsVaults) + 1;

int ArchitectureFamilyFor(SectorArchitectureElement element)
{
	return static_cast<int>(M_ArchitectureDescriptor(element).family);
}

std::vector<SectorArchitectureElement> ArchitectureFamilyElements(int family)
{
	std::vector<SectorArchitectureElement> result;
	for (const SectorArchitectureDescriptor &descriptor :
			M_ArchitectureCatalog())
	{
		if (static_cast<int>(descriptor.family) == family)
			result.push_back(descriptor.element);
	}
	return result;
}

const char *ArchitecturePreviewName(DesignPreviewRole role)
{
	switch (role)
	{
		case DesignPreviewRole::architecture:
			return "cyan solid-mass";
		case DesignPreviewRole::architectureFloor:
			return "gold floor";
		case DesignPreviewRole::architectureCirculation:
			return "green circulation";
		case DesignPreviewRole::architectureWater:
			return "blue water";
		case DesignPreviewRole::architectureWall:
			return "red wall";
		case DesignPreviewRole::architectureCeiling:
			return "violet ceiling";
		case DesignPreviewRole::lift:
			return "yellow lift";
		default:
			return "semantic";
	}
}

std::map<SString, SectorDesignRequest> designerMemory;

struct ArchitectureMemory
{
	SectorArchitectureStyle style = SectorArchitectureStyle::gothic;
	int bays = 1;
	double size = 24.0;
	double height = 16.0;
	double margin = 16.0;
	bool mirrored = false;
	SectorArchitectureFunction function =
			SectorArchitectureFunction::staticGeometry;
};

std::map<SString, ArchitectureMemory> architectureMemory;

SString MemoryKey(const Instance &inst)
{
	return SString::printf("%s/%d", inst.loaded.gameName.c_str(),
						   static_cast<int>(inst.loaded.levelFormat));
}

SString ArchitectureMemoryKey(const Instance &inst,
		SectorArchitectureElement element)
{
	return SString::printf("%s/%s", MemoryKey(inst).c_str(),
			M_ArchitectureDescriptor(element).id);
}

ArchitectureMemory DefaultArchitectureMemory(const Instance &inst,
		SectorArchitectureElement element)
{
	const SectorArchitectureDescriptor &descriptor =
			M_ArchitectureDescriptor(element);
	ArchitectureMemory result;
	result.bays = descriptor.defaultBays;
	result.size = std::max(
			descriptor.defaultSize, descriptor.minimumSize);
	if (descriptor.sizeUsesPlayerClearance)
		result.size = std::max(
				result.size,
				std::min(
						std::max(1, inst.grid.getStep()) * 0.5,
						static_cast<double>(
								std::max(
										8,
										inst.conf.miscInfo.player_r * 2))));
	result.height = descriptor.defaultHeight;
	result.margin = std::max(
			0.0, std::min(
					descriptor.defaultMargin,
					static_cast<double>(
							std::max(1, inst.grid.getStep()))));
	return result;
}

SString SafeMenuLabel(SString label)
{
	for (char &character : label)
		if (character == '/' || character == '|' ||
			character == '&' || character == '_')
			character = '-';
	return label;
}

double ReadDouble(const Fl_Input *input, double fallback)
{
	if (!input || !input->value() || !*input->value())
		return fallback;
	char *end = nullptr;
	const double value = std::strtod(input->value(), &end);
	return end && *end == '\0' && std::isfinite(value) ? value : fallback;
}

int ReadInt(const Fl_Input *input, int fallback)
{
	if (!input || !input->value() || !*input->value())
		return fallback;
	char *end = nullptr;
	const long value = std::strtol(input->value(), &end, 10);
	return end && *end == '\0' ? static_cast<int>(value) : fallback;
}

std::optional<int> ReadOptionalInt(const Fl_Input *input)
{
	if (!input || !input->value() || !*input->value())
		return std::nullopt;
	char *end = nullptr;
	const long value = std::strtol(input->value(), &end, 10);
	if (!end || *end != '\0' ||
		value < std::numeric_limits<int>::min() ||
		value > std::numeric_limits<int>::max())
		return std::nullopt;
	return static_cast<int>(value);
}

bool InvalidOptionalInt(const Fl_Input *input)
{
	return input && input->value() && *input->value() &&
			!ReadOptionalInt(input).has_value();
}

void SetDouble(Fl_Input *input, double value)
{
	input->value(SString::printf("%.6g", value).c_str());
}

void SetInt(Fl_Input *input, int value)
{
	input->value(SString(value).c_str());
}

void SetOptionalInt(Fl_Input *input, const std::optional<int> &value)
{
	if (value)
		SetInt(input, *value);
	else
		input->value("");
}

v2double_t Constrain45(const v2double_t &origin, v2double_t point)
{
	const v2double_t delta = point - origin;
	const double length = delta.hypot();
	if (length <= 0.000001)
		return point;
	constexpr double eighthTurn = 3.14159265358979323846 / 4.0;
	const double angle = std::round(delta.atan2() / eighthTurn) * eighthTurn;
	return origin + v2double_t(std::cos(angle), std::sin(angle)) * length;
}

bool IsAnchorMode(SectorDesignMode mode)
{
	return mode != SectorDesignMode::inset;
}

int SectorForSide(const Document &doc, int side)
{
	if (!doc.isSidedef(side))
		return -1;
	const int sector = doc.sidedefs[side]->sector;
	return doc.isSector(sector) ? sector : -1;
}

bool LineBoundsSector(const Document &doc, int line, int sector)
{
	if (!doc.isLinedef(line) || !doc.isSector(sector))
		return false;
	const LineDef &linedef = *doc.linedefs[line];
	return SectorForSide(doc, linedef.right) == sector ||
			SectorForSide(doc, linedef.left) == sector;
}

double DistanceToSegment(const v2double_t &point,
						 const v2double_t &start,
						 const v2double_t &end)
{
	const v2double_t segment = end - start;
	const double lengthSquared = segment * segment;
	if (lengthSquared <= 0.000001)
		return (point - start).hypot();
	const double portion = std::clamp(
			((point - start) * segment) / lengthSquared, 0.0, 1.0);
	return (point - (start + segment * portion)).hypot();
}

bool DirectlyGrabbedLine(const Document &doc, int line,
						 const v2double_t &point, double scale)
{
	if (!doc.isLinedef(line))
		return false;
	const LineDef &linedef = *doc.linedefs[line];
	if (!doc.isVertex(linedef.start) || !doc.isVertex(linedef.end))
		return false;
	const double grabDistance = std::max(1.5, 4.0 / scale);
	return DistanceToSegment(point,
			doc.vertices[linedef.start]->xy(),
			doc.vertices[linedef.end]->xy()) <= grabDistance;
}

double Cross2D(const v2double_t &a, const v2double_t &b)
{
	return a.x * b.y - a.y * b.x;
}

int BoundaryInDragDirection(const Document &doc, int sector,
							const v2double_t &origin,
							const v2double_t &pointer)
{
	if (!doc.isSector(sector))
		return -1;
	const v2double_t direction = pointer - origin;
	if (direction.hypot() <= 0.000001)
		return -1;

	int bestLine = -1;
	double bestDistance = std::numeric_limits<double>::max();
	for (int line = 0; line < doc.numLinedefs(); ++line)
	{
		const LineDef &linedef = *doc.linedefs[line];
		if (!doc.isVertex(linedef.start) || !doc.isVertex(linedef.end))
			continue;
		const int right = SectorForSide(doc, linedef.right);
		const int left = SectorForSide(doc, linedef.left);
		// Ignore unrelated and self-referencing lines. A proper boundary has
		// the pressed sector on exactly one side.
		if ((right == sector) == (left == sector))
			continue;

		const v2double_t start = doc.vertices[linedef.start]->xy();
		const v2double_t edge =
				doc.vertices[linedef.end]->xy() - start;
		const double divisor = Cross2D(direction, edge);
		if (std::abs(divisor) <= 0.000001)
			continue;
		const v2double_t delta = start - origin;
		const double alongRay = Cross2D(delta, edge) / divisor;
		const double alongEdge = Cross2D(delta, direction) / divisor;
		if (alongRay < -0.000001 ||
			alongEdge < -0.000001 || alongEdge > 1.000001)
			continue;
		if (alongRay < bestDistance - 0.000001 ||
			(std::abs(alongRay - bestDistance) <= 0.000001 &&
			 (bestLine < 0 || line < bestLine)))
		{
			bestDistance = alongRay;
			bestLine = line;
		}
	}
	return bestLine;
}

} // namespace

UI_SectorDesigner::UI_SectorDesigner(Instance &inst, int X, int Y,
									 int W, int H) :
	Fl_Group(X, Y, W, H), inst_(inst)
{
	box(FL_FLAT_BOX);

	closeButton_ = new Fl_Button(X + 10, Y + 10, 26, 24, "X");
	closeButton_->tooltip("Exit Smart Sector Designer");
	closeButton_->callback(closeCallback, this);

	Fl_Box *title = new Fl_Box(X + 46, Y + 8, W - 56, 28,
							  "Smart Sector Designer");
	title->labelfont(FL_HELVETICA_BOLD);
	title->labelsize(18);
	title->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

	modeChoice_ = new Fl_Choice(X + 76, Y + 44, W - 88, 26, "Mode:");
	for (const char *label : MODE_LABELS)
		// Fl_Menu_ parses '/' as a submenu delimiter. Keep mode choices flat
		// so their indices remain identical to SectorDesignMode.
		modeChoice_->add(SafeMenuLabel(label).c_str());
	modeChoice_->callback(modeCallback, this);

	instructions_ = new Fl_Box(X + 10, Y + 75, W - 20, 42);
	instructions_->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_WRAP |
						 FL_ALIGN_INSIDE);
	instructions_->labelsize(11);

	const int buttonY = Y + H - 44;
	scroll_ = new Fl_Scroll(X + 4, Y + 120, W - 8,
						   std::max(80, buttonY - Y - 126));
	scroll_->type(Fl_Scroll::VERTICAL);
	scroll_->box(FL_THIN_DOWN_BOX);
	scroll_->begin();

	int cx = X + 12;
	int cy = Y + 126;
	const int labelX = cx + 82;
	const int inputW = W - 108;

	Fl_Box *geometryTitle = new Fl_Box(cx, cy, W - 24, 22, "Geometry");
	geometryTitle->labelfont(FL_HELVETICA_BOLD);
	geometryTitle->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
	cy += 26;

	auto addInput = [&](Fl_Input *&field, const char *label)
	{
		field = new Fl_Input(labelX, cy, inputW, 24, label);
		field->align(FL_ALIGN_LEFT);
		field->when(FL_WHEN_CHANGED | FL_WHEN_ENTER_KEY);
		field->callback(optionCallback, this);
		cy += 28;
	};

	addInput(widthInput_, "Width:");
	addInput(depthInput_, "Depth:");
	extrudeUseDragCheck_ = new Fl_Check_Button(
			labelX, cy, inputW, 24, "Drag sets depth");
	extrudeUseDragCheck_->tooltip(
			"Disable to use the exact signed Depth value.");
	extrudeUseDragCheck_->callback(connectionCallback, this);
	cy += 28;
	extrudeOppositeCheck_ = new Fl_Check_Button(
			labelX, cy, inputW, 24, "Opposite side (F)");
	extrudeOppositeCheck_->tooltip(
			"Normally the extrusion follows the pointer. Enable this, or "
			"press F over the canvas, to deliberately use the other side.");
	extrudeOppositeCheck_->callback(optionCallback, this);
	cy += 28;
	addInput(offsetInput_, "Offset:");
	addInput(sidesInput_, "Sides:");
	polygonProfile_ =
			new Fl_Choice(labelX, cy, inputW, 24, "Profile:");
	for (const char *label : POLYGON_PROFILE_LABELS)
		polygonProfile_->add(SafeMenuLabel(label).c_str());
	polygonProfile_->tooltip(
			"Professionally grouped from simple convex primitives through "
			"288-vertex grand cathedral tracery.");
	polygonProfile_->callback(connectionCallback, this);
	cy += 28;
	addInput(rotationInput_, "Rotation:");
	rotationInput_->tooltip(
			"Polygon or architectural support-section rotation in degrees.");
	addInput(polygonInnerInput_, "Inner %:");
	polygonInnerInput_->tooltip(
			"Inner radius for stars, gears, and rosettes (10 through 95).");

	architectureStyle_ =
			new Fl_Choice(labelX, cy, inputW, 24, "Style:");
	for (const char *label : ARCHITECTURE_STYLE_LABELS)
		architectureStyle_->add(SafeMenuLabel(label).c_str());
	architectureStyle_->tooltip(
			"Changes support and fountain-center sections: classical round, "
			"Gothic clustered, industrial geared, Art Deco sunburst, and "
			"more. Floor, wall, ceiling, water, and circulation structures "
			"use purpose-built geometry instead.");
	architectureStyle_->callback(optionCallback, this);
	cy += 28;
	architectureFamily_ =
			new Fl_Choice(labelX, cy, inputW, 24, "Family:");
	for (int family = 0; family < ARCHITECTURE_FAMILY_COUNT; ++family)
		architectureFamily_->add(SafeMenuLabel(
				M_ArchitectureFamilyLabel(
						static_cast<SectorArchitectureFamily>(
								family))).c_str());
	architectureFamily_->tooltip(
			"Choose the architectural system first. The Structure menu "
			"then shows only relevant tools instead of mixing columns, "
			"floors, water, walls, and ceilings.");
	architectureFamily_->callback(connectionCallback, this);
	cy += 28;
	architectureElement_ =
			new Fl_Choice(labelX, cy, inputW, 24, "Structure:");
	RebuildArchitectureStructureMenu(0);
	architectureElement_->tooltip(
			"Choose one generator from the selected architecture family.");
	architectureElement_->callback(connectionCallback, this);
	cy += 28;
	architectureDescription_ =
			new Fl_Box(cx, cy, W - 24, 38);
	architectureDescription_->align(
			FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
	architectureDescription_->labelsize(11);
	cy += 42;
	addInput(architectureBays_, "Bays:");
	addInput(architectureSize_, "Struct. size:");
	architectureSize_->tooltip(
			"Support diameter, wall or gallery thickness, stair width, "
			"terrace tread, pool bevel, or ceiling-rib width. While "
			"Architecture owns the canvas, the mouse wheel scales this by "
			"one quarter of the grid step.");
	addInput(architectureHeight_, "Elevation:");
	architectureHeight_->tooltip(
			"Positive relief per tier: raised-floor height, excavation "
			"depth, step rise, or ceiling-recess height.");
	addInput(architectureMargin_, "Margin:");
	architectureMargin_->tooltip(
			"Clearance between the dragged footprint and its generated "
			"structure. Set Margin to 0 and drag to a host boundary to "
			"split/reuse that boundary as a connected two-sided seam.");
	architectureMirror_ = new Fl_Check_Button(
			labelX, cy, inputW, 24, "Mirrored (F)");
	architectureMirror_->tooltip(
			"Mirror the structure across the drag's forward axis. F toggles "
			"this while the designer owns canvas focus.");
	architectureMirror_->callback(optionCallback, this);
	cy += 28;
	architectureFunction_ =
			new Fl_Choice(labelX, cy, inputW, 24, "Function:");
	architectureFunction_->add("Static geometry|Smart Lift");
	architectureFunction_->tooltip(
			"Central platforms may optionally receive a compatible Smart "
			"Lift preset, fresh tag, and local triggers in the same Undo.");
	architectureFunction_->callback(optionCallback, this);
	cy += 28;

	joinChoice_ = new Fl_Choice(labelX, cy, inputW, 24, "Join:");
	joinChoice_->add("Miter|Bevel|Segmented round");
	joinChoice_->callback(optionCallback, this);
	cy += 28;

	addInput(stepCountInput_, "Steps:");
	addInput(stepRiseInput_, "Rise:");
	addInput(stepTreadInput_, "Tread:");
	fitTargetCheck_ = new Fl_Check_Button(labelX, cy, inputW, 24,
										 "Fit target floor");
	fitTargetCheck_->callback(optionCallback, this);
	cy += 28;
	addInput(targetFloorInput_, "Target floor:");

	headroomCheck_ = new Fl_Check_Button(labelX, cy, inputW, 24,
										"Preserve headroom");
	headroomCheck_->callback(optionCallback, this);
	cy += 28;
	replaceCheck_ = new Fl_Check_Button(labelX, cy, inputW, 24,
									   "Replace affected sectors");
	replaceCheck_->tooltip(
			"Explicitly permit replacing protected or occupied geometry.");
	replaceCheck_->callback(optionCallback, this);
	cy += 34;

	Fl_Box *propertiesTitle = new Fl_Box(cx, cy, W - 24, 22,
										"Sector Properties");
	propertiesTitle->labelfont(FL_HELVETICA_BOLD);
	propertiesTitle->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
	cy += 26;

	propertyTarget_ = new Fl_Choice(labelX, cy, inputW, 24, "Edit:");
	propertyTarget_->add("Ring|Inner result");
	propertyTarget_->tooltip(
			"In Inset mode, edit the outer ring and inner cell independently.");
	propertyTarget_->callback(propertyTargetCallback, this);
	cy += 28;

	auto addValueControl = [&](Fl_Choice *&mode, Fl_Input *&value,
							   const char *label)
	{
		mode = new Fl_Choice(cx + 58, cy, 94, 24, label);
		mode->add("Auto|Absolute|Relative");
		mode->callback(optionCallback, this);
		value = new Fl_Input(cx + 160, cy, W - 184, 24);
		value->when(FL_WHEN_CHANGED | FL_WHEN_ENTER_KEY);
		value->callback(optionCallback, this);
		cy += 28;
	};
	addValueControl(floorMode_, floorValue_, "Floor:");
	addValueControl(ceilingMode_, ceilingValue_, "Ceiling:");
	addValueControl(lightMode_, lightValue_, "Light:");
	auto addTextureControl = [&](Fl_Input *&field, Fl_Button *&browse,
								  Fl_Toggle_Button *&automatic,
								  UI_Pic *&preview, const char *label,
								  const char *browseLabel,
								  const char *autoLabel,
								  const char *previewLabel)
	{
		const int previewSize = 56;
		const int browseWidth = 136;
		const int buttonGap = 6;
		field = new Fl_Input(labelX, cy, inputW - previewSize - 8,
							 26, label);
		field->align(FL_ALIGN_LEFT);
		field->when(FL_WHEN_CHANGED | FL_WHEN_ENTER_KEY);
		field->callback(optionCallback, this);
		preview = new UI_Pic(
				inst_, labelX + inputW - previewSize, cy,
				previewSize, previewSize, previewLabel);
		preview->callback(propertyTextureCallback, this);
		browse = new Fl_Button(
				labelX, cy + 30, browseWidth, 26, browseLabel);
		browse->labelsize(11);
		browse->callback(propertyTextureCallback, this);
		automatic = new Fl_Toggle_Button(
				labelX + browseWidth + buttonGap, cy + 30,
				std::max(48, inputW - browseWidth - buttonGap),
				26, autoLabel);
		automatic->labelsize(11);
		automatic->selection_color(fl_rgb_color(64, 152, 88));
		automatic->callback(propertyTextureAutoCallback, this);
		cy += 66;
	};
	addTextureControl(
			floorTexture_, floorTextureButton_, floorTextureAuto_,
			floorTexturePreview_, "Floor flat:", "Choose Floor...",
			"Floor Auto", "Floor flat preview");
	addTextureControl(
			ceilingTexture_, ceilingTextureButton_, ceilingTextureAuto_,
			ceilingTexturePreview_, "Ceil flat:", "Choose Ceiling...",
			"Ceil Auto", "Ceiling flat preview");
	addTextureControl(
			wallTexture_, wallTextureButton_, wallTextureAuto_,
			wallTexturePreview_, "Wall tex:", "Choose Wall...",
			"Wall Auto", "Wall texture preview");
	sectorType_ = new Fl_Input(labelX, cy, 68, 26, "Special:");
	sectorType_->align(FL_ALIGN_LEFT);
	sectorType_->when(FL_WHEN_CHANGED | FL_WHEN_ENTER_KEY);
	sectorType_->callback(optionCallback, this);
	sectorTypeButton_ = new Fl_Button(
			labelX + 74, cy, inputW - 74, 26, "Choose Special...");
	sectorTypeButton_->labelsize(11);
	sectorTypeButton_->callback(sectorSpecialCallback, this);
	cy += 30;
	sectorTypeDescription_ =
			new Fl_Output(labelX, cy, inputW, 24, "Meaning:");
	sectorTypeDescription_->textsize(11);
	sectorTypeDescription_->tooltip(
			"The configured meaning of the selected sector special.");
	cy += 28;
	addInput(sectorTag_, "Tag:");
	floorTexture_->tooltip(
			"Empty means Auto. Choose from every loaded flat.");
	ceilingTexture_->tooltip(
			"Empty means Auto. Choose from every loaded flat.");
	wallTexture_->tooltip(
			"Empty means Auto. Choose from every loaded wall texture.");
	floorTextureAuto_->tooltip(
			"Inherit the contextual floor flat for this property model.");
	ceilingTextureAuto_->tooltip(
			"Inherit the contextual ceiling flat for this property model.");
	wallTextureAuto_->tooltip(
			"Inherit contextual wall textures for this property model.");
	sectorType_->tooltip(
			"Type a sector special number manually, or use Choose Special "
			"to search the active game configuration. Empty preserves or "
			"inherits the contextual special.");
	sectorTypeButton_->tooltip(
			"Open the searchable list of sector specials. Manual numeric "
			"input remains available for port-specific or custom values.");
	sectorTag_->tooltip("Empty preserves or inherits the sector tag.");
	cy += 6;

	Fl_Box *connectionsTitle = new Fl_Box(cx, cy, W - 24, 22,
										 "Connections");
	connectionsTitle->labelfont(FL_HELVETICA_BOLD);
	connectionsTitle->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
	cy += 26;

	startConnection_ = new Fl_Choice(labelX, cy, inputW, 24, "Start:");
	startConnection_->add("Open portal|Smart Door|Solid wall");
	startConnection_->callback(connectionCallback, this);
	cy += 28;
	endConnection_ = new Fl_Choice(labelX, cy, inputW, 24, "End:");
	endConnection_->add("Open portal|Smart Door|Solid wall");
	endConnection_->callback(connectionCallback, this);
	cy += 28;
	doorPreset_ = new Fl_Choice(
			labelX, cy, inputW, 24, "Door behavior:");
	doorPreset_->callback(optionCallback, this);
	cy += 28;
	addInput(doorDepth_, "Door depth:");
	addInput(doorWidth_, "Door width:");
	doorWidth_->tooltip(
			"Zero means Auto: the widest opening with safe track walls.");
	addInput(doorOffset_, "Door offset:");
	doorOffset_->tooltip("Moves the opening along its source segment.");
	doorPlacement_ = new Fl_Choice(
			labelX, cy, inputW, 24, "Door lines:");
	doorPlacement_->add("Auto longest|Selected segments");
	doorPlacement_->callback(connectionCallback, this);
	cy += 28;
	doorSegments_ = new Fl_Multi_Browser(
			labelX, cy, inputW, 72, "Segments:");
	doorSegments_->tooltip(
			"Choose one or more extrusion-chain segments for independent doors.");
	doorSegments_->textsize(11);
	doorSegments_->callback(doorSegmentCallback, this);
	cy += 82;
	addInput(faceTexture_, "Door face:");
	addInput(trackTexture_, "Track wall:");
	faceTexture_->tooltip(
			"Moving door slab texture. Empty means Auto.");
	trackTexture_->tooltip(
			"Narrow doorway side-wall texture. Empty means Auto.");
	faceTextureButton_ = new Fl_Button(
			labelX, cy, 122, 24, "Choose Face...");
	faceTextureButton_->callback(doorTextureCallback, this);
	faceAutoButton_ = new Fl_Toggle_Button(
			labelX + 130, cy, 72, 24, "Face Auto");
	faceAutoButton_->selection_color(fl_rgb_color(64, 152, 88));
	faceAutoButton_->tooltip(
			"When lit, infer the moving door face from its doorway.");
	faceAutoButton_->callback(doorTextureAutoCallback, this);
	cy += 30;
	trackTextureButton_ = new Fl_Button(
			labelX, cy, 122, 24, "Choose Track...");
	trackTextureButton_->callback(doorTextureCallback, this);
	trackAutoButton_ = new Fl_Toggle_Button(
			labelX + 130, cy, 72, 24, "Track Auto");
	trackAutoButton_->selection_color(fl_rgb_color(64, 152, 88));
	trackAutoButton_->tooltip(
			"When lit, infer the narrow doorway track walls.");
	trackAutoButton_->callback(doorTextureAutoCallback, this);
	cy += 30;
	facePreview_ = new UI_Pic(inst_, labelX, cy, 58, 58, "Face preview");
	trackPreview_ = new UI_Pic(inst_, labelX + 74, cy, 58, 58,
							  "Track preview");
	facePreview_->callback(doorTextureCallback, this);
	trackPreview_->callback(doorTextureCallback, this);
	facePreview_->tooltip("Door face preview. Click to choose a texture.");
	trackPreview_->tooltip("Track wall preview. Click to choose a texture.");
	cy += 66;
	liftGuide_ = new Fl_Box(
			labelX, cy, inputW, 94,
			"HOW LIFT WORKS\n"
			"Existing: click the platform; its lowest neighbor is the stop.\n"
			"New: drag outward from a wall to make an alcove.\n"
			"Choose behavior/trigger portals; Enter assigns a fresh tag "
			"and actions in one Undo.");
	liftGuide_->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_WRAP |
					  FL_ALIGN_INSIDE);
	liftGuide_->labelsize(11);
	liftGuide_->box(FL_THIN_DOWN_BOX);
	cy += 100;
	liftPreset_ = new Fl_Choice(
			labelX, cy, inputW, 24, "Lift behavior:");
	liftPreset_->tooltip(
			"Semantic movement and activation behavior from the active "
			"game configuration.");
	liftPreset_->callback(optionCallback, this);
	cy += 30;
	liftTriggers_ = new Fl_Multi_Browser(
			labelX, cy, inputW, 82, "Trigger portals:");
	liftTriggers_->tooltip(
			"Selected usable platform boundaries receive the local lift "
			"action. All are selected by default.");
	liftTriggers_->textsize(11);
	liftTriggers_->callback(liftTriggerCallback, this);
	cy += 92;
	liftStatus_ = new Fl_Output(labelX, cy, inputW, 24, "Lift plan:");
	liftStatus_->textsize(11);
	liftStatus_->tooltip(
			"Resolved platform groups, fresh tags, travel, and trigger count.");
	cy += 30;

	Fl_Box *reviewTitle = new Fl_Box(cx, cy, W - 24, 22, "Review");
	reviewTitle->labelfont(FL_HELVETICA_BOLD);
	reviewTitle->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
	cy += 24;
	summary_ = new Fl_Box(cx, cy, W - 24, 42);
	summary_->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_WRAP |
					FL_ALIGN_INSIDE);
	summary_->labelsize(11);
	cy += 44;
	copyReviewButton_ = new Fl_Button(cx, cy, 112, 24, "Copy Review");
	copyReviewButton_->tooltip(
			"Copy the complete, untruncated review to the clipboard.");
	copyReviewButton_->callback(copyReviewCallback, this);
	expandReviewButton_ = new Fl_Button(
			cx + 120, cy, W - 144, 24, "Expand Review...");
	expandReviewButton_->tooltip(
			"Open a resizable, wrapped view whose text can be selected "
			"and copied.");
	expandReviewButton_->callback(expandReviewCallback, this);
	cy += 30;
	issues_ = new Fl_Hold_Browser(cx, cy, W - 24, 145);
	issues_->textsize(11);
	issues_->tooltip(
			"Select an issue to locate it on the map. Use Copy Review or "
			"Expand Review for the complete message.");
	issues_->callback(issueCallback, this);
	cy += 154;

	new Fl_Box(cx, cy, W - 24, 1);
	scroll_->end();

	commitButton_ = new Fl_Button(X + W - 128, buttonY + 6, 116, 30,
								 "Make Sectors");
	commitButton_->labelfont(FL_HELVETICA_BOLD);
	commitButton_->callback(commitCallback, this);

	end();
	resizable(scroll_);
	hide();
}

UI_SectorDesigner::~UI_SectorDesigner()
{
	if (Active())
		Close();
	if (reviewDisplay_)
		reviewDisplay_->buffer(nullptr);
	delete reviewWindow_;
	delete reviewBuffer_;
}

void UI_SectorDesigner::resize(int X, int Y, int W, int H)
{
	const int oldX = x();
	const int oldY = y();
	const int oldW = w();
	const int deltaX = X - oldX;
	const int deltaY = Y - oldY;
	const int deltaW = W - oldW;

	struct ChildGeometry
	{
		Fl_Widget *widget;
		int x;
		int y;
		int w;
		int h;
	};
	std::vector<ChildGeometry> content;
	if (scroll_)
		for (int index = 0; index < scroll_->children(); ++index)
		{
			Fl_Widget *widget = scroll_->child(index);
			if (dynamic_cast<Fl_Scrollbar *>(widget))
				continue;
			content.push_back({
				widget, widget->x(), widget->y(),
				widget->w(), widget->h()
			});
		}

	Fl_Group::resize(X, Y, W, H);
	if (!scroll_ || oldW <= 0 || deltaW == 0)
		return;

	modeChoice_->resize(X + 76, Y + 44,
					   std::max(80, W - 88), modeChoice_->h());
	instructions_->resize(X + 10, Y + 75,
						  std::max(80, W - 20), instructions_->h());
	scroll_->resize(X + 4, Y + 120,
					std::max(80, W - 8),
					std::max(80, H - 170));
	commitButton_->resize(
			X + W - 128, Y + H - 38,
			116, commitButton_->h());

	const int oldRight = oldX + oldW;
	for (const ChildGeometry &geometry : content)
	{
		int childX = geometry.x + deltaX;
		int childW = geometry.w;
		const bool propertyPreview =
				geometry.widget == floorTexturePreview_ ||
				geometry.widget == ceilingTexturePreview_ ||
				geometry.widget == wallTexturePreview_;
		const bool propertyInput =
				geometry.widget == floorTexture_ ||
				geometry.widget == ceilingTexture_ ||
				geometry.widget == wallTexture_;
		const bool propertyAuto =
				geometry.widget == floorTextureAuto_ ||
				geometry.widget == ceilingTextureAuto_ ||
				geometry.widget == wallTextureAuto_;
		const bool specialWide =
				geometry.widget == sectorTypeButton_ ||
				geometry.widget == sectorTypeDescription_;
		const int rightGap =
				oldRight - (geometry.x + geometry.w);
		if (propertyPreview)
			childX += deltaW;
		else if (propertyInput || propertyAuto || specialWide ||
				 rightGap <= 30)
			childW = std::max(24, geometry.w + deltaW);
		geometry.widget->resize(
				childX, geometry.y + deltaY, childW, geometry.h);
	}
	scroll_->init_sizes();
}

bool UI_SectorDesigner::Active() const
{
	return visible() && inst_.edit.action == EditorAction::designSector;
}

void UI_SectorDesigner::LoadMemory(SectorDesignMode mode)
{
	auto found = designerMemory.find(MemoryKey(inst_));
	if (found != designerMemory.end())
		request_ = found->second;
	else
	{
		request_ = {};
		request_.width = std::max(64, 4 * inst_.conf.miscInfo.player_r);
		request_.depth = config::new_sector_size;
		// A full coarse grid step (commonly 64) collapses ordinary rooms on
		// the first Inset click. Keep the initial ring useful while still
		// respecting fine 8-unit grids; subsequent edits are remembered.
		request_.offset =
				std::max(8, std::min(16, inst_.grid.getStep()));
		request_.polygonSides = 8;
		request_.polygonProfile = SectorPolygonProfile::customRegular;
		request_.polygonInnerRatio = 0.5;
		request_.architectureStyle = SectorArchitectureStyle::gothic;
		request_.architectureElement = SectorArchitectureElement::pillar;
		request_.architectureBays = 4;
		request_.architectureSize = 24;
		request_.architectureHeight = 16;
		request_.architectureMargin = 16;
		request_.stairRise = 8;
		request_.stairTread = std::max(8, inst_.grid.getStep());
		request_.doorDepth = std::max(8, inst_.grid.getStep());
		request_.preserveHeadroom = true;
	}
	LoadArchitectureMemory(request_.architectureElement);
	request_.mode = mode;
	request_.anchors.clear();
	request_.anchorLines.clear();
	request_.targetSectors.clear();
	request_.liftTriggerLines.clear();
	request_.restrictLiftTriggers = false;
	request_.doorLines.clear();
	request_.extrudeOpposite = false;
	request_.extrudeReferenceLine = -1;
	propertyTargetIndex_ = 0;
}

void UI_SectorDesigner::SaveMemory()
{
	SaveArchitectureMemory();
	SectorDesignRequest remembered = request_;
	remembered.anchors.clear();
	remembered.anchorLines.clear();
	remembered.targetSectors.clear();
	remembered.liftTriggerLines.clear();
	remembered.restrictLiftTriggers = false;
	remembered.doorLines.clear();
	remembered.extrudeOpposite = false;
	remembered.extrudeReferenceLine = -1;
	remembered.startSector = -1;
	remembered.endSector = -1;
	designerMemory[MemoryKey(inst_)] = std::move(remembered);
}

void UI_SectorDesigner::Open(SectorDesignMode mode)
{
	LoadMemory(mode);
	doorPresets_ = M_AvailableDoorPresets(inst_.conf,
										  inst_.loaded.levelFormat);
	liftPresets_ = M_AvailableSectorActionPresets(inst_.conf,
			SectorActionKind::lift, inst_.loaded.levelFormat);

	doorPreset_->clear();
	for (const DoorPreset &preset : doorPresets_)
		doorPreset_->add(SafeMenuLabel(
				M_DoorPresetLabel(inst_.conf, preset)).c_str());
	liftPreset_->clear();
	for (const SectorActionPreset &preset : liftPresets_)
		liftPreset_->add(SafeMenuLabel(
				M_SectorActionPresetLabel(inst_.conf, preset)).c_str());

	if (request_.doorOptions.presetId.empty() && !doorPresets_.empty())
	{
		auto repeat = std::find_if(doorPresets_.begin(), doorPresets_.end(),
				[](const DoorPreset &preset)
				{
					return preset.activation == ActivationPolicy::useRepeat;
				});
		request_.doorOptions.presetId =
				(repeat == doorPresets_.end() ?
				 doorPresets_.front() : *repeat).id;
	}
	if (request_.actionPresetId.empty() && !liftPresets_.empty())
	{
		auto repeat = std::find_if(liftPresets_.begin(), liftPresets_.end(),
				[](const SectorActionPreset &preset)
				{
					return preset.activation == ActivationPolicy::useRepeat;
				});
		request_.actionPresetId =
				(repeat == liftPresets_.end() ?
				 liftPresets_.front() : *repeat).id;
	}

	RefreshTargets();
	ClearGesture();
	WriteControls();
	show();
	inst_.Editor_SetAction(EditorAction::designSector);
	if (inst_.main_win && inst_.main_win->canvas)
		inst_.main_win->canvas->take_focus();
	RefreshModeUI();
	Recompute();
}

void UI_SectorDesigner::Close()
{
	if (!visible() && inst_.edit.action != EditorAction::designSector)
		return;
	SaveMemory();
	fixedAnchors_.clear();
	endpointLines_.clear();
	request_.doorLines.clear();
	pointerActive_ = false;
	pointerModifiers_ = 0;
	anchorPressArmed_ = false;
	anchorDragMoved_ = false;
	extrudePressArmed_ = false;
	extrudeDragMoved_ = false;
	extrudePressSector_ = -1;
	extrudeSourceSector_ = -1;
	request_.architectureHostSector = -1;
	request_.extrudeOpposite = false;
	request_.extrudeReferenceLine = -1;
	inst_.edit.designAssistPreview.reset();
	if (reviewWindow_)
		reviewWindow_->hide();
	if (inst_.edit.action == EditorAction::designSector)
		inst_.Editor_ClearAction();
	hide();
	inst_.RedrawMap();
}

void UI_SectorDesigner::RefreshTargets()
{
	request_.targetSectors.clear();
	if (inst_.edit.Selected &&
		inst_.edit.Selected->what_type() == ObjType::sectors)
		request_.targetSectors = inst_.edit.Selected->asArray();
	if (request_.targetSectors.empty() &&
		inst_.edit.highlight.type == ObjType::sectors &&
		inst_.level.isSector(inst_.edit.highlight.num))
		request_.targetSectors.push_back(inst_.edit.highlight.num);
}

void UI_SectorDesigner::ClearGesture()
{
	fixedAnchors_.clear();
	endpointLines_.clear();
	request_.doorLines.clear();
	pointerActive_ = false;
	pointerModifiers_ = 0;
	anchorPressArmed_ = false;
	anchorDragMoved_ = false;
	extrudePressArmed_ = false;
	extrudeDragMoved_ = false;
	extrudePressSector_ = -1;
	extrudeSourceSector_ = -1;
	request_.architectureHostSector = -1;
	request_.extrudeOpposite = false;
	request_.extrudeReferenceLine = -1;
	if (extrudeOppositeCheck_)
		extrudeOppositeCheck_->value(0);
	request_.routeIndex = 0;
	rankedRoutes_.clear();
	routeCyclePosition_ = 0;
	availableDoorLines_.clear();
}

void UI_SectorDesigner::RebuildArchitectureStructureMenu(int family)
{
	family = std::clamp(family, 0, ARCHITECTURE_FAMILY_COUNT - 1);
	architectureFamilyElements_ = ArchitectureFamilyElements(family);
	architectureElement_->clear();
	for (SectorArchitectureElement element : architectureFamilyElements_)
		architectureElement_->add(
				SafeMenuLabel(
						M_ArchitectureDescriptor(element).label).c_str());
}

void UI_SectorDesigner::CaptureArchitectureControls()
{
	request_.architectureStyle = static_cast<SectorArchitectureStyle>(
			std::clamp(
					architectureStyle_->value(), 0,
					static_cast<int>(
							SectorArchitectureStyle::infernal)));
	request_.architectureBays = ReadInt(
			architectureBays_, request_.architectureBays);
	request_.architectureSize = ReadDouble(
			architectureSize_, request_.architectureSize);
	request_.architectureHeight = ReadDouble(
			architectureHeight_, request_.architectureHeight);
	request_.architectureMargin = ReadDouble(
			architectureMargin_, request_.architectureMargin);
	request_.architectureMirrored =
			architectureMirror_->value() != 0;
	const int function = architectureFunction_->value();
	request_.architectureFunction =
			function >= static_cast<int>(
					SectorArchitectureFunction::staticGeometry) &&
			function <= static_cast<int>(
					SectorArchitectureFunction::smartLift) ?
				static_cast<SectorArchitectureFunction>(function) :
				SectorArchitectureFunction::staticGeometry;
}

void UI_SectorDesigner::WriteArchitectureControls()
{
	architectureStyle_->value(
			static_cast<int>(request_.architectureStyle));
	const int family =
			ArchitectureFamilyFor(request_.architectureElement);
	architectureFamily_->value(family);
	const bool correctFamily =
			!architectureFamilyElements_.empty() &&
			ArchitectureFamilyFor(
					architectureFamilyElements_.front()) == family;
	if (!correctFamily)
		RebuildArchitectureStructureMenu(family);
	const auto selected = std::find(
			architectureFamilyElements_.begin(),
			architectureFamilyElements_.end(),
			request_.architectureElement);
	architectureElement_->value(
			selected == architectureFamilyElements_.end() ?
				0 :
				static_cast<int>(std::distance(
						architectureFamilyElements_.begin(), selected)));
	SetInt(architectureBays_, request_.architectureBays);
	SetDouble(architectureSize_, request_.architectureSize);
	SetDouble(architectureHeight_, request_.architectureHeight);
	SetDouble(architectureMargin_, request_.architectureMargin);
	architectureMirror_->value(request_.architectureMirrored);
	architectureFunction_->value(
			static_cast<int>(request_.architectureFunction));
}

void UI_SectorDesigner::SyncArchitectureSelectionFromControls()
{
	const int selectedFamily = std::clamp(
			architectureFamily_->value(), 0,
			ARCHITECTURE_FAMILY_COUNT - 1);
	const int displayedFamily =
			architectureFamilyElements_.empty() ?
				-1 :
				ArchitectureFamilyFor(
						architectureFamilyElements_.front());

	if (selectedFamily != displayedFamily)
	{
		// Preserve the complete previous structure state before replacing the
		// menu. ReadControls also reaches this path if a platform changes a
		// choice value without delivering its FLTK callback.
		CaptureArchitectureControls();
		SaveArchitectureMemory();
		RebuildArchitectureStructureMenu(selectedFamily);
		architectureElement_->value(0);
		LoadArchitectureMemory(
				architectureFamilyElements_.front());
		WriteArchitectureControls();
		return;
	}

	const int index = architectureElement_->value();
	if (index < 0 ||
		index >= static_cast<int>(
				architectureFamilyElements_.size()))
	{
		architectureElement_->value(0);
	}
	const int safeIndex = std::clamp(
			architectureElement_->value(), 0,
			static_cast<int>(
					architectureFamilyElements_.size()) - 1);
	const SectorArchitectureElement selected =
			architectureFamilyElements_[safeIndex];
	if (selected != request_.architectureElement)
	{
		CaptureArchitectureControls();
		SaveArchitectureMemory();
		LoadArchitectureMemory(selected);
		WriteArchitectureControls();
		return;
	}

	CaptureArchitectureControls();
}

void UI_SectorDesigner::SaveArchitectureMemory()
{
	ArchitectureMemory remembered;
	remembered.style = request_.architectureStyle;
	remembered.bays = request_.architectureBays;
	remembered.size = request_.architectureSize;
	remembered.height = request_.architectureHeight;
	remembered.margin = request_.architectureMargin;
	remembered.mirrored = request_.architectureMirrored;
	remembered.function = request_.architectureFunction;
	architectureMemory[
			ArchitectureMemoryKey(
					inst_, request_.architectureElement)] = remembered;
}

void UI_SectorDesigner::LoadArchitectureMemory(
		SectorArchitectureElement element)
{
	const SectorArchitectureDescriptor &descriptor =
			M_ArchitectureDescriptor(element);
	auto found = architectureMemory.find(
			ArchitectureMemoryKey(inst_, element));
	const ArchitectureMemory remembered =
			found == architectureMemory.end() ?
				DefaultArchitectureMemory(inst_, element) :
				found->second;
	request_.architectureElement = element;
	request_.architectureStyle = remembered.style;
	request_.architectureBays = std::clamp(
			remembered.bays,
			descriptor.minimumBays, descriptor.maximumBays);
	request_.architectureSize = std::max(
			remembered.size,
			M_MinimumArchitectureSize(
					remembered.style, element));
	request_.architectureHeight = remembered.height;
	request_.architectureMargin = std::max(0.0, remembered.margin);
	request_.architectureMirrored =
			M_ArchitectureHasControl(
					element, SectorArchitectureControl::mirror) &&
			remembered.mirrored;
	request_.architectureFunction =
			M_ArchitectureSupportsFunction(
					element, remembered.function) ?
				remembered.function :
				SectorArchitectureFunction::staticGeometry;
}

void UI_SectorDesigner::ReadControls()
{
	if (updating_)
		return;
	request_.width = ReadDouble(widthInput_, request_.width);
	request_.depth = ReadDouble(depthInput_, request_.depth);
	request_.extrudeUseDragDepth =
			extrudeUseDragCheck_->value() != 0;
	request_.extrudeOpposite = extrudeOppositeCheck_->value() != 0;
	request_.offset = ReadDouble(offsetInput_, request_.offset);
	request_.polygonSides = ReadInt(sidesInput_, request_.polygonSides);
	request_.polygonProfile = static_cast<SectorPolygonProfile>(
			polygonProfile_->value());
	request_.rotation = ReadDouble(
			rotationInput_, request_.rotation *
					180.0 / std::numbers::pi) *
			std::numbers::pi / 180.0;
	request_.polygonInnerRatio = ReadDouble(
			polygonInnerInput_, request_.polygonInnerRatio * 100.0) /
			100.0;
	SyncArchitectureSelectionFromControls();
	request_.join = static_cast<SectorDesignJoin>(joinChoice_->value());
	request_.stairCount = ReadInt(stepCountInput_, request_.stairCount);
	request_.stairRise = ReadDouble(stepRiseInput_, request_.stairRise);
	request_.stairTread = ReadDouble(stepTreadInput_, request_.stairTread);
	request_.stairFitTarget = fitTargetCheck_->value() != 0;
	request_.stairTargetFloor =
			ReadInt(targetFloorInput_, request_.stairTargetFloor);
	request_.preserveHeadroom = headroomCheck_->value() != 0;
	request_.replaceAffectedSectors = replaceCheck_->value() != 0;

	SectorPropertyOptions *properties = &request_.properties;
	if (request_.mode == SectorDesignMode::inset)
	{
		std::optional<SectorPropertyOptions> &target =
				propertyTargetIndex_ == 0 ?
					request_.ringProperties : request_.innerProperties;
		if (!target)
			target = request_.properties;
		properties = &*target;
	}
	properties->floorMode =
			static_cast<SectorValueMode>(floorMode_->value());
	properties->floorValue =
			ReadInt(floorValue_, properties->floorValue);
	properties->ceilingMode =
			static_cast<SectorValueMode>(ceilingMode_->value());
	properties->ceilingValue =
			ReadInt(ceilingValue_, properties->ceilingValue);
	properties->lightMode =
			static_cast<SectorValueMode>(lightMode_->value());
	properties->lightValue =
			ReadInt(lightValue_, properties->lightValue);
	properties->floorTexture = floorTexture_->value();
	properties->ceilingTexture = ceilingTexture_->value();
	properties->wallTexture = wallTexture_->value();
	properties->sectorType = ReadOptionalInt(sectorType_);
	properties->sectorTag = ReadOptionalInt(sectorTag_);

	request_.startConnection =
			static_cast<SectorConnection>(startConnection_->value());
	request_.endConnection =
			static_cast<SectorConnection>(endConnection_->value());
	request_.doorDepth = std::max(
			8.0, std::abs(ReadDouble(doorDepth_, request_.doorDepth)));
	request_.doorWidth =
			ReadDouble(doorWidth_, request_.doorWidth);
	request_.doorOffset =
			ReadDouble(doorOffset_, request_.doorOffset);
	request_.autoDoorLines = doorPlacement_->value() == 0;
	if (!request_.autoDoorLines)
	{
		request_.doorLines.clear();
		for (int index = 1; index <= doorSegments_->size(); ++index)
			if (doorSegments_->selected(index) &&
				index <= static_cast<int>(availableDoorLines_.size()))
				request_.doorLines.push_back(
						availableDoorLines_[index - 1]);
	}
	request_.doorOptions.faceTexture = faceTexture_->value();
	request_.doorOptions.trackTexture = trackTexture_->value();
	if (!doorPresets_.empty() && doorPreset_->value() >= 0)
		request_.doorOptions.presetId =
				doorPresets_[doorPreset_->value()].id;
	if (!liftPresets_.empty() && liftPreset_->value() >= 0)
		request_.actionPresetId = liftPresets_[liftPreset_->value()].id;
}

void UI_SectorDesigner::ChangeMode(SectorDesignMode mode)
{
	if (mode == request_.mode)
	{
		ReadControls();
		RefreshModeUI();
		Recompute();
		return;
	}

	// Persist every currently visible field against the old mode first. In
	// particular, Inset's Ring/Inner property editor must not reinterpret its
	// fields as the newly chosen mode while the choice callback is running.
	ReadControls();
	request_.mode = mode;
	propertyTargetIndex_ = 0;
	request_.startSector = -1;
	request_.endSector = -1;
	request_.liftTriggerLines.clear();
	request_.restrictLiftTriggers = false;
	ClearGesture();
	RefreshTargets();
	if (reviewWindow_)
		reviewWindow_->hide();
	RefreshModeUI();
	Recompute();
	SString guidance;
	if (WaitingForGesture())
		guidance = WaitingPrompt();
	else if (plan_.valid())
		guidance = "Preview ready; press Enter to apply.";
	else
	{
		auto error = std::find_if(plan_.issues.begin(), plan_.issues.end(),
				[](const SectorDesignIssue &issue)
				{
					return issue.severity ==
							SectorDesignIssueSeverity::error;
				});
		guidance = error == plan_.issues.end() ?
				"Review the highlighted issue." :
				SString::printf("Blocked: %s", error->message.c_str());
	}
	inst_.Status_Set("%s mode. %s", MODE_LABELS[static_cast<int>(mode)],
			guidance.c_str());
	if (inst_.main_win && inst_.main_win->canvas)
		inst_.main_win->canvas->take_focus();
}

void UI_SectorDesigner::WriteControls()
{
	updating_ = true;
	modeChoice_->value(static_cast<int>(request_.mode));
	SetDouble(widthInput_, request_.width);
	SetDouble(depthInput_, request_.depth);
	extrudeUseDragCheck_->value(request_.extrudeUseDragDepth);
	extrudeOppositeCheck_->value(request_.extrudeOpposite);
	SetDouble(offsetInput_, request_.offset);
	SetInt(sidesInput_, request_.polygonSides);
	polygonProfile_->value(static_cast<int>(request_.polygonProfile));
	SetDouble(rotationInput_,
			request_.rotation * 180.0 / std::numbers::pi);
	SetDouble(polygonInnerInput_, request_.polygonInnerRatio * 100.0);
	WriteArchitectureControls();
	joinChoice_->value(static_cast<int>(request_.join));
	SetInt(stepCountInput_, request_.stairCount);
	SetDouble(stepRiseInput_, request_.stairRise);
	SetDouble(stepTreadInput_, request_.stairTread);
	fitTargetCheck_->value(request_.stairFitTarget);
	SetInt(targetFloorInput_, request_.stairTargetFloor);
	headroomCheck_->value(request_.preserveHeadroom);
	replaceCheck_->value(request_.replaceAffectedSectors);

	propertyTarget_->value(propertyTargetIndex_);
	const SectorPropertyOptions *properties = &request_.properties;
	if (request_.mode == SectorDesignMode::inset)
	{
		const std::optional<SectorPropertyOptions> &target =
				propertyTargetIndex_ == 0 ?
					request_.ringProperties : request_.innerProperties;
		if (target)
			properties = &*target;
	}
	floorMode_->value(static_cast<int>(properties->floorMode));
	SetInt(floorValue_, properties->floorValue);
	ceilingMode_->value(static_cast<int>(properties->ceilingMode));
	SetInt(ceilingValue_, properties->ceilingValue);
	lightMode_->value(static_cast<int>(properties->lightMode));
	SetInt(lightValue_, properties->lightValue);
	floorTexture_->value(properties->floorTexture.c_str());
	ceilingTexture_->value(properties->ceilingTexture.c_str());
	wallTexture_->value(properties->wallTexture.c_str());
	SetOptionalInt(sectorType_, properties->sectorType);
	SetOptionalInt(sectorTag_, properties->sectorTag);
	startConnection_->value(static_cast<int>(request_.startConnection));
	endConnection_->value(static_cast<int>(request_.endConnection));
	SetDouble(doorDepth_, request_.doorDepth);
	SetDouble(doorWidth_, request_.doorWidth);
	SetDouble(doorOffset_, request_.doorOffset);
	doorPlacement_->value(request_.autoDoorLines ? 0 : 1);
	faceTexture_->value(request_.doorOptions.faceTexture.c_str());
	trackTexture_->value(request_.doorOptions.trackTexture.c_str());

	int doorIndex = 0;
	for (int index = 0; index < static_cast<int>(doorPresets_.size()); index++)
		if (doorPresets_[index].id.noCaseEqual(
				request_.doorOptions.presetId))
			doorIndex = index;
	doorPreset_->value(doorPresets_.empty() ? -1 : doorIndex);
	int liftIndex = 0;
	for (int index = 0; index < static_cast<int>(liftPresets_.size()); index++)
		if (liftPresets_[index].id.noCaseEqual(request_.actionPresetId))
			liftIndex = index;
	liftPreset_->value(liftPresets_.empty() ? -1 : liftIndex);
	updating_ = false;
	UpdateSectorSpecialDescription();
}

SectorDesignRequest UI_SectorDesigner::CurrentRequest() const
{
	SectorDesignRequest current = request_;
	if (inst_.grid.snaps() &&
			inst_.grid.getMathSettings().pattern != grid::Pattern::polar)
	{
		const grid::Basis basis = inst_.grid.getBasis();
		if (basis.valid())
		{
			current.useConstructionBasis = true;
			current.constructionPrimary = basis.primary;
			current.constructionSecondary = basis.secondary;
		}
	}
	if (current.mode == SectorDesignMode::lift)
		current.extrudeUseDragDepth = true;
	current.anchors = fixedAnchors_;
	current.anchorLines = endpointLines_;
	if (request_.mode == SectorDesignMode::extrude &&
		inst_.level.isSector(extrudeSourceSector_))
		current.properties.modelSector = extrudeSourceSector_;

	v2double_t pointer = pointer_;
	if ((pointerModifiers_ & EMOD_SHIFT) && !fixedAnchors_.empty())
		pointer = Constrain45(fixedAnchors_.back(), pointer);

	if (pointerActive_)
	{
		bool append = false;
		switch (request_.mode)
		{
			case SectorDesignMode::room:
			case SectorDesignMode::polygon:
			case SectorDesignMode::architecture:
				append = fixedAnchors_.size() == 1;
				break;
			case SectorDesignMode::freeform:
			case SectorDesignMode::corridor:
			case SectorDesignMode::stairs:
				append = !fixedAnchors_.empty();
				break;
			case SectorDesignMode::extrude:
			case SectorDesignMode::lift:
				append = !endpointLines_.empty() && fixedAnchors_.empty();
				break;
			default:
				break;
		}
		if (append)
			current.anchors.push_back(pointer);
	}

	if (request_.mode == SectorDesignMode::room &&
		current.anchors.size() >= 2)
	{
		const v2double_t origin = current.anchors[0];
		v2double_t corner = current.anchors[1];
		v2double_t delta = corner - origin;
		if (pointerModifiers_ & EMOD_SHIFT)
		{
			const double size = std::max(std::abs(delta.x),
										std::abs(delta.y));
			delta.x = delta.x < 0 ? -size : size;
			delta.y = delta.y < 0 ? -size : size;
			corner = origin + delta;
		}
		if (pointerModifiers_ & EMOD_COMMAND)
		{
			current.anchors[0] = origin - delta;
			current.anchors[1] = origin + delta;
		}
		else
			current.anchors[1] = corner;
	}

	return current;
}

void UI_SectorDesigner::RefreshModeUI()
{
	const int mode = static_cast<int>(request_.mode);
	const SectorArchitectureDescriptor &architecture =
			M_ArchitectureDescriptor(request_.architectureElement);
	const bool architectureMode =
			request_.mode == SectorDesignMode::architecture;
	const bool architectureMirror =
			M_ArchitectureHasControl(
					request_.architectureElement,
					SectorArchitectureControl::mirror);
	bool architectureSmartLift =
			architectureMode &&
			request_.architectureFunction ==
					SectorArchitectureFunction::smartLift;
	architectureDescription_->copy_label(architecture.description);
	architectureBays_->copy_label(architecture.baysLabel);
	architectureSize_->copy_label(architecture.sizeLabel);
	architectureHeight_->copy_label(architecture.heightLabel);
	if (request_.mode == SectorDesignMode::extrude)
		instructions_->copy_label(request_.extrudeUseDragDepth ?
				"Drag a wall, or drag inside a sector through the wall to "
				"extrude. No selection needed; Shift-click adds a chain." :
				"Click a boundary; signed Depth sets the base direction. "
				"F deliberately flips to the other side.");
	else if (request_.mode == SectorDesignMode::lift)
		instructions_->copy_label(
				"Existing lift: click its platform sector; Shift-click "
				"adds independent platforms. New lift: drag outward from "
				"a wall. Review travel and yellow trigger portals.");
	else if (request_.mode == SectorDesignMode::architecture)
		instructions_->copy_label(architectureMirror ?
				"Drag along the structure's forward axis inside a room or "
				"in clear void. Anchor order controls rise/direction; F "
				"mirrors this layout. Margin 0 connects eligible touching "
				"edges; red geometry explains conflicts." :
				"Drag along the structure's forward axis inside a room or "
				"in clear void. Anchor order controls rise/direction. "
				"Margin 0 connects eligible touching edges; red geometry "
				"explains conflicts.");
	else
		instructions_->copy_label(MODE_HELP[mode]);
	startConnection_->copy_label(
			request_.mode == SectorDesignMode::extrude ?
				"Base seam:" : "Start:");

	auto activeWhen = [](Fl_Widget *widget, bool active)
	{
		if (active)
			widget->activate();
		else
			widget->deactivate();
	};
	activeWhen(widthInput_,
			request_.mode == SectorDesignMode::corridor ||
			request_.mode == SectorDesignMode::stairs);
	activeWhen(depthInput_,
			request_.mode == SectorDesignMode::extrude ||
			request_.mode == SectorDesignMode::lift);
	activeWhen(extrudeUseDragCheck_,
			request_.mode == SectorDesignMode::extrude);
	activeWhen(extrudeOppositeCheck_,
			request_.mode == SectorDesignMode::extrude);
	activeWhen(offsetInput_, request_.mode == SectorDesignMode::inset);
	activeWhen(sidesInput_,
			request_.mode == SectorDesignMode::polygon &&
			request_.polygonProfile ==
				SectorPolygonProfile::customRegular);
	activeWhen(polygonProfile_,
			request_.mode == SectorDesignMode::polygon);
	activeWhen(rotationInput_,
			request_.mode == SectorDesignMode::polygon ||
			(architectureMode &&
			 M_ArchitectureHasControl(
					request_.architectureElement,
					SectorArchitectureControl::style)));
	const bool concaveProfile =
			request_.polygonProfile >= SectorPolygonProfile::star5;
	activeWhen(polygonInnerInput_,
			request_.mode == SectorDesignMode::polygon &&
			concaveProfile);
	activeWhen(architectureStyle_,
			architectureMode &&
			M_ArchitectureHasControl(
					request_.architectureElement,
					SectorArchitectureControl::style));
	activeWhen(architectureFamily_,
			architectureMode);
	activeWhen(architectureElement_,
			architectureMode);
	activeWhen(architectureDescription_, architectureMode);
	activeWhen(architectureBays_,
			architectureMode &&
			M_ArchitectureHasControl(
					request_.architectureElement,
					SectorArchitectureControl::bays));
	activeWhen(architectureSize_,
			architectureMode &&
			M_ArchitectureHasControl(
					request_.architectureElement,
					SectorArchitectureControl::size));
	activeWhen(architectureHeight_,
			architectureMode &&
			M_ArchitectureHasControl(
					request_.architectureElement,
					SectorArchitectureControl::height));
	activeWhen(architectureMargin_,
			architectureMode &&
			M_ArchitectureHasControl(
					request_.architectureElement,
					SectorArchitectureControl::margin));
	activeWhen(architectureMirror_,
			architectureMode && architectureMirror);
	const bool compatibleArchitectureLift =
			M_ArchitectureSupportsFunction(
					request_.architectureElement,
					SectorArchitectureFunction::smartLift) &&
			!liftPresets_.empty();
	architectureFunction_->mode(
			static_cast<int>(SectorArchitectureFunction::smartLift),
			compatibleArchitectureLift ? 0 : FL_MENU_INACTIVE);
	if (!compatibleArchitectureLift && architectureSmartLift)
	{
		request_.architectureFunction =
				SectorArchitectureFunction::staticGeometry;
		architectureSmartLift = false;
	}
	activeWhen(architectureFunction_,
			architectureMode &&
			M_ArchitectureHasControl(
					request_.architectureElement,
					SectorArchitectureControl::function));
	activeWhen(joinChoice_,
			request_.mode == SectorDesignMode::extrude ||
			request_.mode == SectorDesignMode::inset ||
			request_.mode == SectorDesignMode::corridor ||
			request_.mode == SectorDesignMode::stairs);
	const bool stairs = request_.mode == SectorDesignMode::stairs;
	activeWhen(stepCountInput_, stairs);
	activeWhen(stepRiseInput_, stairs);
	activeWhen(stepTreadInput_, stairs);
	activeWhen(fitTargetCheck_, stairs);
	activeWhen(targetFloorInput_, stairs);
	activeWhen(headroomCheck_, stairs);
	activeWhen(replaceCheck_,
			request_.mode == SectorDesignMode::inset ||
			request_.mode == SectorDesignMode::stairs ||
			request_.mode == SectorDesignMode::lift ||
			request_.mode == SectorDesignMode::architecture);
	activeWhen(propertyTarget_, request_.mode == SectorDesignMode::inset);

	const bool hasDoors = !doorPresets_.empty();
	startConnection_->mode(1, hasDoors ? 0 : FL_MENU_INACTIVE);
	endConnection_->mode(1, hasDoors ? 0 : FL_MENU_INACTIVE);
	if (!hasDoors)
	{
		if (request_.startConnection == SectorConnection::door)
			request_.startConnection = SectorConnection::open;
		if (request_.endConnection == SectorConnection::door)
			request_.endConnection = SectorConnection::open;
	}
	const bool connectionsMode =
			request_.mode == SectorDesignMode::extrude ||
			request_.mode == SectorDesignMode::corridor ||
			request_.mode == SectorDesignMode::stairs;
	const bool doorConnection = hasDoors && connectionsMode &&
			(request_.startConnection == SectorConnection::door ||
			 request_.endConnection == SectorConnection::door);
	if (doorConnection)
	{
		doorPreset_->activate();
		doorDepth_->activate();
		faceTexture_->activate();
		trackTexture_->activate();
		faceTextureButton_->activate();
		trackTextureButton_->activate();
		faceAutoButton_->activate();
		trackAutoButton_->activate();
		facePreview_->activate();
		trackPreview_->activate();
	}
	else
	{
		doorPreset_->deactivate();
		doorDepth_->deactivate();
		faceTexture_->deactivate();
		trackTexture_->deactivate();
		faceTextureButton_->deactivate();
		trackTextureButton_->deactivate();
		faceAutoButton_->deactivate();
		trackAutoButton_->deactivate();
		facePreview_->deactivate();
		trackPreview_->deactivate();
	}
	const bool extrudeDoor = doorConnection &&
			request_.mode == SectorDesignMode::extrude &&
			request_.startConnection == SectorConnection::door;
	activeWhen(doorWidth_, extrudeDoor);
	activeWhen(doorOffset_, extrudeDoor);
	activeWhen(doorPlacement_, extrudeDoor);
	activeWhen(doorSegments_, extrudeDoor && !request_.autoDoorLines);
	if (liftPresets_.empty() ||
		(request_.mode != SectorDesignMode::lift &&
		 !architectureSmartLift))
	{
		liftPreset_->deactivate();
		liftTriggers_->deactivate();
	}
	else
	{
		liftPreset_->activate();
		activeWhen(liftTriggers_,
				request_.mode == SectorDesignMode::lift);
	}
	if (request_.mode == SectorDesignMode::lift ||
		architectureSmartLift)
	{
		liftGuide_->show();
		liftStatus_->show();
	}
	else
	{
		liftGuide_->hide();
		liftStatus_->hide();
	}

	const bool startConnection =
			request_.mode == SectorDesignMode::extrude ||
			request_.mode == SectorDesignMode::corridor ||
			request_.mode == SectorDesignMode::stairs;
	const bool endConnection =
			request_.mode == SectorDesignMode::corridor ||
			request_.mode == SectorDesignMode::stairs;
	if (startConnection)
		startConnection_->activate();
	else
		startConnection_->deactivate();
	if (endConnection)
		endConnection_->activate();
	else
		endConnection_->deactivate();
	WriteControls();
}

void UI_SectorDesigner::Recompute()
{
	ReadControls();
	if (request_.mode == SectorDesignMode::corridor &&
		fixedAnchors_.size() == 2 && !pointerActive_)
	{
		struct RouteRank
		{
			int route;
			int errors;
			int collisions;
			double length;
			int bends;
		};
		std::vector<RouteRank> ranks;
		const v2double_t delta =
				fixedAnchors_.back() - fixedAnchors_.front();
		for (int route = 0; route < 4; route++)
		{
			SectorDesignRequest candidate = request_;
			candidate.anchors = fixedAnchors_;
			candidate.anchorLines = endpointLines_;
			candidate.routeIndex = route;
			SectorDesignPlan candidatePlan = M_PlanSectorDesign(
					inst_.level, inst_.conf, &inst_.wad.images,
					inst_.loaded.levelFormat, candidate);
			int errors = 0;
			for (const SectorDesignIssue &issue : candidatePlan.issues)
				errors += issue.severity ==
						SectorDesignIssueSeverity::error;
			const int collisions =
					static_cast<int>(candidatePlan.issues.size());
			const double length = route == 0 ? delta.hypot() :
					std::abs(delta.x) + std::abs(delta.y);
			ranks.push_back({route, errors, collisions, length,
							 route == 0 ? 0 : route == 3 ? 2 : 1});
		}
		std::sort(ranks.begin(), ranks.end(),
				[](const RouteRank &a, const RouteRank &b)
				{
					return std::tie(a.errors, a.collisions, a.length,
									a.bends, a.route) <
						   std::tie(b.errors, b.collisions, b.length,
									b.bends, b.route);
				});
		rankedRoutes_.clear();
		for (const RouteRank &rank : ranks)
			rankedRoutes_.push_back(rank.route);
		routeCyclePosition_ %= static_cast<int>(rankedRoutes_.size());
		request_.routeIndex = rankedRoutes_[routeCyclePosition_];
	}
	SaveMemory();
	bool planningException = false;
	try
	{
		plan_ = M_PlanSectorDesign(inst_.level, inst_.conf,
				&inst_.wad.images, inst_.loaded.levelFormat,
				CurrentRequest());
	}
	catch (const std::exception &error)
	{
		planningException = true;
		plan_ = {};
		plan_.issues.push_back({
			SectorDesignIssueSeverity::error,
			SString::printf("Could not plan: %s", error.what())
			});
	}
	if (!planningException &&
		request_.mode == SectorDesignMode::extrude &&
		request_.extrudeUseDragDepth &&
		request_.extrudeReferenceLine < 0 &&
		extrudePressArmed_ && extrudeDragMoved_ &&
		plan_.extrudeReferenceLine >= 0)
	{
		// Lock the reference as soon as a true mouse drag begins. Bent-chain
		// direction must not jump just because the pointer later becomes
		// marginally closer to a different segment.
		request_.extrudeReferenceLine = plan_.extrudeReferenceLine;
	}
	PresentPlan(planningException);
}

bool UI_SectorDesigner::WaitingForGesture() const
{
	if (planningException_ || plan_.valid())
		return false;

	const SectorDesignRequest current = CurrentRequest();
	switch (current.mode)
	{
		case SectorDesignMode::room:
		case SectorDesignMode::polygon:
		case SectorDesignMode::architecture:
			return current.anchors.size() < 2;

		case SectorDesignMode::freeform:
			return current.anchors.size() < 3;

		case SectorDesignMode::extrude:
			return current.anchorLines.empty();

		case SectorDesignMode::inset:
			return current.targetSectors.empty();

		case SectorDesignMode::corridor:
			return current.anchors.size() < 2;

		case SectorDesignMode::stairs:
			return current.targetSectors.empty() &&
					current.anchors.size() < 2;

		case SectorDesignMode::lift:
			return current.targetSectors.empty() &&
					current.anchorLines.empty();
	}
	return false;
}

SString UI_SectorDesigner::WaitingPrompt() const
{
	const SectorDesignRequest current = CurrentRequest();
	switch (current.mode)
	{
		case SectorDesignMode::room:
			return current.anchors.empty() ?
					"Drag two corners to make a room." :
					"Place the opposite room corner.";

		case SectorDesignMode::polygon:
			return current.anchors.empty() ?
					"Choose the polygon center, then its radius." :
					"Place the polygon radius.";

		case SectorDesignMode::freeform:
			return SString::printf(
					"Add %zu more outline point%s, then close the shape.",
					current.anchors.size() >= 3 ?
						0 : 3 - current.anchors.size(),
					current.anchors.size() == 2 ? "" : "s");

		case SectorDesignMode::extrude:
			return extrudePressSector_ >= 0 ?
					"Drag toward and through the sector boundary to extrude." :
					"Ready for the next extrusion: drag a wall, or drag "
					"from inside a sector through its boundary.";

		case SectorDesignMode::inset:
			return "Select one or more nonadjacent sectors to inset.";

		case SectorDesignMode::corridor:
			return current.anchors.empty() ?
					"Place the corridor start and end." :
					"Place the corridor end.";

		case SectorDesignMode::stairs:
			return current.anchors.empty() ?
					"Drag a stair run, or select an existing sector path." :
					"Place the end of the stair run.";

		case SectorDesignMode::lift:
			return "Click a platform sector (Shift-click adds platforms), "
					"or drag outward from a wall to build an alcove.";

		case SectorDesignMode::architecture:
		{
			const char *label =
					M_ArchitectureDescriptor(
							current.architectureElement).label;
			return current.anchors.empty() ?
					SString::printf(
						"%s selected. Press-drag-release its complete "
						"footprint inside one room (or in clear void).",
						label) :
					"Place the opposite architecture footprint corner.";
		}
	}
	return "Begin a Smart Sector gesture.";
}

SString UI_SectorDesigner::WaitingSummary() const
{
	switch (request_.mode)
	{
		case SectorDesignMode::room: return "Waiting for room corners";
		case SectorDesignMode::polygon: return "Waiting for polygon points";
		case SectorDesignMode::freeform: return "Waiting for an outline";
		case SectorDesignMode::extrude: return "Ready for next extrusion";
		case SectorDesignMode::inset: return "Waiting for sector selection";
		case SectorDesignMode::corridor: return "Waiting for corridor endpoints";
		case SectorDesignMode::stairs: return "Waiting for a stair run";
		case SectorDesignMode::lift: return "Waiting for lift geometry";
		case SectorDesignMode::architecture:
			return "Waiting for an architecture footprint";
	}
	return "Waiting for gesture";
}

void UI_SectorDesigner::PresentPlan(bool planningException)
{
	planningException_ = planningException;
	if (InvalidOptionalInt(sectorType_))
		plan_.issues.push_back({
			SectorDesignIssueSeverity::error,
			"Sector Special must be a whole number or empty"
		});
	if (InvalidOptionalInt(sectorTag_))
		plan_.issues.push_back({
			SectorDesignIssueSeverity::error,
			"Sector Tag must be a whole number or empty"
		});

	availableDoorLines_.clear();
	for (int line : endpointLines_)
		if (inst_.level.isLinedef(line))
			availableDoorLines_.push_back(line);
	updating_ = true;
	doorSegments_->clear();
	for (int line : availableDoorLines_)
	{
		double length = 0;
		if (inst_.level.isLinedef(line))
			length = inst_.level.calcLength(
					*inst_.level.linedefs[line]);
		doorSegments_->add(SString::printf(
				"Line #%d  (%.1f)", line, length).c_str());
		const std::vector<int> &selected = request_.autoDoorLines ?
				plan_.doorSourceLines : request_.doorLines;
		doorSegments_->select(doorSegments_->size(),
				std::find(selected.begin(), selected.end(), line) !=
					selected.end());
	}
	updating_ = false;

	availableLiftTriggerLines_.clear();
	if (!planningException &&
		request_.mode == SectorDesignMode::lift &&
		!request_.targetSectors.empty() && !liftPresets_.empty())
	{
		SectorDesignRequest allTriggers = CurrentRequest();
		allTriggers.liftTriggerLines.clear();
		allTriggers.restrictLiftTriggers = false;
		SectorDesignPlan allPlan = request_.liftTriggerLines.empty() ?
				plan_ : M_PlanSectorDesign(inst_.level, inst_.conf,
						&inst_.wad.images, inst_.loaded.levelFormat,
						allTriggers);
		for (const PlannedLift &lift : allPlan.lifts)
			for (int line : lift.triggerLines)
				if (std::find(availableLiftTriggerLines_.begin(),
							  availableLiftTriggerLines_.end(), line) ==
						availableLiftTriggerLines_.end())
					availableLiftTriggerLines_.push_back(line);
	}
	updating_ = true;
	liftTriggers_->clear();
	for (int line : availableLiftTriggerLines_)
	{
		liftTriggers_->add(SString::printf("Line #%d", line).c_str());
		const bool selected = !request_.restrictLiftTriggers ||
				std::find(request_.liftTriggerLines.begin(),
						  request_.liftTriggerLines.end(), line) !=
						request_.liftTriggerLines.end();
		liftTriggers_->select(liftTriggers_->size(), selected);
	}
	updating_ = false;
	if (request_.mode == SectorDesignMode::lift)
	{
		SString liftStatus;
		if (liftPresets_.empty())
			liftStatus = "Unavailable for this game / format";
		else if (request_.targetSectors.empty() &&
				 endpointLines_.empty())
			liftStatus = "Click a platform, or drag outward from a wall";
		else if (!plan_.lifts.empty())
		{
			int triggers = 0;
			int minimumTravel = std::numeric_limits<int>::max();
			int maximumTravel = std::numeric_limits<int>::min();
			for (const PlannedLift &lift : plan_.lifts)
			{
				triggers += static_cast<int>(lift.triggerLines.size());
				minimumTravel = std::min(minimumTravel, lift.travel);
				maximumTravel = std::max(maximumTravel, lift.travel);
			}
			const SString travel = minimumTravel == maximumTravel ?
					SString(minimumTravel) :
					SString::printf("%d..%d",
							minimumTravel, maximumTravel);
			liftStatus = SString::printf(
					"%zu platform%s - travel %s - %d trigger%s",
					plan_.lifts.size(),
					plan_.lifts.size() == 1 ? "" : "s",
					travel.c_str(),
					triggers, triggers == 1 ? "" : "s");
		}
		else if (plan_.plannedLifts > 0)
			liftStatus = SString::printf(
					"New alcove - depth %.1f - trigger portals after apply",
					plan_.resolvedExtrudeDepth);
		else
			liftStatus = "Review the blocking lift issue";
		liftStatus_->value(liftStatus.c_str());
	}
	else if (request_.mode == SectorDesignMode::architecture &&
			 request_.architectureFunction ==
					SectorArchitectureFunction::smartLift)
	{
		const SString liftStatus = !plan_.resolvedActionPreset ?
				"Smart Lift unavailable or blocked" :
				SString::printf(
					"Platform lift - %s - fresh tag and local portals",
					M_SectorActionPresetLabel(
							inst_.conf,
							*plan_.resolvedActionPreset).c_str());
		liftStatus_->value(liftStatus.c_str());
	}

	const bool waiting = WaitingForGesture();
	if (planningException || waiting)
	{
		inst_.edit.designAssistPreview.reset();
		inst_.RedrawMap();
	}
	else
		UI_SetSectorDesignPreview(inst_, plan_, plan_.retainedSectors);

	const size_t plannedSectorCount =
			plan_.shapes.size() +
			static_cast<size_t>(plan_.plannedRetainedCells);
	SString summary = waiting ? WaitingSummary() : SString::printf(
				"%d sector%s, %d line%s, %d split%s, %d door%s, "
				"%d step%s, %d lift%s",
				static_cast<int>(plannedSectorCount),
				plannedSectorCount == 1 ? "" : "s",
				plan_.plannedLines, plan_.plannedLines == 1 ? "" : "s",
				plan_.plannedSplits, plan_.plannedSplits == 1 ? "" : "s",
				plan_.plannedDoors, plan_.plannedDoors == 1 ? "" : "s",
				plan_.plannedSteps, plan_.plannedSteps == 1 ? "" : "s",
				plan_.plannedLifts, plan_.plannedLifts == 1 ? "" : "s");
	if (!waiting && plan_.plannedStructures > 0)
		summary += SString::printf(
				" • %d structure%s",
				plan_.plannedStructures,
				plan_.plannedStructures == 1 ? "" : "s");
	if (!waiting && plan_.plannedArchitectureHosts > 0)
		summary += " • new host hall";
	if (!waiting && request_.mode == SectorDesignMode::extrude &&
		plan_.extrudeReferenceLine >= 0 &&
		plan_.resolvedExtrudeDepth > 0.0)
		summary += SString::printf(
				" • depth %.1f • %s",
				plan_.resolvedExtrudeDepth,
				plan_.extrudeOpposite ?
					"opposite side" :
					(request_.extrudeUseDragDepth ?
						"toward pointer" : "signed side"));
	if (!waiting && !plan_.lifts.empty())
	{
		summary += " • travel";
		for (const PlannedLift &lift : plan_.lifts)
			summary += SString::printf(" %d", lift.travel);
	}
	const int errorCount = static_cast<int>(std::count_if(
			plan_.issues.begin(), plan_.issues.end(),
			[](const SectorDesignIssue &issue)
			{
				return issue.severity == SectorDesignIssueSeverity::error;
			}));
	if (!waiting && errorCount > 0)
		summary += SString::printf(" • %d error%s",
				errorCount, errorCount == 1 ? "" : "s");
	summary_->copy_label(summary.c_str());
	const SString face = plan_.doorFaceTexture.empty() ?
			(request_.doorOptions.faceTexture.empty() ?
				inst_.conf.default_wall_tex :
				request_.doorOptions.faceTexture) :
			plan_.doorFaceTexture;
	const SString track = plan_.doorTrackTexture.empty() ?
			(request_.doorOptions.trackTexture.empty() ?
				face : request_.doorOptions.trackTexture) :
			plan_.doorTrackTexture;
	faceAutoButton_->value(
			request_.doorOptions.faceTexture.empty() ? 1 : 0);
	trackAutoButton_->value(
			request_.doorOptions.trackTexture.empty() ? 1 : 0);
	facePreview_->GetTex(face);
	trackPreview_->GetTex(track);
	facePreview_->copy_tooltip(SString::printf(
			request_.doorOptions.faceTexture.empty() ?
				"Auto resolved the moving door face to %s. "
				"Click to choose an override." :
				"Door face override: %s. Click to choose another texture.",
			face.c_str()).c_str());
	trackPreview_->copy_tooltip(SString::printf(
			request_.doorOptions.trackTexture.empty() ?
				"Auto resolved the doorway track walls to %s. "
				"Click to choose an override." :
				"Track wall override: %s. Click to choose another texture.",
			track.c_str()).c_str());
	UpdatePropertyTexturePreviews();

	issues_->clear();
	if (waiting)
		issues_->add(WaitingPrompt().c_str());
	else if (plan_.issues.empty())
		issues_->add("Ready - press Enter or Make Sectors");
	if (!waiting)
		for (const SectorDesignIssue &issue : plan_.issues)
		{
			SString location;
			if (issue.sector >= 0)
				location = SString::printf("sector #%d: ", issue.sector);
			else if (issue.line >= 0)
				location = SString::printf("line #%d: ", issue.line);
			issues_->add(SString::printf("%s %s%s",
					issue.severity == SectorDesignIssueSeverity::error ?
						"ERROR:" : "Warning:",
					location.c_str(), issue.message.c_str()).c_str());
		}
	if (waiting)
	{
		commitButton_->copy_tooltip(WaitingPrompt().c_str());
		commitButton_->deactivate();
	}
	else if (plan_.valid())
	{
		commitButton_->copy_tooltip(
				"Revalidate and apply this preview as one Undo operation.");
		commitButton_->activate();
	}
	else
	{
		auto firstError = std::find_if(
				plan_.issues.begin(), plan_.issues.end(),
				[](const SectorDesignIssue &issue)
				{
					return issue.severity ==
							SectorDesignIssueSeverity::error;
				});
		commitButton_->copy_tooltip(firstError == plan_.issues.end() ?
				"Resolve the errors in Review before applying." :
				SString::printf("Blocked: %s",
						firstError->message.c_str()).c_str());
		commitButton_->deactivate();
	}
	UpdateReviewDetails();
	inst_.RedrawMap();
	redraw();
}

SString UI_SectorDesigner::ReviewText() const
{
	const int mode = std::clamp(
			static_cast<int>(request_.mode), 0,
			static_cast<int>(std::size(MODE_LABELS)) - 1);
	const bool waiting = WaitingForGesture();
	int warnings = 0;
	int errors = 0;
	if (!waiting)
	{
		for (const SectorDesignIssue &issue : plan_.issues)
			if (issue.severity == SectorDesignIssueSeverity::error)
				errors++;
			else
				warnings++;
	}

	SString text = "SMART SECTOR DESIGNER REVIEW\n\n";
	text += SString::printf("Mode: %s\n", MODE_LABELS[mode]);
	if (summary_ && summary_->label())
		text += SString::printf("Result: %s\n", summary_->label());
	if (waiting)
	{
		text += "Status: WAITING FOR GESTURE (0 errors, 0 warnings)\n\n";
		text += WaitingPrompt();
		text += "\n";
		return text;
	}
	text += SString::printf(
			"Status: %s (%d error%s, %d warning%s)\n\n",
			plan_.valid() ? "READY" : "BLOCKED",
			errors, errors == 1 ? "" : "s",
			warnings, warnings == 1 ? "" : "s");

	if (request_.mode == SectorDesignMode::lift)
	{
		if (plan_.lifts.empty() && plan_.plannedLifts > 0)
			text += SString::printf(
					"Lift plan: new wall alcove, depth %.1f. Its platform "
					"receives a fresh tag and usable portal triggers during "
					"this same Undo operation.\n\n",
					plan_.resolvedExtrudeDepth);
		for (size_t index = 0; index < plan_.lifts.size(); ++index)
		{
			const PlannedLift &lift = plan_.lifts[index];
			const SString behavior =
					M_SectorActionPresetLabel(inst_.conf, lift.preset);
			text += SString::printf(
					"Platform %zu: sectors %zu, fresh tag %d, lower stop "
					"%d, travel %d, trigger portals %zu, behavior %s "
					"(%s).\n",
					index + 1, lift.sectors.size(), lift.tag,
					lift.lowerStop, lift.travel,
					lift.triggerLines.size(), behavior.c_str(),
					lift.preset.id.c_str());
		}
		if (!plan_.lifts.empty())
			text += "\n";
	}
	else if (request_.mode == SectorDesignMode::polygon &&
			 !plan_.shapes.empty())
	{
		const int profile = std::clamp(
				static_cast<int>(request_.polygonProfile), 0,
				static_cast<int>(std::size(POLYGON_PROFILE_LABELS)) - 1);
		text += SString::printf(
				"Profile: %s. Quantized outline: %zu vertices. "
				"Rotation: %.1f degrees",
				POLYGON_PROFILE_LABELS[profile],
				plan_.shapes.front().outer.size(),
				request_.rotation * 180.0 / std::numbers::pi);
		if (request_.polygonProfile >= SectorPolygonProfile::star5)
			text += SString::printf(
					". Inner radius: %.0f%%",
					request_.polygonInnerRatio * 100.0);
		text += ".\n\n";
	}
	else if (request_.mode == SectorDesignMode::architecture &&
			 plan_.plannedStructures > 0)
	{
		const SectorArchitectureDescriptor &architecture =
				M_ArchitectureDescriptor(request_.architectureElement);
		int lockedHost = -1;
		for (const PlannedSectorShape &shape : plan_.shapes)
			if (shape.modelSector >= 0)
			{
				lockedHost = shape.modelSector;
				break;
			}
		const SString ownership =
				plan_.plannedArchitectureHosts > 0 ?
					"A new walkable host hall is created before its "
					"structures in this same Undo operation." :
				lockedHost >= 0 ?
					SString::printf(
						"Host sector #%d is locked from the gesture start "
						"and retained.", lockedHost) :
					"The existing host is retained.";
		text += SString::printf(
				"Architecture family: %s. Structure: %s. "
				"%d generated architectural sector%s. ",
				M_ArchitectureFamilyLabel(architecture.family),
				architecture.label,
				plan_.plannedStructures,
				plan_.plannedStructures == 1 ? "" : "s");
		if (plan_.plannedRetainedCells > 0)
			text += SString::printf(
					"%d opening sector%s retain%s the host room's "
					"original properties. ",
					plan_.plannedRetainedCells,
					plan_.plannedRetainedCells == 1 ? "" : "s",
					plan_.plannedRetainedCells == 1 ? "s" : "");
		if (M_ArchitectureUsesSectionStyle(
				request_.architectureElement))
		{
			const int style = std::clamp(
					static_cast<int>(request_.architectureStyle), 0,
					static_cast<int>(std::size(
							ARCHITECTURE_STYLE_LABELS)) - 1);
			text += SString::printf(
					"Section style: %s. ",
					ARCHITECTURE_STYLE_LABELS[style]);
		}
		if (M_ArchitectureUsesBays(
				request_.architectureElement))
			text += SString::printf(
					"%d bay%s. ",
					request_.architectureBays,
					request_.architectureBays == 1 ? "" : "s");
		text += SString::printf(
				"%.1f-unit structure size, %.1f-unit edge margin. ",
				request_.architectureSize,
				request_.architectureMargin);
		if (M_ArchitectureUsesHeight(
				request_.architectureElement))
			text += SString::printf(
					"%.1f-unit elevation/depth. ",
					request_.architectureHeight);
		text += SString::printf(
				"Effect: %s. Preview: %s (%s preview). ",
				M_ArchitectureEffectDescription(request_).c_str(),
				ArchitecturePreviewName(
						plan_.shapes.empty() ?
							architecture.role :
							plan_.shapes.back().role),
				ArchitecturePreviewName(
						plan_.shapes.empty() ?
							architecture.role :
							plan_.shapes.back().role));
		if (request_.architectureFunction ==
					SectorArchitectureFunction::smartLift &&
			plan_.resolvedActionPreset)
			text += SString::printf(
					"Function: Smart Lift using %s; the platform receives "
					"one fresh tag and its usable local portals receive "
					"the action in this same Undo. ",
					M_SectorActionPresetLabel(
							inst_.conf,
							*plan_.resolvedActionPreset).c_str());
		if (architecture.family ==
					SectorArchitectureFamily::structuralSupports ||
			architecture.family ==
					SectorArchitectureFamily::wallsScreens)
			text += "Topology: sector boundaries are integrated with the "
					"host, while collapsed floor/ceiling height keeps the "
					"solid mass physically closed. ";
		else
			text += "Topology: every generated walkable or overhead cell "
					"shares open two-sided boundaries with its host or "
					"plan-local parent. ";
		if (request_.architectureMargin <= 0.000001)
			text += "Connection: coincident host edges are split and reused; "
					"walkable structures receive open two-sided portals "
					"without changing the neighboring sector's properties. ";
		text += ownership;
		text += "\n\n";
	}

	if (plan_.issues.empty())
	{
		text += "No issues. Press Enter, Space, or Make Sectors to apply.\n";
		return text;
	}

	for (size_t index = 0; index < plan_.issues.size(); ++index)
	{
		const SectorDesignIssue &issue = plan_.issues[index];
		text += SString::printf(
				"%zu. %s",
				index + 1,
				issue.severity == SectorDesignIssueSeverity::error ?
					"ERROR" : "WARNING");
		if (issue.sector >= 0)
			text += SString::printf(" — sector #%d", issue.sector);
		if (issue.line >= 0)
			text += SString::printf(" — line #%d", issue.line);
		text += "\n";
		text += issue.message;
		text += "\n\n";
	}
	if (!plan_.valid())
		text += "Nothing was changed. Resolve every ERROR and try again.\n";
	return text;
}

void UI_SectorDesigner::UpdateReviewDetails()
{
	if (reviewBuffer_)
		reviewBuffer_->text(ReviewText().c_str());
}

void UI_SectorDesigner::CopyReview()
{
	const SString text = ReviewText();
	Fl::copy(text.c_str(), static_cast<int>(text.length()), 1);
	inst_.Status_Set("Copied the complete Smart Sector review");
}

void UI_SectorDesigner::ShowReviewDetails()
{
	if (!reviewWindow_)
	{
		reviewBuffer_ = new Fl_Text_Buffer();
		reviewWindow_ = new Fl_Double_Window(
				720, 420, "Smart Sector Review");
		reviewWindow_->begin();
		reviewDisplay_ = new Fl_Text_Display(12, 12, 696, 354);
		reviewDisplay_->buffer(reviewBuffer_);
		reviewDisplay_->textsize(13);
		reviewDisplay_->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);
		reviewDisplay_->tooltip(
				"Drag to select text and press Ctrl+C, or use Copy All.");
		Fl_Button *copy = new Fl_Button(12, 378, 110, 30, "Copy All");
		copy->callback(copyReviewCallback, this);
		Fl_Button *close = new Fl_Button(
				608, 378, 100, 30, "Close");
		close->callback(reviewWindowCallback, this);
		reviewWindow_->end();
		reviewWindow_->resizable(reviewDisplay_);
		reviewWindow_->size_range(420, 240);
		reviewWindow_->callback(reviewWindowCallback, this);
		if (inst_.main_win)
			reviewWindow_->position(
					inst_.main_win->x() +
						std::max(0, (inst_.main_win->w() - 720) / 2),
					inst_.main_win->y() +
						std::max(0, (inst_.main_win->h() - 420) / 2));
	}
	UpdateReviewDetails();
	if (inst_.main_win)
	{
		reviewWindow_->show();
		reviewDisplay_->take_focus();
	}
}

bool UI_SectorDesigner::HandleTargetSectorClick(
		const v2double_t &point, keycode_t modifiers)
{
	const Objid picked = inst_.getNearbyObject(ObjType::sectors, point);
	if (!picked.valid() || !inst_.level.isSector(picked.num))
		return false;

	const bool toggle =
			(modifiers & (EMOD_SHIFT | EMOD_COMMAND)) != 0;
	auto found = std::find(request_.targetSectors.begin(),
			request_.targetSectors.end(), picked.num);

	// A plain click within an existing stair selection chooses its path
	// endpoints. Clicking again after both endpoints are set begins a fresh
	// pair, which makes branch correction quick and predictable.
	if (request_.mode == SectorDesignMode::stairs && !toggle &&
		found != request_.targetSectors.end() &&
		request_.targetSectors.size() > 1)
	{
		ClearGesture();
		if (request_.startSector < 0 || request_.endSector >= 0)
		{
			request_.startSector = picked.num;
			request_.endSector = -1;
			inst_.Status_Set(
					"Stair start is sector #%d; click a different selected "
					"sector for the end.", picked.num);
		}
		else if (picked.num == request_.startSector)
			inst_.Status_Set(
					"Sector #%d is already the stair start; choose another "
					"selected sector for the end.", picked.num);
		else
		{
			request_.endSector = picked.num;
			inst_.Status_Set(
					"Stair path is sector #%d to #%d; Enter applies it.",
					request_.startSector, request_.endSector);
		}
		Recompute();
		return true;
	}

	ClearGesture();
	request_.startSector = -1;
	request_.endSector = -1;
	request_.liftTriggerLines.clear();
	request_.restrictLiftTriggers = false;
	if (toggle)
	{
		if (found == request_.targetSectors.end())
			request_.targetSectors.push_back(picked.num);
		else
			request_.targetSectors.erase(found);
	}
	else
		request_.targetSectors.assign(1, picked.num);

	std::sort(request_.targetSectors.begin(),
			request_.targetSectors.end());
	request_.targetSectors.erase(
			std::unique(request_.targetSectors.begin(),
					request_.targetSectors.end()),
			request_.targetSectors.end());
	Recompute();

	if (request_.targetSectors.empty())
		inst_.Status_Set("%s", WaitingPrompt().c_str());
	else if (plan_.valid())
	{
		if (request_.mode == SectorDesignMode::lift)
		{
			int triggerCount = 0;
			for (const PlannedLift &lift : plan_.lifts)
				triggerCount += static_cast<int>(lift.triggerLines.size());
			inst_.Status_Set(
					"%zu lift platform%s ready with %d local trigger%s; "
					"choose behavior/portals, then Enter applies.",
					plan_.lifts.size(),
					plan_.lifts.size() == 1 ? "" : "s",
					triggerCount, triggerCount == 1 ? "" : "s");
		}
		else
			inst_.Status_Set(
					"%zu sector%s targeted; Shift-click toggles more, Enter "
					"applies.",
					request_.targetSectors.size(),
					request_.targetSectors.size() == 1 ? "" : "s");
	}
	else
	{
		auto error = std::find_if(plan_.issues.begin(), plan_.issues.end(),
				[](const SectorDesignIssue &issue)
				{
					return issue.severity ==
							SectorDesignIssueSeverity::error;
				});
		inst_.Status_Set("%zu sector%s targeted%s%s",
				request_.targetSectors.size(),
				request_.targetSectors.size() == 1 ? "" : "s",
				error == plan_.issues.end() ? "" : "; ",
				error == plan_.issues.end() ? "" : error->message.c_str());
	}
	return true;
}

void UI_SectorDesigner::CanvasClick(const v2double_t &point,
									keycode_t modifiers)
{
	ReadControls();
	if (request_.mode == SectorDesignMode::inset)
	{
		if (!HandleTargetSectorClick(point, modifiers))
			inst_.Beep("Click inside a sector to inset it");
		return;
	}

	if (request_.mode == SectorDesignMode::lift)
	{
		const Objid line =
				inst_.getNearbyObject(ObjType::linedefs, point);
		const bool grabbedWall = line.valid() &&
				DirectlyGrabbedLine(inst_.level, line.num, point,
						inst_.grid.getScale());
		if (!grabbedWall && HandleTargetSectorClick(point, modifiers))
			return;
		if (grabbedWall)
		{
			request_.targetSectors.clear();
			request_.liftTriggerLines.clear();
			request_.restrictLiftTriggers = false;
			ClearGesture();
		}
	}

	if (request_.mode == SectorDesignMode::stairs &&
		(!request_.targetSectors.empty() ||
		 (modifiers & (EMOD_SHIFT | EMOD_COMMAND))) &&
		HandleTargetSectorClick(point, modifiers))
		return;

	v2double_t placed = point;
	if ((modifiers & EMOD_SHIFT) && !fixedAnchors_.empty())
		placed = Constrain45(fixedAnchors_.back(), placed);
	placed = inst_.grid.Snap(placed);

	if ((request_.mode == SectorDesignMode::extrude ||
		 request_.mode == SectorDesignMode::lift) &&
		endpointLines_.empty())
	{
		Objid line = inst_.getNearbyObject(ObjType::linedefs, point);
		if (request_.mode == SectorDesignMode::lift && line.valid() &&
			!DirectlyGrabbedLine(
					inst_.level, line.num, point, inst_.grid.getScale()))
			line = {};
		if (request_.mode == SectorDesignMode::extrude &&
			request_.extrudeUseDragDepth && line.valid() &&
			!DirectlyGrabbedLine(
					inst_.level, line.num, point, inst_.grid.getScale()))
			line = {};
		if (line.is_nil())
		{
			if (request_.mode == SectorDesignMode::extrude &&
				request_.extrudeUseDragDepth)
			{
				const Objid sector =
						inst_.getNearbyObject(ObjType::sectors, point);
				if (sector.valid() && inst_.level.isSector(sector.num))
				{
					extrudePressSector_ = sector.num;
					extrudeSourceSector_ = sector.num;
					extrudePressArmed_ = true;
					extrudeDragMoved_ = false;
					extrudePressPoint_ = point;
					pointer_ = placed;
					pointerModifiers_ = modifiers;
					pointerActive_ = false;
					inst_.Status_Set(
							"Drag sector #%d toward the wall to extrude; "
							"no prior selection is needed.",
							sector.num);
					Recompute();
					return;
				}
			}
			inst_.Beep("Start the drag on a boundary or inside a sector");
			return;
		}
		endpointLines_.push_back(line.num);
		pointerActive_ = false;
		if (request_.mode == SectorDesignMode::extrude)
		{
			const Objid sector =
					inst_.getNearbyObject(ObjType::sectors, point);
			if (sector.valid() &&
				LineBoundsSector(inst_.level, line.num, sector.num))
				extrudeSourceSector_ = sector.num;
			else
			{
				const LineDef &linedef = *inst_.level.linedefs[line.num];
				const int right =
						SectorForSide(inst_.level, linedef.right);
				const int left =
						SectorForSide(inst_.level, linedef.left);
				extrudeSourceSector_ = right >= 0 ? right : left;
			}
			extrudePressSector_ = -1;
			request_.extrudeReferenceLine = line.num;
			extrudePressArmed_ = request_.extrudeUseDragDepth;
			extrudeDragMoved_ = false;
			extrudePressPoint_ = point;
			inst_.Status_Set(request_.extrudeUseDragDepth ?
					"Drag toward the new sector; release locks depth. "
					"F flips, Shift+G snaps, Enter applies." :
					"Boundary selected; signed Depth sets direction. "
					"F flips, Shift+G snaps, Enter applies.");
		}
		else
		{
			request_.extrudeReferenceLine = line.num;
			extrudePressArmed_ = true;
			extrudeDragMoved_ = false;
			extrudePressPoint_ = point;
			inst_.Status_Set(
					"Drag toward the lift alcove; release locks depth. "
					"A click, move, and second click also works.");
		}
		Recompute();
		return;
	}
	if (request_.mode == SectorDesignMode::extrude &&
		(modifiers & EMOD_SHIFT) && fixedAnchors_.empty())
	{
		Objid line = inst_.getNearbyObject(ObjType::linedefs, point);
		if (line.valid() &&
			std::find(endpointLines_.begin(), endpointLines_.end(),
					  line.num) == endpointLines_.end())
		{
			endpointLines_.push_back(line.num);
			extrudePressArmed_ = false;
			extrudeDragMoved_ = false;
			pointerActive_ = false;
			inst_.Status_Set(
					"Added boundary line; release Shift, then drag toward "
					"the new sector");
			Recompute();
			return;
		}
	}
	if (request_.mode == SectorDesignMode::extrude &&
		request_.extrudeUseDragDepth && fixedAnchors_.empty() &&
		!(modifiers & EMOD_SHIFT))
	{
		Objid line = inst_.getNearbyObject(ObjType::linedefs, point);
		if (line.valid() &&
			std::find(endpointLines_.begin(), endpointLines_.end(),
					  line.num) != endpointLines_.end())
		{
			// A fresh press on any member of a selected chain starts a real
			// drag from that seam. This is especially important after the
			// user released Shift while assembling a bent chain.
			request_.extrudeReferenceLine = line.num;
			extrudePressArmed_ = true;
			extrudeDragMoved_ = false;
			extrudePressPoint_ = point;
			pointer_ = placed;
			pointerModifiers_ = modifiers;
			pointerActive_ = false;
			Recompute();
			inst_.Status_Set(
					"Drag from line #%d toward the new sector; release "
					"locks depth.",
					line.num);
			return;
		}
	}

	if ((request_.mode == SectorDesignMode::room ||
		 request_.mode == SectorDesignMode::polygon ||
		 request_.mode == SectorDesignMode::architecture) &&
		fixedAnchors_.size() >= 2)
		ClearGesture();

	const bool supportsAnchorDrag =
			request_.mode == SectorDesignMode::room ||
			request_.mode == SectorDesignMode::polygon ||
			request_.mode == SectorDesignMode::architecture ||
			request_.mode == SectorDesignMode::corridor ||
			request_.mode == SectorDesignMode::stairs;
	if (supportsAnchorDrag && fixedAnchors_.empty())
	{
		if (request_.mode == SectorDesignMode::architecture)
		{
			const Objid host =
					inst_.getNearbyObject(ObjType::sectors, point);
			request_.architectureHostSector =
					host.valid() && inst_.level.isSector(host.num) ?
						host.num : -1;
		}
		anchorPressArmed_ = true;
		anchorDragMoved_ = false;
		anchorPressPoint_ = point;
	}

	if (request_.mode == SectorDesignMode::freeform &&
		fixedAnchors_.size() >= 3)
	{
		const double closeDistance =
				std::max(1.0, 8.0 / inst_.grid.getScale());
		if ((placed - fixedAnchors_.front()).hypot() <= closeDistance)
		{
			pointerActive_ = false;
			Commit();
			return;
		}
	}

	if (request_.mode == SectorDesignMode::corridor ||
		request_.mode == SectorDesignMode::stairs)
	{
		Objid line = inst_.getNearbyObject(ObjType::linedefs, point);
		// Keep endpoint ownership parallel with every placed route point.
		// A -1 placeholder is intentional: without it, a line found only at
		// the far endpoint was previously misread as the start connection.
		endpointLines_.push_back(line.valid() ? line.num : -1);
	}
	fixedAnchors_.push_back(placed);
	pointer_ = placed;
	pointerModifiers_ = modifiers;
	pointerActive_ = false;
	extrudePressArmed_ = false;
	extrudeDragMoved_ = false;
	Recompute();
	if (request_.mode == SectorDesignMode::extrude)
		inst_.Status_Set(
				"Depth locked at %.1f on the %s; Enter or Space applies.",
				plan_.resolvedExtrudeDepth,
				plan_.extrudeOpposite ? "opposite side" : "pointer side");
	else if (request_.mode == SectorDesignMode::architecture &&
			 fixedAnchors_.size() == 1)
	{
		if (request_.architectureHostSector >= 0)
			inst_.Status_Set(
					"Architecture host sector #%d locked; drag the complete "
					"layout footprint.",
					request_.architectureHostSector);
		else
			inst_.Status_Set(
					"Clear-void hall started; drag a footprint that does not "
					"touch existing geometry.");
	}
}

void UI_SectorDesigner::CanvasMove(const v2double_t &point,
								   keycode_t modifiers,
								   bool primaryButtonDown)
{
	if (!Active())
		return;
	const bool wasPointerActive = pointerActive_;
	pointer_ = point;
	if ((modifiers & EMOD_SHIFT) && !fixedAnchors_.empty())
		pointer_ = Constrain45(fixedAnchors_.back(), pointer_);
	pointer_ = inst_.grid.Snap(pointer_);
	pointerModifiers_ = modifiers;
	pointerActive_ = IsAnchorMode(request_.mode) &&
			(!fixedAnchors_.empty() || !endpointLines_.empty());
	if (anchorPressArmed_)
	{
		const double threshold =
				std::max(1.0, 4.0 / inst_.grid.getScale());
		if ((point - anchorPressPoint_).hypot() >= threshold)
			anchorDragMoved_ = true;
	}
	bool extrudeInteraction = false;
	if ((request_.mode == SectorDesignMode::lift ||
		 (request_.mode == SectorDesignMode::extrude &&
		  request_.extrudeUseDragDepth)) &&
		extrudePressArmed_ &&
		primaryButtonDown)
	{
		extrudeInteraction = true;
		const double threshold =
				std::max(1.0, 4.0 / inst_.grid.getScale());
		if ((point - extrudePressPoint_).hypot() >= threshold)
		{
			extrudeDragMoved_ = true;
			if (endpointLines_.empty() &&
				inst_.level.isSector(extrudePressSector_))
			{
				const int line = BoundaryInDragDirection(
						inst_.level, extrudePressSector_,
						extrudePressPoint_, point);
				if (line >= 0)
				{
					endpointLines_.push_back(line);
					request_.extrudeReferenceLine = line;
					extrudeSourceSector_ = extrudePressSector_;
					extrudePressSector_ = -1;
					pointerActive_ = true;
					inst_.Status_Set(
							"Extruding sector #%d from line #%d; "
							"release to lock depth.",
							extrudeSourceSector_, line);
				}
			}
		}
	}
	if (!pointerActive_ && !wasPointerActive && !extrudeInteraction)
		return;
	Recompute();
	if ((request_.mode == SectorDesignMode::extrude ||
		 request_.mode == SectorDesignMode::lift) &&
		!endpointLines_.empty() && plan_.resolvedExtrudeDepth > 0.0)
		inst_.Status_Set(
				"%s %.1f units %s; %s, %sEnter applies.",
				request_.mode == SectorDesignMode::lift ?
					"Lift alcove" : "Extrude",
				plan_.resolvedExtrudeDepth,
				plan_.extrudeOpposite ?
					"on the opposite side" : "toward the pointer",
				extrudePressArmed_ && extrudeDragMoved_ ?
					"release to lock" : "click to lock",
				request_.mode == SectorDesignMode::extrude ?
					"F flips, Shift+G snaps, " : "");
}

void UI_SectorDesigner::CanvasRelease(const v2double_t &point,
									  keycode_t modifiers)
{
	if (!Active())
		return;

	if (anchorPressArmed_)
	{
		const double threshold =
				std::max(1.0, 4.0 / inst_.grid.getScale());
		const bool completeDrag =
				fixedAnchors_.size() == 1 &&
				(anchorDragMoved_ ||
				 (point - anchorPressPoint_).hypot() >= threshold);
		anchorPressArmed_ = false;
		anchorDragMoved_ = false;
		if (!completeDrag)
			return;

		v2double_t placed = point;
		if (modifiers & EMOD_SHIFT)
			placed = Constrain45(fixedAnchors_.front(), placed);
		placed = inst_.grid.Snap(placed);
		fixedAnchors_.push_back(placed);
		if (request_.mode == SectorDesignMode::corridor ||
			request_.mode == SectorDesignMode::stairs)
		{
			const Objid line =
					inst_.getNearbyObject(ObjType::linedefs, point);
			endpointLines_.push_back(line.valid() ? line.num : -1);
		}
		pointer_ = placed;
		pointerModifiers_ = modifiers;
		pointerActive_ = false;
		Recompute();
		if (plan_.valid())
		{
			if (request_.mode == SectorDesignMode::architecture)
			{
				const char *label =
						M_ArchitectureDescriptor(
								request_.architectureElement).label;
				if (request_.architectureHostSector >= 0)
					inst_.Status_Set(
							"%s drag ready in host sector #%d; generated "
							"boundaries are integrated. Enter or Make "
							"Sectors applies.",
							label,
							request_.architectureHostSector);
				else
					inst_.Status_Set(
							"%s drag ready with a new connected host hall; "
							"Enter or Make Sectors applies.",
							label);
			}
			else
				inst_.Status_Set(
						"Gesture locked; Enter, Space, or Make Sectors "
						"applies.");
		}
		else
		{
			auto error = std::find_if(
					plan_.issues.begin(), plan_.issues.end(),
					[](const SectorDesignIssue &issue)
					{
						return issue.severity ==
								SectorDesignIssueSeverity::error;
					});
			const SString message = error == plan_.issues.end() ?
					"Gesture blocked; review the red intent geometry." :
					SString::printf(
						"Blocked: %s Red geometry marks the exact "
						"affected intent.",
						error->message.c_str());
			inst_.Status_Set("%s", message.c_str());
		}
		return;
	}

	if ((request_.mode != SectorDesignMode::lift &&
		 (request_.mode != SectorDesignMode::extrude ||
		  !request_.extrudeUseDragDepth)) ||
		!extrudePressArmed_)
		return;

	const bool completeDrag = extrudeDragMoved_;
	extrudePressArmed_ = false;
	extrudeDragMoved_ = false;
	extrudePressSector_ = -1;
	if (!completeDrag)
		return;
	if (endpointLines_.empty())
	{
		extrudeSourceSector_ = -1;
		pointerActive_ = false;
		Recompute();
		inst_.Beep(
				"Drag toward a sector boundary before releasing");
		return;
	}

	pointer_ = inst_.grid.Snap(point);
	pointerModifiers_ = modifiers;
	fixedAnchors_.assign(1, pointer_);
	pointerActive_ = false;
	Recompute();
	inst_.Status_Set(
			"%s depth locked at %.1f on the %s; Enter, Space, or Make Sectors applies.",
			request_.mode == SectorDesignMode::lift ? "Lift alcove" : "Extrusion",
			plan_.resolvedExtrudeDepth,
			plan_.extrudeOpposite ? "opposite side" : "pointer side");
}

bool UI_SectorDesigner::CanvasKey(keycode_t key)
{
	const keycode_t base = key & FL_KEY_MASK;
	const keycode_t modifiers = key & EMOD_ALL_MASK;
	if ((base == FL_Enter || base == FL_KP_Enter || base == ' ') &&
		modifiers == 0)
	{
		Commit();
		return true;
	}
	if (base == FL_Tab && modifiers == 0)
	{
		CycleRoute();
		return true;
	}
	if (base == 'f' &&
		(modifiers == 0 || modifiers == EMOD_SHIFT))
	{
		Flip();
		return true;
	}
	return false;
}

bool UI_SectorDesigner::CanvasWheel(int deltaY)
{
	if (!Active() || deltaY == 0 ||
		(request_.mode != SectorDesignMode::corridor &&
		 request_.mode != SectorDesignMode::inset &&
		 request_.mode != SectorDesignMode::architecture))
		return false;

	ReadControls();
	const double step = std::max(1, inst_.grid.getStep());
	const int notches = std::max(1, std::abs(deltaY));
	if (request_.mode == SectorDesignMode::architecture)
	{
		// Structural dimensions need finer control than a room-width gesture.
		// Quarter-grid scaling keeps a 64-unit grid useful for 8/16/24-unit
		// piers, walls, treads, and ribs while following the active scale.
		const double scaleStep = std::max(1.0, step * 0.25);
		const double minimum =
				M_MinimumArchitectureSize(
						request_.architectureStyle,
						request_.architectureElement);
		const double delta =
				(deltaY < 0 ? 1.0 : -1.0) * notches * scaleStep;
		request_.architectureSize =
				std::max(minimum, request_.architectureSize + delta);
		SetDouble(architectureSize_, request_.architectureSize);
		Recompute();
		const char *label =
				M_ArchitectureDescriptor(
						request_.architectureElement).label;
		SString result = SString::printf(
				"Architecture structure size %.1f "
				"(wheel step %.1f; %s minimum %.0f)",
				request_.architectureSize, scaleStep,
				M_ArchitectureUsesSectionStyle(
						request_.architectureElement) ?
					ARCHITECTURE_STYLE_LABELS[
						std::clamp(
							static_cast<int>(
									request_.architectureStyle), 0,
							static_cast<int>(std::size(
									ARCHITECTURE_STYLE_LABELS)) - 1)] :
					label,
				minimum);
		if (!WaitingForGesture() && !plan_.valid())
		{
			auto error = std::find_if(
					plan_.issues.begin(), plan_.issues.end(),
					[](const SectorDesignIssue &issue)
					{
						return issue.severity ==
								SectorDesignIssueSeverity::error;
					});
			if (error != plan_.issues.end())
				result += SString::printf(" - %s",
						error->message.c_str());
		}
		inst_.Status_Set("%s", result.c_str());
		return true;
	}

	auto steppedMagnitude = [step, notches, deltaY](double value)
	{
		double magnitude = std::abs(value);
		if (magnitude <= 0.000001)
			magnitude = step;
		const double remainder = std::fmod(magnitude, step);
		// Preserve an explicitly typed sub-grid remainder. Every successful
		// wheel change is still exactly N grid steps, while scrolling down can
		// never make a small value larger merely to snap it to the grid.
		const double minimum = remainder > 0.000001 ?
				remainder : step;
		const double delta =
				(deltaY < 0 ? 1.0 : -1.0) * notches * step;
		return std::max(minimum, magnitude + delta);
	};
	if (request_.mode == SectorDesignMode::corridor)
	{
		request_.width = steppedMagnitude(request_.width);
		SetDouble(widthInput_, request_.width);
		Recompute();
		inst_.Status_Set(
				"Corridor width %.1f (mouse wheel uses the %.1f-unit grid)",
				request_.width, step);
		return true;
	}

	// Preserve inward/outward intent while changing the ring thickness. Crossing
	// zero from the wheel would unexpectedly turn an inset into an expansion;
	// type a negative Offset explicitly to choose the outward operation.
	const double direction = request_.offset < 0.0 ? -1.0 : 1.0;
	request_.offset =
			direction * steppedMagnitude(request_.offset);
	SetDouble(offsetInput_, request_.offset);
	Recompute();
	inst_.Status_Set(
			"Inset / Ring offset %.1f %s (mouse wheel uses the %.1f-unit grid)",
			std::abs(request_.offset),
			request_.offset > 0.0 ? "inward" : "outward", step);
	return true;
}

void UI_SectorDesigner::RemoveLastAnchor()
{
	pointerActive_ = false;
	anchorPressArmed_ = false;
	anchorDragMoved_ = false;
	extrudePressArmed_ = false;
	extrudeDragMoved_ = false;
	extrudePressSector_ = -1;
	if (!fixedAnchors_.empty())
	{
		fixedAnchors_.pop_back();
		if ((request_.mode == SectorDesignMode::corridor ||
			 request_.mode == SectorDesignMode::stairs) &&
			endpointLines_.size() > fixedAnchors_.size())
			endpointLines_.pop_back();
	}
	else if (!endpointLines_.empty())
		endpointLines_.pop_back();
	else
	{
		inst_.Status_Set("No Smart Sector anchor to remove");
		return;
	}
	if (request_.mode == SectorDesignMode::extrude)
	{
		request_.extrudeReferenceLine = endpointLines_.empty() ?
				-1 : endpointLines_.front();
		if (endpointLines_.empty())
			extrudeSourceSector_ = -1;
	}
	request_.routeIndex = 0;
	Recompute();
}

void UI_SectorDesigner::Escape()
{
	const bool targetGesture =
			(request_.mode == SectorDesignMode::inset ||
			 request_.mode == SectorDesignMode::stairs ||
			 request_.mode == SectorDesignMode::lift) &&
			(!request_.targetSectors.empty() ||
			 request_.startSector >= 0 || request_.endSector >= 0);
	if (!fixedAnchors_.empty() || !endpointLines_.empty() ||
		pointerActive_ || targetGesture)
	{
		ClearGesture();
		if (targetGesture)
		{
			request_.targetSectors.clear();
			request_.startSector = -1;
			request_.endSector = -1;
			request_.liftTriggerLines.clear();
			request_.restrictLiftTriggers = false;
		}
		Recompute();
		inst_.Status_Set("Gesture cleared; press Escape again to exit");
		return;
	}
	UI_CloseSectorDesigner(inst_);
}

void UI_SectorDesigner::Commit()
{
	if (committing_)
		return;
	committing_ = true;
	struct CommitGuard
	{
		bool &flag;
		~CommitGuard()
		{
			flag = false;
		}
	} guard{committing_};

	auto reportFirstError = [this](const char *prefix)
	{
		auto error = std::find_if(plan_.issues.begin(), plan_.issues.end(),
				[](const SectorDesignIssue &issue)
				{
					return issue.severity ==
							SectorDesignIssueSeverity::error;
				});
		if (error == plan_.issues.end())
		{
			inst_.Beep("%s", prefix);
			return;
		}
		const int row = static_cast<int>(
				error - plan_.issues.begin()) + 1;
		if (row <= issues_->size())
		{
			issues_->select(row);
			issues_->middleline(row);
			const int reviewTop = std::max(
					0, scroll_->yposition() +
						issues_->y() - scroll_->y() - 8);
			scroll_->scroll_to(scroll_->xposition(), reviewTop);
		}
		inst_.Beep("%s: %s", prefix, error->message.c_str());
		// An explicit failed apply deserves more than a clipped browser row.
		// In the real editor, open the nonmodal wrapped review automatically;
		// standalone panel tests have no main window and stay headless.
		if (inst_.main_win)
			ShowReviewDetails();
	};

	// Enter and the button share this path. Recompute first so a changed
	// field, grid state, or document can never apply a stale green preview.
	Recompute();
	if (WaitingForGesture())
	{
		inst_.Status_Set("%s", WaitingPrompt().c_str());
		return;
	}
	if (!plan_.valid())
	{
		reportFirstError("Cannot make sectors");
		return;
	}

	SectorDesignRequest current = CurrentRequest();
	SectorDesignPlan applied;
	try
	{
		if (!M_ApplySectorDesign(inst_.level, inst_.conf, &inst_.wad.images,
								inst_.loaded.levelFormat, current, &applied))
		{
			for (SectorDesignIssue &issue : applied.issues)
			{
				if (issue.sector >= 0 &&
					!inst_.level.isSector(issue.sector))
					issue.sector = -1;
				if (issue.line >= 0 &&
					!inst_.level.isLinedef(issue.line))
					issue.line = -1;
				if (issue.position &&
					(!std::isfinite(issue.position->x) ||
					 !std::isfinite(issue.position->y)))
					issue.position.reset();
			}
			if (applied.valid())
				applied.issues.push_back({
					SectorDesignIssueSeverity::error,
					"The map topology changed while the extrusion was "
					"being applied; adjust the gesture and try again."
				});
			plan_ = std::move(applied);
			PresentPlan(false);
			reportFirstError("Cannot make sectors");
			return;
		}
	}
	catch (const std::exception &error)
	{
		plan_ = {};
		plan_.issues.push_back({
			SectorDesignIssueSeverity::error,
			SString::printf("Application failed safely: %s", error.what())
		});
		PresentPlan(true);
		reportFirstError("Could not make sectors");
		return;
	}
	catch (...)
	{
		plan_ = {};
		plan_.issues.push_back({
			SectorDesignIssueSeverity::error,
			"Application failed safely with an unknown error."
		});
		PresentPlan(true);
		reportFirstError("Could not make sectors");
		return;
	}

	std::vector<int> result = applied.createdSectors;
	if (current.mode == SectorDesignMode::inset)
		for (int sector : current.targetSectors)
			if (std::find(result.begin(), result.end(), sector) ==
				result.end())
				result.push_back(sector);
	if (result.empty())
		result = current.targetSectors;
	result.erase(std::remove_if(result.begin(), result.end(),
			[this](int sector)
			{
				return !inst_.level.isSector(sector);
			}), result.end());
	std::sort(result.begin(), result.end());
	result.erase(std::unique(result.begin(), result.end()), result.end());
	if (!inst_.edit.Selected)
		inst_.edit.Selected.emplace(ObjType::sectors);
	inst_.Selection_Push();
	inst_.edit.Selected->change_type(ObjType::sectors);
	for (int sector : result)
		inst_.edit.Selected->set(sector);

	if (current.mode == SectorDesignMode::inset ||
		current.mode == SectorDesignMode::stairs ||
		current.mode == SectorDesignMode::lift)
	{
		// Selection-driven operations are intentionally one-shot. Keeping
		// their just-created sectors armed made a second accidental Enter
		// inset, stair, or lift them again.
		request_.targetSectors.clear();
		request_.startSector = -1;
		request_.endSector = -1;
		request_.liftTriggerLines.clear();
		request_.restrictLiftTriggers = false;
	}
	else
		request_.targetSectors = result;
	ClearGesture();
	inst_.edit.designAssistPreview.reset();
	if (reviewWindow_)
		reviewWindow_->hide();
	if (current.mode == SectorDesignMode::lift)
		inst_.Status_Set("Made %d smart lift%s",
				std::max(1, applied.plannedLifts),
				applied.plannedLifts == 1 ? "" : "s");
	else if (current.mode == SectorDesignMode::stairs)
		inst_.Status_Set("Made %d smart stair step%s",
				std::max(1, applied.plannedSteps),
				applied.plannedSteps == 1 ? "" : "s");
	else if (current.mode == SectorDesignMode::architecture)
	{
		const char *label =
				M_ArchitectureDescriptor(
						current.architectureElement).label;
		inst_.Status_Set("Built %s (%d architectural sector%s)%s",
				label,
				std::max(1, applied.plannedStructures),
				applied.plannedStructures == 1 ? "" : "s",
				applied.plannedArchitectureHosts > 0 ?
					" with a new host hall" : "");
	}
	else
		inst_.Status_Set("Made %d smart sector%s",
				static_cast<int>(result.size()),
				result.size() == 1 ? "" : "s");
	Recompute();
}

void UI_SectorDesigner::CycleRoute()
{
	if (request_.mode != SectorDesignMode::corridor ||
		fixedAnchors_.size() != 2)
	{
		inst_.Status_Set("Tab cycles routes after placing two corridor endpoints");
		return;
	}
	routeCyclePosition_ = (routeCyclePosition_ + 1) % 4;
	if (!rankedRoutes_.empty())
		request_.routeIndex = rankedRoutes_[routeCyclePosition_];
	Recompute();
	inst_.Status_Set("Corridor route %d of 4", routeCyclePosition_ + 1);
}

void UI_SectorDesigner::Flip()
{
	if (request_.mode == SectorDesignMode::extrude)
	{
		request_.extrudeOpposite = !request_.extrudeOpposite;
		WriteControls();
		Recompute();
		inst_.Status_Set(
				request_.extrudeOpposite ?
					"Extrusion flipped to the opposite side; F restores "
					"the pointer side." :
					"Extrusion follows the pointer; F uses the opposite side.");
		return;
	}
	if (request_.mode == SectorDesignMode::corridor)
	{
		std::reverse(fixedAnchors_.begin(), fixedAnchors_.end());
		std::reverse(endpointLines_.begin(), endpointLines_.end());
		std::swap(request_.startConnection, request_.endConnection);
		WriteControls();
		Recompute();
		return;
	}
	if (request_.mode == SectorDesignMode::architecture &&
		M_ArchitectureHasControl(
				request_.architectureElement,
				SectorArchitectureControl::mirror))
	{
		request_.architectureMirrored =
				!request_.architectureMirrored;
		WriteControls();
		Recompute();
		inst_.Status_Set("%s layout %s",
				M_ArchitectureDescriptor(
						request_.architectureElement).label,
				request_.architectureMirrored ?
					"mirrored" : "restored");
		return;
	}
	inst_.Status_Set(
			"F flips extrusion/corridor direction or a mirrorable structure");
}

void UI_SectorDesigner::Refresh()
{
	if (Active())
	{
		const bool staleLine = std::any_of(
				endpointLines_.begin(), endpointLines_.end(),
				[this](int line)
				{
					return line >= 0 && !inst_.level.isLinedef(line);
				});
		if (staleLine)
			ClearGesture();
		request_.targetSectors.erase(std::remove_if(
				request_.targetSectors.begin(),
				request_.targetSectors.end(),
				[this](int sector)
				{
					return !inst_.level.isSector(sector);
				}), request_.targetSectors.end());
		if (std::find(request_.targetSectors.begin(),
					request_.targetSectors.end(), request_.startSector) ==
				request_.targetSectors.end())
			request_.startSector = -1;
		if (std::find(request_.targetSectors.begin(),
					request_.targetSectors.end(), request_.endSector) ==
				request_.targetSectors.end())
			request_.endSector = -1;
		request_.liftTriggerLines.erase(std::remove_if(
				request_.liftTriggerLines.begin(),
				request_.liftTriggerLines.end(),
				[this](int line)
				{
					return !inst_.level.isLinedef(line);
				}), request_.liftTriggerLines.end());
		Recompute();
	}
}

void UI_SectorDesigner::NavigateToIssue()
{
	if (WaitingForGesture())
	{
		inst_.Status_Set("%s", WaitingPrompt().c_str());
		return;
	}
	const int index = issues_->value() - 1;
	if (index < 0 || index >= static_cast<int>(plan_.issues.size()))
		return;
	const SectorDesignIssue &issue = plan_.issues[index];
	if (issue.position &&
		std::isfinite(issue.position->x) &&
		std::isfinite(issue.position->y))
	{
		inst_.grid.MoveTo(*issue.position);
		inst_.RedrawMap();
	}
	else if (issue.line >= 0 && inst_.level.isLinedef(issue.line))
		inst_.GoToObject(Objid(ObjType::linedefs, issue.line));
	else if (issue.sector >= 0 && inst_.level.isSector(issue.sector))
		inst_.GoToObject(Objid(ObjType::sectors, issue.sector));
	else
		inst_.Status_Set("%s", issue.message.c_str());
}

void UI_SectorDesigner::ReadLiftTriggers()
{
	if (updating_)
		return;
	std::vector<int> selected;
	for (int index = 1; index <= liftTriggers_->size(); index++)
		if (liftTriggers_->selected(index))
			selected.push_back(availableLiftTriggerLines_[index - 1]);
	const bool allSelected =
			selected.size() == availableLiftTriggerLines_.size();
	request_.restrictLiftTriggers = !allSelected;
	request_.liftTriggerLines =
			allSelected ? std::vector<int>{} : std::move(selected);
	Recompute();
	if (plan_.valid())
		inst_.Status_Set(
				"%zu of %zu usable lift portal%s will trigger the platform; "
				"Enter applies.",
				allSelected ? availableLiftTriggerLines_.size() :
					request_.liftTriggerLines.size(),
				availableLiftTriggerLines_.size(),
				availableLiftTriggerLines_.size() == 1 ? "" : "s");
}

void UI_SectorDesigner::ReadDoorSegments()
{
	if (updating_)
		return;
	request_.autoDoorLines = false;
	request_.doorLines.clear();
	for (int index = 1; index <= doorSegments_->size(); ++index)
		if (doorSegments_->selected(index) &&
			index <= static_cast<int>(availableDoorLines_.size()))
			request_.doorLines.push_back(availableDoorLines_[index - 1]);
	doorPlacement_->value(1);
	RefreshModeUI();
	Recompute();
}

void UI_SectorDesigner::ChooseDoorTexture(bool face)
{
	ReadControls();
	SString selected = face ?
			request_.doorOptions.faceTexture :
			request_.doorOptions.trackTexture;
	const SString inferred = face ?
			(plan_.inferredDoorFaceTexture.empty() ?
				inst_.conf.default_wall_tex :
				plan_.inferredDoorFaceTexture) :
			(plan_.inferredDoorTrackTexture.empty() ?
				(plan_.inferredDoorFaceTexture.empty() ?
					inst_.conf.default_wall_tex :
					plan_.inferredDoorFaceTexture) :
				plan_.inferredDoorTrackTexture);
	if (!UI_ChooseSmartDoorTexture(inst_,
			face ? "Door Face Texture" : "Track Wall Texture",
			inferred, selected))
		return;
	if (face)
		request_.doorOptions.faceTexture = selected;
	else
		request_.doorOptions.trackTexture = selected;
	WriteControls();
	Recompute();
}

void UI_SectorDesigner::UseAutoDoorTexture(bool face)
{
	ReadControls();
	if (face)
		request_.doorOptions.useAutoFaceTexture();
	else
		request_.doorOptions.useAutoTrackTexture();
	WriteControls();
	Recompute();
	const SString resolved = face ?
			plan_.doorFaceTexture : plan_.doorTrackTexture;
	inst_.Status_Set("%s Auto resolved to %s",
			face ? "Door face" : "Track wall",
			resolved.empty() ? "(none)" : resolved.c_str());
}

SString UI_SectorDesigner::InferredPropertyTexture(
		PropertyTexture texture) const
{
	int model = -1;
	for (const PlannedSectorShape &shape : plan_.shapes)
		if (inst_.level.isSector(shape.modelSector))
		{
			model = shape.modelSector;
			break;
		}
	if (model < 0 && inst_.level.isSector(request_.properties.modelSector))
		model = request_.properties.modelSector;
	if (model < 0)
		for (int sector : request_.targetSectors)
			if (inst_.level.isSector(sector))
			{
				model = sector;
				break;
			}
	if (model < 0)
		for (int line : endpointLines_)
			if (inst_.level.isLinedef(line))
			{
				const LineDef &linedef = *inst_.level.linedefs[line];
				for (int side : {linedef.right, linedef.left})
					if (inst_.level.isSidedef(side))
					{
						model = inst_.level.sidedefs[side]->sector;
						break;
					}
				if (model >= 0)
					break;
			}

	if (texture == PropertyTexture::floor)
		return inst_.level.isSector(model) ?
				inst_.level.sectors[model]->FloorTex() :
				inst_.conf.default_floor_tex;
	if (texture == PropertyTexture::ceiling)
		return inst_.level.isSector(model) ?
				inst_.level.sectors[model]->CeilTex() :
				inst_.conf.default_ceil_tex;

	if (inst_.level.isSector(model))
		for (int line = 0; line < inst_.level.numLinedefs(); ++line)
		{
			const LineDef &linedef = *inst_.level.linedefs[line];
			for (int side : {linedef.right, linedef.left})
			{
				if (!inst_.level.isSidedef(side) ||
					inst_.level.sidedefs[side]->sector != model)
					continue;
				const SideDef &sidedef = *inst_.level.sidedefs[side];
				for (const SString &candidate :
					 {sidedef.MidTex(), sidedef.UpperTex(),
					  sidedef.LowerTex()})
					if (!candidate.empty() && candidate != "-")
						return candidate;
			}
		}
	return inst_.conf.default_wall_tex;
}

void UI_SectorDesigner::UpdatePropertyTexturePreviews()
{
	const SectorPropertyOptions *properties = &request_.properties;
	if (request_.mode == SectorDesignMode::inset)
	{
		const std::optional<SectorPropertyOptions> &target =
				propertyTargetIndex_ == 0 ?
					request_.ringProperties : request_.innerProperties;
		if (target)
			properties = &*target;
	}

	struct PreviewControl
	{
		PropertyTexture texture;
		const SString *overrideName;
		Fl_Toggle_Button *automatic;
		UI_Pic *preview;
		const char *description;
	};
	const std::array<PreviewControl, 3> controls = {{
		{PropertyTexture::floor, &properties->floorTexture,
		 floorTextureAuto_, floorTexturePreview_, "floor flat"},
		{PropertyTexture::ceiling, &properties->ceilingTexture,
		 ceilingTextureAuto_, ceilingTexturePreview_, "ceiling flat"},
		{PropertyTexture::wall, &properties->wallTexture,
		 wallTextureAuto_, wallTexturePreview_, "wall texture"}
	}};
	for (const PreviewControl &control : controls)
	{
		const bool automatic = control.overrideName->empty();
		const SString resolved = automatic ?
				InferredPropertyTexture(control.texture) :
				*control.overrideName;
		control.automatic->value(automatic ? 1 : 0);
		if (control.texture == PropertyTexture::wall)
			control.preview->GetTex(resolved);
		else
			control.preview->GetFlat(resolved);
		control.preview->copy_tooltip(SString::printf(
				automatic ?
					"Auto currently resolves this %s to %s. "
					"Click to choose an override." :
					"Explicit %s: %s. Click to choose another.",
				control.description,
				resolved.empty() ? "(none)" : resolved.c_str()).c_str());
	}
}

void UI_SectorDesigner::ChoosePropertyTexture(PropertyTexture texture)
{
	ReadControls();
	SectorPropertyOptions *properties = &request_.properties;
	if (request_.mode == SectorDesignMode::inset)
	{
		std::optional<SectorPropertyOptions> &target =
				propertyTargetIndex_ == 0 ?
					request_.ringProperties : request_.innerProperties;
		if (!target)
			target = request_.properties;
		properties = &*target;
	}

	SString *selected = texture == PropertyTexture::floor ?
			&properties->floorTexture :
			texture == PropertyTexture::ceiling ?
				&properties->ceilingTexture :
				&properties->wallTexture;
	const bool flat = texture != PropertyTexture::wall;
	const char *purpose = texture == PropertyTexture::floor ?
			"Sector Floor Flat" :
			texture == PropertyTexture::ceiling ?
				"Sector Ceiling Flat" : "Sector Wall Texture";
	if (!UI_ChooseLoadedImage(
			inst_, flat ? UI_ImageSelectionKind::flat :
						 UI_ImageSelectionKind::wallTexture,
			purpose, InferredPropertyTexture(texture), *selected))
		return;
	WriteControls();
	Recompute();
}

void UI_SectorDesigner::UseAutoPropertyTexture(PropertyTexture texture)
{
	ReadControls();
	SectorPropertyOptions *properties = &request_.properties;
	if (request_.mode == SectorDesignMode::inset)
	{
		std::optional<SectorPropertyOptions> &target =
				propertyTargetIndex_ == 0 ?
					request_.ringProperties : request_.innerProperties;
		if (!target)
			target = request_.properties;
		properties = &*target;
	}
	SString *selected = texture == PropertyTexture::floor ?
			&properties->floorTexture :
			texture == PropertyTexture::ceiling ?
				&properties->ceilingTexture :
				&properties->wallTexture;
	selected->clear();
	WriteControls();
	Recompute();
	const SString resolved = InferredPropertyTexture(texture);
	inst_.Status_Set("%s Auto resolves to %s",
			texture == PropertyTexture::floor ? "Floor flat" :
			texture == PropertyTexture::ceiling ? "Ceiling flat" :
				"Wall texture",
			resolved.empty() ? "(none)" : resolved.c_str());
}

void UI_SectorDesigner::ChooseSectorSpecial()
{
	if (!inst_.main_win)
	{
		inst_.Beep("The sector-special browser is unavailable");
		return;
	}
	inst_.main_win->BrowserMode(BrowserMode::sectorTypes);
	if (const std::optional<int> special = ReadOptionalInt(sectorType_))
		inst_.main_win->browser->JumpToValue(*special);
}

void UI_SectorDesigner::UpdateSectorSpecialDescription()
{
	if (!sectorTypeDescription_)
		return;
	if (InvalidOptionalInt(sectorType_))
	{
		sectorTypeDescription_->value("Enter a whole number or choose one");
		return;
	}
	const std::optional<int> special = ReadOptionalInt(sectorType_);
	if (!special)
	{
		sectorTypeDescription_->value("Auto - inherit or preserve context");
		return;
	}
	const auto found = inst_.conf.sector_types.find(*special);
	if (found != inst_.conf.sector_types.end() &&
		!found->second.desc.empty())
		sectorTypeDescription_->value(SString::printf(
				"%d - %s", *special, found->second.desc.c_str()).c_str());
	else if (*special == 0)
		sectorTypeDescription_->value("0 - Normal / no special");
	else
		sectorTypeDescription_->value(SString::printf(
				"%d - Custom / undeclared special", *special).c_str());
}

void UI_SectorDesigner::BrowsedItem(
		BrowserMode kind, int number, const char *, int)
{
	if (kind != BrowserMode::sectorTypes)
		return;
	SetInt(sectorType_, number);
	UpdateSectorSpecialDescription();
	Recompute();
	if (inst_.main_win && inst_.main_win->canvas)
		inst_.main_win->canvas->take_focus();
}

void UI_SectorDesigner::ChangePropertyTarget()
{
	if (updating_)
		return;
	ReadControls();
	propertyTargetIndex_ = propertyTarget_->value();
	WriteControls();
	Recompute();
}

void UI_SectorDesigner::optionCallback(Fl_Widget *widget, void *data)
{
	UI_SectorDesigner *panel =
			static_cast<UI_SectorDesigner *>(data);
	if (widget == panel->architectureStyle_)
	{
		panel->ReadControls();
		const double minimum = M_MinimumArchitectureSize(
				panel->request_.architectureStyle,
				panel->request_.architectureElement);
		if (panel->request_.architectureSize < minimum)
		{
			panel->request_.architectureSize = minimum;
			SetDouble(panel->architectureSize_, minimum);
		}
	}
	panel->UpdateSectorSpecialDescription();
	panel->Recompute();
}

void UI_SectorDesigner::connectionCallback(Fl_Widget *widget, void *data)
{
	UI_SectorDesigner *panel =
			static_cast<UI_SectorDesigner *>(data);
	if (panel->updating_)
		return;
	// ReadControls performs the architecture menu transition atomically,
	// including a defensive resynchronization when FLTK or a platform backend
	// changes a choice value without delivering the expected callback.
	panel->ReadControls();
	if (widget == panel->architectureElement_ ||
		widget == panel->architectureFamily_)
	{
		panel->ClearGesture();
		const double minimum = M_MinimumArchitectureSize(
				panel->request_.architectureStyle,
				panel->request_.architectureElement);
		if (panel->request_.architectureSize < minimum)
		{
			panel->request_.architectureSize = minimum;
			SetDouble(panel->architectureSize_, minimum);
		}
	}
	panel->RefreshModeUI();
	panel->Recompute();
}

void UI_SectorDesigner::modeCallback(Fl_Widget *, void *data)
{
	UI_SectorDesigner *panel = static_cast<UI_SectorDesigner *>(data);
	if (panel->updating_)
		return;
	const int chosen = panel->modeChoice_->value();
	if (chosen < 0 || chosen >= MODE_COUNT)
	{
		panel->modeChoice_->value(
				static_cast<int>(panel->request_.mode));
		panel->inst_.Beep("Unknown Smart Sector mode");
		return;
	}
	panel->ChangeMode(static_cast<SectorDesignMode>(chosen));
}

void UI_SectorDesigner::propertyTargetCallback(Fl_Widget *, void *data)
{
	static_cast<UI_SectorDesigner *>(data)->ChangePropertyTarget();
}

void UI_SectorDesigner::issueCallback(Fl_Widget *, void *data)
{
	static_cast<UI_SectorDesigner *>(data)->NavigateToIssue();
}

void UI_SectorDesigner::liftTriggerCallback(Fl_Widget *, void *data)
{
	static_cast<UI_SectorDesigner *>(data)->ReadLiftTriggers();
}

void UI_SectorDesigner::doorSegmentCallback(Fl_Widget *, void *data)
{
	static_cast<UI_SectorDesigner *>(data)->ReadDoorSegments();
}

void UI_SectorDesigner::doorTextureCallback(Fl_Widget *widget, void *data)
{
	UI_SectorDesigner *panel =
			static_cast<UI_SectorDesigner *>(data);
	panel->ChooseDoorTexture(
			widget == panel->faceTextureButton_ ||
			widget == panel->facePreview_);
}

void UI_SectorDesigner::doorTextureAutoCallback(
		Fl_Widget *widget, void *data)
{
	UI_SectorDesigner *panel =
			static_cast<UI_SectorDesigner *>(data);
	panel->UseAutoDoorTexture(widget == panel->faceAutoButton_);
}

void UI_SectorDesigner::propertyTextureCallback(
		Fl_Widget *widget, void *data)
{
	UI_SectorDesigner *panel =
			static_cast<UI_SectorDesigner *>(data);
	if (widget == panel->floorTextureButton_ ||
		widget == panel->floorTexturePreview_)
		panel->ChoosePropertyTexture(PropertyTexture::floor);
	else if (widget == panel->ceilingTextureButton_ ||
			 widget == panel->ceilingTexturePreview_)
		panel->ChoosePropertyTexture(PropertyTexture::ceiling);
	else
		panel->ChoosePropertyTexture(PropertyTexture::wall);
}

void UI_SectorDesigner::propertyTextureAutoCallback(
		Fl_Widget *widget, void *data)
{
	UI_SectorDesigner *panel =
			static_cast<UI_SectorDesigner *>(data);
	if (widget == panel->floorTextureAuto_)
		panel->UseAutoPropertyTexture(PropertyTexture::floor);
	else if (widget == panel->ceilingTextureAuto_)
		panel->UseAutoPropertyTexture(PropertyTexture::ceiling);
	else
		panel->UseAutoPropertyTexture(PropertyTexture::wall);
}

void UI_SectorDesigner::sectorSpecialCallback(Fl_Widget *, void *data)
{
	static_cast<UI_SectorDesigner *>(data)->ChooseSectorSpecial();
}

void UI_SectorDesigner::copyReviewCallback(Fl_Widget *, void *data)
{
	static_cast<UI_SectorDesigner *>(data)->CopyReview();
}

void UI_SectorDesigner::expandReviewCallback(Fl_Widget *, void *data)
{
	static_cast<UI_SectorDesigner *>(data)->ShowReviewDetails();
}

void UI_SectorDesigner::reviewWindowCallback(Fl_Widget *, void *data)
{
	UI_SectorDesigner *panel = static_cast<UI_SectorDesigner *>(data);
	if (panel->reviewWindow_)
		panel->reviewWindow_->hide();
	if (panel->inst_.main_win && panel->inst_.main_win->canvas)
		panel->inst_.main_win->canvas->take_focus();
}

void UI_SectorDesigner::commitCallback(Fl_Widget *, void *data)
{
	static_cast<UI_SectorDesigner *>(data)->Commit();
}

void UI_SectorDesigner::closeCallback(Fl_Widget *, void *data)
{
	UI_SectorDesigner *panel = static_cast<UI_SectorDesigner *>(data);
	UI_CloseSectorDesigner(panel->inst_);
}

void UI_OpenSectorDesigner(Instance &inst, SectorDesignMode mode)
{
	if (UI_SmartSectorOpen_Override)
	{
		UI_SmartSectorOpen_Override(inst, mode);
		return;
	}
	if (inst.main_win)
		inst.main_win->ShowSectorDesigner(mode);
}

void UI_CloseSectorDesigner(Instance &inst)
{
	if (inst.main_win)
		inst.main_win->HideSectorDesigner();
	else
	{
		inst.edit.designAssistPreview.reset();
		if (inst.edit.action == EditorAction::designSector)
			inst.Editor_ClearAction();
		inst.RedrawMap();
	}
}

bool UI_SectorDesignerActive(const Instance &inst)
{
	return inst.main_win && inst.main_win->sector_design_box &&
		   inst.main_win->sector_design_box->Active();
}

bool UI_SectorDesignerOwnsCanvas(const Instance &inst)
{
	return UI_SectorDesignerActive(inst) && inst.main_win->canvas &&
			Fl::focus() == inst.main_win->canvas;
}

void UI_SectorDesignerCanvasClick(Instance &inst, const v2double_t &point,
								  keycode_t modifiers)
{
	if (UI_SectorDesignerActive(inst))
		inst.main_win->sector_design_box->CanvasClick(point, modifiers);
}

void UI_SectorDesignerCanvasMove(Instance &inst, const v2double_t &point,
								 keycode_t modifiers,
								 bool primaryButtonDown)
{
	if (UI_SectorDesignerActive(inst))
		inst.main_win->sector_design_box->CanvasMove(
				point, modifiers, primaryButtonDown);
}

void UI_SectorDesignerCanvasRelease(Instance &inst,
									const v2double_t &point,
									keycode_t modifiers)
{
	if (UI_SectorDesignerActive(inst))
		inst.main_win->sector_design_box->CanvasRelease(point, modifiers);
}

bool UI_SectorDesignerCanvasKey(Instance &inst, keycode_t key)
{
	return UI_SectorDesignerOwnsCanvas(inst) &&
			inst.main_win->sector_design_box->CanvasKey(key);
}

bool UI_SectorDesignerCanvasWheel(Instance &inst, int deltaY)
{
	return UI_SectorDesignerActive(inst) &&
			inst.main_win->sector_design_box->CanvasWheel(deltaY);
}

void UI_SectorDesignerRemoveLastAnchor(Instance &inst)
{
	if (UI_SectorDesignerActive(inst))
		inst.main_win->sector_design_box->RemoveLastAnchor();
}

void UI_SectorDesignerEscape(Instance &inst)
{
	if (UI_SectorDesignerOwnsCanvas(inst))
		inst.main_win->sector_design_box->Escape();
}

void UI_SectorDesignerCommit(Instance &inst)
{
	if (UI_SectorDesignerOwnsCanvas(inst))
		inst.main_win->sector_design_box->Commit();
}

void UI_SectorDesignerCycleRoute(Instance &inst)
{
	if (UI_SectorDesignerOwnsCanvas(inst))
		inst.main_win->sector_design_box->CycleRoute();
}

void UI_SectorDesignerFlip(Instance &inst)
{
	if (UI_SectorDesignerOwnsCanvas(inst))
		inst.main_win->sector_design_box->Flip();
}

void UI_SectorDesignerRefresh(Instance &inst)
{
	if (UI_SectorDesignerOwnsCanvas(inst))
		inst.main_win->sector_design_box->Refresh();
}

void UI_SetSectorDesignPreview(Instance &inst,
							   const SectorDesignPlan &plan,
							   const std::vector<int> &retainedSectors)
{
	DesignAssistPreview preview;
	const v2double_t origin = inst.grid.getOrig();
	const double scale = inst.grid.getScale();
	const double screenLimit =
			static_cast<double>(std::numeric_limits<int>::max()) / 8.0;
	auto renderable = [&](const v2double_t &point)
	{
		if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
			!std::isfinite(scale) || scale <= 0.0)
			return false;
		const double screenX = (point.x - origin.x) * scale;
		const double screenY = (point.y - origin.y) * scale;
		return std::isfinite(screenX) && std::isfinite(screenY) &&
				std::abs(screenX) <= screenLimit &&
				std::abs(screenY) <= screenLimit;
	};
	for (const DesignPreviewPath &path : plan.previewPaths)
		if (path.points.size() >= 2 &&
			std::all_of(path.points.begin(), path.points.end(), renderable))
			preview.paths.push_back(path);
	for (const DesignPreviewPoint &point : plan.previewPoints)
		if (renderable(point.position))
			preview.points.push_back(point);
	for (const SectorDesignIssue &issue : plan.issues)
	{
		const DesignPreviewRole role =
				issue.severity == SectorDesignIssueSeverity::error ?
					DesignPreviewRole::conflict :
					DesignPreviewRole::warning;
		if (issue.position && renderable(*issue.position))
			preview.points.push_back({*issue.position, role});
		if (issue.line >= 0 && inst.level.isLinedef(issue.line))
		{
			const LineDef &line = *inst.level.linedefs[issue.line];
			if (inst.level.isVertex(line.start) &&
				inst.level.isVertex(line.end))
			{
				const std::vector<v2double_t> points{
					inst.level.getStart(line).xy(),
					inst.level.getEnd(line).xy()
				};
				if (std::all_of(
						points.begin(), points.end(), renderable))
					preview.paths.push_back({
						points, role, false, false
					});
			}
		}
	}
	for (const DesignPreviewLabel &label : plan.previewLabels)
		if (renderable(label.position))
			preview.labels.push_back(label);
	for (int sector : retainedSectors)
		if (inst.level.isSector(sector))
			preview.sectors.set(sector);
	inst.edit.designAssistPreview.emplace(std::move(preview));
	inst.RedrawMap();
}
