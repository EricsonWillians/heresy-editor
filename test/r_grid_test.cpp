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
#include "gtest/gtest.h"

#include <cmath>
#include <set>
#include <sstream>

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

	ASSERT_EQ(grid.getScale(), 1.0 / 64.0);
	ASSERT_EQ(positionUpdates, posAdjustBefore + 1);
	ASSERT_EQ(pointerPositionUpdates, pointerPosBefore + 1);
	ASSERT_EQ(redrawMapCounts, redrawMapBefore + 1);
	ASSERT_EQ(scaleSettings.size(), scaleSettingsBefore + 1);
	ASSERT_EQ(scaleSettings.back(), 1.0 / 64.0);
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
