//------------------------------------------------------------------------
//  GRID STUFF
//------------------------------------------------------------------------
//
//  Eureka DOOM Editor
//
//  Copyright (C) 2007-2016 Andrew Apted
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

#ifndef __EUREKA_R_GRID_H__
#define __EUREKA_R_GRID_H__

#include "m_vector.h"
#include "m_strings.h"

#include <vector>

class Instance;

namespace grid
{
constexpr int kMinimumStep = 1;
constexpr int kMaximumStep = 65536;

// Ordered coarse/fine cycle used by GRID_Bump.  The grouped menu exposes
// many more mathematical sequences without making +/- stepping erratic.
static const int values[] =
{
	65536, 32768, 16384, 8192, 4096, 3072, 2048, 1536,
	1024, 768, 512, 384, 256, 192, 128, 96, 64, 48,
	32, 24, 16, 12, 8, 6, 4, 3, 2, 1,
	-1	// off
};

enum class Pattern
{
	orthogonal,
	oblique,
	polar
};

enum class Rounding
{
	nearest,
	lower,
	upper,
	towardOrigin,
	awayFromOrigin
};

struct MathSettings
{
	Pattern pattern = Pattern::orthogonal;
	Rounding rounding = Rounding::nearest;
	v2double_t origin = {};
	double rotation = 0.0;
	double secondaryRatio = 1.0;
	double axisAngle = 90.0;
	int angularDivisions = 16;
	int majorEvery = 8;
};

struct Basis
{
	v2double_t primary;
	v2double_t secondary;
	double determinant = 0.0;

	bool valid() const noexcept;
};

struct StepPreset
{
	const char *group;
	const char *label;
	int step;
};

struct GeometryPreset
{
	const char *id;
	const char *label;
	const char *description;
	MathSettings settings;
};

bool MathSettingsValid(const MathSettings &settings,
		SString *reason = nullptr) noexcept;
const std::vector<StepPreset> &StepPresets();
const std::vector<GeometryPreset> &GeometryPresets();

class Listener
{
public:
	virtual void gridRedrawMap() = 0;
	virtual void gridSetGrid(int grid) = 0;
	virtual void gridUpdateSnap() = 0;
	virtual void gridAdjustPos() = 0;
	virtual void gridPointerPos() = 0;
	virtual void gridSetScale(double scale) = 0;
	virtual void gridBeep(const char *message) = 0;
	virtual void gridUpdateRatio() = 0;
};

class State final
{
private:
	// the actual grid step (64, 128, etc)
	int step = 64;

	// if true, new and moved objects are forced to be on the grid
	bool snap = true;

	// if non-zero, new lines will be forced to have a certain ratio
	int ratio = 0;

	// whether the grid is being displayed or not.
	bool shown = true;

	// Init may temporarily synchronize snapping with grid visibility.
	// Those internal transitions are not user choices to remember.
	bool initializing = false;

	MathSettings math;

	// map coordinates for centre of canvas
	v2double_t orig = {};

	// scale for drawing map
	// (multiply a map coordinate by this to get a screen coord)
	double Scale = 1.0;

public:
	explicit State(Listener& listener) : listener(listener)
	{
	}

public:
	void Init();

	inline bool isShown() const
	{
		return shown;
	}

	void SetShown(bool enable);
	void SetSnap (bool enable);

	void ToggleShown();
	void ToggleSnap();

	// change the view so that the map coordinates (x, y)
	// appear at the centre of the window
	void MoveTo(const v2double_t &newpos);

	void Scroll(const v2double_t &delta);

	// move the origin so that the focus point of the last zoom
	// operation (scale change) is map_x/y.
	void RefocusZoom(const v2double_t &map, float before_Scale);

	// choose the scale nearest to (and less than) the wanted one
	void NearestScale(double want_scale);

	// force grid stepping size to arbitrary value
	void ForceStep(int new_step);
	void SetMathSettings(const MathSettings &settings);
	void SetOrigin(const v2double_t &origin);
	void ResetMathSettings();

	// compute new grid step from current scale
	void StepFromScale();

	// increase or decrease the grid size.  The 'delta' parameter
	// is positive to increase it, negative to decrease it.
	void AdjustStep (int delta);
	void AdjustScale(int delta);

	// return X/Y coordinate snapped to grid
	// (or unchanged is the 'snap' flag is off)
	double SnapX(double map_x) const;
	double SnapY(double map_y) const;
	v2double_t Snap(const v2double_t &map) const;

	// return X/Y coordinate snapped to grid (always)
	int ForceSnapX(double map_x) const;
	int ForceSnapY(double map_y) const;
	v2double_t ForceSnap(const v2double_t &map) const;
	std::vector<v2double_t> SnapCandidates(
			const v2double_t &map) const;
	Basis getBasis() const noexcept;
	bool isDefaultOrthogonal() const noexcept;

	// snap X/Y coordinate to ratio lock
	// (unchanged is the ratio snapping is off)
	void RatioSnapXY(v2double_t& var, const v2double_t &start) const;

	// quantization snap, can pick coordinate on other side
	int QuantSnapX(double map_x, bool want_furthest, int *dir = NULL) const;
	int QuantSnapY(double map_y, bool want_furthest, int *dir = NULL) const;

	// snap to the natural resolution of canvas
	void NaturalSnapXY(double& var_x, double& var_y) const;

	// check if the X/Y coordinate is on a grid point
	bool OnGridX(double map_x) const;
	bool OnGridY(double map_y) const;

	bool OnGrid(double map_x, double map_y) const;

	void configureRatio(int ratio, bool redraw);

	int getStep() const
	{
		return step;
	}
	const MathSettings &getMathSettings() const
	{
		return math;
	}
	bool snaps() const
	{
		return snap;
	}
	int getRatio() const
	{
		return ratio;
	}
	const v2double_t &getOrig() const
	{
		return orig;
	}
	double getScale() const
	{
		return Scale;
	}
	
	bool parseUser(const std::vector<SString> &tokens);
	void writeUser(std::ostream &os) const;

private:
	void RawSetStep(int i);
	void RawSetScale(int i);
	void RawSetShown(bool new_shown);
	
	void configureGrid(int step, bool shown);
	void configureSnap(bool snap);

	static const double scale_values[];

	Listener& listener;
};

std::string getValuesFLTKMenuString();

} // namespace grid

#endif  /* __EUREKA_R_GRID_H__ */

//--- editor settings ---
// vi:ts=4:sw=4:noexpandtab
