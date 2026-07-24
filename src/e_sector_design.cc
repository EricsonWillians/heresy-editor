//------------------------------------------------------------------------
//  SMART SECTOR DESIGNER
//------------------------------------------------------------------------

#include "e_sector_design.h"

#include "Document.h"
#include "LineDef.h"
#include "Sector.h"
#include "SideDef.h"
#include "Vertex.h"
#include "WadData.h"
#include "WindowsSanitization.h"
#include "e_hover.h"
#include "e_objects.h"
#include "w_rawdef.h"

#include <clipper2/clipper.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <queue>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace
{

using namespace Clipper2Lib;

constexpr double UDMF_SCALE = 65536.0;
constexpr double PLAN_EPSILON = 1.0 / UDMF_SCALE;

int SectorForSide(const Document &doc, int side);
DesignPreviewRole ArchitecturePreviewRole(
		SectorArchitectureElement element);
bool IsArchitecturePreviewRole(DesignPreviewRole role);

constexpr std::uint32_t ArchitectureControlBit(
		SectorArchitectureControl control)
{
	return static_cast<std::uint32_t>(control);
}

constexpr std::uint32_t ArchitectureFunctionBit(
		SectorArchitectureFunction function)
{
	return 1u << static_cast<unsigned>(function);
}

constexpr std::uint32_t A_STYLE =
		ArchitectureControlBit(SectorArchitectureControl::style);
constexpr std::uint32_t A_BAYS =
		ArchitectureControlBit(SectorArchitectureControl::bays);
constexpr std::uint32_t A_SIZE =
		ArchitectureControlBit(SectorArchitectureControl::size);
constexpr std::uint32_t A_HEIGHT =
		ArchitectureControlBit(SectorArchitectureControl::height);
constexpr std::uint32_t A_MARGIN =
		ArchitectureControlBit(SectorArchitectureControl::margin);
constexpr std::uint32_t A_MIRROR =
		ArchitectureControlBit(SectorArchitectureControl::mirror);
constexpr std::uint32_t A_FUNCTION =
		ArchitectureControlBit(SectorArchitectureControl::function);
constexpr std::uint32_t A_STATIC =
		ArchitectureFunctionBit(
				SectorArchitectureFunction::staticGeometry);
constexpr std::uint32_t A_SMART_LIFT =
		ArchitectureFunctionBit(SectorArchitectureFunction::smartLift);

const std::vector<SectorArchitectureDescriptor> &ArchitectureCatalogData()
{
	static const std::vector<SectorArchitectureDescriptor> catalog =
	{
		{"pillar", SectorArchitectureElement::pillar,
		 SectorArchitectureFamily::structuralSupports, "Single pillar",
		 "One style-aware solid support.", DesignPreviewRole::architecture,
		 A_STYLE | A_SIZE | A_MARGIN, A_STATIC,
		 "Bays:", "Diameter:", "Elevation:", 1, 1, 1, 24, 16, 16, 4},
		{"paired_pillars", SectorArchitectureElement::pairedPillars,
		 SectorArchitectureFamily::structuralSupports, "Paired pillars",
		 "Two style-aware supports across the short axis.",
		 DesignPreviewRole::architecture,
		 A_STYLE | A_SIZE | A_MARGIN, A_STATIC,
		 "Bays:", "Diameter:", "Elevation:", 2, 1, 2, 24, 16, 16, 4},
		{"triumphal_arch", SectorArchitectureElement::triumphalArch,
		 SectorArchitectureFamily::structuralSupports,
		 "Triumphal arch piers",
		 "A monumental pair of enlarged passage piers.",
		 DesignPreviewRole::architecture,
		 A_STYLE | A_SIZE | A_MARGIN, A_STATIC,
		 "Bays:", "Pier size:", "Elevation:", 2, 1, 2, 24, 16, 16, 4},
		{"corner_piers", SectorArchitectureElement::cornerPiers,
		 SectorArchitectureFamily::structuralSupports, "Four corner piers",
		 "Four enlarged style-aware corner supports.",
		 DesignPreviewRole::architecture,
		 A_STYLE | A_SIZE | A_MARGIN, A_STATIC,
		 "Bays:", "Pier size:", "Elevation:", 4, 1, 4, 24, 16, 16, 4},
		{"colonnade", SectorArchitectureElement::colonnade,
		 SectorArchitectureFamily::structuralSupports, "Linear colonnade",
		 "One repeated row of style-aware columns.",
		 DesignPreviewRole::architecture,
		 A_STYLE | A_BAYS | A_SIZE | A_MARGIN, A_STATIC,
		 "Columns:", "Diameter:", "Elevation:", 4, 1, 32, 24, 16, 16, 4},
		{"arcade", SectorArchitectureElement::arcade,
		 SectorArchitectureFamily::structuralSupports, "Double-row arcade",
		 "Two repeated rows framing a central aisle.",
		 DesignPreviewRole::architecture,
		 A_STYLE | A_BAYS | A_SIZE | A_MARGIN, A_STATIC,
		 "Bays:", "Pier size:", "Elevation:", 4, 1, 32, 24, 16, 16, 4},
		{"cloister", SectorArchitectureElement::cloister,
		 SectorArchitectureFamily::structuralSupports, "Perimeter cloister",
		 "Repeated supports around the complete footprint.",
		 DesignPreviewRole::architecture,
		 A_STYLE | A_BAYS | A_SIZE | A_MARGIN, A_STATIC,
		 "Bays/side:", "Pier size:", "Elevation:", 4, 1, 16, 24, 16, 16, 4},
		{"hypostyle_hall", SectorArchitectureElement::hypostyleHall,
		 SectorArchitectureFamily::structuralSupports,
		 "Hypostyle column hall",
		 "A dense repeated support grid.", DesignPreviewRole::architecture,
		 A_STYLE | A_BAYS | A_SIZE | A_MARGIN, A_STATIC,
		 "Columns/row:", "Diameter:", "Elevation:", 4, 1, 16, 24, 16, 16, 4},
		{"buttressed_bay", SectorArchitectureElement::buttressedBay,
		 SectorArchitectureFamily::structuralSupports, "Buttressed bay",
		 "Repeated interior supports with heavier exterior buttresses.",
		 DesignPreviewRole::architecture,
		 A_STYLE | A_BAYS | A_SIZE | A_MARGIN, A_STATIC,
		 "Bays:", "Support size:", "Elevation:", 4, 1, 16, 24, 16, 16, 4},
		{"flying_buttresses", SectorArchitectureElement::flyingButtresses,
		 SectorArchitectureFamily::structuralSupports,
		 "Flying-buttress array",
		 "Paired inner and outer buttress stations.",
		 DesignPreviewRole::architecture,
		 A_STYLE | A_BAYS | A_SIZE | A_MARGIN, A_STATIC,
		 "Bays:", "Support size:", "Elevation:", 4, 1, 16, 24, 16, 16, 4},
		{"nave", SectorArchitectureElement::nave,
		 SectorArchitectureFamily::structuralSupports, "Four-row nave",
		 "Four longitudinal support rows with a broad center aisle.",
		 DesignPreviewRole::architecture,
		 A_STYLE | A_BAYS | A_SIZE | A_MARGIN, A_STATIC,
		 "Bays:", "Pier size:", "Elevation:", 4, 1, 16, 24, 16, 16, 4},
		{"transept", SectorArchitectureElement::transept,
		 SectorArchitectureFamily::structuralSupports,
		 "Cruciform transept",
		 "Crossing support rows for a cruciform hall.",
		 DesignPreviewRole::architecture,
		 A_STYLE | A_BAYS | A_SIZE | A_MARGIN, A_STATIC,
		 "Stations:", "Pier size:", "Elevation:", 4, 1, 16, 24, 16, 16, 4},
		{"apse", SectorArchitectureElement::apse,
		 SectorArchitectureFamily::structuralSupports, "Columned apse",
		 "A curved-ended arrangement of repeated supports.",
		 DesignPreviewRole::architecture,
		 A_STYLE | A_BAYS | A_SIZE | A_MARGIN, A_STATIC,
		 "Columns:", "Diameter:", "Elevation:", 5, 2, 24, 24, 16, 16, 4},
		{"rotunda", SectorArchitectureElement::rotunda,
		 SectorArchitectureFamily::structuralSupports, "Radial rotunda",
		 "A circular ring of style-aware supports.",
		 DesignPreviewRole::architecture,
		 A_STYLE | A_BAYS | A_SIZE | A_MARGIN, A_STATIC,
		 "Column pairs:", "Diameter:", "Elevation:",
		 4, 3, 32, 24, 16, 16, 4},
		{"sanctuary", SectorArchitectureElement::sanctuary,
		 SectorArchitectureFamily::structuralSupports, "Complex sanctuary",
		 "A layered ceremonial support composition.",
		 DesignPreviewRole::architecture,
		 A_STYLE | A_BAYS | A_SIZE | A_MARGIN, A_STATIC,
		 "Stations:", "Support size:", "Elevation:", 4, 1, 16, 24, 16, 16, 4},
		{"fortified_keep", SectorArchitectureElement::fortifiedKeep,
		 SectorArchitectureFamily::structuralSupports, "Fortified keep",
		 "A heavy central and corner support composition.",
		 DesignPreviewRole::architecture,
		 A_STYLE | A_SIZE | A_MARGIN, A_STATIC,
		 "Bays:", "Mass size:", "Elevation:", 4, 1, 4, 24, 16, 16, 4},

		{"raised_dais", SectorArchitectureElement::raisedDais,
		 SectorArchitectureFamily::floorsTerraces, "Raised dais",
		 "One beveled walkable raised floor.",
		 DesignPreviewRole::architectureFloor,
		 A_SIZE | A_HEIGHT | A_MARGIN, A_STATIC,
		 "Bays:", "Bevel:", "Rise:", 1, 1, 1, 24, 16, 16, 1},
		{"sunken_court", SectorArchitectureElement::sunkenCourt,
		 SectorArchitectureFamily::floorsTerraces, "Sunken court",
		 "One beveled walkable recessed floor.",
		 DesignPreviewRole::architectureFloor,
		 A_SIZE | A_HEIGHT | A_MARGIN, A_STATIC,
		 "Bays:", "Bevel:", "Depth:", 1, 1, 1, 24, 16, 16, 1},
		{"tiered_ziggurat", SectorArchitectureElement::tieredZiggurat,
		 SectorArchitectureFamily::floorsTerraces, "Tiered ziggurat",
		 "Three nested walkable floor tiers.",
		 DesignPreviewRole::architectureFloor,
		 A_SIZE | A_HEIGHT | A_MARGIN, A_STATIC,
		 "Tiers:", "Tread:", "Rise/tier:", 3, 3, 3, 24, 16, 16, 1},
		{"grand_stair", SectorArchitectureElement::grandStair,
		 SectorArchitectureFamily::circulation, "Grand staircase",
		 "A straight directional run of walkable steps.",
		 DesignPreviewRole::architectureCirculation,
		 A_BAYS | A_SIZE | A_HEIGHT | A_MARGIN, A_STATIC,
		 "Steps:", "Stair width:", "Rise/step:", 4, 2, 32, 24, 8, 16, 1,
		 true},
		{"fountain_basin", SectorArchitectureElement::fountainBasin,
		 SectorArchitectureFamily::waterworks,
		 "Fountain basin and centerpiece",
		 "An elliptical basin with a style-aware solid centerpiece.",
		 DesignPreviewRole::architectureWater,
		 A_STYLE | A_SIZE | A_HEIGHT | A_MARGIN, A_STATIC,
		 "Bays:", "Center size:", "Depth/relief:", 1, 1, 1, 24, 16, 16, 4},
		{"reflecting_pool", SectorArchitectureElement::reflectingPool,
		 SectorArchitectureFamily::waterworks, "Beveled reflecting pool",
		 "A broad beveled recessed water cell.",
		 DesignPreviewRole::architectureWater,
		 A_SIZE | A_HEIGHT | A_MARGIN, A_STATIC,
		 "Bays:", "Bevel:", "Depth:", 1, 1, 1, 24, 16, 16, 1},
		{"balcony_gallery", SectorArchitectureElement::balconyGallery,
		 SectorArchitectureFamily::circulation,
		 "Perimeter balcony gallery",
		 "A raised walkable perimeter ring with a retained center.",
		 DesignPreviewRole::architectureCirculation,
		 A_SIZE | A_HEIGHT | A_MARGIN, A_STATIC,
		 "Bays:", "Gallery depth:", "Rise:", 1, 1, 1, 24, 16, 16, 1},
		{"processional_channel", SectorArchitectureElement::processionalChannel,
		 SectorArchitectureFamily::waterworks, "Processional channel",
		 "A lowered longitudinal channel.",
		 DesignPreviewRole::architectureWater,
		 A_SIZE | A_HEIGHT | A_MARGIN, A_STATIC,
		 "Bays:", "Channel width:", "Depth:", 1, 1, 1, 24, 16, 16, 1},
		{"screen_wall", SectorArchitectureElement::screenWall,
		 SectorArchitectureFamily::wallsScreens,
		 "Perforated screen wall",
		 "Alternating solid wall panels and full-height openings.",
		 DesignPreviewRole::architectureWall,
		 A_BAYS | A_SIZE | A_MARGIN, A_STATIC,
		 "Openings:", "Thickness:", "Elevation:", 4, 1, 16, 24, 16, 16, 1},
		{"coffered_ceiling", SectorArchitectureElement::cofferedCeiling,
		 SectorArchitectureFamily::ceilingsVaults, "Coffered ceiling",
		 "A grid of raised ceiling recess cells.",
		 DesignPreviewRole::architectureCeiling,
		 A_BAYS | A_SIZE | A_HEIGHT | A_MARGIN, A_STATIC,
		 "Columns:", "Rib width:", "Recess:", 4, 1, 12, 24, 16, 16, 1},
		{"groin_vaults", SectorArchitectureElement::groinVaults,
		 SectorArchitectureFamily::ceilingsVaults, "Groin-vault bays",
		 "Repeated octagonal raised ceiling cells.",
		 DesignPreviewRole::architectureCeiling,
		 A_BAYS | A_SIZE | A_HEIGHT | A_MARGIN, A_STATIC,
		 "Vaults:", "Rib gap:", "Relief:", 4, 1, 16, 24, 16, 16, 1},
		{"raised_bridge", SectorArchitectureElement::raisedBridge,
		 SectorArchitectureFamily::circulation, "Raised bridge",
		 "One narrow directional raised walkway.",
		 DesignPreviewRole::architectureCirculation,
		 A_SIZE | A_HEIGHT | A_MARGIN, A_STATIC,
		 "Bays:", "Bridge width:", "Rise:", 1, 1, 1, 24, 16, 16, 1,
		 true},

		{"cross_core", SectorArchitectureElement::crossCore,
		 SectorArchitectureFamily::structuralSupports,
		 "Cross-shaped structural core",
		 "A single concave style-aware cruciform solid mass.",
		 DesignPreviewRole::architecture,
		 A_STYLE | A_SIZE | A_MARGIN, A_STATIC,
		 "Bays:", "Arm width:", "Elevation:", 1, 1, 1, 32, 16, 16, 4},
		{"hollow_tower", SectorArchitectureElement::hollowTower,
		 SectorArchitectureFamily::structuralSupports,
		 "Hollow tower shell",
		 "Four style-aware wall masses around an open center with opposed entries.",
		 DesignPreviewRole::architecture,
		 A_STYLE | A_SIZE | A_MARGIN, A_STATIC,
		 "Bays:", "Wall thickness:", "Elevation:", 4, 1, 4, 24, 16, 16, 4},
		{"buttressed_tower", SectorArchitectureElement::buttressedTower,
		 SectorArchitectureFamily::structuralSupports,
		 "Buttressed tower",
		 "A style-aware solid core with repeated projecting buttresses.",
		 DesignPreviewRole::architecture,
		 A_STYLE | A_BAYS | A_SIZE | A_MARGIN, A_STATIC,
		 "Buttresses:", "Core size:", "Elevation:", 8, 4, 16, 32, 16, 16, 4},
		{"shear_wall_pair", SectorArchitectureElement::shearWallPair,
		 SectorArchitectureFamily::structuralSupports,
		 "Paired shear walls",
		 "Two parallel solid wall slabs framing a traversable passage.",
		 DesignPreviewRole::architecture,
		 A_SIZE | A_MARGIN, A_STATIC,
		 "Bays:", "Wall thickness:", "Elevation:", 2, 2, 2, 24, 16, 16, 8},
		{"stepped_monument", SectorArchitectureElement::steppedMonument,
		 SectorArchitectureFamily::structuralSupports,
		 "Stepped monument plinth",
		 "Nested style-aware solid tiers rising toward a monument center.",
		 DesignPreviewRole::architecture,
		 A_STYLE | A_BAYS | A_SIZE | A_HEIGHT | A_MARGIN, A_STATIC,
		 "Tiers:", "Tread:", "Rise/tier:", 3, 2, 6, 24, 16, 16, 4},

		{"central_platform", SectorArchitectureElement::centralPlatform,
		 SectorArchitectureFamily::floorsTerraces,
		 "Central platform / lift",
		 "A centered walkable platform that may optionally become a Smart Lift.",
		 DesignPreviewRole::architectureFloor,
		 A_SIZE | A_HEIGHT | A_MARGIN | A_FUNCTION,
		 A_STATIC | A_SMART_LIFT,
		 "Bays:", "Platform inset:", "Rise:", 1, 1, 1, 24, 16, 16, 4},
		{"split_level_stage", SectorArchitectureElement::splitLevelStage,
		 SectorArchitectureFamily::floorsTerraces, "Split-level stage",
		 "Two adjacent walkable stage levels ordered by the drag direction.",
		 DesignPreviewRole::architectureFloor,
		 A_SIZE | A_HEIGHT | A_MARGIN, A_STATIC,
		 "Levels:", "Center gap:", "Rise/level:", 2, 2, 2, 16, 16, 16, 4},
		{"corner_terraces", SectorArchitectureElement::cornerTerraces,
		 SectorArchitectureFamily::floorsTerraces, "Corner terraces",
		 "Mirrored nested corner-anchored walkable terraces.",
		 DesignPreviewRole::architectureFloor,
		 A_BAYS | A_SIZE | A_HEIGHT | A_MARGIN | A_MIRROR, A_STATIC,
		 "Tiers:", "Tread:", "Rise/tier:", 3, 2, 6, 24, 16, 16, 8},
		{"octagonal_podium", SectorArchitectureElement::octagonalPodium,
		 SectorArchitectureFamily::floorsTerraces,
		 "Multi-tier octagonal podium",
		 "Nested style-aware polygonal walkable podium levels.",
		 DesignPreviewRole::architectureFloor,
		 A_STYLE | A_BAYS | A_SIZE | A_HEIGHT | A_MARGIN, A_STATIC,
		 "Tiers:", "Tread:", "Rise/tier:", 3, 2, 6, 24, 16, 16, 4},
		{"horseshoe_amphitheater",
		 SectorArchitectureElement::horseshoeAmphitheater,
		 SectorArchitectureFamily::floorsTerraces,
		 "Horseshoe amphitheater",
		 "Three-sided seating rows rising away from an open stage.",
		 DesignPreviewRole::architectureFloor,
		 A_BAYS | A_SIZE | A_HEIGHT | A_MARGIN, A_STATIC,
		 "Rows:", "Row depth:", "Rise/row:", 4, 2, 12, 24, 8, 16, 8},

		{"switchback_stair", SectorArchitectureElement::switchbackStair,
		 SectorArchitectureFamily::circulation, "Switchback stairs",
		 "Two mirrored parallel flights joined by a landing.",
		 DesignPreviewRole::architectureCirculation,
		 A_BAYS | A_SIZE | A_HEIGHT | A_MARGIN | A_MIRROR, A_STATIC,
		 "Steps/flight:", "Flight width:", "Rise/step:", 4, 2, 16, 24, 8, 16, 8,
		 true},
		{"bifurcated_stair", SectorArchitectureElement::bifurcatedStair,
		 SectorArchitectureFamily::circulation,
		 "Bifurcated ceremonial stairs",
		 "One broad lower flight splitting into two upper flights.",
		 DesignPreviewRole::architectureCirculation,
		 A_BAYS | A_SIZE | A_HEIGHT | A_MARGIN | A_MIRROR, A_STATIC,
		 "Steps/flight:", "Flight width:", "Rise/step:", 4, 2, 16, 24, 8, 16, 8,
		 true},
		{"spiral_stair", SectorArchitectureElement::spiralStair,
		 SectorArchitectureFamily::circulation, "Spiral stairs",
		 "Clockwise or counterclockwise wedge steps around a central well.",
		 DesignPreviewRole::architectureCirculation,
		 A_BAYS | A_SIZE | A_HEIGHT | A_MARGIN | A_MIRROR, A_STATIC,
		 "Steps:", "Well radius:", "Rise/step:", 12, 6, 32, 24, 8, 16, 8},
		{"landing_catwalk", SectorArchitectureElement::landingCatwalk,
		 SectorArchitectureFamily::circulation,
		 "Catwalk with landings",
		 "A narrow raised walk with wider end landing cells.",
		 DesignPreviewRole::architectureCirculation,
		 A_SIZE | A_HEIGHT | A_MARGIN, A_STATIC,
		 "Bays:", "Walk width:", "Rise:", 1, 1, 1, 24, 16, 16, 8,
		 true},
		{"crossing_bridges", SectorArchitectureElement::crossingBridges,
		 SectorArchitectureFamily::circulation, "Crossing bridges",
		 "Two connected perpendicular raised walks sharing a center.",
		 DesignPreviewRole::architectureCirculation,
		 A_SIZE | A_HEIGHT | A_MARGIN, A_STATIC,
		 "Bays:", "Bridge width:", "Rise:", 1, 1, 1, 24, 16, 16, 8,
		 true},

		{"perimeter_moat", SectorArchitectureElement::perimeterMoat,
		 SectorArchitectureFamily::waterworks, "Perimeter moat",
		 "A recessed perimeter ring with a retained dry center.",
		 DesignPreviewRole::architectureWater,
		 A_SIZE | A_HEIGHT | A_MARGIN, A_STATIC,
		 "Bays:", "Moat width:", "Depth:", 1, 1, 1, 24, 16, 16, 8},
		{"cross_canal", SectorArchitectureElement::crossCanal,
		 SectorArchitectureFamily::waterworks, "Cross-shaped canal",
		 "One connected concave cross-shaped lowered channel.",
		 DesignPreviewRole::architectureWater,
		 A_SIZE | A_HEIGHT | A_MARGIN, A_STATIC,
		 "Bays:", "Channel width:", "Depth:", 1, 1, 1, 24, 16, 16, 8},
		{"twin_canals", SectorArchitectureElement::twinCanals,
		 SectorArchitectureFamily::waterworks, "Parallel twin canals",
		 "Two separated longitudinal lowered channels.",
		 DesignPreviewRole::architectureWater,
		 A_SIZE | A_HEIGHT | A_MARGIN, A_STATIC,
		 "Channels:", "Channel width:", "Depth:", 2, 2, 2, 24, 16, 16, 8},
		{"stepped_cascade", SectorArchitectureElement::steppedCascade,
		 SectorArchitectureFamily::waterworks, "Stepped cascade",
		 "Adjacent basins descending along the drag direction.",
		 DesignPreviewRole::architectureWater,
		 A_BAYS | A_SIZE | A_HEIGHT | A_MARGIN, A_STATIC,
		 "Basins:", "Cascade width:", "Drop/basin:", 4, 2, 12, 24, 8, 16, 8},
		{"fountain_court", SectorArchitectureElement::fountainCourt,
		 SectorArchitectureFamily::waterworks, "Four-basin fountain court",
		 "Four recessed basins around a style-aware solid centerpiece.",
		 DesignPreviewRole::architectureWater,
		 A_STYLE | A_SIZE | A_HEIGHT | A_MARGIN, A_STATIC,
		 "Basins:", "Center/gap:", "Depth/relief:", 4, 4, 4, 24, 16, 16, 4},

		{"partition_wall", SectorArchitectureElement::partitionWall,
		 SectorArchitectureFamily::wallsScreens, "Solid partition wall",
		 "One continuous centered floor-to-ceiling wall slab.",
		 DesignPreviewRole::architectureWall,
		 A_SIZE | A_MARGIN, A_STATIC,
		 "Panels:", "Thickness:", "Elevation:", 1, 1, 1, 24, 16, 16, 8},
		{"crenellated_wall", SectorArchitectureElement::crenellatedWall,
		 SectorArchitectureFamily::wallsScreens, "Crenellated wall",
		 "A continuous wall with alternating top-view projections.",
		 DesignPreviewRole::architectureWall,
		 A_BAYS | A_SIZE | A_MARGIN, A_STATIC,
		 "Merlons:", "Thickness:", "Elevation:", 6, 2, 16, 24, 16, 16, 8},
		{"buttressed_wall", SectorArchitectureElement::buttressedWall,
		 SectorArchitectureFamily::wallsScreens, "Buttressed wall",
		 "A continuous slab with repeated projecting solid buttresses.",
		 DesignPreviewRole::architectureWall,
		 A_BAYS | A_SIZE | A_MARGIN, A_STATIC,
		 "Buttresses:", "Thickness:", "Elevation:", 4, 1, 16, 24, 16, 16, 8},
		{"staggered_screen", SectorArchitectureElement::staggeredScreen,
		 SectorArchitectureFamily::wallsScreens,
		 "Staggered privacy screen",
		 "Alternating offset wall panels forming a traversable slalom.",
		 DesignPreviewRole::architectureWall,
		 A_BAYS | A_SIZE | A_MARGIN | A_MIRROR, A_STATIC,
		 "Panels:", "Thickness:", "Elevation:", 5, 2, 16, 24, 16, 16, 8},
		{"gatehouse_passage", SectorArchitectureElement::gatehousePassage,
		 SectorArchitectureFamily::wallsScreens,
		 "Gatehouse with open passage",
		 "Style-aware flanking wall masses preserve a centered traversable opening.",
		 DesignPreviewRole::architectureWall,
		 A_STYLE | A_SIZE | A_MARGIN, A_STATIC,
		 "Bays:", "Passage width:", "Elevation:", 1, 1, 1, 64, 16, 16, 8,
		 true},

		{"tray_ceiling", SectorArchitectureElement::trayCeiling,
		 SectorArchitectureFamily::ceilingsVaults, "Recessed tray ceiling",
		 "One large ceiling recess inside a retained border.",
		 DesignPreviewRole::architectureCeiling,
		 A_SIZE | A_HEIGHT | A_MARGIN, A_STATIC,
		 "Bays:", "Border width:", "Recess:", 1, 1, 1, 24, 16, 16, 4},
		{"barrel_vault", SectorArchitectureElement::barrelVault,
		 SectorArchitectureFamily::ceilingsVaults, "Stepped barrel vault",
		 "Parallel ceiling bands approximate a smooth barrel profile.",
		 DesignPreviewRole::architectureCeiling,
		 A_BAYS | A_SIZE | A_HEIGHT | A_MARGIN, A_STATIC,
		 "Bands:", "Ridge width:", "Total relief:", 5, 3, 15, 24, 16, 16, 4},
		{"ribbed_cross_vault", SectorArchitectureElement::ribbedCrossVault,
		 SectorArchitectureFamily::ceilingsVaults, "Ribbed cross vault",
		 "Four ceiling cells rise toward a shared crossing.",
		 DesignPreviewRole::architectureCeiling,
		 A_SIZE | A_HEIGHT | A_MARGIN, A_STATIC,
		 "Bays:", "Rib width:", "Relief:", 4, 4, 4, 24, 16, 16, 4},
		{"domed_ceiling", SectorArchitectureElement::domedCeiling,
		 SectorArchitectureFamily::ceilingsVaults,
		 "Concentric polygonal dome",
		 "Nested style-aware ceiling rings rise toward the center.",
		 DesignPreviewRole::architectureCeiling,
		 A_STYLE | A_BAYS | A_SIZE | A_HEIGHT | A_MARGIN, A_STATIC,
		 "Rings:", "Ring width:", "Rise/ring:", 4, 2, 8, 24, 8, 16, 4},
		{"beam_lattice", SectorArchitectureElement::beamLattice,
		 SectorArchitectureFamily::ceilingsVaults,
		 "Downstand beam lattice",
		 "A grid of lowered ceiling strips forming structural beams.",
		 DesignPreviewRole::architectureCeiling,
		 A_BAYS | A_SIZE | A_HEIGHT | A_MARGIN, A_STATIC,
		 "Beams/axis:", "Beam width:", "Drop:", 4, 1, 12, 16, 16, 16, 4}
	};
	return catalog;
}

void AddIssue(SectorDesignPlan &plan, SectorDesignIssueSeverity severity,
			  const SString &message, int sector = -1, int line = -1,
			  std::optional<v2double_t> position = std::nullopt)
{
	plan.issues.push_back({severity, message, sector, line, position});
}

double CoordinateScale(MapFormat format)
{
	return format == MapFormat::udmf ? UDMF_SCALE : 1.0;
}

v2double_t Quantize(MapFormat format, const v2double_t &point)
{
	const double scale = CoordinateScale(format);
	const double fixedLimit = static_cast<double>(
			std::numeric_limits<int64_t>::max() / 4) / scale;
	if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
		std::abs(point.x) > fixedLimit || std::abs(point.y) > fixedLimit)
		return point;
	return {
		std::round(point.x * scale) / scale,
		std::round(point.y * scale) / scale
	};
}

bool CoordinateIsSafe(MapFormat format, const v2double_t &point)
{
	if (!std::isfinite(point.x) || !std::isfinite(point.y))
		return false;
	if (format != MapFormat::udmf)
	{
		const double x = std::round(point.x);
		const double y = std::round(point.y);
		return x >= std::numeric_limits<int16_t>::min() &&
				x <= std::numeric_limits<int16_t>::max() &&
				y >= std::numeric_limits<int16_t>::min() &&
				y <= std::numeric_limits<int16_t>::max();
	}
	const double scale = CoordinateScale(format);
	const double limit = static_cast<double>(
			std::numeric_limits<int64_t>::max() / 4) / scale;
	return std::abs(point.x) <= limit && std::abs(point.y) <= limit;
}

bool RequestCoordinatesAreSafe(const Document &doc, MapFormat format,
							   const SectorDesignRequest &request,
							   SectorDesignPlan &plan)
{
	auto checkPoint = [&](const v2double_t &point)
	{
		if (CoordinateIsSafe(format, point))
			return true;
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 "A design coordinate is outside the active format's safe range.",
				 -1, -1, point);
		return false;
	};

	bool safe = true;
	for (const v2double_t &anchor : request.anchors)
		safe = checkPoint(anchor) && safe;

	std::set<int> relevantVertices;
	for (int line : request.anchorLines)
		if (doc.isLinedef(line))
		{
			relevantVertices.insert(doc.linedefs[line]->start);
			relevantVertices.insert(doc.linedefs[line]->end);
		}
	if (!request.targetSectors.empty())
	{
		std::set<int> sectors(request.targetSectors.begin(),
							 request.targetSectors.end());
		for (const std::shared_ptr<LineDef> &line : doc.linedefs)
		{
			const int right = SectorForSide(doc, line->right);
			const int left = SectorForSide(doc, line->left);
			if (sectors.count(right) || sectors.count(left))
			{
				relevantVertices.insert(line->start);
				relevantVertices.insert(line->end);
			}
		}
	}
	for (int vertex : relevantVertices)
		if (doc.isVertex(vertex))
			safe = checkPoint(doc.vertices[vertex]->xy()) && safe;
	return safe;
}

double Cross(const v2double_t &a, const v2double_t &b,
			 const v2double_t &c)
{
	return (b.x - a.x) * (c.y - a.y) -
			(b.y - a.y) * (c.x - a.x);
}

double SignedArea(const std::vector<v2double_t> &path)
{
	double area = 0;
	for (size_t index = 0; index < path.size(); ++index)
	{
		const v2double_t &a = path[index];
		const v2double_t &b = path[(index + 1) % path.size()];
		area += a.x * b.y - b.x * a.y;
	}
	return area * 0.5;
}

bool SamePoint(const v2double_t &a, const v2double_t &b)
{
	return std::abs(a.x - b.x) <= PLAN_EPSILON &&
			std::abs(a.y - b.y) <= PLAN_EPSILON;
}

std::vector<v2double_t> CleanPath(MapFormat format,
								  const std::vector<v2double_t> &raw)
{
	std::vector<v2double_t> result;
	for (const v2double_t &rawPoint : raw)
	{
		v2double_t point = Quantize(format, rawPoint);
		if (result.empty() || !SamePoint(result.back(), point))
			result.push_back(point);
	}
	if (result.size() > 1 && SamePoint(result.front(), result.back()))
		result.pop_back();

	bool removed = true;
	while (removed && result.size() >= 3)
	{
		removed = false;
		for (size_t index = 0; index < result.size(); ++index)
		{
			const v2double_t &before =
					result[(index + result.size() - 1) % result.size()];
			const v2double_t &point = result[index];
			const v2double_t &after = result[(index + 1) % result.size()];
			if (std::abs(Cross(before, point, after)) <= PLAN_EPSILON)
			{
				result.erase(result.begin() + index);
				removed = true;
				break;
			}
		}
	}
	return result;
}

void MakeClockwise(std::vector<v2double_t> &path)
{
	if (SignedArea(path) > 0)
		std::reverse(path.begin(), path.end());
}

void MakeCounterClockwise(std::vector<v2double_t> &path)
{
	if (SignedArea(path) < 0)
		std::reverse(path.begin(), path.end());
}

Path64 ToClipperPath(const std::vector<v2double_t> &path, MapFormat format)
{
	const double scale = CoordinateScale(format);
	Path64 result;
	result.reserve(path.size());
	for (const v2double_t &point : path)
		result.emplace_back(std::llround(point.x * scale),
							std::llround(point.y * scale));
	return result;
}

std::vector<v2double_t> FromClipperPath(const Path64 &path, MapFormat format)
{
	const double scale = CoordinateScale(format);
	std::vector<v2double_t> result;
	result.reserve(path.size());
	for (const Point64 &point : path)
		result.emplace_back(point.x / scale, point.y / scale);
	return CleanPath(format, result);
}

JoinType ToClipperJoin(SectorDesignJoin join)
{
	switch (join)
	{
	case SectorDesignJoin::bevel: return JoinType::Bevel;
	case SectorDesignJoin::round: return JoinType::Round;
	default: return JoinType::Miter;
	}
}

enum class SegmentRelation
{
	none,
	touch,
	proper,
	exact,
	overlap
};

bool PointOnSegment(const v2double_t &point, const v2double_t &a,
					const v2double_t &b)
{
	return std::abs(Cross(a, b, point)) <= PLAN_EPSILON &&
			point.x >= std::min(a.x, b.x) - PLAN_EPSILON &&
			point.x <= std::max(a.x, b.x) + PLAN_EPSILON &&
			point.y >= std::min(a.y, b.y) - PLAN_EPSILON &&
			point.y <= std::max(a.y, b.y) + PLAN_EPSILON;
}

SegmentRelation ClassifySegments(const v2double_t &a,
								 const v2double_t &b,
								 const v2double_t &c,
								 const v2double_t &d)
{
	if ((SamePoint(a, c) && SamePoint(b, d)) ||
		(SamePoint(a, d) && SamePoint(b, c)))
		return SegmentRelation::exact;

	const double abC = Cross(a, b, c);
	const double abD = Cross(a, b, d);
	const double cdA = Cross(c, d, a);
	const double cdB = Cross(c, d, b);
	const bool collinear = std::abs(abC) <= PLAN_EPSILON &&
			std::abs(abD) <= PLAN_EPSILON &&
			std::abs(cdA) <= PLAN_EPSILON &&
			std::abs(cdB) <= PLAN_EPSILON;
	if (collinear)
	{
		const bool overlap = PointOnSegment(a, c, d) ||
				PointOnSegment(b, c, d) ||
				PointOnSegment(c, a, b) ||
				PointOnSegment(d, a, b);
		if (!overlap)
			return SegmentRelation::none;
		if (SamePoint(a, c) || SamePoint(a, d) ||
			SamePoint(b, c) || SamePoint(b, d))
		{
			const bool onlyEndpoint =
				!(PointOnSegment(a, c, d) && !SamePoint(a, c) && !SamePoint(a, d)) &&
				!(PointOnSegment(b, c, d) && !SamePoint(b, c) && !SamePoint(b, d)) &&
				!(PointOnSegment(c, a, b) && !SamePoint(c, a) && !SamePoint(c, b)) &&
				!(PointOnSegment(d, a, b) && !SamePoint(d, a) && !SamePoint(d, b));
			if (onlyEndpoint)
				return SegmentRelation::touch;
		}
		return SegmentRelation::overlap;
	}

	const bool crossesAB = (abC > PLAN_EPSILON && abD < -PLAN_EPSILON) ||
			(abC < -PLAN_EPSILON && abD > PLAN_EPSILON);
	const bool crossesCD = (cdA > PLAN_EPSILON && cdB < -PLAN_EPSILON) ||
			(cdA < -PLAN_EPSILON && cdB > PLAN_EPSILON);
	if (crossesAB && crossesCD)
		return SegmentRelation::proper;
	if (PointOnSegment(a, c, d) || PointOnSegment(b, c, d) ||
		PointOnSegment(c, a, b) || PointOnSegment(d, a, b))
		return SegmentRelation::touch;
	return SegmentRelation::none;
}

bool SegmentContained(const v2double_t &a, const v2double_t &b,
					  const v2double_t &containerA,
					  const v2double_t &containerB)
{
	return PointOnSegment(a, containerA, containerB) &&
			PointOnSegment(b, containerA, containerB);
}

bool PathSelfIntersects(const std::vector<v2double_t> &path)
{
	for (size_t first = 0; first < path.size(); ++first)
	{
		size_t firstEnd = (first + 1) % path.size();
		for (size_t second = first + 1; second < path.size(); ++second)
		{
			size_t secondEnd = (second + 1) % path.size();
			if (first == second || firstEnd == second ||
				secondEnd == first)
				continue;
			SegmentRelation relation = ClassifySegments(
					path[first], path[firstEnd],
					path[second], path[secondEnd]);
			if (relation != SegmentRelation::none)
				return true;
		}
	}
	return false;
}

bool PointInsidePath(const v2double_t &point,
					 const std::vector<v2double_t> &path)
{
	bool inside = false;
	for (size_t i = 0, j = path.size() - 1; i < path.size(); j = i++)
	{
		const v2double_t &a = path[i];
		const v2double_t &b = path[j];
		if (PointOnSegment(point, a, b))
			return true;
		if (((a.y > point.y) != (b.y > point.y)) &&
			(point.x < (b.x - a.x) * (point.y - a.y) /
				(b.y - a.y) + a.x))
			inside = !inside;
	}
	return inside;
}

bool PointOnPathBoundary(const v2double_t &point,
						 const std::vector<v2double_t> &path)
{
	for (size_t index = 0; index < path.size(); ++index)
		if (PointOnSegment(
				point, path[index],
				path[(index + 1) % path.size()]))
			return true;
	return false;
}

bool PointStrictlyInsidePath(const v2double_t &point,
							 const std::vector<v2double_t> &path)
{
	return !PointOnPathBoundary(point, path) &&
			PointInsidePath(point, path);
}

void AddPreviewShape(SectorDesignPlan &plan, const PlannedSectorShape &shape)
{
	plan.previewPaths.push_back({shape.outer, shape.role, true, true});
	for (const auto &hole : shape.holes)
		plan.previewPaths.push_back({hole, DesignPreviewRole::cut, true, false});
}

void AppendClipperShapes(SectorDesignPlan &plan, const Paths64 &paths,
						 MapFormat format, DesignPreviewRole role,
						 int modelSector)
{
	struct Item
	{
		std::vector<v2double_t> path;
		double area = 0;
		int parent = -1;
		int depth = 0;
		int shape = -1;
	};
	std::vector<Item> items;
	for (const Path64 &rawPath : paths)
	{
		std::vector<v2double_t> path = FromClipperPath(rawPath, format);
		double area = std::abs(SignedArea(path));
		if (path.size() >= 3 && area > PLAN_EPSILON)
			items.push_back({std::move(path), area});
	}
	std::sort(items.begin(), items.end(),
			[](const Item &a, const Item &b)
			{
				return a.area > b.area;
			});

	for (size_t index = 0; index < items.size(); ++index)
	{
		double bestArea = std::numeric_limits<double>::max();
		for (size_t candidate = 0; candidate < index; ++candidate)
		{
			if (items[candidate].area < bestArea &&
				PointInsidePath(items[index].path.front(),
								items[candidate].path))
			{
				items[index].parent = static_cast<int>(candidate);
				bestArea = items[candidate].area;
			}
		}
		if (items[index].parent >= 0)
			items[index].depth = items[items[index].parent].depth + 1;

		if ((items[index].depth % 2) == 0)
		{
			MakeClockwise(items[index].path);
			PlannedSectorShape shape;
			shape.outer = items[index].path;
			shape.role = role;
			shape.modelSector = modelSector;
			items[index].shape = static_cast<int>(plan.shapes.size());
			plan.shapes.push_back(std::move(shape));
		}
		else
		{
			int ancestor = items[index].parent;
			while (ancestor >= 0 && items[ancestor].shape < 0)
				ancestor = items[ancestor].parent;
			if (ancestor >= 0)
			{
				MakeCounterClockwise(items[index].path);
				plan.shapes[items[ancestor].shape].holes.push_back(
						items[index].path);
				items[index].shape = items[ancestor].shape;
			}
		}
	}
}

int SectorForSide(const Document &doc, int side)
{
	if (!doc.isSidedef(side))
		return -1;
	int sector = doc.sidedefs[side]->sector;
	return doc.isSector(sector) ? sector : -1;
}

bool ExtractSectorLoops(const Document &doc, int sector,
						std::vector<std::vector<v2double_t>> &loops)
{
	struct Edge
	{
		int start;
		int end;
		bool used = false;
	};
	std::vector<Edge> edges;
	std::unordered_map<int, std::vector<int>> outgoing;
	std::unordered_map<int, int> incoming;

	for (int line = 0; line < doc.numLinedefs(); ++line)
	{
		const LineDef &linedef = *doc.linedefs[line];
		const int right = SectorForSide(doc, linedef.right);
		const int left = SectorForSide(doc, linedef.left);
		if (right == sector && left == sector)
			return false;
		int start = -1;
		int end = -1;
		if (right == sector)
		{
			start = linedef.start;
			end = linedef.end;
		}
		else if (left == sector)
		{
			start = linedef.end;
			end = linedef.start;
		}
		if (start < 0 || !doc.isVertex(start) || !doc.isVertex(end))
			continue;
		int edge = static_cast<int>(edges.size());
		edges.push_back({start, end});
		outgoing[start].push_back(edge);
		incoming[end]++;
	}

	if (edges.empty())
		return false;
	for (const auto &[vertex, edgeList] : outgoing)
		if (edgeList.size() != 1 || incoming[vertex] != 1)
			return false;

	for (size_t seed = 0; seed < edges.size(); ++seed)
	{
		if (edges[seed].used)
			continue;
		std::vector<v2double_t> loop;
		int edge = static_cast<int>(seed);
		const int firstVertex = edges[edge].start;
		for (size_t guard = 0; guard <= edges.size(); ++guard)
		{
			Edge &current = edges[edge];
			if (current.used)
				return false;
			current.used = true;
			loop.push_back(doc.vertices[current.start]->xy());
			if (current.end == firstVertex)
				break;
			auto found = outgoing.find(current.end);
			if (found == outgoing.end() || found->second.size() != 1)
				return false;
			edge = found->second.front();
		}
		if (loop.size() < 3)
			return false;
		loops.push_back(std::move(loop));
	}
	return !loops.empty();
}

bool PointInsideLoop(const std::vector<v2double_t> &loop,
					 const v2double_t &point)
{
	if (loop.size() < 3)
		return false;
	bool inside = false;
	for (size_t current = 0, previous = loop.size() - 1;
		 current < loop.size(); previous = current++)
	{
		const v2double_t &a = loop[previous];
		const v2double_t &b = loop[current];
		if ((a.y > point.y) != (b.y > point.y) &&
			point.x < (b.x - a.x) * (point.y - a.y) /
					(b.y - a.y) + a.x)
			inside = !inside;
	}
	return inside;
}

bool PointInsideLoops(
		const std::vector<std::vector<v2double_t>> &loops,
		const v2double_t &point)
{
	bool inside = false;
	for (const std::vector<v2double_t> &loop : loops)
		if (PointInsideLoop(loop, point))
			inside = !inside;
	return inside;
}

bool PointInsideShape(const PlannedSectorShape &shape,
					  const v2double_t &point)
{
	if (!PointInsideLoop(shape.outer, point))
		return false;
	for (const std::vector<v2double_t> &hole : shape.holes)
		if (PointInsideLoop(hole, point))
			return false;
	return true;
}

struct CapturedSectorRegion
{
	int sector = -1;
	std::vector<std::vector<v2double_t>> loops;
};

std::vector<CapturedSectorRegion> CaptureSectorRegions(const Document &doc)
{
	std::vector<CapturedSectorRegion> regions;
	regions.reserve(doc.numSectors());
	for (int sector = 0; sector < doc.numSectors(); ++sector)
	{
		CapturedSectorRegion region;
		region.sector = sector;
		if (ExtractSectorLoops(doc, sector, region.loops))
			regions.push_back(std::move(region));
	}
	return regions;
}

bool PointWasOccupied(const std::vector<CapturedSectorRegion> &regions,
					  const v2double_t &point)
{
	return std::any_of(
			regions.begin(), regions.end(),
			[&](const CapturedSectorRegion &region)
			{
				return PointInsideLoops(region.loops, point);
			});
}

std::optional<v2double_t> SectorInteriorPoint(
		const Document &doc, int sector)
{
	std::vector<std::vector<v2double_t>> loops;
	if (!ExtractSectorLoops(doc, sector, loops))
		return std::nullopt;

	for (int line = 0; line < doc.numLinedefs(); ++line)
	{
		const LineDef &linedef = *doc.linedefs[line];
		const int right = SectorForSide(doc, linedef.right);
		const int left = SectorForSide(doc, linedef.left);
		if ((right == sector) == (left == sector))
			continue;
		const v2double_t start = doc.getStart(linedef).xy();
		const v2double_t end = doc.getEnd(linedef).xy();
		const v2double_t edge = end - start;
		const double length = edge.hypot();
		if (length <= PLAN_EPSILON)
			continue;
		const v2double_t rightNormal{edge.y / length, -edge.x / length};
		const v2double_t inward =
				right == sector ? rightNormal : rightNormal * -1.0;
		const double distance = std::min(0.25, length * 0.01);
		const v2double_t candidate =
				(start + end) * 0.5 + inward * distance;
		if (PointInsideLoops(loops, candidate))
			return candidate;
	}
	return std::nullopt;
}

void RemoveIncidentalVoidSectors(
		EditOperation &op, Document &doc,
		const PlannedSectorShape &shape,
		const std::vector<CapturedSectorRegion> &previousRegions,
		const SString &defaultWallTexture,
		std::vector<int> &created)
{
	std::vector<int> discard;
	for (int sector : created)
	{
		const std::optional<v2double_t> sample =
				SectorInteriorPoint(doc, sector);
		if (!sample || PointInsideShape(shape, *sample))
			continue;
		// Splitting a shape through occupied space can legitimately create
		// retained cells outside the requested shape. They remain in the map
		// with inherited properties, but are not part of the generated result.
		if (!PointWasOccupied(previousRegions, *sample))
			discard.push_back(sector);
	}

	std::sort(discard.begin(), discard.end(), std::greater<int>());
	for (int sector : discard)
	{
		std::vector<int> exposedLines;
		for (int line = 0; line < doc.numLinedefs(); ++line)
			if (SectorForSide(doc, doc.linedefs[line]->right) == sector ||
				SectorForSide(doc, doc.linedefs[line]->left) == sector)
				exposedLines.push_back(line);
		created.erase(
				std::remove(created.begin(), created.end(), sector),
				created.end());
		op.del(ObjType::sectors, sector);
		for (int &createdSector : created)
			if (createdSector > sector)
				createdSector--;

		for (int line : exposedLines)
		{
			if (!doc.isLinedef(line))
				continue;
			LineDef &linedef = *doc.linedefs[line];
			if (linedef.right < 0 && linedef.left >= 0)
				doc.linemod.flipLinedef(op, line);
			if (linedef.right >= 0 && linedef.left < 0)
			{
				int flags = linedef.flags;
				flags &= ~MLF_TwoSided;
				flags |= MLF_Blocking;
				op.changeLinedef(line, &LineDef::flags, flags);
				const int side = doc.linedefs[line]->right;
				if (doc.isSidedef(side) &&
					(doc.sidedefs[side]->MidTex().empty() ||
					 doc.sidedefs[side]->MidTex() == "-"))
					op.changeSidedef(
							side, SideDef::F_MID_TEX,
							BA_InternaliseString(defaultWallTexture));
			}
		}
	}

	created.erase(std::remove_if(
			created.begin(), created.end(),
			[&](int sector)
			{
				const std::optional<v2double_t> sample =
						SectorInteriorPoint(doc, sector);
				return sample && !PointInsideShape(shape, *sample);
			}), created.end());
}

bool PathsHaveSameBoundary(MapFormat format,
						   const std::vector<v2double_t> &firstRaw,
						   const std::vector<v2double_t> &secondRaw)
{
	const std::vector<v2double_t> first = CleanPath(format, firstRaw);
	const std::vector<v2double_t> second = CleanPath(format, secondRaw);
	if (first.size() != second.size() || first.empty())
		return false;

	for (size_t start = 0; start < second.size(); ++start)
	{
		if (!SamePoint(first.front(), second[start]))
			continue;
		bool forward = true;
		bool reverse = true;
		for (size_t index = 1; index < first.size(); ++index)
		{
			forward &= SamePoint(
					first[index],
					second[(start + index) % second.size()]);
			reverse &= SamePoint(
					first[index],
					second[(start + second.size() - index) %
						   second.size()]);
		}
		if (forward || reverse)
			return true;
	}
	return false;
}

int MatchingExistingSectorBoundary(const Document &doc, MapFormat format,
								   const std::vector<v2double_t> &path)
{
	for (int sector = 0; sector < doc.numSectors(); ++sector)
	{
		std::vector<std::vector<v2double_t>> loops;
		if (!ExtractSectorLoops(doc, sector, loops))
			continue;
		for (const std::vector<v2double_t> &loop : loops)
			if (PathsHaveSameBoundary(format, path, loop))
				return sector;
	}
	return -1;
}

int ChooseModelSector(const Document &doc, const SectorDesignRequest &request)
{
	if (doc.isSector(request.properties.modelSector))
		return request.properties.modelSector;
	for (int sector : request.targetSectors)
		if (doc.isSector(sector))
			return sector;
	for (int line : request.anchorLines)
	{
		if (!doc.isLinedef(line))
			continue;
		const LineDef &linedef = *doc.linedefs[line];
		int sector = SectorForSide(doc, linedef.right);
		if (sector >= 0)
			return sector;
		sector = SectorForSide(doc, linedef.left);
		if (sector >= 0)
			return sector;
	}
	return -1;
}

bool SectorHasProtectedAction(const Document &doc, const ConfigData &config,
							  int sector)
{
	if (!doc.isSector(sector))
		return false;

	std::set<int> doorSpecials;
	for (const DoorPreset &preset : config.door_presets)
		if (preset.special > 0)
			doorSpecials.insert(preset.special);
	std::set<int> liftSpecials;
	for (const SectorActionPreset &preset : config.sector_action_presets)
		if (preset.kind == SectorActionKind::lift &&
			preset.special > 0)
			liftSpecials.insert(preset.special);

	int doorBoundaries = 0;
	for (int line = 0; line < doc.numLinedefs(); ++line)
	{
		const LineDef &linedef = *doc.linedefs[line];
		if (!doc.touchesSector(linedef, sector))
			continue;
		if (liftSpecials.count(linedef.type))
		{
			for (const SectorActionPreset &preset :
					config.sector_action_presets)
			{
				if (preset.kind != SectorActionKind::lift ||
					preset.special != linedef.type)
					continue;
				if (preset.activation == ActivationPolicy::encoded &&
					doc.sectors[sector]->tag != 0 &&
					linedef.arg1 == doc.sectors[sector]->tag)
					return true;
				for (size_t argument = 0;
					 argument < preset.args.size(); ++argument)
					if (preset.args[argument].targetTag &&
						doc.sectors[sector]->tag != 0 &&
						linedef.Arg(static_cast<int>(argument) + 1) ==
								doc.sectors[sector]->tag)
						return true;
			}
		}

		// Canonical local doors act on the sector behind the line. Smart Door
		// enforces that same orientation, so the ordinary room on the front
		// side is never mistaken for a door even if it borders several.
		if (doorSpecials.count(linedef.type) &&
			SectorForSide(doc, linedef.left) == sector)
			doorBoundaries++;
	}

	// Smart Door sectors have two activating back-side portals. A closed
	// imported door with one unusual portal is also recognizable by height.
	if (doorBoundaries >= 2)
		return true;
	if (doorBoundaries == 1 &&
		doc.sectors[sector]->ceilh <= doc.sectors[sector]->floorh)
		return true;
	return false;
}

struct SectorDesignTextureCandidate
{
	SString texture;
	double weight = 0;
	int firstLine = -1;
};

class SectorDesignTextureWeights
{
public:
	void add(const SString &texture, double weight, int line)
	{
		if (texture.empty() || is_null_tex(texture))
			return;
		const SString key = texture.asUpper();
		SectorDesignTextureCandidate &candidate = candidates_[key];
		if (candidate.firstLine < 0)
		{
			candidate.texture = texture;
			candidate.firstLine = line;
		}
		candidate.weight += std::max(0.0, weight);
		candidate.firstLine = std::min(candidate.firstLine, line);
	}

	bool empty() const
	{
		return candidates_.empty();
	}

	SString best() const
	{
		const SectorDesignTextureCandidate *winner = nullptr;
		for (const auto &[key, candidate] : candidates_)
			if (!winner ||
				candidate.weight > winner->weight ||
				(std::abs(candidate.weight - winner->weight) < 0.0001 &&
				 candidate.firstLine < winner->firstLine))
				winner = &candidate;
		return winner ? winner->texture : SString();
	}

private:
	std::map<SString, SectorDesignTextureCandidate> candidates_;
};

void ResolveGeneratedDoorTextures(const Document &doc,
								  const ConfigData &config,
								  const ImageSet *images,
								  const SectorDesignRequest &request,
								  SectorDesignPlan &plan)
{
	std::set<int> sourceLines(plan.doorSourceLines.begin(),
							 plan.doorSourceLines.end());
	for (const PlannedConnectionChange &connection : plan.connections)
		if (connection.connection == SectorConnection::door)
			sourceLines.insert(connection.line);

	SectorDesignTextureWeights upperWeights;
	SectorDesignTextureWeights middleWeights;
	for (int line : sourceLines)
	{
		if (!doc.isLinedef(line))
			continue;
		const LineDef &linedef = *doc.linedefs[line];
		const double length = doc.calcLength(linedef);
		for (int side : {linedef.right, linedef.left})
		{
			if (!doc.isSidedef(side))
				continue;
			const SideDef &sidedef = *doc.sidedefs[side];
			upperWeights.add(sidedef.UpperTex(), length, line);
			middleWeights.add(sidedef.MidTex(), length, line);
		}
	}

	plan.inferredDoorFaceTexture = !upperWeights.empty() ?
			upperWeights.best() :
			!middleWeights.empty() ? middleWeights.best() :
			config.default_wall_tex;
	if (plan.inferredDoorFaceTexture.empty())
		plan.inferredDoorFaceTexture = config.default_wall_tex;

	// Generated track walls do not exist yet, so they have no preexisting
	// one-sided middle textures to inherit.  Smart Door's next deterministic
	// fallback is the inferred face texture.
	plan.inferredDoorTrackTexture = plan.inferredDoorFaceTexture;
	if (plan.inferredDoorTrackTexture.empty())
		plan.inferredDoorTrackTexture = config.default_wall_tex;

	plan.doorFaceTexture = request.doorOptions.faceTexture.empty() ?
			plan.inferredDoorFaceTexture :
			request.doorOptions.faceTexture;
	plan.doorTrackTexture = request.doorOptions.trackTexture.empty() ?
			plan.inferredDoorTrackTexture :
			request.doorOptions.trackTexture;

	// Freeze the previewed resolution into the apply subplan.  Apply still
	// replans the whole gesture, so Auto remains contextual without allowing
	// insertion defaults to change the answer after geometry is created.
	plan.resolvedDoorOptions = request.doorOptions;
	plan.resolvedDoorOptions.faceTexture = plan.doorFaceTexture;
	plan.resolvedDoorOptions.trackTexture = plan.doorTrackTexture;

	if (!images)
		return;
	if (!images->W_TextureIsKnown(config, plan.doorFaceTexture))
		AddIssue(plan, SectorDesignIssueSeverity::warning,
				 SString::printf(
					 "Door face texture '%s' is not currently loaded.",
					 plan.doorFaceTexture.c_str()));
	if (!images->W_TextureIsKnown(config, plan.doorTrackTexture))
		AddIssue(plan, SectorDesignIssueSeverity::warning,
				 SString::printf(
					 "Door track texture '%s' is not currently loaded.",
					 plan.doorTrackTexture.c_str()));
}

void ResolveProperties(const Document &doc, const ConfigData &config,
					   const ImageSet *images,
					   const SectorDesignRequest &request,
					   MapFormat format, SectorDesignPlan &plan)
{
	if (request.properties.modelSector >= 0 &&
		!doc.isSector(request.properties.modelSector))
	{
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 "The selected model sector no longer exists.",
				 request.properties.modelSector);
	}
	const SectorPropertyOptions inheritedOnly;
	std::vector<int> resolvedFloors(plan.shapes.size(), 0);
	std::vector<int> resolvedCeilings(plan.shapes.size(), 128);
	std::vector<int> resolvedLights(plan.shapes.size(), 160);
	for (size_t shapeIndex = 0;
		 shapeIndex < plan.shapes.size(); ++shapeIndex)
	{
		PlannedSectorShape &shape = plan.shapes[shapeIndex];
		if (shape.modelShape >= static_cast<int>(shapeIndex) ||
			shape.modelShape < -1)
		{
			AddIssue(plan, SectorDesignIssueSeverity::error,
					 "A generated sector has an invalid plan-local model "
					 "reference.");
			continue;
		}
		const SectorPropertyOptions &options =
				shape.inheritModelOnly ?
					inheritedOnly :
					(shape.properties ?
						*shape.properties : request.properties);
		int model = shape.modelSector;
		if (model < 0 && doc.isSector(options.modelSector))
			model = options.modelSector;
		if (model < 0 && shape.modelShape < 0)
			model = ChooseModelSector(doc, request);
		shape.modelSector = model;

		int baseFloor = 0;
		int baseCeiling = 128;
		int baseLight = 160;
		if (shape.modelShape >= 0)
		{
			baseFloor = resolvedFloors[shape.modelShape];
			baseCeiling = resolvedCeilings[shape.modelShape];
			baseLight = resolvedLights[shape.modelShape];
		}
		else if (doc.isSector(model))
		{
			const Sector &sector = *doc.sectors[model];
			baseFloor = sector.floorh;
			baseCeiling = sector.ceilh;
			baseLight = sector.light;
		}
		int floor = baseFloor + shape.floorDelta;
		int ceiling = baseCeiling + shape.ceilingDelta;
		if (options.floorMode == SectorValueMode::absolute)
			floor = options.floorValue + shape.floorDelta;
		else if (options.floorMode == SectorValueMode::relative)
			floor += options.floorValue;
		if (options.ceilingMode == SectorValueMode::absolute)
			ceiling = options.ceilingValue + shape.ceilingDelta;
		else if (options.ceilingMode == SectorValueMode::relative)
			ceiling += options.ceilingValue;
		if (shape.closed)
			ceiling = floor;
		resolvedFloors[shapeIndex] = floor;
		resolvedCeilings[shapeIndex] = ceiling;
		if (floor > ceiling)
			AddIssue(plan, SectorDesignIssueSeverity::error,
					 "A generated sector would have its floor above its ceiling.");
		else if (!shape.closed &&
				 (shape.role == DesignPreviewRole::stair ||
				  IsArchitecturePreviewRole(shape.role)) &&
				 ceiling - floor < config.miscInfo.player_h)
			AddIssue(plan, SectorDesignIssueSeverity::warning,
					 shape.role == DesignPreviewRole::stair ?
						"A generated stair step has inadequate player clearance." :
						"A generated architectural sector has inadequate "
						"player clearance.");
		int light = baseLight;
		if (options.lightMode == SectorValueMode::absolute)
			light = options.lightValue;
		else if (options.lightMode == SectorValueMode::relative)
			light += options.lightValue;
		resolvedLights[shapeIndex] = light;
		if (light < 0 || light > 255)
			AddIssue(plan, SectorDesignIssueSeverity::error,
					 "The requested light level is outside 0 through 255.");
		if (options.sectorType)
		{
			const int maximum = format == MapFormat::udmf ?
					std::numeric_limits<int>::max() :
					std::numeric_limits<uint16_t>::max();
			if (*options.sectorType < 0 || *options.sectorType > maximum)
				AddIssue(plan, SectorDesignIssueSeverity::error,
						 "The requested sector special is outside the active format's range.");
			else if (*options.sectorType != 0 &&
					 config.sector_types.find(*options.sectorType) ==
						config.sector_types.end())
				AddIssue(plan, SectorDesignIssueSeverity::warning,
						 SString::printf(
							"Sector special %d is not declared by the active configuration.",
							*options.sectorType));
		}
		if (options.sectorTag)
		{
			const int minimum = format == MapFormat::udmf ?
					std::numeric_limits<int>::min() : -32767;
			const int maximum = format == MapFormat::udmf ?
					std::numeric_limits<int>::max() :
					std::numeric_limits<int16_t>::max();
			if (*options.sectorTag < minimum ||
				*options.sectorTag > maximum)
				AddIssue(plan, SectorDesignIssueSeverity::error,
						 "The requested sector tag is outside the active format's range.");
		}

		if (!images)
			continue;
		if (!options.wallTexture.empty() &&
			!images->W_TextureIsKnown(config,
					options.wallTexture))
			AddIssue(plan, SectorDesignIssueSeverity::warning,
					 SString::printf("Wall texture '%s' is not currently loaded.",
						options.wallTexture.c_str()));
		if (!options.floorTexture.empty() &&
			!images->W_FlatIsKnown(config,
					options.floorTexture))
			AddIssue(plan, SectorDesignIssueSeverity::warning,
					 SString::printf("Floor flat '%s' is not currently loaded.",
						options.floorTexture.c_str()));
		if (!options.ceilingTexture.empty() &&
			!images->W_FlatIsKnown(config,
					options.ceilingTexture))
			AddIssue(plan, SectorDesignIssueSeverity::warning,
					 SString::printf("Ceiling flat '%s' is not currently loaded.",
						options.ceilingTexture.c_str()));
	}
}

PlannedSectorChange PropertyChangeForExisting(
		const Document &doc, int sector,
		const SectorPropertyOptions &properties,
		SectorDesignPlan &plan)
{
	PlannedSectorChange change;
	change.sector = sector;
	const Sector &source = *doc.sectors[sector];
	if (properties.floorMode == SectorValueMode::absolute)
		change.floor = properties.floorValue;
	else if (properties.floorMode == SectorValueMode::relative)
		change.floor = source.floorh + properties.floorValue;
	if (properties.ceilingMode == SectorValueMode::absolute)
		change.ceiling = properties.ceilingValue;
	else if (properties.ceilingMode == SectorValueMode::relative)
		change.ceiling = source.ceilh + properties.ceilingValue;
	if (properties.lightMode == SectorValueMode::absolute)
		change.light = properties.lightValue;
	else if (properties.lightMode == SectorValueMode::relative)
		change.light = source.light + properties.lightValue;
	if (!properties.floorTexture.empty())
		change.floorTexture = properties.floorTexture;
	if (!properties.ceilingTexture.empty())
		change.ceilingTexture = properties.ceilingTexture;
	change.type = properties.sectorType;
	change.tag = properties.sectorTag;

	const int floor = change.floor.value_or(source.floorh);
	const int ceiling = change.ceiling.value_or(source.ceilh);
	if (floor > ceiling)
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 "Inset properties would put a floor above its ceiling.",
				 sector);
	const int light = change.light.value_or(source.light);
	if (light < 0 || light > 255)
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 "Inset light is outside 0 through 255.", sector);
	return change;
}

void ValidatePath(const Document &doc, const ConfigData &config,
				  const SectorDesignRequest &request,
				  MapFormat format, SectorDesignPlan &plan,
				  std::vector<v2double_t> &path)
{
	path = CleanPath(format, path);
	if (path.size() < 3)
	{
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 "The generated outline needs at least three distinct vertices.");
		return;
	}
	for (const v2double_t &point : path)
		if (!CoordinateIsSafe(format, point))
			AddIssue(plan, SectorDesignIssueSeverity::error,
					 "A generated coordinate is outside the safe map range.",
					 -1, -1, point);
	if (std::abs(SignedArea(path)) <= PLAN_EPSILON)
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 "The generated outline has zero area.");
	const double minimumArea =
			format == MapFormat::udmf ? PLAN_EPSILON : 1.0;
	if (std::abs(SignedArea(path)) < minimumArea)
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 "The generated outline is a sliver after quantization.");
	if (PathSelfIntersects(path))
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 "The generated outline intersects itself.");
	const int duplicateSector =
			MatchingExistingSectorBoundary(doc, format, path);
	if (duplicateSector >= 0)
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 "The generated outline exactly repeats an existing sector "
				 "boundary and would not create anything. Drag a shorter "
				 "distance or use the other side.",
				 duplicateSector);

	for (size_t index = 0; index < path.size(); ++index)
	{
		const v2double_t &a = path[index];
		const v2double_t &b = path[(index + 1) % path.size()];
		if ((b - a).hypot() <= PLAN_EPSILON)
			AddIssue(plan, SectorDesignIssueSeverity::error,
					 "Format quantization collapsed a generated edge.", -1, -1,
					 a);
		for (int line = 0; line < doc.numLinedefs(); ++line)
		{
			const LineDef &linedef = *doc.linedefs[line];
			if (!doc.isVertex(linedef.start) || !doc.isVertex(linedef.end))
				continue;
			const v2double_t c = doc.vertices[linedef.start]->xy();
			const v2double_t d = doc.vertices[linedef.end]->xy();
			SegmentRelation relation = ClassifySegments(a, b, c, d);
			if (relation == SegmentRelation::exact)
			{
				plan.reusedVertices += 2;
				if (linedef.type != 0 &&
					std::find(request.anchorLines.begin(),
							  request.anchorLines.end(), line) ==
							request.anchorLines.end())
					AddIssue(plan,
							 request.replaceAffectedSectors ?
								SectorDesignIssueSeverity::warning :
								SectorDesignIssueSeverity::error,
							 request.replaceAffectedSectors ?
								"The outline explicitly replaces a protected action line." :
								"The outline would reuse a protected action line.",
							 -1, line);
			}
			else if (relation == SegmentRelation::proper)
			{
				plan.plannedSplits++;
				if (linedef.type != 0)
					AddIssue(plan,
							 request.replaceAffectedSectors ?
								SectorDesignIssueSeverity::warning :
								SectorDesignIssueSeverity::error,
							 request.replaceAffectedSectors ?
								"The outline explicitly subdivides a protected action line." :
								"The outline crosses a protected action line.",
							 -1, line);
			}
			else if (relation == SegmentRelation::overlap)
			{
				if (SegmentContained(a, b, c, d) ||
					SegmentContained(c, d, a, b))
				{
					plan.plannedSplits++;
				}
				else
				{
					AddIssue(plan, SectorDesignIssueSeverity::error,
							 "The outline partially overlaps an existing line.",
							 -1, line);
				}
			}
		}
	}

	for (int line = 0; line < doc.numLinedefs(); ++line)
	{
		const LineDef &linedef = *doc.linedefs[line];
		if (linedef.type == 0 ||
			!doc.isVertex(linedef.start) || !doc.isVertex(linedef.end))
			continue;
		// Extrusion source seams are deliberately part of the generated
		// outline.  PointInsidePath includes boundary points, so without this
		// exemption an action on the chosen seam is first reported as retained
		// by the connection plan and then incorrectly rejected as consumed.
		// The connection subplan owns that seam: Open and Wall retain its
		// action, while Door explicitly replaces it.
		if (std::find(request.anchorLines.begin(),
					  request.anchorLines.end(), line) !=
				request.anchorLines.end())
			continue;
		const v2double_t start = doc.getStart(linedef).xy();
		const v2double_t end = doc.getEnd(linedef).xy();
		if (PointInsidePath(start, path) && PointInsidePath(end, path))
			AddIssue(plan,
					 request.replaceAffectedSectors ?
						SectorDesignIssueSeverity::warning :
						SectorDesignIssueSeverity::error,
					 request.replaceAffectedSectors ?
						"The generated sector explicitly consumes a protected action line." :
						"The generated sector would consume a protected action line.",
					 -1, line);
	}

	v2double_t sample{0.0, 0.0};
	for (const v2double_t &point : path)
		sample += point;
	sample /= static_cast<double>(path.size());
	Objid host = hover::getNearestSector(doc, sample);
	if (host.type == ObjType::sectors &&
		SectorHasProtectedAction(doc, config, host.num) &&
		std::find(request.targetSectors.begin(),
				  request.targetSectors.end(), host.num) ==
				request.targetSectors.end())
		AddIssue(plan,
				 request.replaceAffectedSectors ?
					SectorDesignIssueSeverity::warning :
					SectorDesignIssueSeverity::error,
				 request.replaceAffectedSectors ?
					"The generated geometry explicitly replaces a protected door or lift sector." :
					"The generated geometry occupies a protected door or lift sector.",
				 host.num, -1, sample);
}

void ValidatePlannedShapeRelations(SectorDesignPlan &plan)
{
	auto shapeDescendsFrom = [&](size_t child, size_t ancestor)
	{
		std::set<int> visited;
		int parent = plan.shapes[child].modelShape;
		while (parent >= 0 &&
			   parent < static_cast<int>(plan.shapes.size()) &&
			   visited.insert(parent).second)
		{
			if (parent == static_cast<int>(ancestor))
				return true;
			parent = plan.shapes[parent].modelShape;
		}
		return false;
	};

	for (size_t child = 0; child < plan.shapes.size(); ++child)
	{
		const int parent = plan.shapes[child].modelShape;
		if (parent < 0 ||
			parent >= static_cast<int>(plan.shapes.size()) ||
			plan.shapes[child].outer.empty() ||
			plan.shapes[parent].outer.empty())
			continue;
		bool contained = PointInsidePath(
				plan.shapes[child].outer.front(),
				plan.shapes[parent].outer);
		for (size_t childEdge = 0;
			 childEdge < plan.shapes[child].outer.size() && contained;
			 ++childEdge)
			for (size_t parentEdge = 0;
				 parentEdge < plan.shapes[parent].outer.size();
				 ++parentEdge)
			{
				const SegmentRelation relation = ClassifySegments(
						plan.shapes[child].outer[childEdge],
						plan.shapes[child].outer[
							(childEdge + 1) %
							 plan.shapes[child].outer.size()],
						plan.shapes[parent].outer[parentEdge],
						plan.shapes[parent].outer[
							(parentEdge + 1) %
							 plan.shapes[parent].outer.size()]);
				if (relation == SegmentRelation::proper ||
					relation == SegmentRelation::overlap)
				{
					contained = false;
					break;
				}
			}
		if (!contained)
		{
			AddIssue(plan, SectorDesignIssueSeverity::error,
					 "A generated sector leaves its declared parent "
					 "structure.");
			return;
		}
	}

	for (size_t first = 0; first < plan.shapes.size(); first++)
	for (size_t second = first + 1; second < plan.shapes.size(); second++)
	{
		const auto &a = plan.shapes[first].outer;
		const auto &b = plan.shapes[second].outer;
		bool boundaryContact = false;
		bool invalid = false;
		for (size_t ai = 0; ai < a.size() && !invalid; ai++)
		for (size_t bi = 0; bi < b.size(); bi++)
		{
			SegmentRelation relation = ClassifySegments(
					a[ai], a[(ai + 1) % a.size()],
					b[bi], b[(bi + 1) % b.size()]);
			if (relation == SegmentRelation::proper ||
				relation == SegmentRelation::overlap)
			{
				invalid = true;
				break;
			}
			boundaryContact |= relation == SegmentRelation::exact ||
							   relation == SegmentRelation::touch;
		}
		if (!invalid && !boundaryContact)
		{
			const bool firstInsideSecond =
					PointInsidePath(a.front(), b);
			const bool secondInsideFirst =
					PointInsidePath(b.front(), a);
			if (firstInsideSecond || secondInsideFirst)
			{
				const bool ownedContainment =
						(firstInsideSecond &&
						 shapeDescendsFrom(first, second)) ||
						(secondInsideFirst &&
						 shapeDescendsFrom(second, first));
				invalid = !ownedContainment;
			}
		}
		if (invalid)
		{
			AddIssue(plan, SectorDesignIssueSeverity::error,
					 "Generated sector outlines overlap ambiguously.");
			return;
		}
	}
}

int EstimateCreatedLines(const Document &doc,
						 const SectorDesignPlan &plan)
{
	using SegmentKey = std::array<double, 4>;
	std::set<SegmentKey> segments;
	auto addPath = [&](const std::vector<v2double_t> &path)
	{
		for (size_t index = 0; index < path.size(); ++index)
		{
			v2double_t a = path[index];
			v2double_t b = path[(index + 1) % path.size()];
			if (std::tie(b.x, b.y) < std::tie(a.x, a.y))
				std::swap(a, b);
			segments.insert({a.x, a.y, b.x, b.y});
		}
	};
	for (const PlannedSectorShape &shape : plan.shapes)
	{
		addPath(shape.outer);
		for (const auto &hole : shape.holes)
			addPath(hole);
	}

	int created = 0;
	for (const SegmentKey &segment : segments)
	{
		const v2double_t a{segment[0], segment[1]};
		const v2double_t b{segment[2], segment[3]};
		bool reused = false;
		int crossings = 0;
		for (const std::shared_ptr<LineDef> &line : doc.linedefs)
		{
			if (!doc.isVertex(line->start) || !doc.isVertex(line->end))
				continue;
			const SegmentRelation relation = ClassifySegments(
					a, b, doc.vertices[line->start]->xy(),
					doc.vertices[line->end]->xy());
			reused |= relation == SegmentRelation::exact;
			crossings += relation == SegmentRelation::proper;
		}
		if (!reused)
			created += crossings + 1;
	}
	return created;
}

void PlanRectangle(const Document &doc, const SectorDesignRequest &request,
				   MapFormat format, SectorDesignPlan &plan)
{
	if (request.anchors.size() < 2)
	{
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 "Drag between two corners to create a room.");
		return;
	}
	const v2double_t a = request.anchors[0];
	const v2double_t b = request.anchors[1];
	PlannedSectorShape shape;
	if (request.useConstructionBasis)
	{
		const v2double_t &primary = request.constructionPrimary;
		const v2double_t &secondary = request.constructionSecondary;
		const double determinant =
				primary.x * secondary.y - primary.y * secondary.x;
		if (!std::isfinite(primary.x) || !std::isfinite(primary.y) ||
				!std::isfinite(secondary.x) ||
				!std::isfinite(secondary.y) ||
				std::abs(determinant) <= PLAN_EPSILON)
		{
			AddIssue(plan, SectorDesignIssueSeverity::error,
					"The active construction grid has invalid room axes.");
			return;
		}
		const v2double_t delta = b - a;
		const double alongPrimary =
				(delta.x * secondary.y - delta.y * secondary.x) /
				determinant;
		const double alongSecondary =
				(primary.x * delta.y - primary.y * delta.x) /
				determinant;
		const v2double_t primaryCorner =
				a + primary * alongPrimary;
		const v2double_t secondaryCorner =
				a + secondary * alongSecondary;
		shape.outer = {a, secondaryCorner, b, primaryCorner};
	}
	else
		shape.outer = {
				{a.x, a.y}, {a.x, b.y}, {b.x, b.y}, {b.x, a.y}};
	MakeClockwise(shape.outer);
	plan.shapes.push_back(std::move(shape));
}

std::vector<v2double_t> RadialProfile(
		const v2double_t &center, double radius, double rotation,
		const std::vector<double> &radii)
{
	std::vector<v2double_t> result;
	result.reserve(radii.size());
	for (size_t index = 0; index < radii.size(); ++index)
	{
		const double angle = rotation -
				2.0 * std::numbers::pi * index / radii.size();
		const double localRadius = radius * radii[index];
		result.emplace_back(
				center.x + std::cos(angle) * localRadius,
				center.y + std::sin(angle) * localRadius);
	}
	return result;
}

std::vector<double> RegularRadii(int vertices)
{
	return std::vector<double>(std::max(0, vertices), 1.0);
}

std::vector<double> StarRadii(int points, double inner)
{
	std::vector<double> radii(points * 2);
	for (size_t index = 0; index < radii.size(); ++index)
		radii[index] = index % 2 == 0 ? 1.0 : inner;
	return radii;
}

std::vector<double> GearRadii(int teeth, double root)
{
	std::vector<double> radii(teeth * 4);
	for (size_t index = 0; index < radii.size(); ++index)
		radii[index] = index % 4 == 1 || index % 4 == 2 ? 1.0 : root;
	return radii;
}

std::vector<double> CrossRadii(bool maltese, double inner)
{
	const int vertices = maltese ? 16 : 12;
	std::vector<double> radii(vertices);
	for (int index = 0; index < vertices; ++index)
	{
		if (maltese)
		{
			const int phase = index % 4;
			radii[index] = phase == 1 || phase == 2 ? 1.0 : inner;
		}
		else
			radii[index] = index % 3 == 0 ? 1.0 : inner;
	}
	return radii;
}

std::vector<double> SawbladeRadii(int teeth, double root)
{
	std::vector<double> radii(teeth * 4);
	for (size_t index = 0; index < radii.size(); ++index)
	{
		const int phase = static_cast<int>(index % 4);
		radii[index] = phase == 1 ? 1.0 :
				phase == 2 ? std::lerp(root, 1.0, 0.72) : root;
	}
	return radii;
}

std::vector<double> RosetteRadii(int lobes, int samples, double inner)
{
	std::vector<double> radii(lobes * samples);
	for (size_t index = 0; index < radii.size(); ++index)
	{
		const double phase = 2.0 * std::numbers::pi *
				(index % samples) / samples;
		const double wave = 0.5 + 0.5 * std::cos(phase);
		radii[index] = inner + (1.0 - inner) * wave;
	}
	return radii;
}

void PlanProfilePolygon(const SectorDesignRequest &request,
						SectorDesignPlan &plan)
{
	if (request.anchors.size() < 2)
	{
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 "Choose a center and radius for the polygon.");
		return;
	}
	if (request.polygonProfile == SectorPolygonProfile::customRegular &&
		(request.polygonSides < 3 || request.polygonSides > 64))
	{
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 "Polygon sides must be between 3 and 64.");
		return;
	}
	const v2double_t center = request.anchors[0];
	const v2double_t delta = request.anchors[1] - center;
	const double radius = delta.hypot();
	if (radius <= PLAN_EPSILON)
	{
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 "The polygon radius is too small.");
		return;
	}
	const double rotation = delta.atan2() + request.rotation;
	const double inner = std::clamp(
			request.polygonInnerRatio, 0.1, 0.95);
	std::vector<double> radii;
	switch (request.polygonProfile)
	{
		case SectorPolygonProfile::customRegular:
			radii = RegularRadii(request.polygonSides);
			break;
		case SectorPolygonProfile::triangle:
			radii = RegularRadii(3);
			break;
		case SectorPolygonProfile::square:
			radii = RegularRadii(4);
			break;
		case SectorPolygonProfile::pentagon:
			radii = RegularRadii(5);
			break;
		case SectorPolygonProfile::hexagon:
			radii = RegularRadii(6);
			break;
		case SectorPolygonProfile::octagon:
			radii = RegularRadii(8);
			break;
		case SectorPolygonProfile::decagon:
			radii = RegularRadii(10);
			break;
		case SectorPolygonProfile::dodecagon:
			radii = RegularRadii(12);
			break;
		case SectorPolygonProfile::circle16:
			radii = RegularRadii(16);
			break;
		case SectorPolygonProfile::circle32:
			radii = RegularRadii(32);
			break;
		case SectorPolygonProfile::circle64:
			radii = RegularRadii(64);
			break;
		case SectorPolygonProfile::circle96:
			radii = RegularRadii(96);
			break;
		case SectorPolygonProfile::star5:
			radii = StarRadii(5, inner);
			break;
		case SectorPolygonProfile::star8:
			radii = StarRadii(8, inner);
			break;
		case SectorPolygonProfile::star12:
			radii = StarRadii(12, inner);
			break;
		case SectorPolygonProfile::star16:
			radii = StarRadii(16, inner);
			break;
		case SectorPolygonProfile::greekCross:
			radii = CrossRadii(false, inner);
			break;
		case SectorPolygonProfile::malteseCross:
			radii = CrossRadii(true, inner);
			break;
		case SectorPolygonProfile::gear8:
			radii = GearRadii(8, inner);
			break;
		case SectorPolygonProfile::gear16:
			radii = GearRadii(16, inner);
			break;
		case SectorPolygonProfile::gear24:
			radii = GearRadii(24, inner);
			break;
		case SectorPolygonProfile::sawblade32:
			radii = SawbladeRadii(32, inner);
			break;
		case SectorPolygonProfile::trefoil:
			radii = RosetteRadii(3, 12, inner);
			break;
		case SectorPolygonProfile::quatrefoil:
			radii = RosetteRadii(4, 12, inner);
			break;
		case SectorPolygonProfile::cinquefoil:
			radii = RosetteRadii(5, 12, inner);
			break;
		case SectorPolygonProfile::octofoil:
			radii = RosetteRadii(8, 12, inner);
			break;
		case SectorPolygonProfile::rosette8:
			radii = RosetteRadii(8, 6, inner);
			break;
		case SectorPolygonProfile::rosette16:
			radii = RosetteRadii(16, 6, inner);
			break;
		case SectorPolygonProfile::rosette24:
			radii = RosetteRadii(24, 6, inner);
			break;
		case SectorPolygonProfile::cathedralTracery32:
			radii = RosetteRadii(32, 6, inner);
			break;
		case SectorPolygonProfile::cathedralTracery48:
			radii = RosetteRadii(48, 6, inner);
			break;
	}
	PlannedSectorShape shape;
	shape.outer = RadialProfile(center, radius, rotation, radii);
	MakeClockwise(shape.outer);
	plan.shapes.push_back(std::move(shape));
}

const char *ArchitectureStyleName(SectorArchitectureStyle style)
{
	switch (style)
	{
		case SectorArchitectureStyle::functional: return "Functional";
		case SectorArchitectureStyle::classical: return "Classical";
		case SectorArchitectureStyle::romanesque: return "Romanesque";
		case SectorArchitectureStyle::gothic: return "Gothic";
		case SectorArchitectureStyle::industrial: return "Industrial";
		case SectorArchitectureStyle::artDeco: return "Art Deco";
		case SectorArchitectureStyle::infernal: return "Infernal";
	}
	return "Architectural";
}

const char *ArchitectureElementName(SectorArchitectureElement element)
{
	return M_ArchitectureDescriptor(element).label;
}

DesignPreviewRole ArchitecturePreviewRole(
		SectorArchitectureElement element)
{
	return M_ArchitectureDescriptor(element).role;
}

bool IsArchitecturePreviewRole(DesignPreviewRole role)
{
	return role == DesignPreviewRole::architecture ||
			role == DesignPreviewRole::architectureFloor ||
			role == DesignPreviewRole::architectureCirculation ||
			role == DesignPreviewRole::architectureWater ||
			role == DesignPreviewRole::architectureWall ||
			role == DesignPreviewRole::architectureCeiling;
}

double ArchitectureMaximumScale(SectorArchitectureElement element)
{
	switch (element)
	{
		case SectorArchitectureElement::triumphalArch:
		case SectorArchitectureElement::cornerPiers:
			return 1.25;
		case SectorArchitectureElement::sanctuary:
			return 1.4;
		case SectorArchitectureElement::fortifiedKeep:
			return 1.5;
		default:
			return 1.0;
	}
}

bool ArchitectureUsesSectionStyle(SectorArchitectureElement element)
{
	return M_ArchitectureHasControl(
			element, SectorArchitectureControl::style);
}

bool ArchitectureUsesBays(SectorArchitectureElement element)
{
	return M_ArchitectureHasControl(
			element, SectorArchitectureControl::bays);
}

bool ArchitectureUsesHeight(SectorArchitectureElement element)
{
	return M_ArchitectureHasControl(
			element, SectorArchitectureControl::height);
}

bool UsesLegacySupportBuilder(SectorArchitectureElement element)
{
	switch (element)
	{
		case SectorArchitectureElement::pillar:
		case SectorArchitectureElement::pairedPillars:
		case SectorArchitectureElement::triumphalArch:
		case SectorArchitectureElement::cornerPiers:
		case SectorArchitectureElement::colonnade:
		case SectorArchitectureElement::arcade:
		case SectorArchitectureElement::cloister:
		case SectorArchitectureElement::hypostyleHall:
		case SectorArchitectureElement::buttressedBay:
		case SectorArchitectureElement::flyingButtresses:
		case SectorArchitectureElement::nave:
		case SectorArchitectureElement::transept:
		case SectorArchitectureElement::apse:
		case SectorArchitectureElement::rotunda:
		case SectorArchitectureElement::sanctuary:
		case SectorArchitectureElement::fortifiedKeep:
			return true;
		default:
			return false;
	}
}

bool ArchitectureUsesDirection(SectorArchitectureElement element)
{
	switch (element)
	{
		case SectorArchitectureElement::grandStair:
		case SectorArchitectureElement::raisedBridge:
		case SectorArchitectureElement::splitLevelStage:
		case SectorArchitectureElement::cornerTerraces:
		case SectorArchitectureElement::horseshoeAmphitheater:
		case SectorArchitectureElement::switchbackStair:
		case SectorArchitectureElement::bifurcatedStair:
		case SectorArchitectureElement::spiralStair:
		case SectorArchitectureElement::steppedCascade:
			return true;
		default:
			return false;
	}
}

struct ArchitecturalSupport
{
	v2double_t center{0.0, 0.0};
	double scale = 1.0;
};

std::vector<v2double_t> StyledPillar(
		const v2double_t &center, double size,
		SectorArchitectureStyle style, double rotation)
{
	const double radius = size * 0.5;
	switch (style)
	{
		case SectorArchitectureStyle::functional:
			return RadialProfile(center, radius, rotation,
								 RegularRadii(4));
		case SectorArchitectureStyle::classical:
			return RadialProfile(center, radius, rotation,
								 RegularRadii(16));
		case SectorArchitectureStyle::romanesque:
			return RadialProfile(center, radius, rotation,
								 RegularRadii(8));
		case SectorArchitectureStyle::gothic:
			// Four deep lobes approximate a clustered cathedral pier while
			// remaining portable straight-line geometry.
			return RadialProfile(center, radius, rotation,
								 RosetteRadii(4, 6, 0.54));
		case SectorArchitectureStyle::industrial:
			return RadialProfile(center, radius, rotation,
								 GearRadii(8, 0.82));
		case SectorArchitectureStyle::artDeco:
			return RadialProfile(center, radius, rotation,
								 StarRadii(8, 0.76));
		case SectorArchitectureStyle::infernal:
			// Preserve an aggressively pointed silhouette without valleys
			// that can become several cells in the sector-loop inserter.
			return RadialProfile(center, radius, rotation,
								 StarRadii(8, 0.70));
	}
	return {};
}

std::vector<v2double_t> BeveledRectangle(
		double minX, double minY, double maxX, double maxY,
		double bevel)
{
	bevel = std::clamp(
			bevel, 0.0,
			std::max(0.0, std::min(maxX - minX, maxY - minY) * 0.5));
	if (bevel <= PLAN_EPSILON)
		return {
			{minX, minY}, {minX, maxY},
			{maxX, maxY}, {maxX, minY}
		};
	return {
		{minX + bevel, minY}, {minX, minY + bevel},
		{minX, maxY - bevel}, {minX + bevel, maxY},
		{maxX - bevel, maxY}, {maxX, maxY - bevel},
		{maxX, minY + bevel}, {maxX - bevel, minY}
	};
}

std::vector<v2double_t> EllipseProfile(
		const v2double_t &center, double radiusX, double radiusY,
		int vertices, double rotation)
{
	std::vector<v2double_t> result;
	vertices = std::clamp(vertices, 8, 64);
	const double cosine = std::cos(rotation);
	const double sine = std::sin(rotation);
	for (int index = 0; index < vertices; ++index)
	{
		const double angle =
				2.0 * std::numbers::pi * index / vertices;
		const double x = std::cos(angle) * radiusX;
		const double y = std::sin(angle) * radiusY;
		result.push_back({
			center.x + x * cosine - y * sine,
			center.y + x * sine + y * cosine
		});
	}
	return result;
}

bool ArchitectureShapeInsideHost(
		const Document &doc, int host,
		const std::vector<v2double_t> &outline,
		std::optional<v2double_t> &firstOutside)
{
	if (!doc.isSector(host) || outline.empty())
		return false;

	auto pointBelongsToHost = [&](const v2double_t &point)
	{
		bool inside = false;
		for (const std::shared_ptr<LineDef> &line : doc.linedefs)
		{
			if (!doc.isVertex(line->start) ||
				!doc.isVertex(line->end))
				continue;
			const int right = SectorForSide(doc, line->right);
			const int left = SectorForSide(doc, line->left);
			if ((right == host) == (left == host))
				continue;
			const v2double_t start = doc.getStart(*line).xy();
			const v2double_t end = doc.getEnd(*line).xy();
			// Boundary points belong to the host independent of linedef
			// direction. This is also the exact seam used to connect a
			// zero-margin floor to an adjacent existing sector.
			if (PointOnSegment(point, start, end))
				return true;
			if ((start.y > point.y) != (end.y > point.y) &&
				point.x < (end.x - start.x) *
						(point.y - start.y) /
						(end.y - start.y) + start.x)
				inside = !inside;
		}
		if (inside)
			return true;
		if (!firstOutside)
			firstOutside = point;
		return false;
	};

	for (size_t index = 0; index < outline.size(); ++index)
	{
		const v2double_t &start = outline[index];
		const v2double_t &end = outline[(index + 1) % outline.size()];
		if (!pointBelongsToHost(start) ||
			!pointBelongsToHost((start + end) * 0.5))
			return false;
	}

	v2double_t center{0.0, 0.0};
	for (const v2double_t &point : outline)
		center += point;
	center /= static_cast<double>(outline.size());
	if (!pointBelongsToHost(center))
		return false;

	// Catch an off-center hole or foreign cell completely enclosed by a
	// large generated structure. Vertex and centroid samples alone cannot
	// detect that topology.
	for (int line = 0; line < doc.numLinedefs(); ++line)
	{
		const LineDef &linedef = *doc.linedefs[line];
		if (!doc.isVertex(linedef.start) ||
			!doc.isVertex(linedef.end))
			continue;
		const int right = SectorForSide(doc, linedef.right);
		const int left = SectorForSide(doc, linedef.left);
		if ((right == host) == (left == host))
			continue;
		const v2double_t start = doc.getStart(linedef).xy();
		const v2double_t end = doc.getEnd(linedef).xy();
		if (PointStrictlyInsidePath(start, outline) ||
			PointStrictlyInsidePath(end, outline) ||
			PointStrictlyInsidePath((start + end) * 0.5, outline))
		{
			if (!firstOutside)
				firstOutside = (start + end) * 0.5;
			return false;
		}
		for (size_t edge = 0; edge < outline.size(); ++edge)
			if (ClassifySegments(
					outline[edge],
					outline[(edge + 1) % outline.size()],
					start, end) == SegmentRelation::proper)
			{
				if (!firstOutside)
					firstOutside = outline[edge];
				return false;
			}
	}
	return true;
}

void PlanVolumetricArchitecture(
		const Document &doc, const ConfigData &config,
		const SectorDesignRequest &request,
		SectorDesignPlan &plan, MapFormat format,
		int host, int hostShape,
		bool generatedHost, double minX, double minY,
		double maxX, double maxY)
{
	const double margin = request.architectureMargin;
	const double innerMinX = minX + margin;
	const double innerMaxX = maxX - margin;
	const double innerMinY = minY + margin;
	const double innerMaxY = maxY - margin;
	const double innerWidth = innerMaxX - innerMinX;
	const double innerHeight = innerMaxY - innerMinY;
	const v2double_t center{
		(innerMinX + innerMaxX) * 0.5,
		(innerMinY + innerMaxY) * 0.5
	};
	if (innerWidth <= PLAN_EPSILON || innerHeight <= PLAN_EPSILON)
	{
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 "The edge margin consumes the complete architectural "
				 "footprint.", host, -1, center);
		return;
	}

	const int elevation = std::max(
			1, static_cast<int>(std::lround(request.architectureHeight)));
	if (ArchitectureUsesHeight(request.architectureElement) &&
		std::abs(request.architectureHeight - elevation) > PLAN_EPSILON)
		AddIssue(plan, SectorDesignIssueSeverity::warning,
				 SString::printf(
					 "Structure elevation is quantized from %.3f to %d "
					 "map units.", request.architectureHeight, elevation),
				 host, -1, center);

	const bool horizontal = innerWidth >= innerHeight;
	const double longBegin = horizontal ? innerMinX : innerMinY;
	const double longEnd = horizontal ? innerMaxX : innerMaxY;
	const double longSpan = longEnd - longBegin;
	const double crossCenter = horizontal ? center.y : center.x;
	const double crossSpan = horizontal ? innerHeight : innerWidth;
	const double crossExtent = crossSpan * 0.5;
	auto position = [&](double along, double cross)
	{
		return horizontal ?
				v2double_t{along, crossCenter + cross} :
				v2double_t{crossCenter + cross, along};
	};
	auto localRectangle = [&](double along0, double along1,
							 double cross0, double cross1)
	{
		return std::vector<v2double_t>{
			position(along0, cross0), position(along0, cross1),
			position(along1, cross1), position(along1, cross0)
		};
	};
	auto localPath = [&](std::initializer_list<v2double_t> points)
	{
		std::vector<v2double_t> result;
		result.reserve(points.size());
		for (const v2double_t &point : points)
			result.push_back(position(point.x, point.y));
		return result;
	};
	const bool forward = horizontal ?
			request.anchors.back().x >= request.anchors.front().x :
			request.anchors.back().y >= request.anchors.front().y;
	auto directedAlong = [&](double distance)
	{
		return forward ? longBegin + distance : longEnd - distance;
	};
	auto directedRectangle = [&](double distance0, double distance1,
								 double cross0, double cross1)
	{
		const double along0 = directedAlong(distance0);
		const double along1 = directedAlong(distance1);
		return localRectangle(
				std::min(along0, along1), std::max(along0, along1),
				cross0, cross1);
	};
	auto localCross = [&](double armWidth)
	{
		const double alongCenter = (longBegin + longEnd) * 0.5;
		const double half = std::min({
			armWidth * 0.5, longSpan * 0.48, crossSpan * 0.48
		});
		return localPath({
			{longBegin, -half},
			{alongCenter - half, -half},
			{alongCenter - half, -crossExtent},
			{alongCenter + half, -crossExtent},
			{alongCenter + half, -half},
			{longEnd, -half},
			{longEnd, half},
			{alongCenter + half, half},
			{alongCenter + half, crossExtent},
			{alongCenter - half, crossExtent},
			{alongCenter - half, half},
			{longBegin, half}
		});
	};
	auto radialRectangle = [&](double angle, double innerRadius,
							   double outerRadius, double width)
	{
		const v2double_t direction{std::cos(angle), std::sin(angle)};
		const v2double_t normal{-direction.y, direction.x};
		const v2double_t inner = center + direction * innerRadius;
		const v2double_t outer = center + direction * outerRadius;
		const v2double_t half = normal * (width * 0.5);
		return std::vector<v2double_t>{
			inner - half, inner + half, outer + half, outer - half
		};
	};

	const size_t firstStructure = plan.shapes.size();
	const DesignPreviewRole previewRole =
			ArchitecturePreviewRole(request.architectureElement);
	int outsideStructures = 0;
	std::optional<v2double_t> firstOutside;
	auto addShape = [&](std::vector<v2double_t> outline,
					 int floorDelta, int ceilingDelta,
					 bool closed, int parentShape = -2)
	{
		MakeClockwise(outline);
		bool inside = true;
		if (generatedHost)
		{
			const std::vector<v2double_t> &hostOutline =
					plan.shapes[hostShape].outer;
			for (const v2double_t &point : outline)
				if (!PointInsidePath(point, hostOutline))
				{
					inside = false;
					if (!firstOutside)
						firstOutside = point;
					break;
				}
		}
		else
			inside = ArchitectureShapeInsideHost(
					doc, host, outline, firstOutside);

		PlannedSectorShape shape;
		shape.outer = std::move(outline);
		shape.role = inside ?
				previewRole :
				DesignPreviewRole::conflict;
		shape.modelSector = host;
		shape.modelShape = parentShape == -2 ? hostShape : parentShape;
		shape.inheritModelOnly = shape.modelShape >= 0;
		shape.floorDelta = floorDelta;
		shape.ceilingDelta = ceilingDelta;
		shape.closed = closed;
		const int index = static_cast<int>(plan.shapes.size());
		plan.shapes.push_back(std::move(shape));
		if (!inside)
			outsideStructures++;
		return index;
	};
	auto requireDimensions = [&](double width, double height,
								 const char *explanation)
	{
		if (innerWidth + PLAN_EPSILON >= width &&
			innerHeight + PLAN_EPSILON >= height)
			return true;
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 SString::printf(
					 "%s needs at least %.1fx%.1f units inside the edge "
					 "margin; only %.1fx%.1f are available.",
					 explanation, width, height,
					 innerWidth, innerHeight),
				 host, -1, center);
		return false;
	};
	auto warnNarrowPassage = [&](double passage, const char *name)
	{
		const double required =
				std::max(0, config.miscInfo.player_r * 2);
		if (required <= 0.0 ||
			passage + PLAN_EPSILON >= required)
			return;
		AddIssue(plan, SectorDesignIssueSeverity::warning,
				 SString::printf(
					 "%s leaves %.1f units of passage width, below the "
					 "configured %.1f-unit player diameter.",
					 name, passage, required),
				 host, -1, center);
	};

	switch (request.architectureElement)
	{
		case SectorArchitectureElement::raisedDais:
			addShape(BeveledRectangle(
					innerMinX, innerMinY, innerMaxX, innerMaxY,
					request.architectureSize * 0.25),
					elevation, 0, false);
			break;

		case SectorArchitectureElement::sunkenCourt:
			addShape(BeveledRectangle(
					innerMinX, innerMinY, innerMaxX, innerMaxY,
					request.architectureSize * 0.25),
					-elevation, 0, false);
			break;

		case SectorArchitectureElement::tieredZiggurat:
		{
			const double tread = request.architectureSize;
			if (!requireDimensions(
					tread * 4.0 + 2.0, tread * 4.0 + 2.0,
					"The three-tier ziggurat"))
				break;
			const int lower = addShape(BeveledRectangle(
					innerMinX, innerMinY, innerMaxX, innerMaxY,
					tread * 0.2), elevation, 0, false);
			const int middle = addShape(BeveledRectangle(
					innerMinX + tread, innerMinY + tread,
					innerMaxX - tread, innerMaxY - tread,
					tread * 0.15), elevation, 0, false, lower);
			addShape(BeveledRectangle(
					innerMinX + tread * 2.0,
					innerMinY + tread * 2.0,
					innerMaxX - tread * 2.0,
					innerMaxY - tread * 2.0,
					tread * 0.1), elevation, 0, false, middle);
			break;
		}

		case SectorArchitectureElement::grandStair:
		{
			const int steps = request.architectureBays;
			const double tread = longSpan / steps;
			const double halfWidth = std::min(
					crossExtent,
					std::max(request.architectureSize,
							 crossExtent * 0.72));
			if (tread <= PLAN_EPSILON ||
				halfWidth <= PLAN_EPSILON)
			{
				AddIssue(plan, SectorDesignIssueSeverity::error,
						 "The grand staircase has no usable tread or width.",
						 host, -1, center);
				break;
			}
			const bool forward = horizontal ?
					request.anchors.back().x >=
						request.anchors.front().x :
					request.anchors.back().y >=
						request.anchors.front().y;
			for (int level = 0; level < steps; ++level)
			{
				const int segment = forward ?
						level : steps - level - 1;
				addShape(localRectangle(
						longBegin + segment * tread,
						longBegin + (segment + 1) * tread,
						-halfWidth, halfWidth),
						elevation * (level + 1), 0, false);
			}
			break;
		}

		case SectorArchitectureElement::fountainBasin:
		{
			const double radiusX = innerWidth * 0.5;
			const double radiusY = innerHeight * 0.5;
			if (!requireDimensions(
					request.architectureSize * 2.0,
					request.architectureSize * 2.0,
					"The fountain basin"))
				break;
			const int basin = addShape(EllipseProfile(
					center, radiusX, radiusY, 32, 0.0),
					-elevation, 0, false);
			const double pedestalSize = std::min({
				request.architectureSize,
				radiusX,
				radiusY
			});
			addShape(StyledPillar(
					center, pedestalSize,
					request.architectureStyle, request.rotation),
					elevation * 2, 0, true, basin);
			break;
		}

		case SectorArchitectureElement::reflectingPool:
			addShape(BeveledRectangle(
					innerMinX, innerMinY, innerMaxX, innerMaxY,
					std::min(request.architectureSize,
							 std::min(innerWidth, innerHeight) * 0.18)),
					-elevation, 0, false);
			break;

		case SectorArchitectureElement::balconyGallery:
		{
			const double depth = request.architectureSize;
			if (!requireDimensions(
					depth * 2.0 + 2.0, depth * 2.0 + 2.0,
					"The perimeter balcony"))
				break;
			const int gallery = addShape(BeveledRectangle(
					innerMinX, innerMinY,
					innerMaxX, innerMaxY, 0),
					elevation, 0, false);
			addShape(BeveledRectangle(
					innerMinX + depth, innerMinY + depth,
					innerMaxX - depth, innerMaxY - depth, 0),
					-elevation, 0, false, gallery);
			break;
		}

		case SectorArchitectureElement::processionalChannel:
		{
			const double channelWidth =
					std::min(request.architectureSize, crossSpan);
			addShape(localRectangle(
					longBegin, longEnd,
					-channelWidth * 0.5, channelWidth * 0.5),
					-elevation, 0, false);
			break;
		}

		case SectorArchitectureElement::screenWall:
		{
			const double thickness =
					std::min(request.architectureSize, crossSpan);
			const int cells = request.architectureBays * 2 + 1;
			const double cellLength = longSpan / cells;
			if (cellLength <= PLAN_EPSILON ||
				thickness <= PLAN_EPSILON)
			{
				AddIssue(plan, SectorDesignIssueSeverity::error,
						 "The screen wall bays collapse at this footprint "
						 "size.", host, -1, center);
				break;
			}
			for (int cell = 0; cell < cells; cell += 2)
			{
				const double along0 =
						longBegin + cell * cellLength;
				addShape(localRectangle(
						along0, along0 + cellLength,
						-thickness * 0.5, thickness * 0.5),
						0, 0, true);
			}
			break;
		}

		case SectorArchitectureElement::cofferedCeiling:
		{
			const int columns = request.architectureBays;
			const int rows = std::clamp((columns + 1) / 2, 1, 6);
			const double cellLong = longSpan / columns;
			const double cellCross = crossSpan / rows;
			const double rib = std::min({
				request.architectureSize,
				cellLong * 0.4,
				cellCross * 0.4
			});
			if (cellLong - rib <= PLAN_EPSILON ||
				cellCross - rib <= PLAN_EPSILON)
			{
				AddIssue(plan, SectorDesignIssueSeverity::error,
						 "The ceiling coffers collapse between their ribs. "
						 "Reduce Bays or Structure size.",
						 host, -1, center);
				break;
			}
			for (int column = 0; column < columns; ++column)
				for (int row = 0; row < rows; ++row)
				{
					const double along0 =
							longBegin + column * cellLong + rib * 0.5;
					const double along1 =
							longBegin + (column + 1) * cellLong -
								rib * 0.5;
					const double cross0 =
							-crossExtent + row * cellCross + rib * 0.5;
					const double cross1 =
							-crossExtent + (row + 1) * cellCross -
								rib * 0.5;
					addShape(localRectangle(
							along0, along1, cross0, cross1),
							0, elevation, false);
				}
			break;
		}

		case SectorArchitectureElement::groinVaults:
		{
			const int bays = request.architectureBays;
			const double bayLength = longSpan / bays;
			const double gap = std::min({
				request.architectureSize,
				bayLength * 0.35,
				crossSpan * 0.25
			});
			const double radiusLong = (bayLength - gap) * 0.5;
			const double radiusCross = (crossSpan - gap) * 0.5;
			if (radiusLong <= PLAN_EPSILON ||
				radiusCross <= PLAN_EPSILON)
			{
				AddIssue(plan, SectorDesignIssueSeverity::error,
						 "The vault bays collapse between their ribs. "
						 "Reduce Bays or Structure size.",
						 host, -1, center);
				break;
			}
			for (int bay = 0; bay < bays; ++bay)
			{
				const double along =
						longBegin + (bay + 0.5) * bayLength;
				std::vector<v2double_t> outline;
				for (int vertex = 0; vertex < 8; ++vertex)
				{
					const double angle =
							2.0 * std::numbers::pi * vertex / 8.0;
					outline.push_back(position(
							along + std::cos(angle) * radiusLong,
							std::sin(angle) * radiusCross));
				}
				addShape(std::move(outline), 0, elevation, false);
			}
			break;
		}

		case SectorArchitectureElement::raisedBridge:
		{
			const double bridgeWidth =
					std::min(request.architectureSize, crossSpan);
			addShape(localRectangle(
					longBegin, longEnd,
					-bridgeWidth * 0.5, bridgeWidth * 0.5),
					elevation, 0, false);
			break;
		}

		case SectorArchitectureElement::crossCore:
		{
			double styleScale = 1.0;
			switch (request.architectureStyle)
			{
				case SectorArchitectureStyle::classical: styleScale = 1.18; break;
				case SectorArchitectureStyle::romanesque: styleScale = 1.1; break;
				case SectorArchitectureStyle::gothic: styleScale = 0.78; break;
				case SectorArchitectureStyle::industrial: styleScale = 1.3; break;
				case SectorArchitectureStyle::artDeco: styleScale = 0.9; break;
				case SectorArchitectureStyle::infernal: styleScale = 0.7; break;
				default: break;
			}
			addShape(localCross(request.architectureSize * styleScale),
					 0, 0, true);
			break;
		}

		case SectorArchitectureElement::hollowTower:
		{
			const double thickness = std::min({
				request.architectureSize,
				longSpan * 0.18,
				crossSpan * 0.18
			});
			const double cornerSize = std::max(
					thickness, std::min(
						thickness * 1.35,
						std::min(longSpan, crossSpan) * 0.24));
			const double radius = cornerSize * 0.5;
			const double entryHalf = std::min(
					crossExtent - thickness * 1.25,
					std::max(thickness, crossSpan * 0.16));
			if (thickness <= PLAN_EPSILON ||
				entryHalf <= PLAN_EPSILON ||
				longSpan <= cornerSize * 2.25 ||
				crossSpan <= cornerSize * 2.25)
			{
				AddIssue(plan, SectorDesignIssueSeverity::error,
						 "The hollow tower needs more room for its shell, "
						 "corner masses, and opposed entries.",
						 host, -1, center);
				break;
			}
			warnNarrowPassage(
					entryHalf * 2.0,
					"The hollow tower entry");
			for (double along : {longBegin + radius, longEnd - radius})
				for (double cross :
					 {-crossExtent + radius, crossExtent - radius})
					addShape(StyledPillar(
							position(along, cross), cornerSize,
							request.architectureStyle, request.rotation),
							0, 0, true);

			const double wallBegin = longBegin + cornerSize;
			const double wallEnd = longEnd - cornerSize;
			for (double side : {-1.0, 1.0})
				addShape(localRectangle(
						wallBegin, wallEnd,
						side < 0 ? -crossExtent :
							crossExtent - thickness,
						side < 0 ? -crossExtent + thickness :
							crossExtent),
						0, 0, true);
			for (double end : {longBegin, longEnd - thickness})
				for (double side : {-1.0, 1.0})
					addShape(localRectangle(
							end, end + thickness,
							side < 0 ?
								-crossExtent + cornerSize :
								entryHalf,
							side < 0 ?
								-entryHalf :
								crossExtent - cornerSize),
							0, 0, true);
			break;
		}

		case SectorArchitectureElement::buttressedTower:
		{
			const double diameter = std::min(
					request.architectureSize,
					std::min(innerWidth, innerHeight) * 0.42);
			const double coreRadius = diameter * 0.5;
			const double outerRadius =
					std::min(innerWidth, innerHeight) * 0.47;
			if (outerRadius <= coreRadius + 1.0)
			{
				AddIssue(plan, SectorDesignIssueSeverity::error,
						 "The buttressed tower needs room beyond its core.",
						 host, -1, center);
				break;
			}
			addShape(StyledPillar(
					center, diameter, request.architectureStyle,
					request.rotation), 0, 0, true);
			const int buttresses = request.architectureBays;
			const double width = std::min(
					diameter * 0.28,
					2.0 * std::numbers::pi * coreRadius /
						std::max(8, buttresses * 3));
			for (int index = 0; index < buttresses; ++index)
			{
				const double angle = request.rotation +
						2.0 * std::numbers::pi * index / buttresses;
				addShape(radialRectangle(
						angle, coreRadius + PLAN_EPSILON,
						outerRadius, std::max(1.0, width)),
						0, 0, true);
			}
			break;
		}

		case SectorArchitectureElement::shearWallPair:
		{
			const double wall = std::min(
					request.architectureSize, crossSpan * 0.24);
			if (crossSpan - wall * 2.0 <= 2.0)
			{
				AddIssue(plan, SectorDesignIssueSeverity::error,
						 "The paired shear walls leave no usable passage.",
						 host, -1, center);
				break;
			}
			warnNarrowPassage(
					crossSpan - wall * 2.0,
					"The shear-wall passage");
			addShape(localRectangle(
					longBegin, longEnd,
					-crossExtent, -crossExtent + wall),
					0, 0, true);
			addShape(localRectangle(
					longBegin, longEnd,
					crossExtent - wall, crossExtent),
					0, 0, true);
			break;
		}

		case SectorArchitectureElement::steppedMonument:
		{
			const int tiers = request.architectureBays;
			const double outerDiameter =
					std::min(innerWidth, innerHeight);
			const double finalDiameter =
					outerDiameter -
					2.0 * request.architectureSize * (tiers - 1);
			if (finalDiameter + PLAN_EPSILON <
				M_MinimumArchitectureSize(
						request.architectureStyle,
						request.architectureElement))
			{
				AddIssue(plan, SectorDesignIssueSeverity::error,
						 "The monument tiers collapse before reaching the "
						 "center. Reduce Tiers or Tread.",
						 host, -1, center);
				break;
			}
			int parent = -2;
			for (int tier = 0; tier < tiers; ++tier)
			{
				const double diameter =
						outerDiameter -
						tier * request.architectureSize * 2.0;
				parent = addShape(StyledPillar(
						center, diameter,
						request.architectureStyle, request.rotation),
						elevation, 0, true, parent);
			}
			break;
		}

		case SectorArchitectureElement::centralPlatform:
		{
			const int platform = addShape(BeveledRectangle(
					innerMinX, innerMinY, innerMaxX, innerMaxY,
					std::min(request.architectureSize * 0.25,
							 std::min(innerWidth, innerHeight) * 0.2)),
					elevation, 0, false);
			if (request.architectureFunction ==
				SectorArchitectureFunction::smartLift)
			{
				plan.shapes[platform].smartLift = true;
				plan.shapes[platform].role = DesignPreviewRole::lift;
				plan.plannedLifts = 1;
			}
			break;
		}

		case SectorArchitectureElement::splitLevelStage:
		{
			const double halfGap = std::min(
					request.architectureSize * 0.5,
					longSpan * 0.12);
			if (longSpan <= halfGap * 2.0 + 2.0)
			{
				AddIssue(plan, SectorDesignIssueSeverity::error,
						 "The split-level stage needs room on both sides "
						 "of its center gap.", host, -1, center);
				break;
			}
			addShape(directedRectangle(
					0, longSpan * 0.5 - halfGap,
					-crossExtent, crossExtent),
					elevation, 0, false);
			addShape(directedRectangle(
					longSpan * 0.5 + halfGap, longSpan,
					-crossExtent, crossExtent),
					elevation * 2, 0, false);
			break;
		}

		case SectorArchitectureElement::cornerTerraces:
		{
			const int tiers = request.architectureBays;
			const double tread = request.architectureSize;
			const double extent = tread * (tiers * 2.0 + 1.0);
			if (!requireDimensions(
					extent, extent,
					"The corner terraces"))
				break;
			int parent = -2;
			for (int tier = 0; tier < tiers; ++tier)
			{
				const double inset = tier * tread;
				const double length = extent - inset * 2.0;
				const double cross0 =
						request.architectureMirrored ?
							-crossExtent + inset :
							crossExtent - inset - length;
				parent = addShape(directedRectangle(
						inset, inset + length,
						cross0, cross0 + length),
						elevation, 0, false, parent);
			}
			break;
		}

		case SectorArchitectureElement::octagonalPodium:
		{
			const int tiers = request.architectureBays;
			const double outerRadius =
					std::min(innerWidth, innerHeight) * 0.5;
			const double finalRadius =
					outerRadius -
					request.architectureSize * (tiers - 1);
			if (finalRadius <= 1.0)
			{
				AddIssue(plan, SectorDesignIssueSeverity::error,
						 "The podium tiers collapse before reaching the "
						 "center. Reduce Tiers or Tread.",
						 host, -1, center);
				break;
			}
			std::vector<double> radii(8, 1.0);
			double inner = 1.0;
			switch (request.architectureStyle)
			{
				case SectorArchitectureStyle::functional: inner = 0.94; break;
				case SectorArchitectureStyle::classical: inner = 0.98; break;
				case SectorArchitectureStyle::romanesque: inner = 0.88; break;
				case SectorArchitectureStyle::gothic: inner = 0.78; break;
				case SectorArchitectureStyle::industrial: inner = 0.9; break;
				case SectorArchitectureStyle::artDeco: inner = 0.84; break;
				case SectorArchitectureStyle::infernal: inner = 0.7; break;
			}
			for (size_t index = 1; index < radii.size(); index += 2)
				radii[index] = inner;
			int parent = -2;
			for (int tier = 0; tier < tiers; ++tier)
			{
				const double radius =
						outerRadius -
						tier * request.architectureSize;
				parent = addShape(RadialProfile(
						center, radius, request.rotation, radii),
						elevation, 0, false, parent);
			}
			break;
		}

		case SectorArchitectureElement::horseshoeAmphitheater:
		{
			const int rows = request.architectureBays;
			const double pitch = request.architectureSize;
			const double seatDepth =
					std::max(1.0, pitch * 0.68);
			if (!requireDimensions(
					pitch * (rows + 1),
					pitch * (rows * 2 + 1),
					"The horseshoe amphitheater"))
				break;
			for (int row = 0; row < rows; ++row)
			{
				const double inset = row * pitch;
				const double xmin = longBegin + inset;
				const double xmax = longEnd - inset;
				const double ymin = -crossExtent + inset;
				const double ymax = crossExtent - inset;
				std::vector<v2double_t> u = localPath({
					{xmin, ymin}, {xmin, ymax},
					{xmax, ymax}, {xmax, ymin},
					{xmax - seatDepth, ymin},
					{xmax - seatDepth, ymax - seatDepth},
					{xmin + seatDepth, ymax - seatDepth},
					{xmin + seatDepth, ymin}
				});
				if (!forward)
					for (v2double_t &point : u)
						point.x = longBegin + longEnd - point.x;
				addShape(std::move(u),
						elevation * (row + 1), 0, false);
			}
			break;
		}

		case SectorArchitectureElement::switchbackStair:
		{
			const int steps = request.architectureBays;
			const double landingDepth = longSpan / (steps * 2.0 + 1.0);
			const double flightLength =
					longSpan - landingDepth;
			const double tread = flightLength / steps;
			const double gap = std::min(
					std::max(2.0, request.architectureSize * 0.2),
					crossSpan * 0.15);
			const double flightWidth = std::min(
					request.architectureSize,
					(crossSpan - gap) * 0.5);
			if (tread <= PLAN_EPSILON || flightWidth <= 1.0)
			{
				AddIssue(plan, SectorDesignIssueSeverity::error,
						 "The switchback stair has no usable tread or "
						 "flight width.", host, -1, center);
				break;
			}
			warnNarrowPassage(
					flightWidth,
					"Each switchback flight");
			const double side = request.architectureMirrored ? -1.0 : 1.0;
			const double first0 = side > 0 ?
					-gap * 0.5 - flightWidth : gap * 0.5;
			const double first1 = side > 0 ?
					-gap * 0.5 : gap * 0.5 + flightWidth;
			const double second0 = -first1;
			const double second1 = -first0;
			for (int step = 0; step < steps; ++step)
				addShape(directedRectangle(
						step * tread, (step + 1) * tread,
						first0, first1),
						elevation * (step + 1), 0, false);
			addShape(directedRectangle(
					flightLength, flightLength + landingDepth,
					first0, first1),
					elevation * (steps + 1), 0, false);
			addShape(directedRectangle(
					flightLength, flightLength + landingDepth,
					std::min(first1, second1),
					std::max(first0, second0)),
					elevation * (steps + 1), 0, false);
			addShape(directedRectangle(
					flightLength, flightLength + landingDepth,
					second0, second1),
					elevation * (steps + 1), 0, false);
			for (int step = 0; step < steps; ++step)
				addShape(directedRectangle(
						step * tread, (step + 1) * tread,
						second0, second1),
						elevation * (steps * 2 - step + 1),
						0, false);
			break;
		}

		case SectorArchitectureElement::bifurcatedStair:
		{
			const int steps = request.architectureBays;
			const double halfLength = longSpan * 0.5;
			const double tread = halfLength / steps;
			const double flightWidth = std::min(
					request.architectureSize, crossSpan * 0.28);
			const double gap = std::min(
					std::max(2.0, flightWidth * 0.2),
					crossSpan * 0.1);
			if (tread <= PLAN_EPSILON ||
				flightWidth * 2.0 + gap > crossSpan + PLAN_EPSILON)
			{
				AddIssue(plan, SectorDesignIssueSeverity::error,
						 "The bifurcated stair needs a wider footprint or "
						 "narrower flights.", host, -1, center);
				break;
			}
			const double leftWidth = flightWidth *
					(request.architectureMirrored ? 0.82 : 1.18);
			const double rightWidth = flightWidth *
					(request.architectureMirrored ? 1.18 : 0.82);
			warnNarrowPassage(
					std::min(leftWidth, rightWidth),
					"The narrower bifurcated flight");
			const double left0 = -gap * 0.5 - leftWidth;
			const double left1 = -gap * 0.5;
			const double right0 = gap * 0.5;
			const double right1 = gap * 0.5 + rightWidth;
			for (int step = 0; step < steps; ++step)
			{
				addShape(directedRectangle(
						step * tread, (step + 1) * tread,
						left0, left1),
						elevation * (step + 1), 0, false);
				addShape(directedRectangle(
						step * tread, (step + 1) * tread,
						left1, right0),
						elevation * (step + 1), 0, false);
				addShape(directedRectangle(
						step * tread, (step + 1) * tread,
						right0, right1),
						elevation * (step + 1), 0, false);
			}
			for (int step = 0; step < steps; ++step)
			{
				const double distance0 = halfLength + step * tread;
				const double distance1 = halfLength + (step + 1) * tread;
				const int level = steps + step + 1;
				addShape(directedRectangle(
						distance0, distance1,
						left0, left1),
						elevation * level, 0, false);
				addShape(directedRectangle(
						distance0, distance1,
						right0, right1),
						elevation * level, 0, false);
			}
			break;
		}

		case SectorArchitectureElement::spiralStair:
		{
			const int steps = request.architectureBays;
			const double radiusX = innerWidth * 0.5;
			const double radiusY = innerHeight * 0.5;
			const double outerRadius =
					std::min(radiusX, radiusY);
			const double innerRadius = std::min(
					request.architectureSize, outerRadius * 0.58);
			if (outerRadius - innerRadius <= 2.0)
			{
				AddIssue(plan, SectorDesignIssueSeverity::error,
						 "The spiral stair needs more tread outside its "
						 "central well.", host, -1, center);
				break;
			}
			const double direction =
					request.architectureMirrored ? 1.0 : -1.0;
			const double startAngle =
					(request.anchors.back() -
					 request.anchors.front()).atan2();
			const double angleStep =
					2.0 * std::numbers::pi / (steps + 1);
			for (int step = 0; step < steps; ++step)
			{
				const double angle0 =
						startAngle + direction * step * angleStep;
				const double angle1 =
						startAngle + direction * (step + 1) * angleStep;
				const auto point = [&](double angle, double radius)
				{
					return v2double_t{
						center.x + std::cos(angle) * radius,
						center.y + std::sin(angle) * radius
					};
				};
				addShape({
					point(angle0, innerRadius),
					point(angle1, innerRadius),
					point(angle1, outerRadius),
					point(angle0, outerRadius)
				}, elevation * (step + 1), 0, false);
			}
			break;
		}

		case SectorArchitectureElement::landingCatwalk:
		{
			const double width = std::min(
					request.architectureSize, crossSpan * 0.6);
			const double landingLength =
					std::min(longSpan * 0.22, width * 1.5);
			if (longSpan <= landingLength * 2.0 + 1.0)
			{
				AddIssue(plan, SectorDesignIssueSeverity::error,
						 "The catwalk needs room between its end landings.",
						 host, -1, center);
				break;
			}
			const double landingHalf =
					std::min(crossExtent, width);
			auto addLanding = [&](double begin, double end)
			{
				if (landingHalf > width * 0.5 + PLAN_EPSILON)
					addShape(localRectangle(
							begin, end,
							-landingHalf, -width * 0.5),
							elevation, 0, false);
				addShape(localRectangle(
						begin, end,
						-width * 0.5, width * 0.5),
						elevation, 0, false);
				if (landingHalf > width * 0.5 + PLAN_EPSILON)
					addShape(localRectangle(
							begin, end,
							width * 0.5, landingHalf),
							elevation, 0, false);
			};
			addLanding(longBegin, longBegin + landingLength);
			addShape(localRectangle(
					longBegin + landingLength,
					longEnd - landingLength,
					-width * 0.5, width * 0.5),
					elevation, 0, false);
			addLanding(longEnd - landingLength, longEnd);
			break;
		}

		case SectorArchitectureElement::crossingBridges:
			addShape(localCross(request.architectureSize),
					 elevation, 0, false);
			break;

		case SectorArchitectureElement::perimeterMoat:
		{
			const double moat = request.architectureSize;
			if (!requireDimensions(
					moat * 2.0 + 2.0, moat * 2.0 + 2.0,
					"The perimeter moat"))
				break;
			const int outer = addShape(BeveledRectangle(
					innerMinX, innerMinY, innerMaxX, innerMaxY,
					std::min(moat * 0.2,
							 std::min(innerWidth, innerHeight) * 0.1)),
					-elevation, 0, false);
			addShape(BeveledRectangle(
					innerMinX + moat, innerMinY + moat,
					innerMaxX - moat, innerMaxY - moat,
					std::min(moat * 0.15,
							 std::min(innerWidth, innerHeight) * 0.08)),
					elevation, 0, false, outer);
			break;
		}

		case SectorArchitectureElement::crossCanal:
			addShape(localCross(request.architectureSize),
					 -elevation, 0, false);
			break;

		case SectorArchitectureElement::twinCanals:
		{
			const double width = std::min(
					request.architectureSize, crossSpan * 0.28);
			const double gap = std::min(
					std::max(2.0, width * 0.5),
					crossSpan - width * 2.0);
			if (width * 2.0 + gap > crossSpan + PLAN_EPSILON)
			{
				AddIssue(plan, SectorDesignIssueSeverity::error,
						 "The twin canals need a wider footprint or "
						 "narrower channels.", host, -1, center);
				break;
			}
			addShape(localRectangle(
					longBegin, longEnd,
					-gap * 0.5 - width, -gap * 0.5),
					-elevation, 0, false);
			addShape(localRectangle(
					longBegin, longEnd,
					gap * 0.5, gap * 0.5 + width),
					-elevation, 0, false);
			break;
		}

		case SectorArchitectureElement::steppedCascade:
		{
			const int basins = request.architectureBays;
			const double basinLength = longSpan / basins;
			const double width = std::min(
					request.architectureSize, crossSpan);
			if (basinLength <= PLAN_EPSILON || width <= PLAN_EPSILON)
			{
				AddIssue(plan, SectorDesignIssueSeverity::error,
						 "The stepped cascade has no usable basin length "
						 "or width.", host, -1, center);
				break;
			}
			for (int basin = 0; basin < basins; ++basin)
				addShape(directedRectangle(
						basin * basinLength,
						(basin + 1) * basinLength,
						-width * 0.5, width * 0.5),
						-elevation * (basin + 1), 0, false);
			break;
		}

		case SectorArchitectureElement::fountainCourt:
		{
			const double gap = std::min({
				request.architectureSize,
				innerWidth * 0.22,
				innerHeight * 0.22
			});
			const double cellWidth = (innerWidth - gap) * 0.5;
			const double cellHeight = (innerHeight - gap) * 0.5;
			if (cellWidth <= 2.0 || cellHeight <= 2.0)
			{
				AddIssue(plan, SectorDesignIssueSeverity::error,
						 "The fountain court needs room for four separate "
						 "basins.", host, -1, center);
				break;
			}
			const double radiusX = cellWidth * 0.43;
			const double radiusY = cellHeight * 0.43;
			for (double x : {-1.0, 1.0})
				for (double y : {-1.0, 1.0})
					addShape(EllipseProfile(
							{center.x + x * (cellWidth + gap) * 0.5,
							 center.y + y * (cellHeight + gap) * 0.5},
							radiusX, radiusY, 16, 0.0),
							-elevation, 0, false);
			const double centerSize = std::min({
				request.architectureSize,
				gap * 0.8,
				std::min(innerWidth, innerHeight) * 0.2
			});
			addShape(StyledPillar(
					center, std::max(1.0, centerSize),
					request.architectureStyle, request.rotation),
					elevation * 2, 0, true);
			break;
		}

		case SectorArchitectureElement::partitionWall:
		{
			const double thickness =
					std::min(request.architectureSize, crossSpan);
			addShape(localRectangle(
					longBegin, longEnd,
					-thickness * 0.5, thickness * 0.5),
					0, 0, true);
			break;
		}

		case SectorArchitectureElement::crenellatedWall:
		{
			const double thickness =
					std::min(request.architectureSize, crossSpan * 0.5);
			const double merlonLength = longSpan /
					(request.architectureBays * 2.0 + 1.0);
			for (int segment = 0;
				 segment < request.architectureBays * 2 + 1;
				 ++segment)
			{
				const double along0 =
						longBegin +
						segment * merlonLength;
				addShape(localRectangle(
						along0, along0 + merlonLength,
						-thickness * 0.5, thickness * 0.5),
						0, 0, true);
				if ((segment % 2) == 1)
				{
					addShape(localRectangle(
							along0, along0 + merlonLength,
							-thickness, -thickness * 0.5),
							0, 0, true);
					addShape(localRectangle(
							along0, along0 + merlonLength,
							thickness * 0.5, thickness),
							0, 0, true);
				}
			}
			break;
		}

		case SectorArchitectureElement::buttressedWall:
		{
			const double thickness =
					std::min(request.architectureSize, crossSpan * 0.5);
			const double station = longSpan /
					(request.architectureBays + 1.0);
			const double buttressWidth =
					std::min(thickness, station * 0.45);
			auto addWallSegment =
					[&](double along0, double along1)
			{
				if (along1 - along0 <= PLAN_EPSILON)
					return;
				addShape(localRectangle(
						along0, along1,
						-thickness * 0.5, thickness * 0.5),
						0, 0, true);
			};
			double cursor = longBegin;
			for (int index = 0;
				 index < request.architectureBays; ++index)
			{
				const double along =
						longBegin + (index + 1) * station;
				const double along0 =
						along - buttressWidth * 0.5;
				const double along1 =
						along + buttressWidth * 0.5;
				addWallSegment(cursor, along0);
				addWallSegment(along0, along1);
				addShape(localRectangle(
						along0, along1,
						-thickness, -thickness * 0.5),
						0, 0, true);
				addShape(localRectangle(
						along0, along1,
						thickness * 0.5, thickness),
						0, 0, true);
				cursor = along1;
			}
			addWallSegment(cursor, longEnd);
			break;
		}

		case SectorArchitectureElement::staggeredScreen:
		{
			const int panels = request.architectureBays;
			const double thickness =
					std::min(request.architectureSize, crossSpan * 0.22);
			const double cell = longSpan / panels;
			const double panelLength = cell * 0.72;
			const double offset = std::min(
					crossSpan * 0.2, thickness * 1.5);
			for (int panel = 0; panel < panels; ++panel)
			{
				const bool positive =
						((panel % 2) == 0) !=
						request.architectureMirrored;
				const double along =
						longBegin + (panel + 0.5) * cell;
				const double cross =
						positive ? offset : -offset;
				addShape(localRectangle(
						along - panelLength * 0.5,
						along + panelLength * 0.5,
						cross - thickness * 0.5,
						cross + thickness * 0.5),
						0, 0, true);
			}
			break;
		}

		case SectorArchitectureElement::gatehousePassage:
		{
			const double passage = std::min(
					request.architectureSize,
					longSpan * 0.62);
			const double massLength =
					(longSpan - passage) * 0.5;
			if (passage <= 1.0 || massLength <= 1.0 ||
				crossSpan <= 2.0)
			{
				AddIssue(plan, SectorDesignIssueSeverity::error,
						 "The gatehouse needs both a usable passage and "
						 "flanking wall mass.", host, -1, center);
				break;
			}
			warnNarrowPassage(
					passage,
					"The gatehouse opening");
			const double bevelFactor =
					request.architectureStyle ==
						SectorArchitectureStyle::functional ? 0.08 :
					request.architectureStyle ==
						SectorArchitectureStyle::classical ? 0.24 :
					request.architectureStyle ==
						SectorArchitectureStyle::romanesque ? 0.14 :
					request.architectureStyle ==
						SectorArchitectureStyle::gothic ? 0.42 :
					request.architectureStyle ==
						SectorArchitectureStyle::industrial ? 0.04 :
					request.architectureStyle ==
						SectorArchitectureStyle::artDeco ? 0.32 : 0.48;
			const double bevel =
					std::min(massLength, crossSpan) *
					std::min(0.45, bevelFactor);
			auto gateMass = [&](double along0, double along1)
			{
				std::vector<v2double_t> world = {
					position(along0 + bevel, -crossExtent),
					position(along0, -crossExtent + bevel),
					position(along0, crossExtent - bevel),
					position(along0 + bevel, crossExtent),
					position(along1 - bevel, crossExtent),
					position(along1, crossExtent - bevel),
					position(along1, -crossExtent + bevel),
					position(along1 - bevel, -crossExtent)
				};
				addShape(std::move(world), 0, 0, true);
			};
			const double passage0 =
					(longBegin + longEnd - passage) * 0.5;
			const double passage1 = passage0 + passage;
			gateMass(longBegin, passage0);
			gateMass(passage1, longEnd);
			break;
		}

		case SectorArchitectureElement::trayCeiling:
		{
			const double border = request.architectureSize;
			if (!requireDimensions(
					border * 2.0 + 2.0, border * 2.0 + 2.0,
					"The recessed tray ceiling"))
				break;
			addShape(BeveledRectangle(
					innerMinX + border, innerMinY + border,
					innerMaxX - border, innerMaxY - border,
					std::min(border * 0.25,
							 std::min(innerWidth, innerHeight) * 0.1)),
					0, elevation, false);
			break;
		}

		case SectorArchitectureElement::barrelVault:
		{
			const int bands = request.architectureBays;
			const double bandWidth = crossSpan / bands;
			if (bandWidth <= PLAN_EPSILON)
			{
				AddIssue(plan, SectorDesignIssueSeverity::error,
						 "The barrel-vault bands collapse at this width.",
						 host, -1, center);
				break;
			}
			for (int band = 0; band < bands; ++band)
			{
				const double normalized =
						std::abs((band + 0.5) / bands * 2.0 - 1.0);
				const int relief = std::max(
						1, static_cast<int>(std::lround(
							elevation *
							std::sqrt(std::max(
								0.0, 1.0 - normalized * normalized)))));
				addShape(localRectangle(
						longBegin, longEnd,
						-crossExtent + band * bandWidth,
						-crossExtent + (band + 1) * bandWidth),
						0, relief, false);
			}
			break;
		}

		case SectorArchitectureElement::ribbedCrossVault:
		{
			const double halfRib = std::min(
					request.architectureSize * 0.5,
					std::min(longSpan, crossSpan) * 0.22);
			const double alongCenter =
					(longBegin + longEnd) * 0.5;
			if (longSpan <= halfRib * 2.0 + 2.0 ||
				crossSpan <= halfRib * 2.0 + 2.0)
			{
				AddIssue(plan, SectorDesignIssueSeverity::error,
						 "The ribbed cross vault needs more room around "
						 "its crossing.", host, -1, center);
				break;
			}
			const int quadrantRelief =
					std::max(1, elevation / 2);
			addShape(localRectangle(
					longBegin, alongCenter - halfRib,
					-crossExtent, -halfRib),
					0, quadrantRelief, false);
			addShape(localRectangle(
					alongCenter + halfRib, longEnd,
					-crossExtent, -halfRib),
					0, quadrantRelief, false);
			addShape(localRectangle(
					longBegin, alongCenter - halfRib,
					halfRib, crossExtent),
					0, quadrantRelief, false);
			addShape(localRectangle(
					alongCenter + halfRib, longEnd,
					halfRib, crossExtent),
					0, quadrantRelief, false);
			addShape(localRectangle(
					alongCenter - halfRib,
					alongCenter + halfRib,
					-halfRib, halfRib),
					0, elevation, false);
			break;
		}

		case SectorArchitectureElement::domedCeiling:
		{
			const int rings = request.architectureBays;
			const double outerDiameter =
					std::min(innerWidth, innerHeight);
			const double finalDiameter =
					outerDiameter -
					2.0 * request.architectureSize * (rings - 1);
			if (finalDiameter + PLAN_EPSILON <
				M_MinimumArchitectureSize(
						request.architectureStyle,
						request.architectureElement))
			{
				AddIssue(plan, SectorDesignIssueSeverity::error,
						 "The dome rings collapse before reaching the "
						 "center. Reduce Rings or Ring width.",
						 host, -1, center);
				break;
			}
			int parent = -2;
			for (int ring = 0; ring < rings; ++ring)
			{
				const double diameter =
						outerDiameter -
						ring * request.architectureSize * 2.0;
				parent = addShape(StyledPillar(
						center, diameter,
						request.architectureStyle, request.rotation),
						0, elevation, false, parent);
			}
			break;
		}

		case SectorArchitectureElement::beamLattice:
		{
			const int beams = request.architectureBays;
			const double beam = std::min({
				request.architectureSize,
				longSpan / (beams * 2.0 + 1.0),
				crossSpan / (beams * 2.0 + 1.0)
			});
			if (beam <= PLAN_EPSILON)
			{
				AddIssue(plan, SectorDesignIssueSeverity::error,
						 "The beam lattice collapses at this bay count.",
						 host, -1, center);
				break;
			}
			Paths64 strips;
			strips.reserve(static_cast<size_t>(beams) * 2);
			for (int index = 0; index < beams; ++index)
			{
				const double along =
						longBegin +
						(index + 1.0) * longSpan / (beams + 1.0);
				strips.push_back(ToClipperPath(
						localRectangle(
								along - beam * 0.5,
								along + beam * 0.5,
								-crossExtent, crossExtent),
						format));
			}
			for (int index = 0; index < beams; ++index)
			{
				const double cross =
						-crossExtent +
						(index + 1.0) * crossSpan / (beams + 1.0);
				strips.push_back(ToClipperPath(
						localRectangle(
								longBegin, longEnd,
								cross - beam * 0.5,
								cross + beam * 0.5),
						format));
			}
			const size_t before = plan.shapes.size();
			AppendClipperShapes(
					plan, Union(strips, FillRule::NonZero),
					format, previewRole, host);
			if (plan.shapes.size() == before)
			{
				AddIssue(plan, SectorDesignIssueSeverity::error,
						 "The beam lattice collapses after format "
						 "quantization.", host, -1, center);
				break;
			}
			for (size_t shapeIndex = before;
				 shapeIndex < plan.shapes.size(); ++shapeIndex)
			{
				PlannedSectorShape &shape = plan.shapes[shapeIndex];
				shape.modelShape = hostShape;
				shape.inheritModelOnly = hostShape >= 0;
				shape.ceilingDelta = -elevation;
				shape.holesRetainModel = true;
				plan.plannedRetainedCells +=
						static_cast<int>(shape.holes.size());
			}
			break;
		}

		default:
			AddIssue(plan, SectorDesignIssueSeverity::error,
					 "The selected volumetric architecture is unsupported.");
			break;
	}

	if (outsideStructures > 0)
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 SString::printf(
					 "%d architectural sector%s leave%s host sector #%d "
					 "or enclose a foreign cell. Increase Margin, reduce "
					 "Structure size, or use a larger continuous host.",
					 outsideStructures,
					 outsideStructures == 1 ? "" : "s",
					 outsideStructures == 1 ? "s the" : " the",
					 host),
				 host, -1, firstOutside);

	plan.plannedStructures =
			static_cast<int>(plan.shapes.size() - firstStructure);
	if (plan.plannedStructures == 0)
		plan.previewPaths.push_back({
			BeveledRectangle(
					innerMinX, innerMinY, innerMaxX, innerMaxY,
					std::min(request.architectureSize * 0.25,
							 std::min(innerWidth, innerHeight) * 0.2)),
			DesignPreviewRole::conflict, true, true
		});
	if (host >= 0 && doc.isSector(host) &&
		config.miscInfo.player_h > 0)
	{
		const int baseClearance =
				doc.sectors[host]->ceilh -
				doc.sectors[host]->floorh;
		for (size_t shapeIndex = firstStructure;
			 shapeIndex < plan.shapes.size(); ++shapeIndex)
		{
			const PlannedSectorShape &shape =
					plan.shapes[shapeIndex];
			if (shape.closed)
				continue;
			int floorDelta = shape.floorDelta;
			int ceilingDelta = shape.ceilingDelta;
			int parent = shape.modelShape;
			int guard = 0;
			while (parent >= static_cast<int>(firstStructure) &&
				   parent < static_cast<int>(plan.shapes.size()) &&
				   guard++ < 64)
			{
				floorDelta += plan.shapes[parent].floorDelta;
				ceilingDelta +=
						plan.shapes[parent].ceilingDelta;
				parent = plan.shapes[parent].modelShape;
			}
			const int clearance =
					baseClearance + ceilingDelta - floorDelta;
			if (clearance >= config.miscInfo.player_h)
				continue;
			v2double_t warning = center;
			if (!shape.outer.empty())
			{
				warning = {0.0, 0.0};
				for (const v2double_t &point : shape.outer)
					warning += point;
				warning /= shape.outer.size();
			}
			AddIssue(plan, SectorDesignIssueSeverity::warning,
					SString::printf(
						"This structure leaves %d units of clearance, "
						"below the configured %d-unit player height.",
						clearance, config.miscInfo.player_h),
					host, -1, warning);
			break;
		}
	}
	if (plan.plannedStructures > 0)
	{
		if (ArchitectureUsesDirection(
				request.architectureElement))
		{
			const v2double_t from = request.anchors.front();
			const v2double_t to = request.anchors.back();
			plan.previewPaths.push_back({
				{from, to}, DesignPreviewRole::anchor, false, false
			});
			plan.previewLabels.push_back({
				from + (to - from) * 0.72,
				request.architectureMirrored ?
					"forward -> / mirrored" : "forward ->",
				DesignPreviewRole::anchor
			});
		}

		if (ArchitectureUsesHeight(
				request.architectureElement) &&
			plan.plannedStructures > 1 &&
			plan.plannedStructures <= 48)
		{
			for (size_t shapeIndex = firstStructure;
				 shapeIndex < plan.shapes.size(); ++shapeIndex)
			{
				const PlannedSectorShape &shape =
						plan.shapes[shapeIndex];
				int floorDelta = shape.floorDelta;
				int ceilingDelta = shape.ceilingDelta;
				int parent = shape.modelShape;
				int guard = 0;
				while (parent >= static_cast<int>(firstStructure) &&
					   parent < static_cast<int>(plan.shapes.size()) &&
					   guard++ < 64)
				{
					floorDelta += plan.shapes[parent].floorDelta;
					ceilingDelta +=
							plan.shapes[parent].ceilingDelta;
					parent = plan.shapes[parent].modelShape;
				}
				SString effect;
				if (shape.smartLift)
					effect = "Smart Lift";
				else if (floorDelta != 0)
					effect = SString::printf(
							"floor %c%d",
							floorDelta > 0 ? '+' : '-',
							std::abs(floorDelta));
				else if (ceilingDelta != 0)
					effect = SString::printf(
							"ceiling %c%d",
							ceilingDelta > 0 ? '+' : '-',
							std::abs(ceilingDelta));
				if (effect.empty() || shape.outer.empty())
					continue;
				v2double_t labelPosition{0.0, 0.0};
				for (const v2double_t &point : shape.outer)
					labelPosition += point;
				labelPosition /= shape.outer.size();
				plan.previewLabels.push_back({
					labelPosition,
					SString::printf(
							"%zu: %s",
							shapeIndex - firstStructure + 1,
							effect.c_str()),
					shape.role
				});
			}
		}

		SString relief;
		switch (request.architectureElement)
		{
			case SectorArchitectureElement::raisedDais:
			case SectorArchitectureElement::balconyGallery:
			case SectorArchitectureElement::raisedBridge:
				relief = SString::printf(" / floor +%d", elevation);
				break;
			case SectorArchitectureElement::sunkenCourt:
			case SectorArchitectureElement::reflectingPool:
			case SectorArchitectureElement::processionalChannel:
				relief = SString::printf(" / floor -%d", elevation);
				break;
			case SectorArchitectureElement::tieredZiggurat:
				relief = SString::printf(
						" / 3 tiers x +%d", elevation);
				break;
			case SectorArchitectureElement::grandStair:
				relief = SString::printf(
						" / %d steps x +%d",
						request.architectureBays, elevation);
				break;
			case SectorArchitectureElement::fountainBasin:
				relief = SString::printf(
						" / basin -%d / centerpiece +%d",
						elevation, elevation);
				break;
			case SectorArchitectureElement::screenWall:
				relief = SString::printf(
						" / %d openings", request.architectureBays);
				break;
			case SectorArchitectureElement::cofferedCeiling:
			case SectorArchitectureElement::groinVaults:
				relief = SString::printf(
						" / ceiling +%d", elevation);
				break;
			default:
				break;
		}
		plan.previewLabels.push_back({
			center,
			SString::printf("%s - %d sector%s%s%s",
					ArchitectureElementName(request.architectureElement),
					plan.plannedStructures,
					plan.plannedStructures == 1 ? "" : "s",
					relief.c_str(),
					generatedHost ? " + new hall" : ""),
			previewRole
		});
	}
}

void PlanArchitecture(const Document &doc, const ConfigData &config,
						  const SectorDesignRequest &request,
						  SectorDesignPlan &plan, MapFormat format)
{
	const SectorArchitectureDescriptor &descriptor =
			M_ArchitectureDescriptor(request.architectureElement);
	if (descriptor.element != request.architectureElement)
	{
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 "The selected architecture catalog entry is unavailable.");
		return;
	}
	if (static_cast<int>(request.architectureStyle) < 0 ||
		static_cast<int>(request.architectureStyle) >
				static_cast<int>(
						SectorArchitectureStyle::infernal))
	{
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 "The selected architecture style is unavailable.");
		return;
	}
	if (!std::isfinite(request.rotation))
	{
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 "Architecture rotation must be finite.");
		return;
	}
	if (request.anchors.size() < 2)
	{
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 "Drag an architectural footprint inside a room or in clear "
				 "void space.");
		return;
	}
	for (const v2double_t &anchor : request.anchors)
		if (!std::isfinite(anchor.x) || !std::isfinite(anchor.y))
		{
			AddIssue(plan, SectorDesignIssueSeverity::error,
					 "Architecture anchors must use finite coordinates.");
			return;
		}
	if (ArchitectureUsesBays(request.architectureElement) &&
		(request.architectureBays < descriptor.minimumBays ||
		 request.architectureBays > descriptor.maximumBays))
	{
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 SString::printf(
					 "%s must be between %d and %d.",
					 descriptor.baysLabel,
					 descriptor.minimumBays,
					 descriptor.maximumBays));
		return;
	}
	if (!M_ArchitectureSupportsFunction(
			request.architectureElement,
			request.architectureFunction))
	{
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 "The selected optional function is not supported by this "
				 "structure.");
		return;
	}
	const double minimumSize =
			M_MinimumArchitectureSize(
					request.architectureStyle,
					request.architectureElement);
	if (!std::isfinite(request.architectureSize) ||
		request.architectureSize < minimumSize)
	{
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 SString::printf(
					 "%s needs at least %.0f map "
					 "units to remain valid after format quantization. "
					 "Wheel up to increase Structure size.",
					 ArchitectureElementName(request.architectureElement),
					 minimumSize));
		return;
	}
	if (ArchitectureUsesHeight(request.architectureElement) &&
		(!std::isfinite(request.architectureHeight) ||
		 request.architectureHeight <= 0.0))
	{
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 "Structure elevation must be greater than zero.");
		return;
	}
	if (!std::isfinite(request.architectureMargin) ||
		request.architectureMargin < 0.0)
	{
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 "Architecture margin cannot be negative.");
		return;
	}

	const v2double_t first = request.anchors.front();
	const v2double_t last = request.anchors.back();
	const double minX = std::min(first.x, last.x);
	const double maxX = std::max(first.x, last.x);
	const double minY = std::min(first.y, last.y);
	const double maxY = std::max(first.y, last.y);
	const double width = maxX - minX;
	const double height = maxY - minY;
	const v2double_t center{(minX + maxX) * 0.5,
							(minY + maxY) * 0.5};

	std::vector<v2double_t> hostOutline{
		{minX, minY}, {minX, maxY}, {maxX, maxY}, {maxX, minY}
	};
	MakeClockwise(hostOutline);
	const bool firstInVoid = hover::isPointOutsideOfMap(doc, first);
	int host = -1;
	if (request.architectureHostSector >= 0)
	{
		if (!doc.isSector(request.architectureHostSector))
		{
			plan.previewPaths.push_back({
				hostOutline, DesignPreviewRole::conflict, true, false
			});
			AddIssue(plan, SectorDesignIssueSeverity::error,
					 "The architecture host sector no longer exists. Start "
					 "a new gesture inside the intended room.",
					 request.architectureHostSector, -1, first);
			return;
		}
		host = request.architectureHostSector;
	}
	else if (!firstInVoid)
	{
		const Objid hostObject = hover::getNearestSector(doc, first);
		if (hostObject.type == ObjType::sectors &&
			doc.isSector(hostObject.num))
			host = hostObject.num;
	}
	const bool generatedHost = host < 0;
	int hostShape = -1;
	auto showBlockedIntent = [&]()
	{
		plan.previewPaths.push_back({
			hostOutline, DesignPreviewRole::conflict, true, false
		});
		PlannedSectorShape sample;
		if (UsesLegacySupportBuilder(
				request.architectureElement))
			sample.outer = StyledPillar(
					center, request.architectureSize,
					request.architectureStyle, request.rotation);
		else
		{
			const double radius =
					std::max(1.0, request.architectureSize * 0.5);
			sample.outer = BeveledRectangle(
					center.x - radius, center.y - radius,
					center.x + radius, center.y + radius,
					radius * 0.25);
		}
		MakeClockwise(sample.outer);
		sample.role = DesignPreviewRole::conflict;
		sample.modelSector = host;
		sample.modelShape = hostShape;
		sample.inheritModelOnly = generatedHost && hostShape >= 0;
		sample.closed = true;
		plan.shapes.push_back(std::move(sample));
		plan.plannedStructures = 1;
	};

	if (generatedHost)
	{
		if (!firstInVoid)
		{
			showBlockedIntent();
			AddIssue(plan, SectorDesignIssueSeverity::error,
					 "The architectural gesture did not resolve a host "
					 "sector. Start it clearly inside the intended room or "
					 "in clear void.", -1, -1, first);
			return;
		}

		for (int line = 0; line < doc.numLinedefs(); ++line)
		{
			const LineDef &linedef = *doc.linedefs[line];
			if (!doc.isVertex(linedef.start) ||
				!doc.isVertex(linedef.end))
				continue;
			const v2double_t start = doc.getStart(linedef).xy();
			const v2double_t end = doc.getEnd(linedef).xy();
			const v2double_t middle = (start + end) * 0.5;
			bool contact = PointInsidePath(start, hostOutline) ||
					PointInsidePath(end, hostOutline) ||
					PointInsidePath(middle, hostOutline);
			for (size_t edge = 0;
				 edge < hostOutline.size() && !contact; ++edge)
				contact = ClassifySegments(
						hostOutline[edge],
						hostOutline[(edge + 1) % hostOutline.size()],
						start, end) != SegmentRelation::none;
			if (contact)
			{
				showBlockedIntent();
				AddIssue(plan, SectorDesignIssueSeverity::error,
						 "A void-space architectural hall must not touch "
						 "existing geometry. Move it fully into void or "
						 "place the footprint inside one room.",
						 -1, line);
				return;
			}
		}

		hostShape = static_cast<int>(plan.shapes.size());
		PlannedSectorShape hall;
		hall.outer = hostOutline;
		hall.role = DesignPreviewRole::proposed;
		plan.shapes.push_back(std::move(hall));
		plan.plannedArchitectureHosts = 1;
	}
	else
	{
		plan.retainedSectors.push_back(host);
		if (SectorHasProtectedAction(doc, config, host))
		{
			AddIssue(plan,
					request.replaceAffectedSectors ?
						SectorDesignIssueSeverity::warning :
						SectorDesignIssueSeverity::error,
					request.replaceAffectedSectors ?
						"The architectural layout explicitly subdivides a "
						"protected door or lift sector." :
						"The architectural host is a protected door or lift; "
						"enable Replace affected sectors to include it.",
					host);
			if (!request.replaceAffectedSectors)
			{
				showBlockedIntent();
				return;
			}
		}

		// The envelope is a layout guide inside an existing room, not geometry
		// to be inserted. Keep it visible so spacing, margins, and the selected
		// host remain obvious before the supports are committed.
		plan.previewPaths.push_back({
			hostOutline, DesignPreviewRole::anchor, true, false
		});
	}

	if (!UsesLegacySupportBuilder(
			request.architectureElement))
	{
		PlanVolumetricArchitecture(
				doc, config, request, plan, format,
				host, hostShape, generatedHost,
				minX, minY, maxX, maxY);
		return;
	}

	const double halfSize = request.architectureSize * 0.5 *
			ArchitectureMaximumScale(request.architectureElement);
	const double margin = request.architectureMargin + halfSize;
	if (width < margin * 2.0 || height < margin * 2.0)
	{
		showBlockedIntent();
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 SString::printf(
					 "The %.1fx%.1f footprint is too small: this layout "
					 "needs at least %.1fx%.1f. Wheel down to reduce "
					 "Structure size, reduce Margin, or drag a larger area.",
					 width, height, margin * 2.0, margin * 2.0),
				 host, -1, center);
		return;
	}

	const bool horizontal = width >= height;
	const double longMin = horizontal ? minX : minY;
	const double longMax = horizontal ? maxX : maxY;
	const double shortCenter = horizontal ? center.y : center.x;
	const double shortSpan = horizontal ? height : width;
	auto position = [&](double along, double cross)
	{
		return horizontal ?
				v2double_t{along, shortCenter + cross} :
				v2double_t{shortCenter + cross, along};
	};
	auto alongPositions = [&](int supports)
	{
		std::vector<double> result;
		supports = std::max(1, supports);
		const double begin = longMin + margin;
		const double end = longMax - margin;
		for (int index = 0; index < supports; ++index)
			result.push_back(supports == 1 ? (begin + end) * 0.5 :
					begin + (end - begin) * index / (supports - 1));
		return result;
	};
	auto crossPositions = [&](int supports)
	{
		std::vector<double> result;
		supports = std::max(1, supports);
		const double extent = shortSpan * 0.5 - margin;
		for (int index = 0; index < supports; ++index)
			result.push_back(supports == 1 ? 0.0 :
					-extent + 2.0 * extent * index / (supports - 1));
		return result;
	};

	std::vector<ArchitecturalSupport> supports;
	auto addSupport = [&](const v2double_t &point, double scale = 1.0)
	{
		supports.push_back({point, scale});
	};
	const int stations = request.architectureBays + 1;
	const double longBegin = longMin + margin;
	const double longEnd = longMax - margin;
	const double crossExtent = shortSpan * 0.5 - margin;
	switch (request.architectureElement)
	{
		case SectorArchitectureElement::pillar:
			addSupport(center);
			break;
		case SectorArchitectureElement::pairedPillars:
		{
			const std::vector<double> along = alongPositions(4);
			addSupport(position(along[1], 0));
			addSupport(position(along[2], 0));
			break;
		}
		case SectorArchitectureElement::triumphalArch:
			for (double cross : {-crossExtent * 0.72, -crossExtent * 0.24,
								crossExtent * 0.24, crossExtent * 0.72})
				addSupport(position((longBegin + longEnd) * 0.5, cross),
						   std::abs(cross) > crossExtent * 0.5 ? 1.25 : 1.0);
			break;
		case SectorArchitectureElement::cornerPiers:
			for (double along : {longBegin, longEnd})
				for (double cross : {-crossExtent, crossExtent})
					addSupport(position(along, cross), 1.25);
			break;
		case SectorArchitectureElement::colonnade:
			for (double along : alongPositions(stations))
				addSupport(position(along, 0));
			break;
		case SectorArchitectureElement::arcade:
			for (double along : alongPositions(stations))
				for (double cross : {-crossExtent * 0.6,
									 crossExtent * 0.6})
					addSupport(position(along, cross));
			break;
		case SectorArchitectureElement::cloister:
		{
			for (double along : alongPositions(stations))
				for (double cross : {-crossExtent, crossExtent})
					addSupport(position(along, cross));
			const std::vector<double> cross = crossPositions(stations);
			for (size_t index = 1; index + 1 < cross.size(); ++index)
				for (double along : {longBegin, longEnd})
					addSupport(position(along, cross[index]));
			break;
		}
		case SectorArchitectureElement::hypostyleHall:
		{
			const int rows = std::clamp(
					(request.architectureBays + 2) / 2, 3, 8);
			for (double along : alongPositions(stations))
				for (double cross : crossPositions(rows))
					addSupport(position(along, cross));
			break;
		}
		case SectorArchitectureElement::buttressedBay:
			for (double along : alongPositions(stations))
				for (double cross : {-crossExtent, crossExtent})
					addSupport(position(along, cross));
			break;
		case SectorArchitectureElement::flyingButtresses:
			for (double along : alongPositions(stations))
				for (double cross : {-crossExtent, -crossExtent * 0.55,
									 crossExtent * 0.55, crossExtent})
					addSupport(position(along, cross));
			break;
		case SectorArchitectureElement::nave:
			for (double along : alongPositions(stations))
				for (double cross : {-crossExtent * 0.42,
									 crossExtent * 0.42,
									 -crossExtent, crossExtent})
					addSupport(position(along, cross));
			break;
		case SectorArchitectureElement::transept:
			for (double along : alongPositions(stations))
				for (double cross : {-crossExtent * 0.45,
									 crossExtent * 0.45})
					addSupport(position(along, cross));
			for (double cross : crossPositions(stations))
				addSupport(position((longBegin + longEnd) * 0.5, cross));
			break;
		case SectorArchitectureElement::apse:
		{
			const double longSpan = longEnd - longBegin;
			const double radiusLong =
					std::min(longSpan * 0.24, shortSpan * 0.34);
			const double radiusCross =
					std::min(crossExtent, shortSpan * 0.38);
			const double apseCenter = longEnd - radiusLong;
			const double approachEnd =
					std::max(longBegin, apseCenter - radiusLong);
			const int approachStations =
					std::max(2, request.architectureBays);
			for (int index = 0; index < approachStations; ++index)
			{
				const double along = approachStations == 1 ? longBegin :
						longBegin + (approachEnd - longBegin) *
							index / (approachStations - 1);
				for (double cross : {-crossExtent * 0.45,
									 crossExtent * 0.45})
					addSupport(position(along, cross));
			}
			const int arcSupports =
					std::max(5, request.architectureBays * 2 + 1);
			for (int index = 0; index < arcSupports; ++index)
			{
				const double angle = -std::numbers::pi * 0.5 +
						std::numbers::pi * index / (arcSupports - 1);
				addSupport(position(
						apseCenter + std::cos(angle) * radiusLong,
						std::sin(angle) * radiusCross));
			}
			break;
		}
		case SectorArchitectureElement::rotunda:
		case SectorArchitectureElement::sanctuary:
		{
			const int outerCount = request.architectureElement ==
					SectorArchitectureElement::rotunda ?
					std::max(6, request.architectureBays * 2) :
					std::max(8, request.architectureBays * 4);
			const double radiusX = width * 0.5 - margin;
			const double radiusY = height * 0.5 - margin;
			for (int index = 0; index < outerCount; ++index)
			{
				const double angle = 2.0 * std::numbers::pi *
						index / outerCount;
				addSupport({
					center.x + std::cos(angle) * radiusX,
					center.y + std::sin(angle) * radiusY
				});
			}
			if (request.architectureElement ==
				SectorArchitectureElement::sanctuary)
			{
				for (int index = 0; index < outerCount / 2; ++index)
				{
					const double angle = 2.0 * std::numbers::pi *
							index / (outerCount / 2);
					addSupport({
						center.x + std::cos(angle) * radiusX * 0.48,
						center.y + std::sin(angle) * radiusY * 0.48
					});
				}
				addSupport(center, 1.4);
			}
			break;
		}
		case SectorArchitectureElement::fortifiedKeep:
			for (double along : {longBegin, longEnd})
				for (double cross : {-crossExtent, crossExtent})
					addSupport(position(along, cross), 1.5);
			addSupport(position(longBegin, -std::min(
					crossExtent * 0.35, request.architectureSize * 0.8)));
			addSupport(position(longBegin, std::min(
					crossExtent * 0.35, request.architectureSize * 0.8)));
			break;
		default:
			break;
	}

	std::sort(supports.begin(), supports.end(),
			[](const ArchitecturalSupport &a,
			   const ArchitecturalSupport &b)
			{
				return std::tie(a.center.x, a.center.y) <
					   std::tie(b.center.x, b.center.y);
			});
	// If two layout rules meet at the same station, retain the larger pier.
	bool collapsedLayout = false;
	for (size_t index = 0; index + 1 < supports.size();)
	{
		if (!SamePoint(supports[index].center,
					   supports[index + 1].center))
		{
			index++;
			continue;
		}
		supports[index].scale = std::max(
				supports[index].scale, supports[index + 1].scale);
		supports.erase(supports.begin() + index + 1);
		collapsedLayout = true;
	}
	if (collapsedLayout)
	{
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 "The architectural layout collapses repeated supports at "
				 "this footprint size. Reduce Structure size/Margin or "
				 "drag a larger footprint.", host, -1, center);
	}

	bool denseWarning = false;
	std::set<size_t> overlappingSupports;
	std::optional<v2double_t> firstOverlap;
	for (size_t firstIndex = 0; firstIndex < supports.size(); ++firstIndex)
		for (size_t secondIndex = firstIndex + 1;
			 secondIndex < supports.size(); ++secondIndex)
		{
			const double spacing =
					(supports[firstIndex].center -
					 supports[secondIndex].center).hypot();
			const double combinedRadius =
					request.architectureSize *
					(supports[firstIndex].scale +
					 supports[secondIndex].scale) * 0.5;
			if (spacing < combinedRadius * 0.95)
			{
				overlappingSupports.insert(firstIndex);
				overlappingSupports.insert(secondIndex);
				if (!firstOverlap)
					firstOverlap = supports[firstIndex].center;
			}
			denseWarning |= spacing < combinedRadius * 1.4;
		}
	if (!overlappingSupports.empty())
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 SString::printf(
					 "%zu architectural supports overlap. Reduce "
					 "Structure size/Bays or enlarge the footprint.",
					 overlappingSupports.size()),
				 host, -1, firstOverlap);
	if (denseWarning)
		AddIssue(plan, SectorDesignIssueSeverity::warning,
				 "The support spacing is dense and may restrict player movement.",
				 host);

	int outsideSupports = 0;
	std::optional<v2double_t> firstOutside;
	for (size_t supportIndex = 0;
		 supportIndex < supports.size(); ++supportIndex)
	{
		const ArchitecturalSupport &support = supports[supportIndex];
		PlannedSectorShape shape;
		shape.outer = StyledPillar(
				support.center,
				request.architectureSize * support.scale,
				request.architectureStyle, request.rotation);
		bool outsideHost = false;
		shape.modelSector = host;
		shape.modelShape = hostShape;
		shape.inheritModelOnly = generatedHost;
		shape.closed = true;
		for (const v2double_t &point : shape.outer)
		{
			bool inHost = PointInsidePath(point, hostOutline);
			if (!generatedHost)
			{
				const Objid pointHost =
						hover::getNearestSector(doc, point);
				inHost = pointHost.type == ObjType::sectors &&
						pointHost.num == host;
			}
			if (!inHost)
			{
				outsideHost = true;
				if (!firstOutside)
					firstOutside = point;
			}
		}
		if (outsideHost)
			outsideSupports++;
		shape.role = outsideHost ||
				overlappingSupports.count(supportIndex) ?
					DesignPreviewRole::conflict :
					DesignPreviewRole::architecture;
		MakeClockwise(shape.outer);
		plan.shapes.push_back(std::move(shape));
	}
	if (outsideSupports > 0)
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 SString::printf(
					 "%d architectural support%s leave%s host sector #%d. "
					 "Move or enlarge the footprint, reduce Margin/Size, or "
					 "choose a layout that follows this room's shape.",
					 outsideSupports, outsideSupports == 1 ? "" : "s",
					 outsideSupports == 1 ? "s the" : " the",
					 host),
				 host, -1, firstOutside);
	plan.plannedStructures = static_cast<int>(supports.size());
	plan.previewLabels.push_back({
		center,
		SString::printf("%s %s - %d support%s%s",
				ArchitectureStyleName(request.architectureStyle),
				ArchitectureElementName(request.architectureElement),
				plan.plannedStructures,
				plan.plannedStructures == 1 ? "" : "s",
				generatedHost ? " + new hall" : ""),
		DesignPreviewRole::architecture
	});
}

void PlanFreeform(const SectorDesignRequest &request,
				  SectorDesignPlan &plan)
{
	if (request.anchors.size() < 3)
	{
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 "Place at least three freeform vertices.");
		return;
	}
	PlannedSectorShape shape;
	shape.outer = request.anchors;
	MakeClockwise(shape.outer);
	plan.shapes.push_back(std::move(shape));
}

std::vector<v2double_t> DoorStrip(const v2double_t &endpoint,
								  const v2double_t &toward,
								  double width, double depth)
{
	v2double_t direction = toward - endpoint;
	const double length = direction.hypot();
	if (length <= PLAN_EPSILON)
		return {};
	direction /= length;
	v2double_t normal{-direction.y, direction.x};
	const v2double_t half = normal * (width * 0.5);
	const v2double_t inner = endpoint + direction * depth;
	return {endpoint - half, endpoint + half, inner + half, inner - half};
}

std::vector<v2double_t> OrderBoundaryChain(
		const Document &doc, const std::vector<int> &lines,
		SectorDesignPlan &plan, std::vector<int> *orderedLines = nullptr)
{
	std::map<int, std::vector<int>> incident;
	for (int line : lines)
	{
		if (!doc.isLinedef(line))
		{
			AddIssue(plan, SectorDesignIssueSeverity::error,
					 "An extrusion source line no longer exists.", -1, line);
			return {};
		}
		const LineDef &linedef = *doc.linedefs[line];
		if (!doc.isVertex(linedef.start) || !doc.isVertex(linedef.end) ||
			linedef.start == linedef.end)
		{
			AddIssue(plan, SectorDesignIssueSeverity::error,
					 "An extrusion source line is malformed.", -1, line);
			return {};
		}
		incident[linedef.start].push_back(line);
		incident[linedef.end].push_back(line);
	}
	for (const auto &[vertex, touching] : incident)
		if (touching.size() > 2)
		{
			AddIssue(plan, SectorDesignIssueSeverity::error,
					 "The extrusion boundary chain branches.", -1,
					 touching.front(), doc.vertices[vertex]->xy());
			return {};
		}

	int startVertex = -1;
	for (const auto &[vertex, touching] : incident)
		if (touching.size() == 1 &&
			(startVertex < 0 || vertex < startVertex))
			startVertex = vertex;
	if (startVertex < 0)
	{
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 "The extrusion source must be an open boundary chain.");
		return {};
	}

	std::set<int> unused(lines.begin(), lines.end());
	std::vector<v2double_t> points;
	int vertex = startVertex;
	points.push_back(doc.vertices[vertex]->xy());
	while (!unused.empty())
	{
		int nextLine = -1;
		for (int candidate : incident[vertex])
			if (unused.count(candidate))
			{
				nextLine = candidate;
				break;
			}
		if (nextLine < 0)
		{
			AddIssue(plan, SectorDesignIssueSeverity::error,
					 "The extrusion boundary lines are disconnected.");
			return {};
		}
		unused.erase(nextLine);
		if (orderedLines)
			orderedLines->push_back(nextLine);
		const LineDef &linedef = *doc.linedefs[nextLine];
		vertex = linedef.start == vertex ? linedef.end : linedef.start;
		points.push_back(doc.vertices[vertex]->xy());
	}
	return points;
}

bool InfiniteLineIntersection(const v2double_t &a,
							  const v2double_t &directionA,
							  const v2double_t &b,
							  const v2double_t &directionB,
							  v2double_t &intersection)
{
	const double divisor =
			directionA.x * directionB.y -
			directionA.y * directionB.x;
	if (std::abs(divisor) <= PLAN_EPSILON)
		return false;
	const v2double_t delta = b - a;
	const double amount =
			(delta.x * directionB.y - delta.y * directionB.x) /
			divisor;
	intersection = a + directionA * amount;
	return true;
}

std::vector<v2double_t> OffsetChain(
		const std::vector<v2double_t> &source, double distance,
		double side, SectorDesignJoin join, int roundSegments)
{
	std::vector<v2double_t> directions;
	std::vector<v2double_t> normals;
	for (size_t index = 1; index < source.size(); index++)
	{
		v2double_t direction = source[index] - source[index - 1];
		const double length = direction.hypot();
		if (length <= PLAN_EPSILON)
			return {};
		direction /= length;
		directions.push_back(direction);
		normals.push_back({-direction.y * side, direction.x * side});
	}

	std::vector<v2double_t> result;
	result.push_back(source.front() + normals.front() * distance);
	for (size_t index = 1; index + 1 < source.size(); index++)
	{
		const v2double_t before =
				source[index] + normals[index - 1] * distance;
		const v2double_t after =
				source[index] + normals[index] * distance;
		if (join == SectorDesignJoin::bevel)
		{
			result.push_back(before);
			result.push_back(after);
			continue;
		}
		if (join == SectorDesignJoin::round)
		{
			double from = normals[index - 1].atan2();
			double to = normals[index].atan2();
			double sweep = to - from;
			while (sweep > std::numbers::pi)
				sweep -= 2.0 * std::numbers::pi;
			while (sweep < -std::numbers::pi)
				sweep += 2.0 * std::numbers::pi;
			const int segments = std::max(2, roundSegments);
			for (int part = 0; part <= segments; part++)
			{
				const double angle =
						from + sweep * part / segments;
				result.push_back(source[index] + v2double_t{
					std::cos(angle) * distance,
					std::sin(angle) * distance
				});
			}
			continue;
		}

		v2double_t miter;
		if (InfiniteLineIntersection(before, directions[index - 1],
									 after, directions[index], miter) &&
			(miter - source[index]).hypot() <=
					std::max(distance * 4.0, PLAN_EPSILON))
			result.push_back(miter);
		else
		{
			result.push_back(before);
			result.push_back(after);
		}
	}
	result.push_back(source.back() + normals.back() * distance);
	return result;
}

std::vector<v2double_t> ChainStrip(
		const std::vector<v2double_t> &nearEdge,
		const std::vector<v2double_t> &farEdge)
{
	std::vector<v2double_t> result = nearEdge;
	result.insert(result.end(), farEdge.rbegin(), farEdge.rend());
	return result;
}

void PlanExtrude(const Document &doc, const SectorDesignRequest &request,
				 MapFormat format, SectorDesignPlan &plan)
{
	if (request.anchorLines.empty())
	{
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 "Extrude needs an existing boundary line or chain.");
		return;
	}
	std::vector<int> orderedLines;
	std::vector<v2double_t> source =
			OrderBoundaryChain(doc, request.anchorLines, plan, &orderedLines);
	if (source.empty())
		return;

	std::set<int> commonSectors;
	const LineDef &firstLine = *doc.linedefs[orderedLines.front()];
	for (int side : {firstLine.right, firstLine.left})
	{
		const int sector = SectorForSide(doc, side);
		if (sector >= 0)
			commonSectors.insert(sector);
	}
	for (size_t index = 1; index < orderedLines.size(); ++index)
	{
		const LineDef &line = *doc.linedefs[orderedLines[index]];
		std::set<int> touching;
		for (int side : {line.right, line.left})
		{
			const int sector = SectorForSide(doc, side);
			if (sector >= 0)
				touching.insert(sector);
		}
		std::set<int> intersection;
		std::set_intersection(commonSectors.begin(), commonSectors.end(),
				touching.begin(), touching.end(),
				std::inserter(intersection, intersection.begin()));
		commonSectors = std::move(intersection);
	}
	if (commonSectors.empty())
	{
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 "Every extrusion line must bound one common source sector.");
		return;
	}

	size_t referenceIndex = 0;
	if (request.extrudeReferenceLine >= 0)
	{
		auto reference = std::find(orderedLines.begin(), orderedLines.end(),
								  request.extrudeReferenceLine);
		if (reference == orderedLines.end())
		{
			AddIssue(plan, SectorDesignIssueSeverity::error,
					 "The extrusion direction reference is no longer in the "
					 "selected boundary chain.",
					 -1, request.extrudeReferenceLine);
			return;
		}
		referenceIndex = static_cast<size_t>(
				reference - orderedLines.begin());
	}

	double side = 1.0;
	double distance = std::abs(request.depth);
	if (request.extrudeUseDragDepth && !request.anchors.empty())
	{
		const v2double_t pointer = request.anchors.back();
		if (request.extrudeReferenceLine < 0)
		{
			double bestDistance = std::numeric_limits<double>::max();
			for (size_t index = 0; index + 1 < source.size(); ++index)
			{
				const v2double_t a = source[index];
				const v2double_t b = source[index + 1];
				const v2double_t segment = b - a;
				const double lengthSquared = segment * segment;
				if (lengthSquared <= PLAN_EPSILON)
					continue;
				const double portion = std::clamp(
						((pointer - a) * segment) / lengthSquared,
						0.0, 1.0);
				const v2double_t closest = a + segment * portion;
				const double pointerDistance =
						(pointer - closest).hypot();
				if (pointerDistance < bestDistance - PLAN_EPSILON ||
					(std::abs(pointerDistance - bestDistance) <=
								PLAN_EPSILON &&
					 orderedLines[index] <
								orderedLines[referenceIndex]))
				{
					bestDistance = pointerDistance;
					referenceIndex = index;
				}
			}
		}

		const v2double_t a = source[referenceIndex];
		const v2double_t b = source[referenceIndex + 1];
		const double referenceLength = (b - a).hypot();
		const double signedDepth = Cross(a, b, pointer) / referenceLength;
		if (signedDepth < 0)
			side = -1.0;
		distance = std::abs(signedDepth);
	}
	else if (request.depth < 0)
		side = -1.0;
	if (request.extrudeOpposite)
		side = -side;

	plan.extrudeReferenceLine = orderedLines[referenceIndex];
	plan.resolvedExtrudeDepth = distance;
	plan.extrudeOpposite = request.extrudeOpposite;
	if (distance <= PLAN_EPSILON)
	{
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 request.extrudeUseDragDepth ?
					 "Move the pointer away from the boundary to set a "
					 "nonzero extrusion depth." :
					 "The extrusion depth is too small.");
		return;
	}

	std::vector<v2double_t> farEdge = OffsetChain(
			source, distance, side, request.join, request.roundSegments);
	if (farEdge.empty())
	{
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 "The extrusion offset could not be constructed.");
		return;
	}

	// Show the exact normal used for the gesture. This makes direction and
	// depth obvious before commit, especially on reversed or bent chains.
	const v2double_t referenceStart = source[referenceIndex];
	const v2double_t referenceEnd = source[referenceIndex + 1];
	v2double_t referenceDirection = referenceEnd - referenceStart;
	referenceDirection /= referenceDirection.hypot();
	const v2double_t referenceNormal{
		-referenceDirection.y * side, referenceDirection.x * side
	};
	const v2double_t nearMidpoint =
			(referenceStart + referenceEnd) * 0.5;
	const v2double_t farMidpoint =
			nearMidpoint + referenceNormal * distance;
	plan.previewPaths.push_back({
		{nearMidpoint, farMidpoint}, DesignPreviewRole::anchor, false, false
	});
	plan.previewPoints.push_back({
		farMidpoint, DesignPreviewRole::anchor
	});
	plan.previewLabels.push_back({
		(nearMidpoint + farMidpoint) * 0.5,
		SString::printf("%.1f", distance),
		DesignPreviewRole::anchor
	});

	std::vector<v2double_t> fullOutline = ChainStrip(source, farEdge);
	MakeClockwise(fullOutline);
	if (request.startConnection == SectorConnection::door)
	{
		std::vector<int> doorLines;
		if (request.autoDoorLines)
		{
			int bestLine = -1;
			double bestLength = -1;
			for (int line : orderedLines)
			{
				const double lineLength =
						doc.calcLength(*doc.linedefs[line]);
				if (lineLength > bestLength + PLAN_EPSILON ||
					(std::abs(lineLength - bestLength) <= PLAN_EPSILON &&
					 (bestLine < 0 || line < bestLine)))
				{
					bestLine = line;
					bestLength = lineLength;
				}
			}
			if (bestLine >= 0)
				doorLines.push_back(bestLine);
		}
		else
		{
			std::set<int> selected;
			for (int line : request.doorLines)
				if (std::find(orderedLines.begin(), orderedLines.end(), line) ==
					orderedLines.end())
					AddIssue(plan, SectorDesignIssueSeverity::error,
							 "A chosen door segment is not in the extrusion chain.",
							 -1, line);
				else
					selected.insert(line);
			for (int line : orderedLines)
				if (selected.count(line))
					doorLines.push_back(line);
			if (doorLines.empty())
				AddIssue(plan, SectorDesignIssueSeverity::error,
						 "Select at least one extrusion segment for a door.");
		}
		if (!plan.valid())
			return;

		const double doorDepth =
				std::max(8.0, std::abs(request.doorDepth));
		if (request.doorWidth < -PLAN_EPSILON)
		{
			AddIssue(plan, SectorDesignIssueSeverity::error,
					 "Door width must be zero (Auto) or positive.");
			return;
		}
		const double clearance = format == MapFormat::udmf ?
				1.0 / UDMF_SCALE : 1.0;
		if (doorDepth + clearance > distance)
		{
			AddIssue(plan, SectorDesignIssueSeverity::error,
					 "Extrusion depth must leave room behind every door.");
			return;
		}

		Paths64 doorClipPaths;
		for (int doorLine : doorLines)
		{
			auto found = std::find(
					orderedLines.begin(), orderedLines.end(), doorLine);
			const size_t index =
					static_cast<size_t>(found - orderedLines.begin());
			const v2double_t a = source[index];
			const v2double_t b = source[index + 1];
			v2double_t direction = b - a;
			const double segmentLength = direction.hypot();
			if (segmentLength <= PLAN_EPSILON)
			{
				AddIssue(plan, SectorDesignIssueSeverity::error,
						 "A chosen door segment has zero length.",
						 -1, doorLine);
				continue;
			}
			direction /= segmentLength;
			const double safeStart = index > 0 ? 8.0 : 0.0;
			const double safeEnd = segmentLength -
					(index + 1 < orderedLines.size() ? 8.0 : 0.0);
			const double safeLength = safeEnd - safeStart;
			const double width = request.doorWidth <= PLAN_EPSILON ?
					safeLength : request.doorWidth;
			const double startDistance =
					safeStart + (safeLength - width) * 0.5 +
					request.doorOffset;
			const double endDistance = startDistance + width;
			if (width < 8.0 - PLAN_EPSILON)
			{
				AddIssue(plan, SectorDesignIssueSeverity::error,
						 "This segment cannot fit an 8-unit door and safe track walls.",
						 -1, doorLine);
				continue;
			}
			if (startDistance < safeStart - PLAN_EPSILON ||
				endDistance > safeEnd + PLAN_EPSILON)
			{
				AddIssue(plan, SectorDesignIssueSeverity::error,
						 "Door width and offset do not fit between the chain's track walls.",
						 -1, doorLine);
				continue;
			}
			const v2double_t normal{
				-direction.y * side, direction.x * side
			};
			const v2double_t nearA = a + direction * startDistance;
			const v2double_t nearB = a + direction * endDistance;
			const v2double_t farA = nearA + normal * doorDepth;
			const v2double_t farB = nearB + normal * doorDepth;
			std::vector<v2double_t> strip{
				nearA, nearB, farB, farA
			};
			MakeClockwise(strip);
			doorClipPaths.push_back(ToClipperPath(strip, format));

			PlannedSectorShape door;
			door.outer = std::move(strip);
			door.smartDoor = true;
			door.role = DesignPreviewRole::door;
			plan.shapes.push_back(std::move(door));
			plan.doorSourceLines.push_back(doorLine);
		}
		if (!plan.valid())
			return;

		Paths64 remainder = Difference(
				{ToClipperPath(fullOutline, format)}, doorClipPaths,
				FillRule::NonZero);
		const size_t before = plan.shapes.size();
		AppendClipperShapes(plan, remainder, format,
							DesignPreviewRole::proposed, -1);
		if (plan.shapes.size() == before)
			AddIssue(plan, SectorDesignIssueSeverity::error,
					 "The selected doors consume the complete extrusion.");
		plan.plannedDoors =
				static_cast<int>(plan.doorSourceLines.size());

		const std::set<int> doorSet(
				plan.doorSourceLines.begin(), plan.doorSourceLines.end());
		for (int line : request.anchorLines)
			plan.connections.push_back({
				line, doorSet.count(line) ?
					SectorConnection::door : SectorConnection::wall
			});
	}
	else
	{
		PlannedSectorShape shape;
		shape.outer = std::move(fullOutline);
		plan.shapes.push_back(std::move(shape));
		for (int line : request.anchorLines)
			plan.connections.push_back({line, request.startConnection});
	}
}

std::vector<v2double_t> BuildRoutedCenterline(
		const SectorDesignRequest &request)
{
	std::vector<v2double_t> route = request.anchors;
	if (route.size() != 2)
		return route;

	const v2double_t start = route.front();
	const v2double_t end = route.back();
	switch ((request.routeIndex % 4 + 4) % 4)
	{
		case 1:
			return {start, {end.x, start.y}, end};
		case 2:
			return {start, {start.x, end.y}, end};
		case 3:
		{
			const double middle = (start.x + end.x) * 0.5;
			return {start, {middle, start.y}, {middle, end.y}, end};
		}
		default:
			return route;
	}
}

double RouteLength(const std::vector<v2double_t> &route)
{
	double length = 0;
	for (size_t index = 1; index < route.size(); ++index)
		length += (route[index] - route[index - 1]).hypot();
	return length;
}

std::vector<v2double_t> SliceRoute(
		const std::vector<v2double_t> &route, double from, double to)
{
	std::vector<v2double_t> result;
	double traveled = 0;
	for (size_t index = 1; index < route.size(); ++index)
	{
		const v2double_t start = route[index - 1];
		const v2double_t end = route[index];
		const double length = (end - start).hypot();
		if (length <= PLAN_EPSILON)
			continue;
		const double segmentStart = traveled;
		const double segmentEnd = traveled + length;
		if (to < segmentStart - PLAN_EPSILON)
			break;
		if (from <= segmentEnd + PLAN_EPSILON &&
			to >= segmentStart - PLAN_EPSILON)
		{
			const double localFrom = std::clamp(
					(from - segmentStart) / length, 0.0, 1.0);
			const double localTo = std::clamp(
					(to - segmentStart) / length, 0.0, 1.0);
			const v2double_t first = start + (end - start) * localFrom;
			const v2double_t last = start + (end - start) * localTo;
			if (result.empty() || !SamePoint(result.back(), first))
				result.push_back(first);
			if (result.empty() || !SamePoint(result.back(), last))
				result.push_back(last);
		}
		traveled = segmentEnd;
	}
	return result;
}

void PlanCorridor(const SectorDesignRequest &request, MapFormat format,
				  SectorDesignPlan &plan)
{
	if (request.anchors.size() < 2)
	{
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 "A corridor needs start and end anchors.");
		return;
	}
	if (request.width <= PLAN_EPSILON)
	{
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 "Corridor width must be positive.");
		return;
	}
	if (request.startConnection == SectorConnection::wall &&
		request.endConnection == SectorConnection::wall)
	{
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 "At least one corridor endpoint must be open or a door.");
		return;
	}

	std::vector<v2double_t> route = BuildRoutedCenterline(request);

	Path64 center;
	for (const v2double_t &point : route)
	{
		const v2double_t quantized = Quantize(format, point);
		center.emplace_back(std::llround(quantized.x * CoordinateScale(format)),
							std::llround(quantized.y * CoordinateScale(format)));
	}
	const double scaledWidth = request.width * CoordinateScale(format) * 0.5;
	const double arcTolerance = request.join == SectorDesignJoin::round ?
			std::max(0.25, scaledWidth /
					std::max(4, request.roundSegments)) : 0.0;
	Paths64 corridor = InflatePaths({center}, scaledWidth,
			ToClipperJoin(request.join), EndType::Butt, 2.0, arcTolerance);
	if (corridor.empty())
	{
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 "The corridor route collapsed after quantization.");
		return;
	}

	Paths64 doors;
	std::vector<std::vector<v2double_t>> doorPaths;
	auto addDoor = [&](bool start)
	{
		const size_t endpointIndex = start ? 0 : route.size() - 1;
		const size_t towardIndex = start ? 1 : route.size() - 2;
		std::vector<v2double_t> strip = DoorStrip(
				route[endpointIndex], route[towardIndex], request.width,
				std::max(PLAN_EPSILON, std::abs(request.doorDepth)));
		if (!strip.empty())
		{
			doors.push_back(ToClipperPath(strip, format));
			doorPaths.push_back(std::move(strip));
		}
	};
	if (request.startConnection == SectorConnection::door)
		addDoor(true);
	if (request.endConnection == SectorConnection::door)
		addDoor(false);

	if (!doors.empty())
		corridor = Difference(corridor, doors, FillRule::NonZero);
	AppendClipperShapes(plan, corridor, format,
						DesignPreviewRole::proposed, -1);
	for (std::vector<v2double_t> &doorPath : doorPaths)
	{
		MakeClockwise(doorPath);
		PlannedSectorShape door;
		door.outer = std::move(doorPath);
		door.smartDoor = true;
		door.role = DesignPreviewRole::door;
		plan.shapes.push_back(std::move(door));
		plan.plannedDoors++;
	}
	if (!request.anchorLines.empty() &&
		request.anchorLines.front() >= 0)
		plan.connections.push_back({request.anchorLines.front(),
									request.startConnection});
	if (request.anchorLines.size() > 1 &&
		request.anchorLines.back() >= 0)
		plan.connections.push_back({request.anchorLines.back(),
									request.endConnection});
	if (request.startConnection == SectorConnection::door &&
		(request.anchorLines.empty() ||
		 request.anchorLines.front() < 0))
		AddIssue(plan, SectorDesignIssueSeverity::error,
				"The start door must be anchored to an existing line.");
	if (request.endConnection == SectorConnection::door &&
		(request.anchorLines.size() < 2 ||
		 request.anchorLines.back() < 0))
		AddIssue(plan, SectorDesignIssueSeverity::error,
				"The end door must be anchored to an existing line.");
}

void PlanInset(const Document &doc, const ConfigData &config,
			   const SectorDesignRequest &request, MapFormat format,
			   SectorDesignPlan &plan)
{
	if (request.targetSectors.empty())
	{
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 "Select at least one sector to inset or expand.");
		return;
	}
	if (std::abs(request.offset) <= PLAN_EPSILON)
	{
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 "Inset distance must be nonzero.");
		return;
	}
	std::set<int> selected(request.targetSectors.begin(),
						  request.targetSectors.end());
	for (int line = 0; line < doc.numLinedefs(); ++line)
	{
		const LineDef &linedef = *doc.linedefs[line];
		const int right = SectorForSide(doc, linedef.right);
		const int left = SectorForSide(doc, linedef.left);
		if (right != left && selected.count(right) &&
			selected.count(left))
		{
			AddIssue(plan, SectorDesignIssueSeverity::error,
					 "Inset sectors must be nonadjacent.", right, line);
			return;
		}
	}
	for (int sector : request.targetSectors)
	{
		if (!doc.isSector(sector))
		{
			AddIssue(plan, SectorDesignIssueSeverity::error,
					 "A selected sector no longer exists.", sector);
			continue;
		}
		if (SectorHasProtectedAction(doc, config, sector))
		{
			AddIssue(plan,
					 request.replaceAffectedSectors ?
						SectorDesignIssueSeverity::warning :
						SectorDesignIssueSeverity::error,
					 request.replaceAffectedSectors ?
						"A selected protected door or lift is explicitly included." :
						"A selected sector is a protected door or lift.",
					 sector);
			if (!request.replaceAffectedSectors)
				continue;
		}
		std::vector<std::vector<v2double_t>> loops;
		if (!ExtractSectorLoops(doc, sector, loops))
		{
			AddIssue(plan, SectorDesignIssueSeverity::error,
					 "The selected sector does not have well-formed loops.",
					 sector);
			continue;
		}
		Paths64 paths;
		for (std::vector<v2double_t> loop : loops)
		{
			// Clipper expects outer and hole windings to be opposite. The
			// editor's boundary traversal already guarantees that.
			paths.push_back(ToClipperPath(loop, format));
		}
		const double delta = -request.offset * CoordinateScale(format);
		const double arcTolerance = request.join == SectorDesignJoin::round ?
				std::max(0.25, std::abs(delta) /
						std::max(4, request.roundSegments)) : 0.0;
		Paths64 result = InflatePaths(paths, delta,
				ToClipperJoin(request.join), EndType::Polygon,
				2.0, arcTolerance);
		if (result.empty())
		{
			AddIssue(plan, SectorDesignIssueSeverity::error,
					 "The inset completely collapses this sector.", sector);
			continue;
		}
		const size_t before = plan.shapes.size();
		AppendClipperShapes(plan, result, format,
							DesignPreviewRole::proposed, sector);
		const std::optional<SectorPropertyOptions> &generatedProperties =
				request.offset > 0 ? request.innerProperties :
									 request.ringProperties;
		for (size_t shapeIndex = before;
			 shapeIndex < plan.shapes.size(); shapeIndex++)
			if (generatedProperties)
				plan.shapes[shapeIndex].properties = *generatedProperties;
		const std::optional<SectorPropertyOptions> &existingProperties =
				request.offset > 0 ? request.ringProperties :
									 request.innerProperties;
		if (existingProperties)
			plan.sectorChanges.push_back(PropertyChangeForExisting(
					doc, sector, *existingProperties, plan));
		if (request.offset < 0)
		{
			for (size_t shapeIndex = before;
				 shapeIndex < plan.shapes.size(); shapeIndex++)
			{
				const auto &expanded = plan.shapes[shapeIndex].outer;
				for (int line = 0; line < doc.numLinedefs(); ++line)
				{
					const LineDef &linedef = *doc.linedefs[line];
					if (!doc.isVertex(linedef.start) ||
						!doc.isVertex(linedef.end))
						continue;
					const int right = SectorForSide(doc, linedef.right);
					const int left = SectorForSide(doc, linedef.left);
					if ((right < 0 || selected.count(right)) &&
						(left < 0 || selected.count(left)))
						continue;
					const v2double_t middle =
							(doc.getStart(linedef).xy() +
							 doc.getEnd(linedef).xy()) * 0.5;
					if (PointInsidePath(middle, expanded))
					{
						AddIssue(plan,
								 request.replaceAffectedSectors ?
									SectorDesignIssueSeverity::warning :
									SectorDesignIssueSeverity::error,
								 request.replaceAffectedSectors ?
									"The outward ring explicitly consumes an unselected sector." :
									"The outward ring would consume an unselected sector.",
								 selected.count(right) ? left : right, line);
						break;
					}
				}
			}
		}
		if (plan.shapes.size() - before > 1)
			AddIssue(plan, SectorDesignIssueSeverity::warning,
					 "The offset separates into multiple sector islands.",
					 sector);
	}
}

std::vector<int> OrderStairSectors(const Document &doc,
								   const SectorDesignRequest &request,
								   SectorDesignPlan &plan)
{
	std::set<int> selected;
	for (int sector : request.targetSectors)
	{
		if (!doc.isSector(sector))
			AddIssue(plan, SectorDesignIssueSeverity::error,
					 "A selected stair sector no longer exists.", sector);
		else
			selected.insert(sector);
	}
	std::map<int, std::set<int>> adjacency;
	for (int sector : selected)
		adjacency[sector];
	for (const auto &line : doc.linedefs)
	{
		int right = SectorForSide(doc, line->right);
		int left = SectorForSide(doc, line->left);
		if (right != left && selected.count(right) && selected.count(left))
		{
			adjacency[right].insert(left);
			adjacency[left].insert(right);
		}
	}
	bool branched = false;
	size_t edgeCount = 0;
	for (const auto &[sector, neighbors] : adjacency)
	{
		edgeCount += neighbors.size();
		branched |= neighbors.size() > 2;
	}
	edgeCount /= 2;
	if (!plan.valid() || selected.empty())
		return {};

	int start = request.startSector;
	if (branched)
	{
		if (!selected.count(request.startSector) ||
			!selected.count(request.endSector))
		{
			AddIssue(plan, SectorDesignIssueSeverity::error,
					 "A branched stair selection needs explicit start and end sectors.");
			return {};
		}
		if (edgeCount >= selected.size())
		{
			AddIssue(plan, SectorDesignIssueSeverity::error,
					 "The anchored stair selection has an ambiguous cycle.");
			return {};
		}

		std::queue<int> pending;
		std::map<int, int> parent;
		pending.push(request.startSector);
		parent[request.startSector] = -1;
		while (!pending.empty() && !parent.count(request.endSector))
		{
			int sector = pending.front();
			pending.pop();
			for (int neighbor : adjacency[sector])
				if (!parent.count(neighbor))
				{
					parent[neighbor] = sector;
					pending.push(neighbor);
				}
		}
		if (!parent.count(request.endSector))
		{
			AddIssue(plan, SectorDesignIssueSeverity::error,
					 "The anchored stair endpoints are not connected.");
			return {};
		}
		std::vector<int> order;
		for (int sector = request.endSector; sector >= 0;
			 sector = parent[sector])
			order.push_back(sector);
		std::reverse(order.begin(), order.end());
		if (order.size() != selected.size())
			AddIssue(plan, SectorDesignIssueSeverity::warning,
					 "Only the explicit path through the branched selection will become stairs.");
		return order;
	}
	if (!selected.count(start))
	{
		start = -1;
		for (const auto &[sector, neighbors] : adjacency)
			if (neighbors.size() <= 1 && (start < 0 || sector < start))
				start = sector;
	}
	if (start < 0)
	{
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 "A closed stair loop needs an explicit start sector.");
		return {};
	}

	std::vector<int> order;
	int previous = -1;
	int current = start;
	while (current >= 0)
	{
		order.push_back(current);
		int next = -1;
		for (int neighbor : adjacency[current])
			if (neighbor != previous)
			{
				next = neighbor;
				break;
			}
		previous = current;
		current = next;
		if (current >= 0 &&
			std::find(order.begin(), order.end(), current) != order.end())
		{
			AddIssue(plan, SectorDesignIssueSeverity::error,
					 "The stair selection contains a cycle.");
			return {};
		}
	}
	if (order.size() != selected.size())
	{
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 "Selected stair sectors are not one connected path.");
		return {};
	}
	if (request.endSector >= 0 && order.back() != request.endSector)
		std::reverse(order.begin(), order.end());
	if (request.endSector >= 0 && order.back() != request.endSector)
	{
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 "The chosen stair end is not an endpoint of the path.",
				 request.endSector);
		return {};
	}
	return order;
}

void PlanStairs(const Document &doc, const ConfigData &config,
				const SectorDesignRequest &request, MapFormat format,
				SectorDesignPlan &plan)
{
	if (!request.targetSectors.empty())
	{
		std::vector<int> order = OrderStairSectors(doc, request, plan);
		if (order.empty())
			return;
		for (int sector : order)
			if (SectorHasProtectedAction(doc, config, sector))
				AddIssue(plan,
						 request.replaceAffectedSectors ?
							SectorDesignIssueSeverity::warning :
							SectorDesignIssueSeverity::error,
						 request.replaceAffectedSectors ?
							"A protected door or lift is explicitly included in the stair path." :
							"A stair sector is a protected door or lift.",
						 sector);
		if (!plan.valid())
			return;

		const int baseFloor = doc.sectors[order.front()]->floorh;
		double rise = request.stairRise;
		if (request.stairFitTarget && order.size() > 1)
			rise = static_cast<double>(request.stairTargetFloor - baseFloor) /
					static_cast<double>(order.size() - 1);
		bool uneven = false;
		for (size_t index = 0; index < order.size(); ++index)
		{
			int sector = order[index];
			const Sector &source = *doc.sectors[sector];
			const double exact = baseFloor + rise * index;
			const int floor = static_cast<int>(std::lround(exact));
			uneven |= std::abs(exact - floor) > PLAN_EPSILON;
			const int ceiling = request.preserveHeadroom ?
					source.ceilh + floor - source.floorh : source.ceilh;
			if (ceiling - floor < config.miscInfo.player_h)
				AddIssue(plan, SectorDesignIssueSeverity::warning,
						 "A stair step has inadequate player clearance.",
						 sector);
			plan.sectorChanges.push_back({
				sector, floor, ceiling, std::nullopt
			});
			std::vector<std::vector<v2double_t>> loops;
			if (ExtractSectorLoops(doc, sector, loops))
			{
				bool labelled = false;
				for (auto &loop : loops)
				{
					plan.previewPaths.push_back({
						loop, DesignPreviewRole::stair, true, true
					});
					if (!labelled && !loop.empty())
					{
						v2double_t center{0.0, 0.0};
						for (const v2double_t &point : loop)
							center += point;
						center /= static_cast<double>(loop.size());
						plan.previewLabels.push_back({
							center,
							SString::printf("%zu", index + 1),
							DesignPreviewRole::stair
						});
						labelled = true;
					}
				}
			}
		}
		if (uneven)
			AddIssue(plan, SectorDesignIssueSeverity::warning,
					 "The fitted stair rise is uneven after height rounding.");
		plan.plannedSteps = static_cast<int>(order.size());
		return;
	}

	if (request.anchors.size() < 2)
	{
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 "Choose the start and end of the stair run.");
		return;
	}
	std::vector<v2double_t> route = BuildRoutedCenterline(request);
	const double completeLength = RouteLength(route);
	if (request.startConnection == SectorConnection::wall &&
		request.endConnection == SectorConnection::wall)
	{
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 "At least one stair endpoint must remain connected.");
		return;
	}
	double trimStart = 0;
	double trimEnd = completeLength;
	const double doorDepth = std::max(8.0, std::abs(request.doorDepth));
	std::vector<PlannedSectorShape> endpointDoors;
	auto addDoor = [&](bool start)
	{
		if ((start && (request.anchorLines.empty() ||
					  request.anchorLines.front() < 0)) ||
			(!start && (request.anchorLines.size() < 2 ||
					   request.anchorLines.back() < 0)))
		{
			AddIssue(plan, SectorDesignIssueSeverity::error,
					 start ?
						"The stair start door must be anchored to an existing line." :
						"The stair end door must be anchored to an existing line.");
			return;
		}
		const size_t endpoint = start ? 0 : route.size() - 1;
		const size_t toward = start ? 1 : route.size() - 2;
		std::vector<v2double_t> outline = DoorStrip(
				route[endpoint], route[toward], request.width, doorDepth);
		if (outline.empty())
			return;
		MakeClockwise(outline);
		PlannedSectorShape door;
		door.outer = std::move(outline);
		door.role = DesignPreviewRole::door;
		door.smartDoor = true;
		endpointDoors.push_back(std::move(door));
		if (start)
			trimStart += doorDepth;
		else
			trimEnd -= doorDepth;
		plan.plannedDoors++;
	};
	if (request.startConnection == SectorConnection::door)
		addDoor(true);
	if (request.endConnection == SectorConnection::door)
		addDoor(false);
	if (trimEnd - trimStart <= PLAN_EPSILON)
	{
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 "Door depths consume the complete stair run.");
		return;
	}
	if (trimStart > 0 || trimEnd < completeLength)
		route = SliceRoute(route, trimStart, trimEnd);
	for (PlannedSectorShape &door : endpointDoors)
		plan.shapes.push_back(std::move(door));
	if (!request.anchorLines.empty() &&
		request.anchorLines.front() >= 0)
		plan.connections.push_back({request.anchorLines.front(),
									request.startConnection});
	if (request.anchorLines.size() > 1 &&
		request.anchorLines.back() >= 0)
		plan.connections.push_back({request.anchorLines.back(),
									request.endConnection});

	const double length = RouteLength(route);
	if (length <= PLAN_EPSILON || request.width <= PLAN_EPSILON)
	{
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 "The stair run has invalid length or width.");
		return;
	}
	int count = request.stairCount > 0 ? request.stairCount :
			std::max(1, static_cast<int>(std::floor(
					length / std::max(PLAN_EPSILON, request.stairTread))));
	const double tread = length / count;
	double rise = request.stairRise;
	if (request.stairFitTarget && count > 1)
	{
		int baseFloor = 0;
		const int model = ChooseModelSector(doc, request);
		if (doc.isSector(model))
			baseFloor = doc.sectors[model]->floorh;
		if (request.properties.floorMode == SectorValueMode::absolute)
			baseFloor = request.properties.floorValue;
		else if (request.properties.floorMode == SectorValueMode::relative)
			baseFloor += request.properties.floorValue;
		rise = static_cast<double>(request.stairTargetFloor - baseFloor) /
				static_cast<double>(count - 1);
	}

	bool uneven = false;
	const double scaledWidth =
			request.width * CoordinateScale(format) * 0.5;
	const double arcTolerance = request.join == SectorDesignJoin::round ?
			std::max(0.25, scaledWidth /
					std::max(4, request.roundSegments)) : 0.0;
	for (int index = 0; index < count; ++index)
	{
		const std::vector<v2double_t> slice = SliceRoute(
				route, tread * index, tread * (index + 1));
		if (slice.size() < 2)
		{
			AddIssue(plan, SectorDesignIssueSeverity::error,
					 "A stair tread collapsed along the chosen route.");
			continue;
		}
		Paths64 footprint = InflatePaths(
				{ToClipperPath(slice, format)}, scaledWidth,
				ToClipperJoin(request.join), EndType::Butt, 2.0,
				arcTolerance);
		const size_t before = plan.shapes.size();
		AppendClipperShapes(plan, footprint, format,
							DesignPreviewRole::stair, -1);
		if (plan.shapes.size() == before)
		{
			AddIssue(plan, SectorDesignIssueSeverity::error,
					 "A stair tread collapsed after format quantization.");
			continue;
		}
		const double exactDelta = rise * index;
		const int floorDelta = static_cast<int>(std::lround(exactDelta));
		uneven |= std::abs(exactDelta - floorDelta) > PLAN_EPSILON;
		for (size_t shape = before; shape < plan.shapes.size(); ++shape)
		{
			plan.shapes[shape].floorDelta = floorDelta;
			plan.shapes[shape].ceilingDelta =
					request.preserveHeadroom ? floorDelta : 0;
		}
	}
	if (uneven)
		AddIssue(plan, SectorDesignIssueSeverity::warning,
				 "The stair rise is uneven after height quantization.");
	plan.plannedSteps = count;
}

bool ActionPresetSupported(const ConfigData &config,
						   const SectorActionPreset &preset,
						   MapFormat format)
{
	if (config.line_types.find(preset.special) == config.line_types.end())
		return false;
	if (preset.activation == ActivationPolicy::encoded)
	{
		if (format != MapFormat::doom)
			return false;
		return std::none_of(preset.args.begin(), preset.args.end(),
				[](const SectorActionArgument &argument)
				{
					return argument.targetTag;
				});
	}
	if (format != MapFormat::hexen && format != MapFormat::udmf)
		return false;
	if (std::count_if(preset.args.begin(), preset.args.end(),
			[](const SectorActionArgument &argument)
			{
				return argument.targetTag;
			}) != 1)
		return false;
	for (const SectorActionArgument &argument : preset.args)
		if (format == MapFormat::hexen && !argument.targetTag &&
			(argument.value < 0 || argument.value > 255))
			return false;
	return true;
}

const SectorActionPreset *FindActionPreset(
		const std::vector<SectorActionPreset> &presets, const SString &id)
{
	auto found = std::find_if(presets.begin(), presets.end(),
			[&](const SectorActionPreset &preset)
			{
				return preset.id.noCaseEqual(id);
			});
	return found == presets.end() ? nullptr : &*found;
}

std::unordered_set<int> UsedTags(const Document &doc)
{
	std::unordered_set<int> tags;
	tags.insert(0);
	for (const auto &sector : doc.sectors)
		tags.insert(sector->tag);
	for (const auto &line : doc.linedefs)
	{
		tags.insert(line->arg1);
		tags.insert(line->arg2);
		tags.insert(line->arg3);
		tags.insert(line->arg4);
		tags.insert(line->arg5);
	}
	return tags;
}

void PlanLiftSelection(const Document &doc, const ConfigData &config,
					   MapFormat format, const SectorDesignRequest &request,
					   SectorDesignPlan &plan)
{
	std::vector<SectorActionPreset> available =
			M_AvailableSectorActionPresets(config, SectorActionKind::lift,
										  format);
	if (available.empty())
	{
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 "Smart Lift is unavailable for this game and map format.");
		return;
	}
	const SectorActionPreset *preset = FindActionPreset(
			available, request.actionPresetId);
	if (!preset)
	{
		preset = &available.front();
		auto repeating = std::find_if(available.begin(), available.end(),
				[](const SectorActionPreset &candidate)
				{
					return candidate.activation ==
							ActivationPolicy::useRepeat ||
							candidate.id.noCaseStartsWith("lower-repeat");
				});
		if (repeating != available.end())
			preset = &*repeating;
	}
	plan.resolvedActionPreset = *preset;

	if (request.targetSectors.empty())
	{
		if (!request.anchorLines.empty())
		{
			SectorDesignRequest extrude = request;
			extrude.mode = SectorDesignMode::extrude;
			extrude.startConnection = SectorConnection::open;
			PlanExtrude(doc, extrude, format, plan);
			if (!plan.shapes.empty())
			{
				plan.shapes.front().smartLift = true;
				plan.shapes.front().role = DesignPreviewRole::lift;
				plan.plannedLifts = 1;
			}
			return;
		}
		AddIssue(plan, SectorDesignIssueSeverity::error,
				 "Select lift sectors or choose a wall for a lift alcove.");
		return;
	}

	std::set<int> selected;
	for (int sector : request.targetSectors)
	{
		if (!doc.isSector(sector))
			AddIssue(plan, SectorDesignIssueSeverity::error,
					 "A selected lift sector no longer exists.", sector);
		else
			selected.insert(sector);
	}
	if (!plan.valid())
		return;

	std::map<int, std::set<int>> adjacency;
	for (int sector : selected)
		adjacency[sector];
	for (const auto &line : doc.linedefs)
	{
		int right = SectorForSide(doc, line->right);
		int left = SectorForSide(doc, line->left);
		if (right != left && selected.count(right) && selected.count(left))
		{
			adjacency[right].insert(left);
			adjacency[left].insert(right);
		}
	}

	std::unordered_set<int> usedTags = UsedTags(doc);
	std::set<int> requestedTriggers(request.liftTriggerLines.begin(),
									request.liftTriggerLines.end());
	const bool filterTriggers = request.restrictLiftTriggers ||
			!requestedTriggers.empty();
	std::set<int> acceptedTriggers;
	int nextTag = 1;
	std::set<int> visited;
	for (int seed : selected)
	{
		if (visited.count(seed))
			continue;
		PlannedLift lift;
		std::queue<int> pending;
		pending.push(seed);
		visited.insert(seed);
		while (!pending.empty())
		{
			int sector = pending.front();
			pending.pop();
			lift.sectors.push_back(sector);
			for (int neighbor : adjacency[sector])
				if (visited.insert(neighbor).second)
					pending.push(neighbor);
		}
		while (usedTags.count(nextTag))
			nextTag++;
		if (format == MapFormat::hexen && nextTag > 255)
		{
			AddIssue(plan, SectorDesignIssueSeverity::error,
					 "No free Hexen-format lift tag remains.");
			return;
		}
		if (format == MapFormat::doom &&
			nextTag > std::numeric_limits<int16_t>::max())
		{
			AddIssue(plan, SectorDesignIssueSeverity::error,
					 "No free Doom-format lift tag remains.");
			return;
		}
		lift.tag = nextTag++;
		usedTags.insert(lift.tag);
		lift.preset = *preset;

		std::set<int> component(lift.sectors.begin(), lift.sectors.end());
		std::set<int> adjacentFloors;
		for (int line = 0; line < doc.numLinedefs(); ++line)
		{
			const LineDef &linedef = *doc.linedefs[line];
			int right = SectorForSide(doc, linedef.right);
			int left = SectorForSide(doc, linedef.left);
			bool rightIn = component.count(right);
			bool leftIn = component.count(left);
			if (rightIn == leftIn)
				continue;
			int neighbor = rightIn ? left : right;
			if (neighbor >= 0)
			{
				adjacentFloors.insert(doc.sectors[neighbor]->floorh);
				if (!filterTriggers ||
					requestedTriggers.count(line))
				{
					lift.triggerLines.push_back(line);
					acceptedTriggers.insert(line);
				}
				if ((!filterTriggers ||
					 requestedTriggers.count(line)) &&
					linedef.type != 0)
					AddIssue(plan, SectorDesignIssueSeverity::warning,
							 "An existing portal special will be replaced.",
							 rightIn ? right : left, line);
			}
		}
		if (lift.triggerLines.empty())
			AddIssue(plan, SectorDesignIssueSeverity::error,
					 "A lift component has no usable two-sided portal.",
					 seed);
		int platformFloor = doc.sectors[seed]->floorh;
		lift.lowerStop = platformFloor;
		for (int adjacentFloor : adjacentFloors)
			if (adjacentFloor < lift.lowerStop)
				lift.lowerStop = adjacentFloor;
		lift.travel = platformFloor - lift.lowerStop;
		if (lift.travel == 0)
			AddIssue(plan, SectorDesignIssueSeverity::warning,
					 "The lift has no inferred vertical travel.", seed);
		if (adjacentFloors.size() > 1)
			AddIssue(plan, SectorDesignIssueSeverity::warning,
					 "Neighboring lift floors do not all match.", seed);
		plan.lifts.push_back(std::move(lift));
	}
	for (int line : requestedTriggers)
		if (!acceptedTriggers.count(line))
			AddIssue(plan, SectorDesignIssueSeverity::error,
					 "A chosen lift trigger is not a usable platform portal.",
					 -1, line);
	plan.plannedLifts = static_cast<int>(plan.lifts.size());
}

void ApplyProperties(EditOperation &op, const ConfigData &config,
					 const SectorDesignRequest &request,
					 const PlannedSectorShape &shape,
					 const std::vector<int> &sectors)
{
	const SectorPropertyOptions inheritedOnly;
	const SectorPropertyOptions &properties = shape.inheritModelOnly ?
			inheritedOnly :
			(shape.properties ? *shape.properties : request.properties);
	for (int sector : sectors)
	{
		Sector &target = *op.doc.sectors[sector];
		if (op.doc.isSector(shape.modelSector))
			target = *op.doc.sectors[shape.modelSector];
		else
			target.SetDefaults(config);

		if (properties.floorMode == SectorValueMode::absolute)
			target.floorh = properties.floorValue;
		else if (properties.floorMode == SectorValueMode::relative)
			target.floorh += properties.floorValue;
		if (properties.ceilingMode == SectorValueMode::absolute)
			target.ceilh = properties.ceilingValue;
		else if (properties.ceilingMode == SectorValueMode::relative)
			target.ceilh += properties.ceilingValue;
		if (properties.lightMode == SectorValueMode::absolute)
			target.light = properties.lightValue;
		else if (properties.lightMode == SectorValueMode::relative)
			target.light += properties.lightValue;
		target.floorh += shape.floorDelta;
		target.ceilh += shape.ceilingDelta;
		if (!properties.floorTexture.empty())
			target.floor_tex = BA_InternaliseString(
					properties.floorTexture);
		if (!properties.ceilingTexture.empty())
			target.ceil_tex = BA_InternaliseString(
					properties.ceilingTexture);
		if (properties.sectorType)
			target.type = *properties.sectorType;
		if (properties.sectorTag)
			target.tag = *properties.sectorTag;
		if (shape.closed)
			target.ceilh = target.floorh;
	}

	if (shape.closed)
	{
		const std::set<int> generated(
				sectors.begin(), sectors.end());
		for (int line = 0; line < op.doc.numLinedefs(); ++line)
		{
			const LineDef &linedef = *op.doc.linedefs[line];
			const bool right = generated.count(
					SectorForSide(op.doc, linedef.right)) != 0;
			const bool left = generated.count(
					SectorForSide(op.doc, linedef.left)) != 0;
			if (right == left ||
				(linedef.flags & MLF_Blocking))
				continue;
			op.changeLinedef(
					line, &LineDef::flags,
					linedef.flags | MLF_Blocking);
		}
	}

	if (!properties.wallTexture.empty())
	{
		const StringID texture = BA_InternaliseString(
				properties.wallTexture);
		const std::set<int> generated(sectors.begin(), sectors.end());
		for (int side = 0; side < op.doc.numSidedefs(); ++side)
		{
			if (!generated.count(op.doc.sidedefs[side]->sector))
				continue;
			bool oneSided = false;
			for (const auto &line : op.doc.linedefs)
				if ((line->right == side || line->left == side) &&
					line->OneSided())
					oneSided = true;
			if (oneSided)
				op.changeSidedef(side, SideDef::F_MID_TEX, texture);
			else
			{
				op.changeSidedef(side, SideDef::F_UPPER_TEX, texture);
				op.changeSidedef(side, SideDef::F_LOWER_TEX, texture);
			}
		}
	}
}

void AppendLift(EditOperation &op, const PlannedLift &lift,
				MapFormat format)
{
	for (int sector : lift.sectors)
		op.changeSector(sector, Sector::F_TAG, lift.tag);
	std::array<int, 5> arguments = {};
	for (size_t index = 0; index < lift.preset.args.size(); ++index)
		arguments[index] = lift.preset.args[index].targetTag ?
				lift.tag : lift.preset.args[index].value;
	// In Doom format arg1 is the linedef tag. Encoded presets cannot spell
	// @tag in configuration, so Smart Lift supplies the fresh platform tag.
	if (format == MapFormat::doom &&
		lift.preset.activation == ActivationPolicy::encoded)
		arguments[0] = lift.tag;
	for (int line : lift.triggerLines)
	{
		op.changeLinedef(line, &LineDef::type, lift.preset.special);
		M_SetLineArguments(op, line, arguments);
		M_SetLineActivation(op, line, format, lift.preset.activation);
		LineDef &linedef = *op.doc.linedefs[line];
		int flags = linedef.flags | MLF_TwoSided;
		flags &= ~MLF_Blocking;
		op.changeLinedef(line, &LineDef::flags, flags);
	}
}

int IsolateConnectionSidedef(EditOperation &op, int line, bool right)
{
	LineDef &target = *op.doc.linedefs[line];
	const int side = right ? target.right : target.left;
	if (!op.doc.isSidedef(side))
		return -1;

	int references = 0;
	for (const std::shared_ptr<LineDef> &candidate : op.doc.linedefs)
	{
		references += candidate->right == side;
		references += candidate->left == side;
	}
	if (references <= 1)
		return side;

	const int copy = op.addNew(ObjType::sidedefs);
	*op.doc.sidedefs[copy] = *op.doc.sidedefs[side];
	if (right)
		op.changeLinedef(line, &LineDef::right, copy);
	else
		op.changeLinedef(line, &LineDef::left, copy);
	return copy;
}

struct ConnectionApplySnapshot
{
	PlannedConnectionChange planned;
	v2double_t sourceStart;
	v2double_t sourceEnd;
	std::set<int> originalSectors;
};

ConnectionApplySnapshot SnapshotConnection(
		const Document &doc, const PlannedConnectionChange &connection)
{
	ConnectionApplySnapshot result;
	result.planned = connection;
	if (!doc.isLinedef(connection.line))
		return result;

	const LineDef &line = *doc.linedefs[connection.line];
	if (doc.isVertex(line.start) && doc.isVertex(line.end))
	{
		result.sourceStart = doc.getStart(line).xy();
		result.sourceEnd = doc.getEnd(line).xy();
	}
	for (int side : {line.right, line.left})
	{
		const int sector = SectorForSide(doc, side);
		if (sector >= 0)
			result.originalSectors.insert(sector);
	}
	return result;
}

std::vector<int> ResolveConnectionLines(
		const Document &doc, const ConnectionApplySnapshot &snapshot,
		const std::set<int> &generatedSectors)
{
	std::vector<int> result;
	for (int line = 0; line < doc.numLinedefs(); ++line)
	{
		const LineDef &candidate = *doc.linedefs[line];
		if (!candidate.TwoSided() ||
			!doc.isVertex(candidate.start) ||
			!doc.isVertex(candidate.end))
			continue;
		const v2double_t start = doc.getStart(candidate).xy();
		const v2double_t end = doc.getEnd(candidate).xy();
		if (!SegmentContained(start, end,
							 snapshot.sourceStart, snapshot.sourceEnd))
			continue;

		const int right = SectorForSide(doc, candidate.right);
		const int left = SectorForSide(doc, candidate.left);
		const bool rightGenerated = generatedSectors.count(right) > 0;
		const bool leftGenerated = generatedSectors.count(left) > 0;
		const bool rightOriginal =
				snapshot.originalSectors.count(right) > 0;
		const bool leftOriginal =
				snapshot.originalSectors.count(left) > 0;
		if ((rightGenerated && leftOriginal) ||
			(leftGenerated && rightOriginal))
			result.push_back(line);
	}
	return result;
}

} // namespace

bool SectorDesignPlan::valid() const
{
	return std::none_of(issues.begin(), issues.end(),
			[](const SectorDesignIssue &issue)
			{
				return issue.severity == SectorDesignIssueSeverity::error;
			});
}

const std::vector<SectorArchitectureDescriptor> &M_ArchitectureCatalog()
{
	return ArchitectureCatalogData();
}

const SectorArchitectureDescriptor &M_ArchitectureDescriptor(
		SectorArchitectureElement element)
{
	const auto &catalog = ArchitectureCatalogData();
	auto found = std::find_if(
			catalog.begin(), catalog.end(),
			[&](const SectorArchitectureDescriptor &descriptor)
			{
				return descriptor.element == element;
			});
	if (found != catalog.end())
		return *found;

	static const SectorArchitectureDescriptor unsupported =
	{
		"unsupported", SectorArchitectureElement::pillar,
		SectorArchitectureFamily::structuralSupports,
		"Unsupported structure",
		"The selected architectural structure is unavailable.",
		DesignPreviewRole::conflict, 0, A_STATIC,
		"Bays:", "Size:", "Elevation:", 1, 1, 1, 1, 1, 0, 1
	};
	return unsupported;
}

bool M_ArchitectureHasControl(SectorArchitectureElement element,
		SectorArchitectureControl control)
{
	return (M_ArchitectureDescriptor(element).controls &
			ArchitectureControlBit(control)) != 0;
}

bool M_ArchitectureSupportsFunction(SectorArchitectureElement element,
		SectorArchitectureFunction function)
{
	const unsigned index = static_cast<unsigned>(function);
	if (index > static_cast<unsigned>(
			SectorArchitectureFunction::smartLift))
		return false;
	return (M_ArchitectureDescriptor(element).functions &
			(1u << index)) != 0;
}

const char *M_ArchitectureFamilyLabel(SectorArchitectureFamily family)
{
	switch (family)
	{
		case SectorArchitectureFamily::structuralSupports:
			return "Structural supports";
		case SectorArchitectureFamily::floorsTerraces:
			return "Floors and terraces";
		case SectorArchitectureFamily::circulation:
			return "Circulation";
		case SectorArchitectureFamily::waterworks:
			return "Waterworks";
		case SectorArchitectureFamily::wallsScreens:
			return "Walls and screens";
		case SectorArchitectureFamily::ceilingsVaults:
			return "Ceilings and vaults";
	}
	return "Architecture";
}

SString M_ArchitectureEffectDescription(
		const SectorDesignRequest &request)
{
	const double height = request.architectureHeight;
	switch (request.architectureElement)
	{
		case SectorArchitectureElement::raisedDais:
			return SString::printf(
					"raises one walkable floor by %.1f units", height);
		case SectorArchitectureElement::sunkenCourt:
			return SString::printf(
					"excavates one walkable floor by %.1f units", height);
		case SectorArchitectureElement::tieredZiggurat:
			return SString::printf(
					"builds three nested walkable tiers at %.1f units per tier",
					height);
		case SectorArchitectureElement::grandStair:
			return SString::printf(
					"builds %d directional steps at %.1f units per step",
					request.architectureBays, height);
		case SectorArchitectureElement::fountainBasin:
			return SString::printf(
					"excavates a basin and raises its centerpiece with %.1f-unit relief",
					height);
		case SectorArchitectureElement::reflectingPool:
			return SString::printf(
					"excavates a beveled pool by %.1f units", height);
		case SectorArchitectureElement::balconyGallery:
			return SString::printf(
					"raises a walkable perimeter gallery by %.1f units",
					height);
		case SectorArchitectureElement::processionalChannel:
			return SString::printf(
					"excavates a longitudinal channel by %.1f units", height);
		case SectorArchitectureElement::screenWall:
			return "builds a solid perforated wall mass";
		case SectorArchitectureElement::cofferedCeiling:
			return SString::printf(
					"raises a grid of ceiling recesses by %.1f units", height);
		case SectorArchitectureElement::groinVaults:
			return SString::printf(
					"raises repeated vault cells by %.1f units", height);
		case SectorArchitectureElement::raisedBridge:
			return SString::printf(
					"raises a directional bridge by %.1f units", height);

		case SectorArchitectureElement::crossCore:
			return "builds one concave floor-to-ceiling cruciform core";
		case SectorArchitectureElement::hollowTower:
			return "builds four solid tower-shell masses with opposed entries";
		case SectorArchitectureElement::buttressedTower:
			return SString::printf(
					"builds a solid tower core with %d buttresses",
					request.architectureBays);
		case SectorArchitectureElement::shearWallPair:
			return "builds two solid shear walls around an open passage";
		case SectorArchitectureElement::steppedMonument:
			return SString::printf(
					"builds %d nested solid monument tiers at %.1f units per tier",
					request.architectureBays, height);
		case SectorArchitectureElement::centralPlatform:
			return request.architectureFunction ==
					SectorArchitectureFunction::smartLift ?
					SString::printf(
						"raises a %.1f-unit platform and finishes it as one Smart Lift",
						height) :
					SString::printf(
						"raises one centered walkable platform by %.1f units",
						height);
		case SectorArchitectureElement::splitLevelStage:
			return SString::printf(
					"builds two ordered stage levels at %.1f-unit intervals",
					height);
		case SectorArchitectureElement::cornerTerraces:
			return SString::printf(
					"builds %d mirrored corner-anchored terraces at %.1f units per tier",
					request.architectureBays, height);
		case SectorArchitectureElement::octagonalPodium:
			return SString::printf(
					"builds %d nested polygonal podium tiers at %.1f units per tier",
					request.architectureBays, height);
		case SectorArchitectureElement::horseshoeAmphitheater:
			return SString::printf(
					"builds %d three-sided seating rows at %.1f units per row",
					request.architectureBays, height);
		case SectorArchitectureElement::switchbackStair:
			return SString::printf(
					"builds two %d-step flights and a shared landing",
					request.architectureBays);
		case SectorArchitectureElement::bifurcatedStair:
			return SString::printf(
					"builds one lower and two upper %d-step ceremonial flights",
					request.architectureBays);
		case SectorArchitectureElement::spiralStair:
			return SString::printf(
					"builds %d %s wedge steps at %.1f units per step",
					request.architectureBays,
					request.architectureMirrored ?
						"counterclockwise" : "clockwise", height);
		case SectorArchitectureElement::landingCatwalk:
			return SString::printf(
					"raises a catwalk and two landings by %.1f units", height);
		case SectorArchitectureElement::crossingBridges:
			return SString::printf(
					"raises two connected crossing bridges by %.1f units",
					height);
		case SectorArchitectureElement::perimeterMoat:
			return SString::printf(
					"excavates a perimeter moat by %.1f units", height);
		case SectorArchitectureElement::crossCanal:
			return SString::printf(
					"excavates one connected cross canal by %.1f units",
					height);
		case SectorArchitectureElement::twinCanals:
			return SString::printf(
					"excavates two parallel canals by %.1f units", height);
		case SectorArchitectureElement::steppedCascade:
			return SString::printf(
					"builds %d basins descending %.1f units each",
					request.architectureBays, height);
		case SectorArchitectureElement::fountainCourt:
			return SString::printf(
					"excavates four basins and raises a centerpiece with %.1f-unit relief",
					height);
		case SectorArchitectureElement::partitionWall:
			return "builds one continuous solid partition wall";
		case SectorArchitectureElement::crenellatedWall:
			return SString::printf(
					"builds a continuous wall with %d projected merlons",
					request.architectureBays);
		case SectorArchitectureElement::buttressedWall:
			return SString::printf(
					"builds one wall slab with %d solid buttresses",
					request.architectureBays);
		case SectorArchitectureElement::staggeredScreen:
			return SString::printf(
					"builds %d alternating wall panels around a traversable slalom",
					request.architectureBays);
		case SectorArchitectureElement::gatehousePassage:
			return "builds two gatehouse masses around a centered open passage";
		case SectorArchitectureElement::trayCeiling:
			return SString::printf(
					"raises one broad ceiling tray by %.1f units", height);
		case SectorArchitectureElement::barrelVault:
			return SString::printf(
					"approximates a barrel vault with %d ceiling bands and %.1f units of relief",
					request.architectureBays, height);
		case SectorArchitectureElement::ribbedCrossVault:
			return SString::printf(
					"raises four ceiling cells toward a %.1f-unit crossing",
					height);
		case SectorArchitectureElement::domedCeiling:
			return SString::printf(
					"raises %d nested dome rings by %.1f units per ring",
					request.architectureBays, height);
		case SectorArchitectureElement::beamLattice:
			return SString::printf(
					"lowers a %d by %d beam lattice by %.1f units",
					request.architectureBays,
					request.architectureBays, height);
		default:
			return M_ArchitectureDescriptor(
					request.architectureElement).description;
	}
}

double M_MinimumArchitectureSize(SectorArchitectureStyle style)
{
	switch (style)
	{
		case SectorArchitectureStyle::romanesque:
			return 8.0;
		case SectorArchitectureStyle::gothic:
			return 16.0;
		case SectorArchitectureStyle::industrial:
			return 32.0;
		case SectorArchitectureStyle::artDeco:
			return 32.0;
		case SectorArchitectureStyle::infernal:
			return 32.0;
		default:
			return 4.0;
	}
}

double M_MinimumArchitectureSize(
		SectorArchitectureStyle style,
		SectorArchitectureElement element)
{
	const SectorArchitectureDescriptor &descriptor =
			M_ArchitectureDescriptor(element);
	return std::max(
			descriptor.minimumSize,
			ArchitectureUsesSectionStyle(element) ?
				M_MinimumArchitectureSize(style) : 1.0);
}

bool M_ArchitectureUsesSectionStyle(SectorArchitectureElement element)
{
	return ArchitectureUsesSectionStyle(element);
}

bool M_ArchitectureUsesBays(SectorArchitectureElement element)
{
	return ArchitectureUsesBays(element);
}

bool M_ArchitectureUsesHeight(SectorArchitectureElement element)
{
	return ArchitectureUsesHeight(element);
}

std::vector<SectorActionPreset> M_AvailableSectorActionPresets(
		const ConfigData &config, SectorActionKind kind, MapFormat format)
{
	std::vector<SectorActionPreset> result;
	for (const SectorActionPreset &preset : config.sector_action_presets)
		if (preset.kind == kind &&
			ActionPresetSupported(config, preset, format))
			result.push_back(preset);
	return result;
}

SString M_SectorActionPresetLabel(const ConfigData &config,
								  const SectorActionPreset &preset)
{
	if (!preset.label.noCaseEqual("@special"))
		return preset.label;
	auto found = config.line_types.find(preset.special);
	return found == config.line_types.end() ?
			preset.id : found->second.desc;
}

SectorDesignPlan M_PlanSectorDesign(const Document &doc,
		const ConfigData &config, const ImageSet *images, MapFormat format,
		const SectorDesignRequest &request)
{
	SectorDesignPlan plan;
	if (!RequestCoordinatesAreSafe(doc, format, request, plan))
		return plan;

	// Corridor and generated-stair endpoints use -1 as an intentional
	// "anchored in void" placeholder. Any nonnegative reference, however,
	// must still exist when the pure plan is recomputed for apply.
	if (request.mode == SectorDesignMode::corridor ||
		(request.mode == SectorDesignMode::stairs &&
		 request.targetSectors.empty()))
		for (int line : request.anchorLines)
			if (line >= 0 && !doc.isLinedef(line))
			{
				AddIssue(plan, SectorDesignIssueSeverity::error,
						 "A connection anchor line no longer exists.",
						 -1, line);
				return plan;
			}

	switch (request.mode)
	{
	case SectorDesignMode::room:
		PlanRectangle(doc, request, format, plan);
		break;
	case SectorDesignMode::polygon:
		PlanProfilePolygon(request, plan);
		break;
	case SectorDesignMode::freeform:
		PlanFreeform(request, plan);
		break;
	case SectorDesignMode::extrude:
		PlanExtrude(doc, request, format, plan);
		break;
	case SectorDesignMode::inset:
		PlanInset(doc, config, request, format, plan);
		break;
	case SectorDesignMode::corridor:
		PlanCorridor(request, format, plan);
		break;
	case SectorDesignMode::stairs:
		PlanStairs(doc, config, request, format, plan);
		break;
	case SectorDesignMode::lift:
		PlanLiftSelection(doc, config, format, request, plan);
		break;
	case SectorDesignMode::architecture:
		PlanArchitecture(doc, config, request, plan, format);
		break;
	}

	if (request.mode == SectorDesignMode::architecture &&
		request.architectureFunction ==
				SectorArchitectureFunction::smartLift &&
		plan.valid())
	{
		const std::vector<SectorActionPreset> available =
				M_AvailableSectorActionPresets(
						config, SectorActionKind::lift, format);
		if (available.empty())
			AddIssue(plan, SectorDesignIssueSeverity::error,
					 "Smart Lift is unavailable for this game and map "
					 "format. Choose Static geometry.");
		else
		{
			const SectorActionPreset *preset =
					FindActionPreset(
							available, request.actionPresetId);
			if (!preset)
			{
				preset = &available.front();
				auto repeating = std::find_if(
						available.begin(), available.end(),
						[](const SectorActionPreset &candidate)
						{
							return candidate.activation ==
									ActivationPolicy::useRepeat ||
									candidate.id.noCaseStartsWith(
											"lower-repeat");
						});
				if (repeating != available.end())
					preset = &*repeating;
			}
			plan.resolvedActionPreset = *preset;
		}
	}

	std::set<int> retained(
			plan.retainedSectors.begin(), plan.retainedSectors.end());
	for (int sector : request.targetSectors)
		if (doc.isSector(sector))
			retained.insert(sector);
	for (int line : request.anchorLines)
		if (doc.isLinedef(line))
		{
			const LineDef &linedef = *doc.linedefs[line];
			const int right = SectorForSide(doc, linedef.right);
			const int left = SectorForSide(doc, linedef.left);
			if (right >= 0)
				retained.insert(right);
			if (left >= 0)
				retained.insert(left);
		}
	plan.retainedSectors.assign(retained.begin(), retained.end());
	for (const PlannedConnectionChange &connection : plan.connections)
		if (doc.isLinedef(connection.line))
		{
			const LineDef &line = *doc.linedefs[connection.line];
			if (!doc.isVertex(line.start) || !doc.isVertex(line.end))
				continue;
			if (line.type != 0)
				AddIssue(plan, SectorDesignIssueSeverity::warning,
						 connection.connection == SectorConnection::door ?
							"The existing connection special will be replaced by the door." :
							"The existing action is retained on this connection.",
						 -1, connection.line);
			DesignPreviewRole role = DesignPreviewRole::retained;
			if (connection.connection == SectorConnection::open)
				role = DesignPreviewRole::opening;
			else if (connection.connection == SectorConnection::door)
				role = DesignPreviewRole::door;
			plan.previewPaths.push_back({
				{doc.getStart(line).xy(), doc.getEnd(line).xy()},
				role, false, false
			});
		}
	for (const PlannedLift &lift : plan.lifts)
		for (int trigger : lift.triggerLines)
			if (doc.isLinedef(trigger))
			{
				const LineDef &line = *doc.linedefs[trigger];
				if (!doc.isVertex(line.start) || !doc.isVertex(line.end))
					continue;
				plan.previewPaths.push_back({
					{doc.getStart(line).xy(), doc.getEnd(line).xy()},
					DesignPreviewRole::lift, false, false
				});
			}

	if (plan.plannedDoors > 0)
	{
		std::vector<DoorPreset> available =
				M_AvailableDoorPresets(config, format);
		if (available.empty())
			AddIssue(plan, SectorDesignIssueSeverity::error,
					 "Door connections are unavailable for this configuration.");
		else
		{
			auto chosen = std::find_if(available.begin(), available.end(),
					[&](const DoorPreset &preset)
					{
						return preset.id.noCaseEqual(
								request.doorOptions.presetId);
					});
			if (chosen == available.end())
				chosen = available.begin();
			ResolveGeneratedDoorTextures(
					doc, config, images, request, plan);
			plan.resolvedDoorOptions.presetId = chosen->id;
		}
	}

	for (PlannedSectorShape &shape : plan.shapes)
	{
		ValidatePath(doc, config, request, format, plan, shape.outer);
		MakeClockwise(shape.outer);
		for (std::vector<v2double_t> &hole : shape.holes)
		{
			ValidatePath(doc, config, request, format, plan, hole);
			MakeCounterClockwise(hole);
			if (!hole.empty() && !PointInsidePath(hole.front(), shape.outer))
				AddIssue(plan, SectorDesignIssueSeverity::error,
						 "A generated hole lies outside its sector outline.");
		}
		AddPreviewShape(plan, shape);
	}
	ValidatePlannedShapeRelations(plan);
	plan.plannedLines = EstimateCreatedLines(doc, plan);
	for (const v2double_t &anchor : request.anchors)
		plan.previewPoints.push_back({
			Quantize(format, anchor), DesignPreviewRole::anchor
		});
	if (request.anchors.size() >= 2)
	{
		const v2double_t midpoint =
				(request.anchors.front() + request.anchors.back()) * 0.5;
		if (request.mode == SectorDesignMode::room ||
			request.mode == SectorDesignMode::architecture)
		{
			const v2double_t dimensions =
					request.anchors.back() - request.anchors.front();
			plan.previewLabels.push_back({
				Quantize(format, midpoint),
				SString::printf("%.1f:%.1f", std::abs(dimensions.x),
								std::abs(dimensions.y)),
				DesignPreviewRole::proposed
			});
		}
		else if (request.mode == SectorDesignMode::corridor ||
				 request.mode == SectorDesignMode::stairs)
			plan.previewLabels.push_back({
				Quantize(format, midpoint),
				SString::printf("%.1f", request.width),
				request.mode == SectorDesignMode::stairs ?
					DesignPreviewRole::stair :
					DesignPreviewRole::proposed
			});
	}
	ResolveProperties(doc, config, images, request, format, plan);
	return plan;
}

bool M_ApplySectorDesign(Document &doc, const ConfigData &config,
		const ImageSet *images, MapFormat format,
		const SectorDesignRequest &request, SectorDesignPlan *appliedPlan)
{
	SectorDesignPlan plan = M_PlanSectorDesign(doc, config, images, format,
											  request);
	if (appliedPlan)
		*appliedPlan = plan;
	if (!plan.valid())
		return false;

	bool returningCorridor = false;
	if (request.mode == SectorDesignMode::corridor &&
		request.anchorLines.size() >= 2)
	{
		const int startLine = request.anchorLines.front();
		const int endLine = request.anchorLines.back();
		if (doc.isLinedef(startLine) && doc.isLinedef(endLine))
		{
			const LineDef &start = *doc.linedefs[startLine];
			const LineDef &end = *doc.linedefs[endLine];
			for (int startSide : {start.right, start.left})
				for (int endSide : {end.right, end.left})
				if (SectorForSide(doc, startSide) >= 0 &&
					SectorForSide(doc, startSide) ==
							SectorForSide(doc, endSide))
					returningCorridor = true;
		}
	}

	EditOperation op(doc.basis);
	try
	{
		// Geometry insertion can turn a source seam into a portal and update
		// its textures. Protect any unusual cross-linedef sidedef sharing
		// before the first generated loop is inserted.
		std::vector<ConnectionApplySnapshot> connectionSnapshots;
		for (const PlannedConnectionChange &connection : plan.connections)
			if (doc.isLinedef(connection.line))
			{
				connectionSnapshots.push_back(
						SnapshotConnection(doc, connection));
				IsolateConnectionSidedef(op, connection.line, true);
				IsolateConnectionSidedef(op, connection.line, false);
			}

		std::vector<int> doorSectors;
		std::vector<int> liftSectors;
		std::vector<std::vector<int>> createdByShape;
		createdByShape.reserve(plan.shapes.size());
		for (size_t shapeIndex = 0;
			 shapeIndex < plan.shapes.size(); ++shapeIndex)
		{
			const PlannedSectorShape &shape = plan.shapes[shapeIndex];
			PlannedSectorShape resolvedShape = shape;
			if (shape.modelShape >= 0)
			{
				if (shape.modelShape >=
						static_cast<int>(createdByShape.size()) ||
					createdByShape[shape.modelShape].empty())
				{
					op.setAbort(false);
					AddIssue(plan, SectorDesignIssueSeverity::error,
							 "A generated sector's plan-local model could "
							 "not be resolved during apply.");
					if (appliedPlan)
						*appliedPlan = plan;
					return false;
				}
				resolvedShape.modelSector =
						createdByShape[shape.modelShape].front();
			}
			std::vector<CapturedSectorRegion> previousRegions;
			if (returningCorridor)
				previousRegions = CaptureSectorRegions(doc);
			std::vector<int> created =
					doc.objects.insertSectorPolygon(op, shape.outer, format);
			for (const auto &hole : shape.holes)
			{
				std::vector<v2double_t> holeOutline = hole;
				if (shape.holesRetainModel)
					MakeClockwise(holeOutline);
				std::vector<int> holeSectors =
						doc.objects.insertSectorPolygon(
								op, holeOutline, format);
				if (shape.holesRetainModel && !holeSectors.empty())
				{
					PlannedSectorShape retainedHole;
					retainedHole.modelSector =
							resolvedShape.modelSector;
					retainedHole.inheritModelOnly =
							resolvedShape.inheritModelOnly;
					ApplyProperties(
							op, config, request,
							retainedHole, holeSectors);
				}
			}
			// Open routed strokes can close an incidental cell against an
			// existing endpoint wall (notably a U returning to its source
			// sector). The polygon inserter necessarily discovers both
			// loops; keep only the requested corridor cells and restore
			// newly enclosed, previously empty cells to void.
			if (returningCorridor)
				RemoveIncidentalVoidSectors(
						op, doc, shape, previousRegions,
						config.default_wall_tex, created);
			if (created.empty())
			{
				op.setAbort(false);
				AddIssue(plan, SectorDesignIssueSeverity::error,
						 "The generated outline did not create a sector.");
				if (appliedPlan)
					*appliedPlan = plan;
				return false;
			}
			if (request.mode == SectorDesignMode::architecture &&
				created.size() != 1)
			{
				op.setAbort(false);
				AddIssue(plan, SectorDesignIssueSeverity::error,
						 SString::printf(
							 "Format quantization split architectural "
							 "section %zu into multiple cells. Increase "
							 "Structure size or change Style before applying.",
							 shapeIndex + 1));
				if (appliedPlan)
					*appliedPlan = plan;
				return false;
			}
			ApplyProperties(
					op, config, request, resolvedShape, created);
			createdByShape.push_back(created);
			plan.createdSectors.insert(plan.createdSectors.end(),
									   created.begin(), created.end());
			if (shape.smartDoor)
				doorSectors.insert(doorSectors.end(),
								  created.begin(), created.end());
			if (shape.smartLift)
				liftSectors.insert(liftSectors.end(),
								  created.begin(), created.end());
		}

		const std::set<int> generatedSectors(
				plan.createdSectors.begin(), plan.createdSectors.end());
		for (const ConnectionApplySnapshot &snapshot :
			 connectionSnapshots)
		{
			const PlannedConnectionChange &connection = snapshot.planned;
			// Generated door sectors are finalized after every shape exists;
			// Smart Door owns the precise split portal lines and their flags.
			if (connection.connection == SectorConnection::door)
				continue;

			const std::vector<int> resolvedLines =
					ResolveConnectionLines(
							doc, snapshot, generatedSectors);
			if (resolvedLines.empty())
			{
				// An inward or through-sector extrusion can put its actual
				// shared interfaces on newly generated edges instead of the
				// selected source span. Preserve that span exactly; generic
				// loop filling already made the interior portals. Routed
				// endpoint tools remain strict because their selected wall
				// must become the endpoint portal.
				if (request.mode == SectorDesignMode::extrude)
					continue;
				op.setAbort(false);
				AddIssue(plan, SectorDesignIssueSeverity::error,
						 "The connection did not produce a shared portal "
						 "on its selected boundary.",
						 -1, connection.line);
				if (appliedPlan)
					*appliedPlan = plan;
				return false;
			}

			const StringID texture = connection.connection ==
					SectorConnection::open ?
					BA_InternaliseString("-") :
					BA_InternaliseString(
						request.properties.wallTexture.empty() ?
							config.default_wall_tex :
							request.properties.wallTexture);
			for (int resolvedLine : resolvedLines)
			{
				LineDef &linedef = *doc.linedefs[resolvedLine];
				int flags = linedef.flags | MLF_TwoSided;
				if (connection.connection == SectorConnection::wall)
					flags |= MLF_Blocking;
				else
					flags &= ~MLF_Blocking;
				op.changeLinedef(
						resolvedLine, &LineDef::flags, flags);

				for (bool right : {true, false})
				{
					const int side = right ? linedef.right : linedef.left;
					if (!doc.isSidedef(side) ||
						doc.sidedefs[side]->mid_tex == texture)
						continue;
					const int isolated = IsolateConnectionSidedef(
							op, resolvedLine, right);
					if (doc.isSidedef(isolated))
						op.changeSidedef(isolated,
								SideDef::F_MID_TEX, texture);
				}
			}
		}

		for (const PlannedSectorChange &change : plan.sectorChanges)
		{
			if (change.floor)
				op.changeSector(change.sector, Sector::F_FLOORH,
								*change.floor);
			if (change.ceiling)
				op.changeSector(change.sector, Sector::F_CEILH,
								*change.ceiling);
			if (change.light)
				op.changeSector(change.sector, Sector::F_LIGHT,
								*change.light);
			if (change.type)
				op.changeSector(change.sector, Sector::F_TYPE,
								*change.type);
			if (change.tag)
				op.changeSector(change.sector, Sector::F_TAG,
								*change.tag);
			if (change.floorTexture)
				op.changeSector(change.sector, Sector::F_FLOOR_TEX,
						BA_InternaliseString(*change.floorTexture));
			if (change.ceilingTexture)
				op.changeSector(change.sector, Sector::F_CEIL_TEX,
						BA_InternaliseString(*change.ceilingTexture));
		}
		for (const PlannedLift &lift : plan.lifts)
			AppendLift(op, lift, format);

		if (!liftSectors.empty())
		{
			SectorDesignRequest liftRequest = request;
			liftRequest.mode = SectorDesignMode::lift;
			liftRequest.targetSectors = liftSectors;
			liftRequest.anchorLines.clear();
			SectorDesignPlan liftPlan = M_PlanSectorDesign(
					doc, config, images, format, liftRequest);
			if (!liftPlan.valid())
			{
				op.setAbort(false);
				plan.issues.insert(plan.issues.end(),
								   liftPlan.issues.begin(),
								   liftPlan.issues.end());
				if (appliedPlan)
					*appliedPlan = plan;
				return false;
			}
			for (const PlannedLift &lift : liftPlan.lifts)
				AppendLift(op, lift, format);
		}

		if (!doorSectors.empty())
		{
			selection_c selection(ObjType::sectors);
			for (int sector : doorSectors)
				selection.set(sector);
			DoorPlan doorPlan = M_PlanSmartDoors(doc, config, images, format,
					selection, plan.resolvedDoorOptions);
			if (!doorPlan.valid() ||
				!M_AppendSmartDoors(op, doorPlan, format))
			{
				op.setAbort(false);
				for (const DoorIssue &issue : doorPlan.issues)
					AddIssue(plan,
						issue.severity == DoorIssueSeverity::error ?
							SectorDesignIssueSeverity::error :
							SectorDesignIssueSeverity::warning,
						issue.message, issue.sector, issue.line);
				if (appliedPlan)
					*appliedPlan = plan;
				return false;
			}
		}

		if (request.mode == SectorDesignMode::stairs)
			op.setMessage("made %d smart stair steps",
						  std::max(1, plan.plannedSteps));
		else if (request.mode == SectorDesignMode::lift)
			op.setMessage("made %d smart lifts",
						  std::max(1, plan.plannedLifts));
		else if (request.mode == SectorDesignMode::architecture)
			op.setMessage("made %s (%d architectural sectors)",
						  ArchitectureElementName(
								  request.architectureElement),
						  std::max(1, plan.plannedStructures));
		else
			op.setMessage("made %d smart sectors",
						  static_cast<int>(plan.createdSectors.size()));
	}
	catch (...)
	{
		op.setAbort(false);
		throw;
	}

	if (appliedPlan)
		*appliedPlan = plan;
	return true;
}
