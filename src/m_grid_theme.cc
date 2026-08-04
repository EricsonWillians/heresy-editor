//------------------------------------------------------------------------
//  GRID VISIBILITY THEMES
//------------------------------------------------------------------------

#include "m_grid_theme.h"

#include "m_config.h"

#include <algorithm>
#include <cmath>

// -1 is a one-time compatibility state for configurations written before
// named visibility themes existed. ConfiguredVisualTheme() distinguishes an
// old stock palette from a genuinely customized one.
int config::grid_visual_theme = -1;
int config::grid_opacity = 75;
rgb_color_t config::dotty_axis_col = rgbMake(170, 170, 170);
rgb_color_t config::dotty_major_col = rgbMake(92, 92, 92);
rgb_color_t config::dotty_minor_col = rgbMake(60, 60, 60);
rgb_color_t config::dotty_point_col = rgbMake(170, 170, 170);
rgb_color_t config::normal_axis_col = rgbMake(170, 170, 170);
rgb_color_t config::normal_main_col = rgbMake(92, 92, 92);
rgb_color_t config::normal_flat_col = rgbMake(68, 68, 68);
rgb_color_t config::normal_small_col = rgbMake(56, 56, 56);
rgb_color_t config::grid_snap_target_col = rgbMake(255, 110, 255);
rgb_color_t config::grid_snap_halo_col = rgbMake(0, 0, 0);
rgb_color_t config::grid_snap_guide_col = rgbMake(220, 140, 255);

namespace
{

//  Design language shared by all built-in themes:
//    grid     - ALWAYS far weaker than geometry.  A full-screen grid
//               pattern aggregates visually into a veil, so grid lines
//               hug the canvas (far from it in contrast terms) while
//               plain linedefs sit at 12:1 or more; only the two axis
//               lines may shine.  Recession means DIM on a dark canvas
//               and PALE on a light canvas - the polarity is per theme
//    geometry - walls and linedefs at maximum contrast against the
//               canvas, semantic hues that never overlap the grid
//    snapping - a single saturated target family with an opposing halo
//               outline, unique among all on-screen colors
//    feedback - selection, hover highlight, and vertices each own a
//               distinct high-contrast hue
//
grid::VisualPalette HighContrastDarkPalette()
{
	grid::VisualPalette palette = {
		rgbMake(0, 0, 0),
		rgbMake(0, 0, 0),
		rgbMake(170, 170, 170),
		rgbMake(92, 92, 92),
		rgbMake(68, 68, 68),
		rgbMake(56, 56, 56),
		rgbMake(170, 170, 170),
		rgbMake(92, 92, 92),
		rgbMake(60, 60, 60),
		rgbMake(170, 170, 170),
		rgbMake(255, 110, 255),
		rgbMake(0, 0, 0),
		rgbMake(220, 140, 255)};

	palette.ink.linedef = rgbMake(220, 220, 220);
	palette.ink.special = rgbMake(0, 255, 128);
	palette.ink.tagged = rgbMake(255, 90, 170);
	palette.ink.blocking = rgbMake(0, 150, 150);
	palette.ink.sectorTag = rgbMake(0, 255, 0);
	palette.ink.sectorTagType = rgbMake(0, 255, 200);
	palette.ink.sectorType = rgbMake(200, 160, 255);
	palette.ink.thing = rgbMake(255, 176, 0);
	palette.ink.vertex = rgbMake(170, 255, 0);
	palette.ink.select = rgbMake(0, 255, 255);
	palette.ink.highlight = rgbMake(255, 255, 0);
	palette.ink.highlightSel = rgbMake(255, 128, 0);
	return palette;
}

grid::VisualPalette VintagePhosphorPalette()
{
	grid::VisualPalette palette = {
		rgbMake(0, 0, 0),
		rgbMake(0, 4, 0),
		rgbMake(60, 200, 60),
		rgbMake(26, 100, 26),
		rgbMake(20, 76, 20),
		rgbMake(17, 64, 17),
		rgbMake(60, 200, 60),
		rgbMake(30, 116, 30),
		rgbMake(18, 70, 18),
		rgbMake(120, 255, 120),
		rgbMake(255, 80, 60),
		rgbMake(10, 0, 0),
		rgbMake(255, 120, 80)};

	palette.ink.linedef = rgbMake(255, 190, 60);
	palette.ink.wall = rgbMake(140, 255, 140);
	palette.ink.special = rgbMake(255, 120, 20);
	palette.ink.tagged = rgbMake(255, 255, 180);
	palette.ink.blocking = rgbMake(0, 180, 120);
	palette.ink.sectorTag = rgbMake(140, 255, 0);
	palette.ink.sectorTagType = rgbMake(190, 255, 90);
	palette.ink.sectorType = rgbMake(0, 255, 200);
	palette.ink.thing = rgbMake(200, 150, 60);
	palette.ink.vertex = rgbMake(200, 255, 0);
	palette.ink.select = rgbMake(255, 255, 255);
	palette.ink.highlight = rgbMake(255, 255, 0);
	palette.ink.highlightSel = rgbMake(255, 160, 0);
	return palette;
}

grid::VisualPalette BlueprintLightPalette()
{
	grid::VisualPalette palette = {
		rgbMake(232, 236, 240),
		rgbMake(250, 250, 252),
		rgbMake(60, 110, 180),
		rgbMake(140, 170, 205),
		rgbMake(165, 190, 215),
		rgbMake(185, 205, 225),
		rgbMake(60, 110, 180),
		rgbMake(140, 170, 205),
		rgbMake(170, 195, 220),
		rgbMake(90, 130, 175),
		rgbMake(190, 0, 90),
		rgbMake(255, 255, 255),
		rgbMake(150, 90, 0)};

	palette.ink.linedef = rgbMake(45, 50, 55);
	palette.ink.wall = rgbMake(0, 0, 0);
	palette.ink.special = rgbMake(0, 80, 180);
	palette.ink.tagged = rgbMake(150, 10, 30);
	palette.ink.blocking = rgbMake(0, 110, 110);
	palette.ink.sectorTag = rgbMake(0, 110, 50);
	palette.ink.sectorTagType = rgbMake(0, 110, 120);
	palette.ink.sectorType = rgbMake(60, 60, 180);
	palette.ink.thing = rgbMake(90, 70, 40);
	palette.ink.vertex = rgbMake(0, 90, 0);
	palette.ink.select = rgbMake(0, 0, 255);
	palette.ink.highlight = rgbMake(180, 80, 0);
	palette.ink.highlightSel = rgbMake(140, 40, 160);
	//  Light-canvas variants of the classic feedback colors: the classic
	//  pastels (LIGHTRED, CAMERA_COLOR, FL_MAGENTA) wash out below 3:1 on
	//  the pale blueprint paper, so they are deepened here.
	palette.ink.camera = rgbMake(150, 40, 160);
	palette.ink.taggedLight = rgbMake(210, 80, 50);
	palette.ink.soundBlock = rgbMake(170, 0, 170);
	return palette;
}

grid::VisualPalette CustomPalette()
{
	return {
		rgbMake(0, 0, 0),
		rgbMake(5, 8, 12),
		config::normal_axis_col,
		config::normal_main_col,
		config::normal_flat_col,
		config::normal_small_col,
		config::dotty_axis_col,
		config::dotty_major_col,
		config::dotty_minor_col,
		config::dotty_point_col,
		config::grid_snap_target_col,
		config::grid_snap_halo_col,
		config::grid_snap_guide_col};
}

bool LegacyColorsMatch(const grid::VisualPalette &palette) noexcept
{
	return config::normal_axis_col == palette.normalAxis &&
			config::normal_main_col == palette.normalMain &&
			config::normal_flat_col == palette.normalFlat &&
			config::normal_small_col == palette.normalSmall &&
			config::dotty_axis_col == palette.dottyAxis &&
			config::dotty_major_col == palette.dottyMajor &&
			config::dotty_minor_col == palette.dottyMinor &&
			config::dotty_point_col == palette.dottyPoint;
}

grid::VisualPalette PreThemeDefaultPalette()
{
	grid::VisualPalette result;
	result.normalAxis = rgbMake(0, 128, 255);
	result.normalMain = rgbMake(0, 0, 238);
	result.normalFlat = rgbMake(60, 60, 120);
	result.normalSmall = rgbMake(60, 60, 120);
	result.dottyAxis = rgbMake(0, 128, 255);
	result.dottyMajor = rgbMake(0, 0, 238);
	result.dottyMinor = rgbMake(0, 0, 187);
	result.dottyPoint = rgbMake(0, 0, 255);
	return result;
}

double LinearChannel(int component) noexcept
{
	const double value = component / 255.0;
	return value <= 0.04045 ?
			value / 12.92 :
			std::pow((value + 0.055) / 1.055, 2.4);
}

} // namespace

const std::vector<grid::VisualThemeDescriptor> &grid::VisualThemes()
{
	static const std::vector<VisualThemeDescriptor> themes = {
		{"high-contrast-dark", "High-Contrast Dark",
		 "Pitch-black canvas, a faint muted grey grid, stark white walls, "
		 "and neon cyan, magenta, amber, and lime indicators.",
		 HighContrastDarkPalette(), true},
		{"vintage-phosphor", "Vintage Phosphor",
		 "Retro CRT look: subtle dark green scanline grid, glowing "
		 "phosphor-green walls, amber linedefs, and a hot coral snap "
		 "target.",
		 VintagePhosphorPalette(), true},
		{"blueprint-light", "Blueprint Light",
		 "Pale drafting-paper canvas with a soft blueprint-blue grid, "
		 "stark black walls, and dark saturated indicators.",
		 BlueprintLightPalette(), true},
		{"custom", "Custom",
		 "Use the individual square, dot, and snap colors below, with the "
		 "classic map object colors.",
		 HighContrastDarkPalette(), false}};
	return themes;
}

int grid::NormalizeVisualTheme(int theme) noexcept
{
	if (theme < kFirstVisualTheme || theme > kCustomVisualTheme)
		return static_cast<int>(VisualTheme::highContrastDark);
	return theme;
}

int grid::ConfiguredVisualTheme() noexcept
{
	// value 4 was the Custom slot before the three-theme overhaul;
	// honor it so existing custom configurations survive the renumbering
	if (config::grid_visual_theme == 4)
		return kCustomVisualTheme;

	if (config::grid_visual_theme >= kFirstVisualTheme &&
			config::grid_visual_theme <= kCustomVisualTheme)
		return config::grid_visual_theme;

	if (LegacyColorsMatch(HighContrastDarkPalette()) ||
			LegacyColorsMatch(PreThemeDefaultPalette()))
		return static_cast<int>(VisualTheme::highContrastDark);

	return kCustomVisualTheme;
}

const grid::VisualThemeDescriptor &grid::VisualThemeInfo(int theme)
{
	return VisualThemes()[NormalizeVisualTheme(theme)];
}

grid::VisualPalette grid::VisualPaletteFor(int theme)
{
	const int normalized = NormalizeVisualTheme(theme);
	if (normalized == kCustomVisualTheme)
		return CustomPalette();
	return VisualThemes()[normalized].palette;
}

grid::VisualPalette grid::ActiveVisualPalette()
{
	return VisualPaletteFor(ConfiguredVisualTheme());
}

double grid::RelativeLuminance(rgb_color_t color) noexcept
{
	return 0.2126 * LinearChannel(RGB_RED(color)) +
			0.7152 * LinearChannel(RGB_GREEN(color)) +
			0.0722 * LinearChannel(RGB_BLUE(color));
}

double grid::ContrastRatio(
		rgb_color_t first, rgb_color_t second) noexcept
{
	const double firstLuminance = RelativeLuminance(first);
	const double secondLuminance = RelativeLuminance(second);
	const double lighter = std::max(firstLuminance, secondLuminance);
	const double darker = std::min(firstLuminance, secondLuminance);
	return (lighter + 0.05) / (darker + 0.05);
}

rgb_color_t grid::FadeColor(
		rgb_color_t from, rgb_color_t to, double amount) noexcept
{
	amount = std::min(1.0, std::max(0.0, amount));

	// NOTE: the RGB_* macros yield unsigned values; cast before
	// subtracting or negative deltas wrap around.
	const int fromR = static_cast<int>(RGB_RED(from));
	const int fromG = static_cast<int>(RGB_GREEN(from));
	const int fromB = static_cast<int>(RGB_BLUE(from));

	const int r = fromR + static_cast<int>(
			(static_cast<int>(RGB_RED(to)) - fromR) * amount);
	const int g = fromG + static_cast<int>(
			(static_cast<int>(RGB_GREEN(to)) - fromG) * amount);
	const int b = fromB + static_cast<int>(
			(static_cast<int>(RGB_BLUE(to)) - fromB) * amount);
	return rgbMake(r, g, b);
}

rgb_color_t grid::DensityFade(
		rgb_color_t color, rgb_color_t canvas, double linePixels) noexcept
{
	// full strength at 40px spacing and above, down to 40% when packed
	const double strength = std::min(1.0, std::max(0.4, linePixels / 40.0));
	return FadeColor(color, canvas, 1.0 - strength);
}

rgb_color_t grid::EnsureContrast(
		rgb_color_t color, rgb_color_t canvas, double minRatio) noexcept
{
	if (ContrastRatio(color, canvas) >= minRatio)
		return color;

	//  fade toward whichever extreme is farther from the canvas; that
	//  direction monotonically increases the contrast ratio
	const rgb_color_t target = RelativeLuminance(canvas) > 0.5 ?
			rgbMake(0, 0, 0) : rgbMake(255, 255, 255);

	//  binary search the minimal fade that satisfies the ratio
	double low = 0.0, high = 1.0;
	for (int iteration = 0; iteration < 24; ++iteration)
	{
		const double mid = (low + high) / 2.0;
		if (ContrastRatio(FadeColor(color, target, mid), canvas) >= minRatio)
			high = mid;
		else
			low = mid;
	}
	return FadeColor(color, target, high);
}

double grid::GridOpacity() noexcept
{
	return std::min(100, std::max(20, config::grid_opacity)) / 100.0;
}

grid::VisualPalette grid::OpacityAdjustedPalette(
		const VisualPalette &palette) noexcept
{
	VisualPalette adjusted = palette;
	const double fade = 1.0 - GridOpacity();
	if (fade <= 0.0)
		return adjusted;

	adjusted.normalAxis = FadeColor(palette.normalAxis, palette.canvas, fade);
	adjusted.normalMain = FadeColor(palette.normalMain, palette.canvas, fade);
	adjusted.normalFlat = FadeColor(palette.normalFlat, palette.canvas, fade);
	adjusted.normalSmall = FadeColor(palette.normalSmall, palette.canvas, fade);
	adjusted.dottyAxis = FadeColor(palette.dottyAxis, palette.canvas, fade);
	adjusted.dottyMajor = FadeColor(palette.dottyMajor, palette.canvas, fade);
	adjusted.dottyMinor = FadeColor(palette.dottyMinor, palette.canvas, fade);
	adjusted.dottyPoint = FadeColor(palette.dottyPoint, palette.canvas, fade);
	return adjusted;
}
