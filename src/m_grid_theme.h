//------------------------------------------------------------------------
//  GRID VISIBILITY THEMES
//------------------------------------------------------------------------

#ifndef __EUREKA_M_GRID_THEME_H__
#define __EUREKA_M_GRID_THEME_H__

#include "im_color.h"

#include <vector>

namespace grid
{

enum class VisualTheme
{
	highContrastDark = 0,
	vintagePhosphor,
	blueprintLight,
	custom
};

constexpr int kFirstVisualTheme =
		static_cast<int>(VisualTheme::highContrastDark);
constexpr int kCustomVisualTheme = static_cast<int>(VisualTheme::custom);

//  Map geometry and editing-feedback colors. Every built-in theme ships a
//  coordinated ink set so linedefs, things, vertices, selection, and the
//  snap reticle never fight the grid for the same hue or brightness.
//  The defaults reproduce the classic hardcoded Eureka colors; the Custom
//  theme uses them unchanged so user-tuned grids keep the familiar map look.
struct MapInk
{
	rgb_color_t linedef = rgbMake(144, 144, 144);  // two-sided linedef
	rgb_color_t wall = rgbMake(255, 255, 255);     // one-sided linedef
	rgb_color_t special = rgbMake(128, 255, 128);  // linedef with a special type
	rgb_color_t tagged = rgbMake(255, 128, 255);   // special targeting a tag
	rgb_color_t blocking = rgbMake(0, 255, 255);   // blocking-flagged linedef

	rgb_color_t sectorTag = rgbMake(0, 255, 0);      // sector with tag
	rgb_color_t sectorTagType = rgbMake(0, 224, 224);  // sector with tag + type
	rgb_color_t sectorType = rgbMake(0, 128, 255);   // sector with type

	rgb_color_t thing = rgbMake(80, 80, 80);       // things outside Things mode
	rgb_color_t vertex = rgbMake(0, 255, 0);       // vertex marks

	rgb_color_t select = rgbMake(128, 192, 255);   // selected objects
	rgb_color_t highlight = rgbMake(255, 255, 0);  // hovered object
	rgb_color_t highlightSel = rgbMake(255, 128, 0);  // hovered + selected

	//  Editing feedback beyond the base geometry.  The defaults reproduce
	//  the classic hardcoded Eureka colors (CAMERA_COLOR, FL_RED, LIGHTRED,
	//  FL_MAGENTA and the sound-propagation levels).
	rgb_color_t camera = rgbMake(255, 192, 255);   // 3D camera marker
	rgb_color_t error = rgbMake(255, 0, 0);        // invalid geometry/bounds
	rgb_color_t taggedLight = rgbMake(255, 128, 128);  // tagged-to-highlight
	rgb_color_t soundBlock = rgbMake(255, 0, 255);   // sound-blocking lines

	//  NOTE: propMaybe is brightened from the classic (64,64,192), which
	//  only reached 2.7:1 on a dark canvas; this keeps the hue at 3.9:1.
	rgb_color_t propMaybe = rgbMake(88, 88, 224);    // sound propagation:
	rgb_color_t propLevel1 = rgbMake(192, 32, 32);   // maybe / one level /
	rgb_color_t propLevel2 = rgbMake(192, 96, 32);   // two levels heard
};

struct VisualPalette
{
	rgb_color_t canvas = rgbMake(0, 0, 0);
	rgb_color_t gridHalo = rgbMake(5, 8, 12);

	rgb_color_t normalAxis = {};
	rgb_color_t normalMain = {};
	rgb_color_t normalFlat = {};
	rgb_color_t normalSmall = {};

	rgb_color_t dottyAxis = {};
	rgb_color_t dottyMajor = {};
	rgb_color_t dottyMinor = {};
	rgb_color_t dottyPoint = {};

	rgb_color_t snapTarget = {};
	rgb_color_t snapHalo = {};
	rgb_color_t snapGuide = {};

	MapInk ink{};
};

struct VisualThemeDescriptor
{
	const char *id;
	const char *label;
	const char *description;
	VisualPalette palette;
	bool builtIn;
};

const std::vector<VisualThemeDescriptor> &VisualThemes();
int NormalizeVisualTheme(int theme) noexcept;
int ConfiguredVisualTheme() noexcept;
const VisualThemeDescriptor &VisualThemeInfo(int theme);
VisualPalette VisualPaletteFor(int theme);
VisualPalette ActiveVisualPalette();

double RelativeLuminance(rgb_color_t color) noexcept;
double ContrastRatio(rgb_color_t first, rgb_color_t second) noexcept;

//  Linearly fade a color toward another (amount 0 = keep, 1 = fully other).
rgb_color_t FadeColor(
		rgb_color_t from, rgb_color_t to, double amount) noexcept;

//  A full-screen grid pattern aggregates visually: many closely spaced
//  lines read far brighter than a single line of the same color and drown
//  the map in a colored veil.  Fade grid lines toward the canvas as their
//  on-screen pixel spacing shrinks, so the perceived ink per area stays
//  roughly constant instead of the grid ever overpowering the geometry.
rgb_color_t DensityFade(
		rgb_color_t color, rgb_color_t canvas, double linePixels) noexcept;

//  Nudge a color away from `canvas` until the pair reaches `minRatio`
//  (WCAG), fading toward white or black - whichever is farther from the
//  canvas - so the hue family is preserved.  Used to adapt identity hue
//  sets (e.g. the design-assist preview palette) to any canvas.  Returns
//  the best reachable color when even the endpoint falls short.
rgb_color_t EnsureContrast(
		rgb_color_t color, rgb_color_t canvas, double minRatio) noexcept;

//  User grid opacity (config::grid_opacity percent, clamped to 20-100),
//  as a 0.2-1.0 strength multiplier.
double GridOpacity() noexcept;

//  A copy of the palette with every grid line color (square, dotty, and
//  axes) faded toward the canvas by the configured grid opacity.  Snap
//  reticle and map ink colors are never touched.
VisualPalette OpacityAdjustedPalette(
		const VisualPalette &palette) noexcept;

} // namespace grid

#endif  /* __EUREKA_M_GRID_THEME_H__ */
