//------------------------------------------------------------------------
//  SMART SECTOR DESIGNER
//------------------------------------------------------------------------

#ifndef __EUREKA_E_SECTOR_DESIGN_H__
#define __EUREKA_E_SECTOR_DESIGN_H__

#include "e_design.h"
#include "e_door.h"
#include "m_game.h"
#include "m_vector.h"

#include <optional>
#include <vector>

struct Document;
class ImageSet;

enum class SectorDesignMode
{
	room,
	polygon,
	freeform,
	extrude,
	inset,
	corridor,
	stairs,
	lift,
	architecture
};

// Ordered from basic convex primitives to high-detail concave profiles.
// Fixed profiles ignore polygonSides; customRegular keeps the editable
// 3-through-64 side count.
enum class SectorPolygonProfile
{
	customRegular,
	triangle,
	square,
	pentagon,
	hexagon,
	octagon,
	decagon,
	dodecagon,
	circle16,
	circle32,
	circle64,
	circle96,
	star5,
	star8,
	star12,
	star16,
	greekCross,
	malteseCross,
	gear8,
	gear16,
	gear24,
	sawblade32,
	trefoil,
	quatrefoil,
	cinquefoil,
	octofoil,
	rosette8,
	rosette16,
	rosette24,
	cathedralTracery32,
	cathedralTracery48
};

enum class SectorArchitectureStyle
{
	functional,
	classical,
	romanesque,
	gothic,
	industrial,
	artDeco,
	infernal
};

enum class SectorArchitectureElement
{
	pillar,
	pairedPillars,
	triumphalArch,
	cornerPiers,
	colonnade,
	arcade,
	cloister,
	hypostyleHall,
	buttressedBay,
	flyingButtresses,
	nave,
	transept,
	apse,
	rotunda,
	sanctuary,
	fortifiedKeep
};

enum class SectorDesignJoin
{
	miter,
	bevel,
	round
};

enum class SectorConnection
{
	open,
	door,
	wall
};

enum class SectorValueMode
{
	automatic,
	absolute,
	relative
};

enum class SectorDesignIssueSeverity
{
	warning,
	error
};

struct SectorPropertyOptions
{
	int modelSector = -1;
	SectorValueMode floorMode = SectorValueMode::automatic;
	SectorValueMode ceilingMode = SectorValueMode::automatic;
	SectorValueMode lightMode = SectorValueMode::automatic;
	int floorValue = 0;
	int ceilingValue = 0;
	int lightValue = 0;
	SString floorTexture;
	SString ceilingTexture;
	SString wallTexture;
	std::optional<int> sectorType;
	std::optional<int> sectorTag;
};

struct SectorDesignRequest
{
	SectorDesignMode mode = SectorDesignMode::room;
	std::vector<v2double_t> anchors;
	std::vector<int> anchorLines;
	std::vector<int> targetSectors;

	double width = 64.0;
	double depth = 128.0;
	double offset = 16.0;
	double rotation = 0.0;
	int polygonSides = 8;
	SectorPolygonProfile polygonProfile =
			SectorPolygonProfile::customRegular;
	double polygonInnerRatio = 0.5;
	SectorDesignJoin join = SectorDesignJoin::miter;
	int roundSegments = 8;
	int routeIndex = 0;
	bool extrudeUseDragDepth = true;
	// Drag extrusion normally follows the pointer. This explicit inversion is
	// preferable to reflecting UI coordinates: it keeps preview and apply on
	// the same side for reversed lines and multi-line boundary chains.
	bool extrudeOpposite = false;
	// Locks drag measurement to a stable member of a selected boundary chain.
	// A negative value lets the planner choose the closest segment.
	int extrudeReferenceLine = -1;

	int stairCount = 0;
	double stairRise = 8.0;
	double stairTread = 16.0;
	bool stairFitTarget = false;
	int stairTargetFloor = 0;
	bool preserveHeadroom = true;
	int startSector = -1;
	int endSector = -1;

	SectorArchitectureStyle architectureStyle =
			SectorArchitectureStyle::gothic;
	SectorArchitectureElement architectureElement =
			SectorArchitectureElement::pillar;
	// Transient host locked from the first unsnapped Architecture press.
	// A negative value lets the pure planner infer the host from anchor zero.
	int architectureHostSector = -1;
	int architectureBays = 4;
	double architectureSize = 24.0;
	double architectureMargin = 16.0;

	SectorConnection startConnection = SectorConnection::open;
	SectorConnection endConnection = SectorConnection::open;
	double doorDepth = 16.0;
	double doorWidth = 0.0;
	double doorOffset = 0.0;
	bool autoDoorLines = true;
	std::vector<int> doorLines;
	DoorOptions doorOptions;

	SString actionPresetId;
	// Empty selects every usable portal; otherwise only these portal lines
	// receive the lift action.
	std::vector<int> liftTriggerLines;
	bool restrictLiftTriggers = false;
	SectorPropertyOptions properties;
	// Inset/ring may independently style the existing inner cell and the
	// generated ring. Unset models fall back to contextual preservation.
	std::optional<SectorPropertyOptions> ringProperties;
	std::optional<SectorPropertyOptions> innerProperties;
	bool replaceAffectedSectors = false;
};

struct SectorDesignIssue
{
	SectorDesignIssueSeverity severity = SectorDesignIssueSeverity::warning;
	SString message;
	int sector = -1;
	int line = -1;
	std::optional<v2double_t> position;
};

struct PlannedSectorShape
{
	std::vector<v2double_t> outer;
	std::vector<std::vector<v2double_t>> holes;
	DesignPreviewRole role = DesignPreviewRole::proposed;
	int modelSector = -1;
	// Plan-local reference to an earlier generated shape. This lets structural
	// inserts inherit a host room created by the same atomic operation.
	int modelShape = -1;
	int floorDelta = 0;
	int ceilingDelta = 0;
	bool smartDoor = false;
	bool smartLift = false;
	// Architectural inserts such as pillars are real sectors whose ceiling
	// is collapsed to their resolved floor during the atomic apply.
	bool closed = false;
	bool inheritModelOnly = false;
	std::optional<SectorPropertyOptions> properties;
};

struct PlannedSectorChange
{
	int sector = -1;
	std::optional<int> floor;
	std::optional<int> ceiling;
	std::optional<int> light;
	std::optional<int> type;
	std::optional<int> tag;
	std::optional<SString> floorTexture;
	std::optional<SString> ceilingTexture;
};

struct PlannedLift
{
	std::vector<int> sectors;
	std::vector<int> triggerLines;
	int tag = 0;
	int lowerStop = 0;
	int travel = 0;
	SectorActionPreset preset;
};

struct PlannedConnectionChange
{
	int line = -1;
	SectorConnection connection = SectorConnection::open;
};

struct SectorDesignPlan
{
	std::vector<PlannedSectorShape> shapes;
	std::vector<PlannedSectorChange> sectorChanges;
	std::vector<PlannedLift> lifts;
	std::vector<PlannedConnectionChange> connections;
	std::vector<SectorDesignIssue> issues;
	std::vector<DesignPreviewPath> previewPaths;
	std::vector<DesignPreviewPoint> previewPoints;
	std::vector<DesignPreviewLabel> previewLabels;
	std::vector<int> retainedSectors;
	std::vector<int> doorSourceLines;
	DoorOptions resolvedDoorOptions;
	SString inferredDoorFaceTexture;
	SString inferredDoorTrackTexture;
	SString doorFaceTexture;
	SString doorTrackTexture;
	std::optional<SectorActionPreset> resolvedActionPreset;
	int extrudeReferenceLine = -1;
	double resolvedExtrudeDepth = 0.0;
	bool extrudeOpposite = false;

	std::vector<int> createdSectors;
	int reusedVertices = 0;
	int plannedLines = 0;
	int plannedSplits = 0;
	int plannedDoors = 0;
	int plannedSteps = 0;
	int plannedLifts = 0;
	int plannedStructures = 0;
	int plannedArchitectureHosts = 0;

	bool valid() const;
};

std::vector<SectorActionPreset> M_AvailableSectorActionPresets(
		const ConfigData &config, SectorActionKind kind, MapFormat format);
SString M_SectorActionPresetLabel(const ConfigData &config,
								  const SectorActionPreset &preset);

// Small intricate sections can collapse into ambiguous loops after map-format
// quantization. The planner and canvas wheel share this style-aware lower
// bound so preview and apply agree about usable structural dimensions.
double M_MinimumArchitectureSize(SectorArchitectureStyle style);

SectorDesignPlan M_PlanSectorDesign(const Document &doc,
		const ConfigData &config, const ImageSet *images, MapFormat format,
		const SectorDesignRequest &request);

bool M_ApplySectorDesign(Document &doc, const ConfigData &config,
		const ImageSet *images, MapFormat format,
		const SectorDesignRequest &request,
		SectorDesignPlan *appliedPlan = nullptr);

#endif
