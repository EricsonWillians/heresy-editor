//------------------------------------------------------------------------
//  SMART SECTOR DESIGNER
//------------------------------------------------------------------------

#ifndef __EUREKA_E_SECTOR_DESIGN_H__
#define __EUREKA_E_SECTOR_DESIGN_H__

#include "e_design.h"
#include "e_door.h"
#include "m_game.h"
#include "m_vector.h"

#include <cstdint>
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
	fortifiedKeep,

	// Volumetric structures. Unlike the support layouts above, these own
	// walkable floor, wall, ceiling, or water-shaped sector geometry.
	raisedDais,
	sunkenCourt,
	tieredZiggurat,
	grandStair,
	fountainBasin,
	reflectingPool,
	balconyGallery,
	processionalChannel,
	screenWall,
	cofferedCeiling,
	groinVaults,
	raisedBridge,

	// Stage 25 additions. Keep these appended: the ordered descriptor catalog
	// and transient per-element UI memory use the existing enum order.
	crossCore,
	hollowTower,
	buttressedTower,
	shearWallPair,
	steppedMonument,
	centralPlatform,
	splitLevelStage,
	cornerTerraces,
	octagonalPodium,
	horseshoeAmphitheater,
	switchbackStair,
	bifurcatedStair,
	spiralStair,
	landingCatwalk,
	crossingBridges,
	perimeterMoat,
	crossCanal,
	twinCanals,
	steppedCascade,
	fountainCourt,
	partitionWall,
	crenellatedWall,
	buttressedWall,
	staggeredScreen,
	gatehousePassage,
	trayCeiling,
	barrelVault,
	ribbedCrossVault,
	domedCeiling,
	beamLattice
};

enum class SectorArchitectureFamily
{
	structuralSupports,
	floorsTerraces,
	circulation,
	waterworks,
	wallsScreens,
	ceilingsVaults
};

enum class SectorArchitectureFunction
{
	staticGeometry,
	smartLift
};

enum class SectorArchitectureControl : std::uint32_t
{
	none = 0,
	style = 1u << 0,
	bays = 1u << 1,
	size = 1u << 2,
	height = 1u << 3,
	margin = 1u << 4,
	mirror = 1u << 5,
	function = 1u << 6
};

struct SectorArchitectureDescriptor
{
	const char *id = "";
	SectorArchitectureElement element = SectorArchitectureElement::pillar;
	SectorArchitectureFamily family =
			SectorArchitectureFamily::structuralSupports;
	const char *label = "";
	const char *description = "";
	DesignPreviewRole role = DesignPreviewRole::architecture;
	std::uint32_t controls = 0;
	std::uint32_t functions = 1;
	const char *baysLabel = "Bays:";
	const char *sizeLabel = "Struct. size:";
	const char *heightLabel = "Elevation:";
	int defaultBays = 4;
	int minimumBays = 1;
	int maximumBays = 32;
	double defaultSize = 24.0;
	double defaultHeight = 16.0;
	double defaultMargin = 16.0;
	double minimumSize = 1.0;
	// Width-like controls that must start at a traversable player clearance.
	// Thicknesses, treads, borders, radii, and ornamental relief deliberately
	// retain their descriptor default instead of inheriting an unsuitable
	// player diameter.
	bool sizeUsesPlayerClearance = false;
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
	// Transient construction axes supplied by the UI when mathematical grid
	// snapping is active. Room planning uses them to build a rotated rectangle
	// or oblique parallelogram instead of snapping only two opposite corners.
	bool useConstructionBasis = false;
	v2double_t constructionPrimary = {1.0, 0.0};
	v2double_t constructionSecondary = {0.0, 1.0};

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
	double architectureHeight = 16.0;
	double architectureMargin = 16.0;
	bool architectureMirrored = false;
	SectorArchitectureFunction architectureFunction =
			SectorArchitectureFunction::staticGeometry;

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
	// Closed architectural inserts such as supports or screen walls collapse
	// their ceiling to their resolved floor. Walkable and ceiling structures
	// leave this false and use the explicit deltas above.
	bool closed = false;
	bool inheritModelOnly = false;
	std::optional<SectorPropertyOptions> properties;
	// Most generated holes preserve void. Overhead lattices instead cut the
	// connected beam sector into the host room and retain that room's
	// properties in every opening. Kept at the aggregate tail so existing
	// plan builders retain source compatibility.
	bool holesRetainModel = false;
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
	int plannedRetainedCells = 0;

	bool valid() const;
};

std::vector<SectorActionPreset> M_AvailableSectorActionPresets(
		const ConfigData &config, SectorActionKind kind, MapFormat format);
SString M_SectorActionPresetLabel(const ConfigData &config,
								  const SectorActionPreset &preset);

// Small intricate sections can collapse into ambiguous loops after map-format
// quantization. The compatibility overload retains the section-style bound;
// the element-aware overload also serves purpose-built structures. The
// planner and canvas wheel share it so preview and apply agree.
double M_MinimumArchitectureSize(SectorArchitectureStyle style);
double M_MinimumArchitectureSize(
		SectorArchitectureStyle style,
		SectorArchitectureElement element);
bool M_ArchitectureUsesSectionStyle(SectorArchitectureElement element);
bool M_ArchitectureUsesBays(SectorArchitectureElement element);
bool M_ArchitectureUsesHeight(SectorArchitectureElement element);
const std::vector<SectorArchitectureDescriptor> &M_ArchitectureCatalog();
const SectorArchitectureDescriptor &M_ArchitectureDescriptor(
		SectorArchitectureElement element);
bool M_ArchitectureHasControl(SectorArchitectureElement element,
		SectorArchitectureControl control);
bool M_ArchitectureSupportsFunction(SectorArchitectureElement element,
		SectorArchitectureFunction function);
const char *M_ArchitectureFamilyLabel(SectorArchitectureFamily family);
SString M_ArchitectureEffectDescription(
		const SectorDesignRequest &request);

SectorDesignPlan M_PlanSectorDesign(const Document &doc,
		const ConfigData &config, const ImageSet *images, MapFormat format,
		const SectorDesignRequest &request);

bool M_ApplySectorDesign(Document &doc, const ConfigData &config,
		const ImageSet *images, MapFormat format,
		const SectorDesignRequest &request,
		SectorDesignPlan *appliedPlan = nullptr);

#endif
