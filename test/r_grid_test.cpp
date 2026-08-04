//------------------------------------------------------------------------
//
//  Eureka DOOM Editor
//
//  Copyright (C) 2024 Ioan Chera
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 2
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//------------------------------------------------------------------------

#include "r_grid.h"
#include "m_config.h"
#include "m_grid_theme.h"
#include "gtest/gtest.h"

#include <cmath>
#include <set>
#include <sstream>

TEST(GridVisualThemes, CatalogIsOrderedStableAndUnique)
{
	const auto &themes = grid::VisualThemes();
	ASSERT_EQ(themes.size(), 4u);

	const char *expectedIds[] = {
		"high-contrast-dark", "vintage-phosphor",
		"blueprint-light", "custom"};
	std::set<std::string> ids;
	for (size_t index = 0; index < themes.size(); ++index)
	{
		EXPECT_STREQ(themes[index].id, expectedIds[index]);
		EXPECT_TRUE(themes[index].label && themes[index].label[0]);
		EXPECT_TRUE(themes[index].description &&
				themes[index].description[0]);
		EXPECT_TRUE(ids.insert(themes[index].id).second);
		EXPECT_EQ(themes[index].builtIn,
				index < static_cast<size_t>(grid::kCustomVisualTheme));
	}
}

//  Contrast requirements are polarity-aware: on a dark canvas the grid
//  recedes by being DIM, on a light canvas by being PALE.  Either way,
//  the hierarchy axis > major > minor holds in canvas-contrast terms.
TEST(GridVisualThemes, BuiltInPalettesMaintainStrongSemanticContrast)
{
	for (int theme = grid::kFirstVisualTheme;
			theme < grid::kCustomVisualTheme; ++theme)
	{
		const grid::VisualPalette palette =
				grid::VisualPaletteFor(theme);
		SCOPED_TRACE(grid::VisualThemeInfo(theme).id);
		const bool darkCanvas =
				grid::RelativeLuminance(palette.canvas) < 0.5;

		// minimum canvas contrast per role, {dark canvas, light canvas}
		const double axisFloor = darkCanvas ? 7.0 : 3.5;
		const double mainFloor = darkCanvas ? 2.8 : 1.6;
		const double flatFloor = darkCanvas ? 2.0 : 1.4;
		const double smallFloor = darkCanvas ? 1.7 : 1.25;
		const double pointFloor = darkCanvas ? 5.0 : 2.5;
		const double targetFloor = darkCanvas ? 6.0 : 3.8;
		const double guideFloor = darkCanvas ? 7.0 : 4.2;

		EXPECT_GE(grid::ContrastRatio(
				palette.normalAxis, palette.canvas), axisFloor);
		EXPECT_GE(grid::ContrastRatio(
				palette.normalMain, palette.canvas), mainFloor);
		EXPECT_GE(grid::ContrastRatio(
				palette.normalFlat, palette.canvas), flatFloor);
		EXPECT_GE(grid::ContrastRatio(
				palette.normalSmall, palette.canvas), smallFloor);

		EXPECT_GE(grid::ContrastRatio(
				palette.dottyAxis, palette.canvas), axisFloor);
		EXPECT_GE(grid::ContrastRatio(
				palette.dottyMajor, palette.canvas), mainFloor);
		EXPECT_GE(grid::ContrastRatio(
				palette.dottyMinor, palette.canvas),
				darkCanvas ? 1.8 : 1.3);
		EXPECT_GE(grid::ContrastRatio(
				palette.dottyPoint, palette.canvas), pointFloor);

		EXPECT_GE(grid::ContrastRatio(
				palette.snapTarget, palette.canvas), targetFloor);
		EXPECT_GE(grid::ContrastRatio(
				palette.snapGuide, palette.canvas), guideFloor);
		EXPECT_GE(grid::ContrastRatio(
				palette.snapTarget, palette.snapHalo), 6.0);

		// hierarchy holds in canvas-contrast terms on both polarities
		EXPECT_GT(grid::ContrastRatio(palette.normalAxis, palette.canvas),
				grid::ContrastRatio(palette.normalMain, palette.canvas));
		EXPECT_GT(grid::ContrastRatio(palette.normalMain, palette.canvas),
				grid::ContrastRatio(palette.normalFlat, palette.canvas));
		EXPECT_GT(grid::ContrastRatio(palette.normalFlat, palette.canvas),
				grid::ContrastRatio(palette.normalSmall, palette.canvas));
		EXPECT_GT(grid::ContrastRatio(palette.dottyMajor, palette.canvas),
				grid::ContrastRatio(palette.dottyMinor, palette.canvas));
		EXPECT_GT(grid::ContrastRatio(palette.dottyPoint, palette.canvas),
				grid::ContrastRatio(palette.dottyMinor, palette.canvas));
	}
}

TEST(GridVisualThemes, BuiltInInkSeparatesGeometryFromGridAndSnap)
{
	for (int theme = grid::kFirstVisualTheme;
			theme < grid::kCustomVisualTheme; ++theme)
	{
		const grid::VisualPalette palette =
				grid::VisualPaletteFor(theme);
		SCOPED_TRACE(grid::VisualThemeInfo(theme).id);
		const grid::MapInk &ink = palette.ink;
		const bool darkCanvas =
				grid::RelativeLuminance(palette.canvas) < 0.5;

		// map geometry must stand clearly against the canvas...
		EXPECT_GE(grid::ContrastRatio(ink.linedef, palette.canvas),
				darkCanvas ? 11.0 : 9.0);
		EXPECT_GE(grid::ContrastRatio(ink.wall, palette.canvas),
				darkCanvas ? 15.0 : 14.0);
		EXPECT_GE(grid::ContrastRatio(ink.special, palette.canvas),
				darkCanvas ? 7.0 : 4.5);
		EXPECT_GE(grid::ContrastRatio(ink.tagged, palette.canvas),
				darkCanvas ? 7.0 : 4.5);
		EXPECT_GE(grid::ContrastRatio(ink.vertex, palette.canvas),
				darkCanvas ? 9.0 : 6.5);
		EXPECT_GE(grid::ContrastRatio(ink.select, palette.canvas),
				darkCanvas ? 9.0 : 6.5);
		EXPECT_GE(grid::ContrastRatio(ink.thing, palette.canvas), 6.5);

		// ...and far above the grid, which always stays the weakest
		// layer.  A full-screen grid pattern aggregates visually, so
		// geometry must outshine it by a wide margin in canvas-contrast
		// terms, on either polarity.
		EXPECT_GE(grid::ContrastRatio(ink.linedef, palette.canvas),
				2.5 * grid::ContrastRatio(
						palette.normalMain, palette.canvas));
		EXPECT_GT(grid::ContrastRatio(ink.thing, palette.canvas),
				grid::ContrastRatio(palette.normalSmall, palette.canvas));
		EXPECT_LT(grid::ContrastRatio(ink.thing, palette.canvas),
				grid::ContrastRatio(ink.linedef, palette.canvas));

		// selection and snapping must not blend into grid or each other
		EXPECT_GE(grid::ContrastRatio(ink.select, palette.normalMain),
				1.5);
		EXPECT_GE(grid::ContrastRatio(palette.snapTarget, ink.highlight),
				1.2);
		EXPECT_GE(grid::ContrastRatio(palette.snapTarget, ink.select),
				1.3);
	}
}

//  Every color drawn over the canvas - including the editing-feedback
//  colors that were hardcoded before (camera, error, tagged feedback,
//  sound propagation) - must stay readable on every built-in theme.
TEST(GridVisualThemes, BuiltInFeedbackColorsPassCanvasContrast)
{
	for (int theme = grid::kFirstVisualTheme;
			theme < grid::kCustomVisualTheme; ++theme)
	{
		const grid::VisualPalette palette =
				grid::VisualPaletteFor(theme);
		SCOPED_TRACE(grid::VisualThemeInfo(theme).id);
		const grid::MapInk &ink = palette.ink;

		const rgb_color_t feedback[] = {
				ink.blocking, ink.sectorTag, ink.sectorTagType,
				ink.sectorType, ink.highlight, ink.highlightSel,
				ink.camera, ink.error, ink.taggedLight, ink.soundBlock,
				ink.propMaybe, ink.propLevel1, ink.propLevel2};
		for (const rgb_color_t color : feedback)
			EXPECT_GE(grid::ContrastRatio(color, palette.canvas), 3.0);
	}
}

//  The new feedback fields must default to the classic hardcoded Eureka
//  colors so the Custom theme and legacy configs keep the familiar look.
TEST(GridVisualThemes, FeedbackDefaultsReproduceClassicColors)
{
	const grid::MapInk ink;
	EXPECT_EQ(ink.camera, rgbMake(255, 192, 255));
	EXPECT_EQ(ink.error, rgbMake(255, 0, 0));
	EXPECT_EQ(ink.taggedLight, rgbMake(255, 128, 128));
	EXPECT_EQ(ink.soundBlock, rgbMake(255, 0, 255));
	//  brightened from the classic (64,64,192), which failed 3:1 on black
	EXPECT_EQ(ink.propMaybe, rgbMake(88, 88, 224));
	EXPECT_EQ(ink.propLevel1, rgbMake(192, 32, 32));
	EXPECT_EQ(ink.propLevel2, rgbMake(192, 96, 32));
}

TEST(GridVisualThemes, EnsureContrastKeepsPassingColorsUnchanged)
{
	const rgb_color_t color = rgbMake(64, 176, 255);
	EXPECT_EQ(grid::EnsureContrast(color, rgbMake(0, 0, 0), 3.0), color);
}

TEST(GridVisualThemes, EnsureContrastConvergesOnBothPolarities)
{
	//  pale blue washes out on the light blueprint canvas...
	const rgb_color_t pale = rgbMake(200, 220, 240);
	const rgb_color_t lightCanvas = rgbMake(232, 236, 240);
	ASSERT_LT(grid::ContrastRatio(pale, lightCanvas), 3.0);
	const rgb_color_t darkFixed =
			grid::EnsureContrast(pale, lightCanvas, 3.0);
	EXPECT_GE(grid::ContrastRatio(darkFixed, lightCanvas), 3.0);

	//  ...and a dim grey disappears on a dark canvas
	const rgb_color_t dim = rgbMake(40, 40, 50);
	const rgb_color_t darkCanvas = rgbMake(0, 0, 0);
	ASSERT_LT(grid::ContrastRatio(dim, darkCanvas), 3.0);
	const rgb_color_t lightFixed =
			grid::EnsureContrast(dim, darkCanvas, 3.0);
	EXPECT_GE(grid::ContrastRatio(lightFixed, darkCanvas), 3.0);
}

TEST(GridVisualThemes, EnsureContrastPreservesHueFamily)
{
	//  blue stays blue-dominant after being deepened for a light canvas
	const rgb_color_t blue = rgbMake(120, 160, 255);
	const rgb_color_t fixed =
			grid::EnsureContrast(blue, rgbMake(232, 236, 240), 3.0);
	EXPECT_GT(static_cast<int>(RGB_BLUE(fixed)),
			static_cast<int>(RGB_RED(fixed)));
	EXPECT_GT(static_cast<int>(RGB_BLUE(fixed)),
			static_cast<int>(RGB_GREEN(fixed)));
}

TEST(GridVisualThemes, DensityFadeDissolvesPackedLinesIntoCanvas)
{
	const rgb_color_t canvas = rgbMake(0, 0, 0);
	const rgb_color_t line = rgbMake(66, 118, 180);

	// identity fades
	EXPECT_EQ(grid::FadeColor(line, canvas, 0.0), line);
	EXPECT_EQ(grid::FadeColor(line, canvas, 1.0), canvas);

	// sparse lines keep full strength
	EXPECT_EQ(grid::DensityFade(line, canvas, 64.0), line);
	EXPECT_EQ(grid::DensityFade(line, canvas, 40.0), line);

	// packed lines dissolve toward the canvas, never below 40% strength
	const rgb_color_t faded = grid::DensityFade(line, canvas, 16.0);
	EXPECT_LT(grid::RelativeLuminance(faded),
			grid::RelativeLuminance(line));
	EXPECT_EQ(faded, grid::FadeColor(line, canvas, 0.6));
	EXPECT_EQ(grid::DensityFade(line, canvas, 1.0),
			grid::FadeColor(line, canvas, 0.6));
	// mid-density lines sit between full strength and the floor
	EXPECT_EQ(grid::DensityFade(line, canvas, 20.0),
			grid::FadeColor(line, canvas, 0.5));
}

TEST(GridVisualThemes, OpacityFadesOnlyGridColorsTowardCanvas)
{
	struct Restore
	{
		int opacity = config::grid_opacity;
		~Restore() { config::grid_opacity = opacity; }
	} restore;

	for (int theme = grid::kFirstVisualTheme;
			theme < grid::kCustomVisualTheme; ++theme)
	{
		const grid::VisualPalette base = grid::VisualPaletteFor(theme);
		SCOPED_TRACE(grid::VisualThemeInfo(theme).id);

		config::grid_opacity = 100;
		EXPECT_EQ(grid::OpacityAdjustedPalette(base).normalMain,
				base.normalMain);

		config::grid_opacity = 50;
		const grid::VisualPalette half =
				grid::OpacityAdjustedPalette(base);
		EXPECT_EQ(half.normalMain,
				grid::FadeColor(base.normalMain, base.canvas, 0.5));
		EXPECT_EQ(half.normalAxis,
				grid::FadeColor(base.normalAxis, base.canvas, 0.5));
		EXPECT_EQ(half.dottyPoint,
				grid::FadeColor(base.dottyPoint, base.canvas, 0.5));

		// snap reticle and map ink are never touched
		EXPECT_EQ(half.snapTarget, base.snapTarget);
		EXPECT_EQ(half.snapGuide, base.snapGuide);
		EXPECT_EQ(half.snapHalo, base.snapHalo);
		EXPECT_EQ(half.ink.linedef, base.ink.linedef);
		EXPECT_EQ(half.ink.select, base.ink.select);

		// grid contrast against the canvas strictly decreases
		EXPECT_LT(grid::ContrastRatio(half.normalMain, base.canvas),
				grid::ContrastRatio(base.normalMain, base.canvas));
	}

	config::grid_opacity = 0;
	EXPECT_DOUBLE_EQ(grid::GridOpacity(), 0.2);
	config::grid_opacity = 500;
	EXPECT_DOUBLE_EQ(grid::GridOpacity(), 1.0);
	config::grid_opacity = 75;
	EXPECT_DOUBLE_EQ(grid::GridOpacity(), 0.75);
}

TEST(GridVisualThemes, InvalidThemeFallsBackAndCustomUsesUserColors)
{
	struct Restore
	{
		int theme = config::grid_visual_theme;
		rgb_color_t axis = config::normal_axis_col;
		rgb_color_t target = config::grid_snap_target_col;
		rgb_color_t halo = config::grid_snap_halo_col;
		rgb_color_t guide = config::grid_snap_guide_col;
		~Restore()
		{
			config::grid_visual_theme = theme;
			config::normal_axis_col = axis;
			config::grid_snap_target_col = target;
			config::grid_snap_halo_col = halo;
			config::grid_snap_guide_col = guide;
		}
	} restore;

	EXPECT_EQ(grid::NormalizeVisualTheme(-1),
			static_cast<int>(grid::VisualTheme::highContrastDark));
	EXPECT_EQ(grid::NormalizeVisualTheme(999),
			static_cast<int>(grid::VisualTheme::highContrastDark));

	config::grid_visual_theme = grid::kCustomVisualTheme;
	config::normal_axis_col = rgbMake(12, 34, 56);
	config::grid_snap_target_col = rgbMake(220, 30, 90);
	config::grid_snap_halo_col = rgbMake(1, 2, 3);
	config::grid_snap_guide_col = rgbMake(45, 210, 180);
	const grid::VisualPalette palette = grid::ActiveVisualPalette();
	EXPECT_EQ(palette.normalAxis, rgbMake(12, 34, 56));
	EXPECT_EQ(palette.snapTarget, rgbMake(220, 30, 90));
	EXPECT_EQ(palette.snapHalo, rgbMake(1, 2, 3));
	EXPECT_EQ(palette.snapGuide, rgbMake(45, 210, 180));
}

TEST(GridVisualThemes, LegacyStockColorsMigrateWithoutLosingCustomization)
{
	struct Restore
	{
		int theme = config::grid_visual_theme;
		grid::VisualPalette custom =
				grid::VisualPaletteFor(grid::kCustomVisualTheme);
		~Restore()
		{
			config::grid_visual_theme = theme;
			config::normal_axis_col = custom.normalAxis;
			config::normal_main_col = custom.normalMain;
			config::normal_flat_col = custom.normalFlat;
			config::normal_small_col = custom.normalSmall;
			config::dotty_axis_col = custom.dottyAxis;
			config::dotty_major_col = custom.dottyMajor;
			config::dotty_minor_col = custom.dottyMinor;
			config::dotty_point_col = custom.dottyPoint;
		}
	} restore;

	config::grid_visual_theme = -1;
	config::normal_axis_col = rgbMake(0, 128, 255);
	config::normal_main_col = rgbMake(0, 0, 238);
	config::normal_flat_col = rgbMake(60, 60, 120);
	config::normal_small_col = rgbMake(60, 60, 120);
	config::dotty_axis_col = rgbMake(0, 128, 255);
	config::dotty_major_col = rgbMake(0, 0, 238);
	config::dotty_minor_col = rgbMake(0, 0, 187);
	config::dotty_point_col = rgbMake(0, 0, 255);
	EXPECT_EQ(grid::ConfiguredVisualTheme(),
			static_cast<int>(grid::VisualTheme::highContrastDark));
	EXPECT_EQ(grid::ActiveVisualPalette().normalMain,
			grid::VisualPaletteFor(
					static_cast<int>(grid::VisualTheme::highContrastDark))
					.normalMain);

	// configurations written before the three-theme overhaul used
	// value 4 for Custom; it must survive the renumbering
	config::grid_visual_theme = 4;
	EXPECT_EQ(grid::ConfiguredVisualTheme(), grid::kCustomVisualTheme);

	config::grid_visual_theme = -1;
	config::normal_main_col = rgbMake(123, 45, 210);
	EXPECT_EQ(grid::ConfiguredVisualTheme(), grid::kCustomVisualTheme);
	EXPECT_EQ(grid::ActiveVisualPalette().normalMain,
			rgbMake(123, 45, 210));
}

class GridStateFixture : public ::testing::Test, public grid::Listener
{
public:
	virtual void gridRedrawMap() override
	{
		++redrawMapCounts;
	}
	virtual void gridSetGrid(int grid) override
	{
		gridSettings.push_back(grid);
	}
	virtual void gridUpdateSnap() override
	{
		++snapUpdates;
	}
	virtual void gridAdjustPos() override
	{
		++positionUpdates;
	}
	virtual void gridPointerPos() override
	{
		++pointerPositionUpdates;
	}
	virtual void gridSetScale(double scale) override
	{
		scaleSettings.push_back(scale);
	}
	virtual void gridBeep(const char* message) override
	{
		beeps.push_back(message);
	}
	virtual void gridUpdateRatio() override
	{
		++ratioUpdates;
	}
protected:
	void TearDown() override
	{
		config::grid_default_size = initialGridDefaultSize;
		config::grid_default_mode = initialGridDefaultMode;
		config::grid_default_snap = initialGridDefaultSnap;
		config::grid_hide_in_free_mode = initialGridHideInFreeMode;
	}

	int redrawMapCounts = 0;
	std::vector<int> gridSettings;
	int snapUpdates = 0;
	int positionUpdates = 0;
	int pointerPositionUpdates = 0;
	std::vector<double> scaleSettings;
	std::vector<std::string> beeps;
	int ratioUpdates = 0;

private:
	int initialGridDefaultSize = config::grid_default_size;
	bool initialGridDefaultMode = config::grid_default_mode;
	bool initialGridDefaultSnap = config::grid_default_snap;
	bool initialGridHideInFreeMode = config::grid_hide_in_free_mode;
};

TEST_F(GridStateFixture, InitNormalGrid)
{
	grid::State grid(*this);

	config::grid_default_size = 16;
	config::grid_default_mode = true;
	
	grid.Init();
	ASSERT_TRUE(grid.isShown());
	ASSERT_EQ(grid.getStep(), 16);
	ASSERT_GE(gridSettings.size(), 1);
	ASSERT_EQ(gridSettings.back(), 16);
	
	ASSERT_GE(redrawMapCounts, 1);
}

TEST_F(GridStateFixture, InitArchitecturalNonPowerOfTwoGrid)
{
	grid::State grid(*this);

	config::grid_default_size = 12;
	config::grid_default_mode = true;

	grid.Init();
	ASSERT_TRUE(grid.isShown());
	ASSERT_EQ(grid.getStep(), 12);
	ASSERT_GE(gridSettings.size(), 1);
	ASSERT_EQ(gridSettings.back(), 12);
	
	ASSERT_GE(redrawMapCounts, 1);
}

TEST_F(GridStateFixture, InitUnderflowGridCapsAt1)
{
	{
		grid::State grid(*this);
		
		config::grid_default_size = 1;
		config::grid_default_mode = true;
		
		grid.Init();
		ASSERT_TRUE(grid.isShown());
		ASSERT_EQ(grid.getStep(), 1);
		ASSERT_GE(gridSettings.size(), 1);
		ASSERT_EQ(gridSettings.back(), 1);
		
		ASSERT_GE(redrawMapCounts, 1);
	}
	{
		grid::State grid(*this);
		
		config::grid_default_size = -8;
		config::grid_default_mode = true;
		
		grid.Init();
		ASSERT_TRUE(grid.isShown());
		ASSERT_EQ(grid.getStep(), 1);
		ASSERT_GE(gridSettings.size(), 1);
		ASSERT_EQ(gridSettings.back(), 1);
		
		ASSERT_GE(redrawMapCounts, 2);
	}
}

TEST_F(GridStateFixture, InitSupportsLargeMapSteps)
{
	{
		grid::State grid(*this);
		
		config::grid_default_size = 1025;
		config::grid_default_mode = true;
		
		grid.Init();
		ASSERT_TRUE(grid.isShown());
		ASSERT_EQ(grid.getStep(), 1024);
		ASSERT_GE(gridSettings.size(), 1);
		ASSERT_EQ(gridSettings.back(), 1024);
		
		ASSERT_GE(redrawMapCounts, 1);
	}
	{
		grid::State grid(*this);
		
		config::grid_default_size = 2048;
		config::grid_default_mode = true;
		
		grid.Init();
		ASSERT_TRUE(grid.isShown());
		ASSERT_EQ(grid.getStep(), 2048);
		ASSERT_GE(gridSettings.size(), 1);
		ASSERT_EQ(gridSettings.back(), 2048);
		
		ASSERT_GE(redrawMapCounts, 2);
	}
}

TEST_F(GridStateFixture, SnapCalled)
{
	grid::State grid(*this);

	grid.Init();
	ASSERT_EQ(snapUpdates, 1);
	
	ASSERT_GE(redrawMapCounts, 1);
}

TEST_F(GridStateFixture, GridDisabledWhenNotConfigured)
{
	grid::State grid(*this);

	config::grid_default_mode = false;
	grid.Init();
	ASSERT_FALSE(grid.isShown());
	ASSERT_GE(gridSettings.size(), 1);
	ASSERT_EQ(gridSettings.back(), -1);
	
	ASSERT_GE(redrawMapCounts, 1);
}

TEST_F(GridStateFixture, InitWithoutSnappingButVisible)
{
	grid::State grid(*this);
	
	config::grid_default_mode = true;
	config::grid_default_snap = false;
	config::grid_default_size = 16;
	grid.Init();
	ASSERT_EQ(snapUpdates, 1);
	ASSERT_FALSE(grid.snaps());
	ASSERT_EQ(grid.getStep(), 16);
	ASSERT_TRUE(grid.isShown());
	ASSERT_GE(gridSettings.size(), 1);
	ASSERT_EQ(gridSettings.back(), 16);
	
	ASSERT_GE(redrawMapCounts, 1);
}

TEST_F(GridStateFixture, InitWithoutSnappingAndInvisible)
{
	grid::State grid(*this);
	
	config::grid_default_mode = false;
	config::grid_default_snap = false;
	config::grid_default_size = 16;
	grid.Init();
	ASSERT_EQ(snapUpdates, 1);
	ASSERT_FALSE(grid.snaps());
	ASSERT_EQ(grid.getStep(), 16);
	ASSERT_FALSE(grid.isShown());
	ASSERT_GE(gridSettings.size(), 1);
	ASSERT_EQ(gridSettings.back(), -1);
	
	ASSERT_GE(redrawMapCounts, 1);
}

TEST_F(GridStateFixture, ChangeShownStatus)
{
	grid::State grid(*this);
	
	config::grid_default_mode = true;
	config::grid_default_size = 16;
	config::grid_default_snap = true;
	grid.Init();
	ASSERT_TRUE(grid.isShown());
	
	grid.SetShown(false);
	ASSERT_FALSE(grid.isShown());
	ASSERT_FALSE(gridSettings.empty());
	ASSERT_EQ(gridSettings.back(), -1);
	
	grid.SetShown(true);
	ASSERT_TRUE(grid.isShown());
	ASSERT_FALSE(gridSettings.empty());
	ASSERT_EQ(gridSettings.back(), 16);
	
	// Now also with snapping
	config::grid_hide_in_free_mode = true;
	int befsnaps = snapUpdates;
	grid.SetShown(false);
	ASSERT_FALSE(grid.isShown());
	ASSERT_FALSE(gridSettings.empty());
	ASSERT_EQ(gridSettings.back(), -1);
	ASSERT_FALSE(grid.snaps());
	ASSERT_EQ(snapUpdates, befsnaps + 1);
	
	befsnaps = snapUpdates;
	grid.SetShown(true);
	ASSERT_TRUE(grid.isShown());
	ASSERT_FALSE(gridSettings.empty());
	ASSERT_EQ(gridSettings.back(), 16);
	ASSERT_TRUE(grid.snaps());
	ASSERT_EQ(snapUpdates, befsnaps + 1);
}

TEST_F(GridStateFixture, SetSnapSameValueChangesNothing)
{
	{
		grid::State grid(*this);
		config::grid_default_snap = false;
		grid.Init();
		int snapUpdatesBefore = snapUpdates;
		int mapRedrawsBefore = redrawMapCounts;
		grid.SetSnap(false);
		ASSERT_EQ(snapUpdates, snapUpdatesBefore);
		ASSERT_EQ(redrawMapCounts, mapRedrawsBefore);
	}
	{
		grid::State grid(*this);
		config::grid_default_snap = true;
		grid.Init();
		int snapUpdatesBefore = snapUpdates;
		int mapRedrawsBefore = redrawMapCounts;
		grid.SetSnap(true);
		ASSERT_EQ(snapUpdates, snapUpdatesBefore);
		ASSERT_EQ(redrawMapCounts, mapRedrawsBefore);
	}
}

TEST_F(GridStateFixture, ExplicitSnapChoiceBecomesNextSessionDefault)
{
	grid::State grid(*this);
	config::grid_default_snap = false;
	grid.Init();
	ASSERT_FALSE(grid.snaps());

	grid.SetSnap(true);
	EXPECT_TRUE(grid.snaps());
	EXPECT_TRUE(config::grid_default_snap);

	grid.SetSnap(false);
	EXPECT_FALSE(grid.snaps());
	EXPECT_FALSE(config::grid_default_snap);
}

TEST_F(GridStateFixture, LegacyPerMapSnapDoesNotOverrideGlobalChoice)
{
	grid::State grid(*this);
	config::grid_default_snap = true;
	grid.Init();
	ASSERT_TRUE(grid.snaps());

	EXPECT_TRUE(grid.parseUser({"snap", "0"}));
	EXPECT_TRUE(grid.snaps());
	EXPECT_TRUE(config::grid_default_snap);
}

TEST_F(GridStateFixture, ToggleSnappingWithoutHidingInFreeMode)
{
	grid::State grid(*this);
	
	config::grid_default_snap = false;
	config::grid_default_mode = false;
	grid.Init();
	ASSERT_FALSE(grid.snaps());
	ASSERT_FALSE(grid.isShown());
	int snapUpdatesBefore = snapUpdates;
	int mapRedrawsBefore = redrawMapCounts;
	grid.SetSnap(true);
	ASSERT_TRUE(grid.snaps());
	ASSERT_FALSE(grid.isShown());
	ASSERT_EQ(snapUpdates, snapUpdatesBefore + 1);
	ASSERT_EQ(redrawMapCounts, mapRedrawsBefore + 1);
	grid.SetSnap(false);
	ASSERT_FALSE(grid.snaps());
	ASSERT_FALSE(grid.isShown());
	ASSERT_EQ(snapUpdates, snapUpdatesBefore + 2);
	ASSERT_EQ(redrawMapCounts, mapRedrawsBefore + 2);
}

TEST_F(GridStateFixture, ToggleSnappingWithHidingInFreeMode)
{
	grid::State grid(*this);

	config::grid_default_snap = false;
	config::grid_default_mode = false;
	config::grid_hide_in_free_mode = true;
	grid.Init();
	ASSERT_FALSE(grid.snaps());
	ASSERT_FALSE(grid.isShown());
	int snapUpdatesBefore = snapUpdates;
	int mapRedrawsBefore = redrawMapCounts;
	grid.SetSnap(true);
	ASSERT_TRUE(grid.snaps());
	ASSERT_TRUE(grid.isShown());
	ASSERT_EQ(snapUpdates, snapUpdatesBefore + 1);
	ASSERT_GE(redrawMapCounts, mapRedrawsBefore + 1);
	ASSERT_FALSE(gridSettings.empty());
	ASSERT_GE(gridSettings.back(), 2);
	mapRedrawsBefore = redrawMapCounts;	// refresh it because the increment is unknown, it's just >= 1
	grid.SetSnap(false);
	ASSERT_FALSE(grid.snaps());
	ASSERT_FALSE(grid.isShown());
	ASSERT_EQ(snapUpdates, snapUpdatesBefore + 2);
	ASSERT_GE(redrawMapCounts, mapRedrawsBefore + 1);
	ASSERT_FALSE(gridSettings.empty());
	ASSERT_GE(gridSettings.back(), -1);
}

TEST_F(GridStateFixture, ToggleControlsWithoutLinking)
{
	grid::State grid(*this);

	config::grid_default_snap = false;
	config::grid_default_mode = false;
	config::grid_hide_in_free_mode = false;
	grid.Init();
	ASSERT_FALSE(grid.snaps());
	ASSERT_FALSE(grid.isShown());
	grid.ToggleShown();
	ASSERT_FALSE(grid.snaps());
	ASSERT_TRUE(grid.isShown());
	grid.ToggleShown();
	ASSERT_FALSE(grid.snaps());
	ASSERT_FALSE(grid.isShown());
	grid.ToggleSnap();
	ASSERT_TRUE(grid.snaps());
	ASSERT_FALSE(grid.isShown());
	grid.ToggleSnap();
	ASSERT_FALSE(grid.snaps());
	ASSERT_FALSE(grid.isShown());
}

TEST_F(GridStateFixture, ToggleControlsWithLinking)
{
	grid::State grid(*this);

	config::grid_default_snap = false;
	config::grid_default_mode = false;
	config::grid_hide_in_free_mode = true;
	grid.Init();
	ASSERT_FALSE(grid.snaps());
	ASSERT_FALSE(grid.isShown());
	grid.ToggleShown();
	ASSERT_TRUE(grid.isShown());
	ASSERT_TRUE(grid.snaps());
	grid.ToggleShown();
	ASSERT_FALSE(grid.isShown());
	ASSERT_FALSE(grid.snaps());
	grid.ToggleSnap();
	ASSERT_TRUE(grid.isShown());
	ASSERT_TRUE(grid.snaps());
	grid.ToggleSnap();
	ASSERT_FALSE(grid.isShown());
	ASSERT_FALSE(grid.snaps());
}

TEST_F(GridStateFixture, MoveToAndScroll)
{
	grid::State grid(*this);

	grid.Init();

	redrawMapCounts = 0;	// reset

	grid.MoveTo({ 100, 200 });
	ASSERT_EQ(grid.getOrig().x, 100);
	ASSERT_EQ(grid.getOrig().y, 200);
	ASSERT_EQ(positionUpdates, 1);
	ASSERT_EQ(pointerPositionUpdates, 1);
	ASSERT_EQ(redrawMapCounts, 1);

	// Same position here
	grid.MoveTo({ 100.001, 200.001 });
	ASSERT_EQ(grid.getOrig().x, 100);
	ASSERT_EQ(grid.getOrig().y, 200);
	ASSERT_EQ(positionUpdates, 1);
	ASSERT_EQ(pointerPositionUpdates, 1);
	ASSERT_EQ(redrawMapCounts, 1);

	// Move more
	grid.MoveTo({ 100, 202 });
	ASSERT_EQ(grid.getOrig().x, 100);
	ASSERT_EQ(grid.getOrig().y, 202);
	ASSERT_EQ(positionUpdates, 2);
	ASSERT_EQ(pointerPositionUpdates, 2);
	ASSERT_EQ(redrawMapCounts, 2);

	// Zero scroll
	grid.Scroll({ 0, 0 });
	ASSERT_EQ(grid.getOrig().x, 100);
	ASSERT_EQ(grid.getOrig().y, 202);
	ASSERT_EQ(positionUpdates, 2);
	ASSERT_EQ(pointerPositionUpdates, 2);
	ASSERT_EQ(redrawMapCounts, 2);

	// Existing scroll
	grid.Scroll({ +1, -1 });
	ASSERT_EQ(grid.getOrig().x, 101);
	ASSERT_EQ(grid.getOrig().y, 201);
	ASSERT_EQ(positionUpdates, 3);
	ASSERT_EQ(pointerPositionUpdates, 3);
	ASSERT_EQ(redrawMapCounts, 3);
}

TEST_F(GridStateFixture, RefocusZoom)
{
	grid::State grid(*this);

	grid.Init();
	ASSERT_EQ(grid.getScale(), 1.0);
	grid.MoveTo({ 100, 100 });

	int redrawMapCountsBefore = redrawMapCounts;
	int pointerPositionUpdatesBefore = pointerPositionUpdates;
	grid.RefocusZoom({ 200, 300 }, 0.75);
	ASSERT_EQ(grid.getOrig().x, 125);
	ASSERT_EQ(grid.getOrig().y, 150);

	ASSERT_EQ(redrawMapCounts, redrawMapCountsBefore + 1);
	ASSERT_EQ(pointerPositionUpdates, pointerPositionUpdatesBefore + 1);
}

TEST_F(GridStateFixture, NearestScaleValid)
{
	grid::State grid(*this);

	grid.Init();
	int posAdjustBefore = positionUpdates;
	int pointerPosBefore = pointerPositionUpdates;
	int redrawMapBefore = redrawMapCounts;
	size_t scaleSettingsBefore = scaleSettings.size();
	grid.NearestScale(12);

	ASSERT_EQ(grid.getScale(), 8);
	ASSERT_EQ(positionUpdates, posAdjustBefore + 1);
	ASSERT_EQ(pointerPositionUpdates, pointerPosBefore + 1);
	ASSERT_EQ(redrawMapCounts, redrawMapBefore + 1);
	ASSERT_EQ(scaleSettings.size(), scaleSettingsBefore + 1);
	ASSERT_EQ(scaleSettings.back(), 8);
}

TEST_F(GridStateFixture, NearestScaleTiny)	// check we don't overflow
{
	grid::State grid(*this);

	grid.Init();
	int posAdjustBefore = positionUpdates;
	int pointerPosBefore = pointerPositionUpdates;
	int redrawMapBefore = redrawMapCounts;
	size_t scaleSettingsBefore = scaleSettings.size();
	grid.NearestScale(0.0001);

	ASSERT_EQ(grid.getScale(), 1.0 / 512.0);
	ASSERT_EQ(positionUpdates, posAdjustBefore + 1);
	ASSERT_EQ(pointerPositionUpdates, pointerPosBefore + 1);
	ASSERT_EQ(redrawMapCounts, redrawMapBefore + 1);
	ASSERT_EQ(scaleSettings.size(), scaleSettingsBefore + 1);
	ASSERT_EQ(scaleSettings.back(), 1.0 / 512.0);
}

TEST_F(GridStateFixture, MathematicalCatalogIsBroadStableAndValid)
{
	const auto &steps = grid::StepPresets();
	EXPECT_GE(steps.size(), 75u);
	std::set<std::string> stepPaths;
	std::set<std::string> groups;
	for (const grid::StepPreset &preset : steps)
	{
		EXPECT_GE(preset.step, grid::kMinimumStep);
		EXPECT_LE(preset.step, grid::kMaximumStep);
		groups.insert(preset.group);
		EXPECT_TRUE(stepPaths.insert(std::string(preset.group) + "/" +
				preset.label).second);
	}
	EXPECT_GE(groups.size(), 6u);

	const auto &geometry = grid::GeometryPresets();
	EXPECT_GE(geometry.size(), 30u);
	std::set<std::string> ids;
	for (const grid::GeometryPreset &preset : geometry)
	{
		EXPECT_TRUE(ids.insert(preset.id).second);
		SString reason;
		EXPECT_TRUE(grid::MathSettingsValid(
				preset.settings, &reason)) << preset.id << ": " << reason;
	}
}

TEST_F(GridStateFixture, RotatedOriginAwareOrthogonalSnapping)
{
	grid::State state(*this);
	config::grid_default_size = 16;
	state.Init();

	grid::MathSettings settings;
	settings.origin = {10.0, -6.0};
	state.SetMathSettings(settings);
	v2double_t snapped = state.ForceSnap({25.2, 10.1});
	EXPECT_NEAR(snapped.x, 26.0, 1e-8);
	EXPECT_NEAR(snapped.y, 10.0, 1e-8);

	settings.origin = {};
	settings.rotation = 45.0;
	state.SetMathSettings(settings);
	snapped = state.ForceSnap({11.1, 11.4});
	EXPECT_NEAR(snapped.x, std::sqrt(128.0), 1e-8);
	EXPECT_NEAR(snapped.y, std::sqrt(128.0), 1e-8);
	EXPECT_TRUE(state.OnGrid(snapped.x, snapped.y));
}

TEST_F(GridStateFixture, ObliqueTriangularLatticeSnapsIntersections)
{
	grid::State state(*this);
	config::grid_default_size = 64;
	state.Init();
	const auto &preset = grid::GeometryPresets().at(9);
	ASSERT_STREQ(preset.id, "triangular_60");
	state.SetMathSettings(preset.settings);

	const v2double_t snapped = state.ForceSnap({95.0, 56.0});
	EXPECT_NEAR(snapped.x, 96.0, 1e-8);
	EXPECT_NEAR(snapped.y, 32.0 * std::sqrt(3.0), 1e-8);
	EXPECT_TRUE(state.OnGrid(snapped.x, snapped.y));
	EXPECT_FALSE(state.isDefaultOrthogonal());
}

TEST_F(GridStateFixture, PolarSnappingSupportsRadiusAngleAndOrigin)
{
	grid::State state(*this);
	config::grid_default_size = 32;
	state.Init();
	grid::MathSettings settings;
	settings.pattern = grid::Pattern::polar;
	settings.angularDivisions = 8;
	settings.origin = {100.0, -50.0};
	state.SetMathSettings(settings);

	const v2double_t snapped =
			state.ForceSnap({149.0, -1.0});
	EXPECT_NEAR(snapped.x, 100.0 + 32.0 * std::sqrt(2.0), 1e-8);
	EXPECT_NEAR(snapped.y, -50.0 + 32.0 * std::sqrt(2.0), 1e-8);
	EXPECT_TRUE(state.OnGrid(snapped.x, snapped.y));
	EXPECT_GE(state.SnapCandidates({149.0, -1.0}).size(), 4u);
}

TEST_F(GridStateFixture, DirectedRoundingModesAreDeterministic)
{
	grid::State state(*this);
	config::grid_default_size = 64;
	state.Init();
	grid::MathSettings settings;
	settings.rounding = grid::Rounding::lower;
	state.SetMathSettings(settings);
	v2double_t snapped = state.ForceSnap({63.0, -1.0});
	EXPECT_DOUBLE_EQ(snapped.x, 0.0);
	EXPECT_DOUBLE_EQ(snapped.y, -64.0);

	settings.rounding = grid::Rounding::towardOrigin;
	state.SetMathSettings(settings);
	snapped = state.ForceSnap({63.0, -1.0});
	EXPECT_NEAR(snapped.x, 0.0, 1e-10);
	EXPECT_NEAR(snapped.y, 0.0, 1e-10);

	settings.rounding = grid::Rounding::awayFromOrigin;
	state.SetMathSettings(settings);
	snapped = state.ForceSnap({1.0, -1.0});
	EXPECT_DOUBLE_EQ(snapped.x, 64.0);
	EXPECT_DOUBLE_EQ(snapped.y, -64.0);
}

TEST_F(GridStateFixture, RatioLockCannotBreakMathematicalGridIntersection)
{
	grid::State state(*this);
	config::grid_default_size = 32;
	config::grid_default_snap = true;
	state.Init();
	grid::MathSettings settings;
	settings.pattern = grid::Pattern::oblique;
	settings.rotation = 17.0;
	settings.axisAngle = 60.0;
	state.SetMathSettings(settings);
	state.configureRatio(2, false);

	v2double_t endpoint{111.0, 73.0};
	state.RatioSnapXY(endpoint, {0.0, 0.0});
	EXPECT_TRUE(state.OnGrid(endpoint.x, endpoint.y));
}

TEST_F(GridStateFixture, MathematicalStateRoundTripsThroughMapUserState)
{
	grid::State source(*this);
	config::grid_default_size = 48;
	source.Init();
	grid::MathSettings settings;
	settings.pattern = grid::Pattern::oblique;
	settings.rounding = grid::Rounding::awayFromOrigin;
	settings.origin = {123.5, -987.25};
	settings.rotation = 17.5;
	settings.secondaryRatio = 1.61803398875;
	settings.axisAngle = 72.0;
	settings.angularDivisions = 72;
	settings.majorEvery = 12;
	source.SetMathSettings(settings);

	std::ostringstream output;
	source.writeUser(output);
	const std::string written = output.str();
	EXPECT_NE(written.find("math_grid 1 4"), std::string::npos);

	grid::State restored(*this);
	restored.Init();
	ASSERT_TRUE(restored.parseUser({"math_grid", "1", "4",
			"123.5", "-987.25", "17.5", "1.61803398875",
			"72", "72", "12"}));
	const grid::MathSettings &actual = restored.getMathSettings();
	EXPECT_EQ(actual.pattern, grid::Pattern::oblique);
	EXPECT_EQ(actual.rounding, grid::Rounding::awayFromOrigin);
	EXPECT_DOUBLE_EQ(actual.origin.x, 123.5);
	EXPECT_DOUBLE_EQ(actual.origin.y, -987.25);
	EXPECT_DOUBLE_EQ(actual.rotation, 17.5);
	EXPECT_DOUBLE_EQ(actual.axisAngle, 72.0);
	EXPECT_EQ(actual.majorEvery, 12);
}

TEST_F(GridStateFixture, ForceStep)
{

}
