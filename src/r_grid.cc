//------------------------------------------------------------------------
//  GRID STUFF
//------------------------------------------------------------------------
//
//  Eureka DOOM Editor
//
//  Copyright (C) 2001-2019 Andrew Apted
//  Copyright (C) 1997-2003 André Majorel et al
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
//
//  Based on Yadex which incorporated code from DEU 5.21 that was put
//  in the public domain in 1994 by Raphaël Quinet and Brendon Wyber.
//
//------------------------------------------------------------------------

#include "Errors.h"

#include "r_grid.h"
#include "m_config.h"
#include "sys_debug.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

// config items
int  config::grid_default_size = 64;
bool config::grid_default_snap = false;
bool config::grid_default_mode = false;

int  config::grid_style;  // 0 = squares, 1 = dotty
bool config::grid_hide_in_free_mode = false;
bool config::grid_snap_indicator = true;

int  config::grid_ratio_high = 3;  // custom ratio (high must be >= low)
int  config::grid_ratio_low  = 1;  // (low must be > 0)

namespace
{

constexpr double kPi = 3.14159265358979323846;

double NormalizeAngle(double angle)
{
	angle = std::fmod(angle, 360.0);
	if (angle < 0.0)
		angle += 360.0;
	return angle;
}

double QuantizeCoefficient(double value, grid::Rounding rounding)
{
	switch (rounding)
	{
	case grid::Rounding::lower:
		return std::floor(value);

	case grid::Rounding::upper:
		return std::ceil(value);

	case grid::Rounding::towardOrigin:
		return std::trunc(value);

	case grid::Rounding::awayFromOrigin:
		return std::copysign(std::ceil(std::abs(value)), value);

	case grid::Rounding::nearest:
	default:
		return std::round(value);
	}
}

grid::MathSettings Orthogonal(double rotation = 0.0,
		double secondaryRatio = 1.0)
{
	grid::MathSettings result;
	result.pattern = grid::Pattern::orthogonal;
	result.rotation = rotation;
	result.secondaryRatio = secondaryRatio;
	return result;
}

grid::MathSettings Oblique(double rotation, double axisAngle,
		double secondaryRatio = 1.0)
{
	grid::MathSettings result;
	result.pattern = grid::Pattern::oblique;
	result.rotation = rotation;
	result.axisAngle = axisAngle;
	result.secondaryRatio = secondaryRatio;
	return result;
}

grid::MathSettings Polar(int divisions, double rotation = 0.0)
{
	grid::MathSettings result;
	result.pattern = grid::Pattern::polar;
	result.rotation = rotation;
	result.angularDivisions = divisions;
	return result;
}

} // namespace

bool grid::Basis::valid() const noexcept
{
	return std::isfinite(primary.x) && std::isfinite(primary.y) &&
			std::isfinite(secondary.x) && std::isfinite(secondary.y) &&
			std::isfinite(determinant) &&
			std::abs(determinant) > 1e-9;
}

bool grid::MathSettingsValid(const MathSettings &settings,
		SString *reason) noexcept
{
	auto fail = [reason](const char *message)
	{
		if (reason)
			*reason = message;
		return false;
	};

	if (!std::isfinite(settings.origin.x) ||
			!std::isfinite(settings.origin.y) ||
			!std::isfinite(settings.rotation) ||
			!std::isfinite(settings.secondaryRatio) ||
			!std::isfinite(settings.axisAngle))
	{
		return fail("Grid geometry values must be finite.");
	}
	if (std::abs(settings.origin.x) > 1000000000.0 ||
			std::abs(settings.origin.y) > 1000000000.0)
	{
		return fail("Grid origin exceeds the one-billion-unit safety range.");
	}
	if (settings.secondaryRatio < 1.0 / 1024.0 ||
			settings.secondaryRatio > 1024.0)
	{
		return fail("Secondary spacing ratio must be between 1/1024 and 1024.");
	}
	if (settings.pattern == Pattern::oblique &&
			(settings.axisAngle < 1.0 || settings.axisAngle > 179.0))
	{
		return fail("The angle between lattice axes must be from 1 to 179 degrees.");
	}
	if (settings.angularDivisions < 3 ||
			settings.angularDivisions > 360)
	{
		return fail("Polar divisions must be from 3 to 360.");
	}
	if (settings.majorEvery < 2 || settings.majorEvery > 64)
	{
		return fail("Major grid interval must be from 2 to 64.");
	}
	if (reason)
		reason->clear();
	return true;
}

const std::vector<grid::StepPreset> &grid::StepPresets()
{
	static const std::vector<StepPreset> presets = {
		{"Essential", "1 unit", 1}, {"Essential", "2 units", 2},
		{"Essential", "4 units", 4}, {"Essential", "8 units", 8},
		{"Essential", "16 units", 16}, {"Essential", "32 units", 32},
		{"Essential", "64 units", 64}, {"Essential", "128 units", 128},
		{"Essential", "256 units", 256}, {"Essential", "512 units", 512},
		{"Essential", "1024 units", 1024},

		{"Doom architecture", "3 units", 3},
		{"Doom architecture", "6 units", 6},
		{"Doom architecture", "12 units", 12},
		{"Doom architecture", "24 units", 24},
		{"Doom architecture", "48 units", 48},
		{"Doom architecture", "96 units", 96},
		{"Doom architecture", "192 units", 192},
		{"Doom architecture", "384 units", 384},
		{"Doom architecture", "768 units", 768},
		{"Doom architecture", "1536 units", 1536},
		{"Doom architecture", "3072 units", 3072},

		{"Large map", "2048 units", 2048},
		{"Large map", "3072 units", 3072},
		{"Large map", "4096 units", 4096},
		{"Large map", "6144 units", 6144},
		{"Large map", "8192 units", 8192},
		{"Large map", "12288 units", 12288},
		{"Large map", "16384 units", 16384},
		{"Large map", "24576 units", 24576},
		{"Large map", "32768 units", 32768},
		{"Large map", "49152 units", 49152},
		{"Large map", "65536 units", 65536},

		{"Decimal engineering", "5 units", 5},
		{"Decimal engineering", "10 units", 10},
		{"Decimal engineering", "20 units", 20},
		{"Decimal engineering", "25 units", 25},
		{"Decimal engineering", "50 units", 50},
		{"Decimal engineering", "100 units", 100},
		{"Decimal engineering", "200 units", 200},
		{"Decimal engineering", "250 units", 250},
		{"Decimal engineering", "500 units", 500},
		{"Decimal engineering", "1000 units", 1000},
		{"Decimal engineering", "2000 units", 2000},
		{"Decimal engineering", "2500 units", 2500},
		{"Decimal engineering", "5000 units", 5000},
		{"Decimal engineering", "10000 units", 10000},

		{"Fibonacci", "3", 3}, {"Fibonacci", "5", 5},
		{"Fibonacci", "8", 8}, {"Fibonacci", "13", 13},
		{"Fibonacci", "21", 21}, {"Fibonacci", "34", 34},
		{"Fibonacci", "55", 55}, {"Fibonacci", "89", 89},
		{"Fibonacci", "144", 144}, {"Fibonacci", "233", 233},
		{"Fibonacci", "377", 377}, {"Fibonacci", "610", 610},
		{"Fibonacci", "987", 987}, {"Fibonacci", "1597", 1597},
		{"Fibonacci", "2584", 2584}, {"Fibonacci", "4181", 4181},
		{"Fibonacci", "6765", 6765}, {"Fibonacci", "10946", 10946},

		{"Powers of three", "3", 3}, {"Powers of three", "9", 9},
		{"Powers of three", "27", 27}, {"Powers of three", "81", 81},
		{"Powers of three", "243", 243}, {"Powers of three", "729", 729},
		{"Powers of three", "2187", 2187},
		{"Powers of three", "6561", 6561},
		{"Powers of three", "19683", 19683},
		{"Powers of three", "59049", 59049}
	};
	return presets;
}

const std::vector<grid::GeometryPreset> &grid::GeometryPresets()
{
	static const std::vector<GeometryPreset> presets = {
		{"square", "Cartesian square",
			"Traditional perpendicular grid.", Orthogonal()},
		{"rectangle_2_1", "Rectangle 2:1",
			"Half-height rectangular cells.", Orthogonal(0.0, 0.5)},
		{"rectangle_3_2", "Rectangle 3:2",
			"Architectural 3:2 rectangular cells.", Orthogonal(0.0, 2.0 / 3.0)},
		{"rectangle_tall_2", "Tall rectangle 1:2",
			"Double-height rectangular cells.", Orthogonal(0.0, 2.0)},
		{"golden", "Golden rectangle",
			"Secondary spacing follows the golden ratio.",
			Orthogonal(0.0, 1.6180339887498948)},
		{"root_two", "Root-two rectangle",
			"ISO paper proportion using square root of two.",
			Orthogonal(0.0, 1.4142135623730951)},
		{"diamond_45", "Diamond 45°",
			"Square lattice rotated forty-five degrees.", Orthogonal(45.0)},
		{"diamond_30", "Diamond 30°",
			"Square lattice rotated thirty degrees.", Orthogonal(30.0)},
		{"diamond_15", "Diamond 15°",
			"Square lattice rotated fifteen degrees.", Orthogonal(15.0)},
		{"triangular_60", "Triangular 60°",
			"Equilateral triangular and hexagonal axial lattice.",
			Oblique(0.0, 60.0)},
		{"triangular_120", "Triangular 120°",
			"Mirrored equilateral triangular lattice.",
			Oblique(0.0, 120.0)},
		{"isometric_30", "Isometric 30°",
			"Isometric axes at thirty and one-hundred-fifty degrees.",
			Oblique(30.0, 120.0)},
		{"isometric_flat", "Hex axial — flat top",
			"Flat-top hexagonal construction lattice.",
			Oblique(0.0, 60.0)},
		{"isometric_point", "Hex axial — point top",
			"Point-top hexagonal construction lattice.",
			Oblique(30.0, 60.0)},
		{"dimetric_2_1", "Dimetric 2:1",
			"Classic two-to-one pixel-art projection axes.",
			Oblique(26.5650511771, 126.8698976458)},
		{"military", "Military projection",
			"Oblique forty-five-degree construction lattice.",
			Oblique(0.0, 45.0)},
		{"cabinet", "Cabinet projection",
			"Half-depth forty-five-degree oblique lattice.",
			Oblique(0.0, 45.0, 0.5)},
		{"polar_8", "Polar 8 directions",
			"Radial grid with forty-five-degree spokes.", Polar(8)},
		{"polar_12", "Polar 12 directions",
			"Radial grid with thirty-degree spokes.", Polar(12)},
		{"polar_16", "Polar 16 directions",
			"Radial grid with 22.5-degree spokes.", Polar(16)},
		{"polar_24", "Polar 24 directions",
			"Radial grid with fifteen-degree spokes.", Polar(24)},
		{"polar_32", "Polar 32 directions",
			"Radial grid with 11.25-degree spokes.", Polar(32)},
		{"polar_36", "Polar 36 directions",
			"Radial grid with ten-degree spokes.", Polar(36)},
		{"polar_48", "Polar 48 directions",
			"Radial grid with 7.5-degree spokes.", Polar(48)},
		{"polar_64", "Polar 64 directions",
			"Radial grid with 5.625-degree spokes.", Polar(64)},
		{"polar_72", "Polar 72 directions",
			"Radial grid with five-degree spokes.", Polar(72)},
		{"polar_90", "Polar 90 directions",
			"Radial grid with four-degree spokes.", Polar(90)},
		{"polar_120", "Polar 120 directions",
			"Radial grid with three-degree spokes.", Polar(120)},
		{"polar_180", "Polar 180 directions",
			"Radial grid with two-degree spokes.", Polar(180)},
		{"polar_360", "Polar 360 directions",
			"Radial grid with one-degree spokes.", Polar(360)}
	};
	return presets;
}

void grid::State::Init()
{
	initializing = true;
	math = {};
	step = config::grid_default_size;

	if (step < kMinimumStep)
		step = kMinimumStep;

	if (step > values[0])
		step = values[0];

	shown = true;  // prevent a beep in AdjustStep

	AdjustStep(0);	// correct step to power of two

	if (!config::grid_default_mode)
	{
		shown = false;

		listener.gridSetGrid(-1);
	}
	else
	{
		shown = true;
	}

	snap = config::grid_default_snap;
	listener.gridUpdateSnap();
	initializing = false;
}


void grid::State::MoveTo(const v2double_t &pos)
{
	// no change?
	if (fabs(pos.x - orig.x) < 0.01 &&
	    fabs(pos.y - orig.y) < 0.01)
		return;

	orig.x = pos.x;
	orig.y = pos.y;

	listener.gridAdjustPos();
	listener.gridPointerPos();
	listener.gridRedrawMap();
}


void grid::State::Scroll(const v2double_t &delta)
{
	MoveTo(orig + delta);
}


int grid::State::ForceSnapX(double map_x) const
{
	const double local = (map_x - math.origin.x) / step;
	return static_cast<int>(std::lround(math.origin.x +
			step * QuantizeCoefficient(local, math.rounding)));
}

int grid::State::ForceSnapY(double map_y) const
{
	const double yStep = step * math.secondaryRatio;
	const double local = (map_y - math.origin.y) / yStep;
	return static_cast<int>(std::lround(math.origin.y +
			yStep * QuantizeCoefficient(local, math.rounding)));
}


double grid::State::SnapX(double map_x) const
{
	if (! snap || step == 0)
		return map_x;

	return ForceSnapX(map_x);
}

double grid::State::SnapY(double map_y) const
{
	if (! snap || step == 0)
		return map_y;

	return ForceSnapY(map_y);
}

grid::Basis grid::State::getBasis() const noexcept
{
	const double rotation = NormalizeAngle(math.rotation) * kPi / 180.0;
	const double angle = math.pattern == Pattern::orthogonal ?
			90.0 : math.axisAngle;
	const double secondaryAngle =
			rotation + angle * kPi / 180.0;
	Basis result;
	result.primary = {step * std::cos(rotation),
			step * std::sin(rotation)};
	const double secondaryLength = step * math.secondaryRatio;
	result.secondary = {secondaryLength * std::cos(secondaryAngle),
			secondaryLength * std::sin(secondaryAngle)};
	if (std::abs(result.primary.x) < 1e-12)
		result.primary.x = 0.0;
	if (std::abs(result.primary.y) < 1e-12)
		result.primary.y = 0.0;
	if (std::abs(result.secondary.x) < 1e-12)
		result.secondary.x = 0.0;
	if (std::abs(result.secondary.y) < 1e-12)
		result.secondary.y = 0.0;
	result.determinant = result.primary.x * result.secondary.y -
			result.primary.y * result.secondary.x;
	return result;
}

bool grid::State::isDefaultOrthogonal() const noexcept
{
	return math.pattern == Pattern::orthogonal &&
			std::abs(NormalizeAngle(math.rotation)) < 1e-9 &&
			std::abs(math.secondaryRatio - 1.0) < 1e-9 &&
			std::abs(math.origin.x) < 1e-9 &&
			std::abs(math.origin.y) < 1e-9 &&
			math.rounding == Rounding::nearest;
}

v2double_t grid::State::ForceSnap(const v2double_t &map) const
{
	if (math.pattern == Pattern::polar)
	{
		const v2double_t local = map - math.origin;
		const double radius = std::hypot(local.x, local.y);
		if (!std::isfinite(radius))
			return map;
		const double snappedRadius = step * QuantizeCoefficient(
				radius / step, math.rounding);
		if (std::abs(snappedRadius) < 1e-12)
			return math.origin;

		const double angularStep = 2.0 * kPi / math.angularDivisions;
		const double rotation = NormalizeAngle(math.rotation) * kPi / 180.0;
		const double coefficient =
				(std::atan2(local.y, local.x) - rotation) / angularStep;
		const double snappedAngle = rotation + angularStep *
				QuantizeCoefficient(coefficient, math.rounding);
		return math.origin + v2double_t{
				snappedRadius * std::cos(snappedAngle),
				snappedRadius * std::sin(snappedAngle)};
	}

	const Basis basis = getBasis();
	if (!basis.valid())
		return map;
	const v2double_t local = map - math.origin;
	const double primary = (local.x * basis.secondary.y -
			local.y * basis.secondary.x) / basis.determinant;
	const double secondary = (basis.primary.x * local.y -
			basis.primary.y * local.x) / basis.determinant;
	return math.origin +
			basis.primary * QuantizeCoefficient(primary, math.rounding) +
			basis.secondary * QuantizeCoefficient(secondary, math.rounding);
}

v2double_t grid::State::Snap(const v2double_t &map) const
{
	return snap ? ForceSnap(map) : map;
}

std::vector<v2double_t> grid::State::SnapCandidates(
		const v2double_t &map) const
{
	std::vector<v2double_t> result;
	auto append = [&](const v2double_t &candidate)
	{
		for (const v2double_t &existing : result)
			if (std::hypot(existing.x - candidate.x,
						existing.y - candidate.y) < 1e-7)
				return;
		result.push_back(candidate);
	};
	append(ForceSnap(map));

	if (math.pattern == Pattern::polar)
	{
		const v2double_t local = map - math.origin;
		const double radial = std::hypot(local.x, local.y) / step;
		const double angularStep = 2.0 * kPi / math.angularDivisions;
		const double rotation = NormalizeAngle(math.rotation) * kPi / 180.0;
		const double angular =
				(std::atan2(local.y, local.x) - rotation) / angularStep;
		const long radialBase = std::lround(radial);
		const long angularBase = std::lround(angular);
		for (long radius = std::max(0l, radialBase - 1);
				radius <= radialBase + 1; ++radius)
		{
			for (long angle = angularBase - 1;
					angle <= angularBase + 1; ++angle)
			{
				if (radius == 0)
					append(math.origin);
				else
				{
					const double radians =
							rotation + angle * angularStep;
					append(math.origin + v2double_t{
							radius * step * std::cos(radians),
							radius * step * std::sin(radians)});
				}
			}
		}
	}
	else
	{
		const Basis basis = getBasis();
		if (basis.valid())
		{
			const v2double_t local = map - math.origin;
			const long primary = std::lround(
					(local.x * basis.secondary.y -
							local.y * basis.secondary.x) /
					basis.determinant);
			const long secondary = std::lround(
					(basis.primary.x * local.y -
							basis.primary.y * local.x) /
					basis.determinant);
			for (long a = primary - 1; a <= primary + 1; ++a)
				for (long b = secondary - 1; b <= secondary + 1; ++b)
					append(math.origin + basis.primary * a +
							basis.secondary * b);
		}
	}

	std::stable_sort(result.begin(), result.end(),
			[&](const v2double_t &first, const v2double_t &second)
			{
				return std::hypot(first.x - map.x, first.y - map.y) <
						std::hypot(second.x - map.x, second.y - map.y);
			});
	return result;
}


void grid::State::RatioSnapXY(v2double_t& var, const v2double_t &start) const
{
	// snap first, otherwise we lose the ratio
	var = Snap(var);

	double dx = var.x - start.x;
	double dy = var.y - start.y;

	double len = std::max(fabs(dx), fabs(dy));

	int sign_x = (dx >= 0) ? +1 : -1;
	int sign_y = (dy >= 0) ? +1 : -1;

	double custom;

	switch (ratio)
	{
	case 0: // unlocked
		break;

	case 1: // 1:1 (45 degrees) + axis aligned
		if (fabs(dx) * 2 < fabs(dy))
		{
			var.x = start.x;
		}
		else if (fabs(dy) * 2 < fabs(dx))
		{
			var.y = start.y;
		}
		else
		{
			var.x = start.x + sign_x * len;
			var.y = start.y + sign_y * len;
		}
		break;

	case 2: // 2:1 + axis aligned
		if (fabs(dx) * 4 < fabs(dy))
		{
			var.x = start.x;
		}
		else if (fabs(dy) * 4 < fabs(dx))
		{
			var.y = start.y;
		}
		else if (fabs(dx) < fabs(dy))
		{
			var.x = start.x + sign_x * len * 0.5;
			var.y = start.y + sign_y * len;
		}
		else
		{
			var.x = start.x + sign_x * len;
			var.y = start.y + sign_y * len * 0.5;
		}
		break;

	case 3: // 4:1 + axis aligned
		if (fabs(dx) * 8 < fabs(dy))
		{
			var.x = start.x;
		}
		else if (fabs(dy) * 8 < fabs(dx))
		{
			var.y = start.y;
		}
		else if (fabs(dx) < fabs(dy))
		{
			var.x = start.x + sign_x * len * 0.25;
			var.y = start.y + sign_y * len;
		}
		else
		{
			var.x = start.x + sign_x * len;
			var.y = start.y + sign_y * len * 0.25;
		}
		break;

	case 4: // 8:1 + axis aligned
		if (fabs(dx) * 16 < fabs(dy))
		{
			var.x = start.x;
		}
		else if (fabs(dy) * 16 < fabs(dx))
		{
			var.y = start.y;
		}
		else if (fabs(dx) < fabs(dy))
		{
			var.x = start.x + sign_x * len * 0.125;
			var.y = start.y + sign_y * len;
		}
		else
		{
			var.x = start.x + sign_x * len;
			var.y = start.y + sign_y * len * 0.125;
		}
		break;

	case 5: // 5:4 + axis aligned
		if (fabs(dx) * 3 < fabs(dy))
		{
			var.x = start.x;
		}
		else if (fabs(dy) * 3 < fabs(dx))
		{
			var.y = start.y;
		}
		else if (fabs(dx) < fabs(dy))
		{
			var.x = start.x + sign_x * len * 0.8;
			var.y = start.y + sign_y * len;
		}
		else
		{
			var.x = start.x + sign_x * len;
			var.y = start.y + sign_y * len * 0.8;
		}
		break;

	case 6: // 7:4 + axis aligned
		if (fabs(dx) * 3 < fabs(dy))
		{
			var.x = start.x;
		}
		else if (fabs(dy) * 3 < fabs(dx))
		{
			var.y = start.y;
		}
		else if (fabs(dx) < fabs(dy))
		{
			var.x = start.x + sign_x * len * 4 / 7;
			var.y = start.y + sign_y * len;
		}
		else
		{
			var.x = start.x + sign_x * len;
			var.y = start.y + sign_y * len * 4 / 7;
		}
		break;

	default: // USER SETTING
		if (config::grid_ratio_low < 1)
			config::grid_ratio_low = 1;
		if (config::grid_ratio_high < config::grid_ratio_low)
			config::grid_ratio_high = config::grid_ratio_low;

		custom = (double)config::grid_ratio_low / (double)config::grid_ratio_high;

		if (custom > 0.1 && fabs(dx) < fabs(dy) * custom * 0.3)
		{
			var.x = start.x;
		}
		else if (custom > 0.1 && fabs(dy) < fabs(dx) * custom * 0.3)
		{
			var.y = start.y;
		}
		else if (fabs(dx) < fabs(dy))
		{
			var.x = start.x + sign_x * len * custom;
			var.y = start.y + sign_y * len;
		}
		else
		{
			var.x = start.x + sign_x * len;
			var.y = start.y + sign_y * len * custom;
		}
	}

	// A world-axis ratio lock can otherwise pull the endpoint back off a
	// rotated or oblique construction lattice. Grid intersection is the
	// stronger promise whenever mathematical snapping is active.
	if (!isDefaultOrthogonal())
		var = Snap(var);
}


int grid::State::QuantSnapX(double map_x, bool want_furthest, int *dir) const
{
	if (OnGridX(map_x))
	{
		if (dir)
			*dir = 0;
		return static_cast<int>(map_x);
	}

	int new_x = ForceSnapX(map_x);

	if (dir)
	{
		if (new_x < map_x)
			*dir = -1;
		else
			*dir = +1;
	}

	if (! want_furthest)
		return new_x;

	if (new_x < map_x)
		return ForceSnapX(map_x + (step - 1));
	else
		return ForceSnapX(map_x - (step - 1));
}

int grid::State::QuantSnapY(double map_y, bool want_furthest, int *dir) const
{
	if (OnGridY(map_y))
	{
		if (dir)
			*dir = 0;
		return static_cast<int>(map_y);
	}

	const double yStep = step * math.secondaryRatio;
	int newY = ForceSnapY(map_y);
	if (dir)
		*dir = newY < map_y ? -1 : +1;
	if (!want_furthest)
		return newY;
	const double offset = std::max(1.0, yStep - 1.0);
	return newY < map_y ?
			ForceSnapY(map_y + offset) :
			ForceSnapY(map_y - offset);
}


void grid::State::NaturalSnapXY(double& var_x, double& var_y) const
{
	// this is only used by UI_Canvas::PointerPos()

	double nat_step = 1.0;

	while (nat_step * 2.0 <= Scale)
		nat_step = nat_step * 2.0;

	while (nat_step * 0.5 >= Scale)
		nat_step = nat_step * 0.5;

	var_x = round(var_x * nat_step) / nat_step;
	var_y = round(var_y * nat_step) / nat_step;
}


bool grid::State::OnGridX(double map_x) const
{
	const double coefficient = (map_x - math.origin.x) / step;
	return std::isfinite(coefficient) &&
			std::abs(coefficient - std::round(coefficient)) < 1e-7;
}

bool grid::State::OnGridY(double map_y) const
{
	const double yStep = step * math.secondaryRatio;
	const double coefficient = (map_y - math.origin.y) / yStep;
	return std::isfinite(coefficient) &&
			std::abs(coefficient - std::round(coefficient)) < 1e-7;
}

bool grid::State::OnGrid(double map_x, double map_y) const
{
	const v2double_t local =
			v2double_t{map_x, map_y} - math.origin;
	if (math.pattern == Pattern::polar)
	{
		const double radius = std::hypot(local.x, local.y);
		const double radial = radius / step;
		if (std::abs(radial - std::round(radial)) >= 1e-7)
			return false;
		if (radius < 1e-7)
			return true;
		const double angularStep = 2.0 * kPi / math.angularDivisions;
		const double rotation = NormalizeAngle(math.rotation) * kPi / 180.0;
		const double angular =
				(std::atan2(local.y, local.x) - rotation) / angularStep;
		return std::abs(angular - std::round(angular)) < 1e-7;
	}

	const Basis basis = getBasis();
	if (!basis.valid())
		return false;
	const double primary = (local.x * basis.secondary.y -
			local.y * basis.secondary.x) / basis.determinant;
	const double secondary = (basis.primary.x * local.y -
			basis.primary.y * local.x) / basis.determinant;
	return std::abs(primary - std::round(primary)) < 1e-7 &&
			std::abs(secondary - std::round(secondary)) < 1e-7;
}

void grid::State::configureGrid(int step, bool shown)
{
	this->step = std::clamp(step, kMinimumStep, kMaximumStep);
	RawSetShown(shown);
	listener.gridRedrawMap();
}

void grid::State::configureSnap(bool snap)
{
	this->snap = snap;
	listener.gridUpdateSnap();
}

void grid::State::configureRatio(int ratio, bool redraw)
{
	this->ratio = ratio;
	listener.gridUpdateRatio();
	if(redraw)
		listener.gridRedrawMap();
}

void grid::State::SetMathSettings(const MathSettings &settings)
{
	SString reason;
	if (!MathSettingsValid(settings, &reason))
	{
		listener.gridBeep(reason.c_str());
		return;
	}
	math = settings;
	math.rotation = NormalizeAngle(math.rotation);
	if (math.pattern == Pattern::orthogonal)
		math.axisAngle = 90.0;
	listener.gridPointerPos();
	listener.gridRedrawMap();
}

void grid::State::SetOrigin(const v2double_t &origin)
{
	MathSettings changed = math;
	changed.origin = origin;
	SetMathSettings(changed);
}

void grid::State::ResetMathSettings()
{
	SetMathSettings(MathSettings{});
}

void grid::State::RefocusZoom(const v2double_t &map, float before_Scale)
{
	double dist_factor = (1.0 - before_Scale / Scale);

	orig.x += (map.x - orig.x) * dist_factor;
	orig.y += (map.y - orig.y) * dist_factor;

	listener.gridPointerPos();
	listener.gridRedrawMap();
}


const double grid::State::scale_values[] =
{
	32.0, 16.0, 8.0, 6.0, 4.0,  3.0, 2.0, 1.5, 1.0,

	1.0 / 1.5, 1.0 / 2.0, 1.0 / 3.0,  1.0 / 4.0,
	1.0 / 6.0, 1.0 / 8.0, 1.0 / 16.0, 1.0 / 32.0,
	1.0 / 64.0
};


#define NUM_SCALE_VALUES 18
constexpr int NUM_GRID_VALUES =
		static_cast<int>(lengthof(grid::values));


void grid::State::RawSetScale(int i)
{
	SYS_ASSERT(0 <= i && i < NUM_SCALE_VALUES);

	Scale = scale_values[i];

	listener.gridAdjustPos();
	listener.gridPointerPos();
	listener.gridSetScale(Scale);

	listener.gridRedrawMap();
}


void grid::State::RawSetStep(int i)
{
	SYS_ASSERT(0 <= i && i < NUM_GRID_VALUES);

	if (i == NUM_GRID_VALUES-1)  /* OFF */
	{
		shown = false;
		listener.gridSetGrid(-1);
	}
	else
	{
		shown = true;
		step  = values[i];
		listener.gridSetGrid(step);
	}

	if (config::grid_hide_in_free_mode)
		SetSnap(shown);

	listener.gridRedrawMap();
}


void grid::State::ForceStep(int new_step)
{
	step = std::clamp(new_step, kMinimumStep, kMaximumStep);
	shown = true;

	listener.gridSetGrid(step);

	if (config::grid_hide_in_free_mode)
		SetSnap(shown);

	listener.gridRedrawMap();
}


void grid::State::StepFromScale()
{
	int pixels_min = 16;

	int result = 0;

	for (int i = 0 ; i < NUM_GRID_VALUES-1 ; i++)
	{
		result = i;

		if (values[i] * Scale / 2 < pixels_min)
			break;
	}

	if (step == values[result])
		return; // no change

	step = values[result];

	listener.gridRedrawMap();
}


void grid::State::AdjustStep(int delta)
{
	if (! shown)
	{
		listener.gridBeep("Grid is off (cannot change step)");
		return;
	}

	int result = -1;

	if (delta > 0)
	{
		for (int i = NUM_GRID_VALUES-2 ; i >= 0 ; i--)
		{
			if (values[i] > step)
			{
				result = i;
				break;
			}
		}
	}
	else if(!delta)	// this is for snapping to the closest grid
	{
		long long bestDistance = std::numeric_limits<long long>::max();
		for (int i = 0; i < NUM_GRID_VALUES - 1; ++i)
		{
			const long long distance =
					std::llabs(static_cast<long long>(values[i]) - step);
			if (distance < bestDistance ||
					(distance == bestDistance && result >= 0 &&
							values[i] < values[result]))
			{
				result = i;
				bestDistance = distance;
			}
		}
	}
	else // (delta < 0)
	{
		for (int i = 0 ; i < NUM_GRID_VALUES-1 ; i++)
		{
			if (values[i] < step)
			{
				result = i;
				break;
			}
		}
	}

	// already at the extreme end?
	if (result < 0)
		return;

	RawSetStep(result);
}


void grid::State::AdjustScale(int delta)
{
	int result = -1;

	if (delta > 0)
	{
		for (int i = NUM_SCALE_VALUES-1 ; i >= 0 ; i--)
		{
			if (scale_values[i] > Scale*1.01)
			{
				result = i;
				break;
			}
		}
	}
	else // (delta < 0)
	{
		for (int i = 0 ; i < NUM_SCALE_VALUES ; i++)
		{
			if (scale_values[i] < Scale*0.99)
			{
				result = i;
				break;
			}
		}
	}

	// already at the extreme end?
	if (result < 0)
		return;

	RawSetScale(result);
}


void grid::State::RawSetShown(bool new_value)
{
	shown = new_value;
	listener.gridSetGrid(shown ? step : -1);
	listener.gridRedrawMap();
}

std::string grid::getValuesFLTKMenuString()
{
	std::string result;
	result.reserve(5 * lengthof(values));
	for(size_t i = 0; i < lengthof(values); ++i)
	{
		int value = values[i];
		if(value >= 0)
			result += SString::printf("%d", value).get();
		else
			result += "OFF";
		if(i < lengthof(values) - 1)
            result += '|';
	}
	return result;
}

void grid::State::SetShown(bool enable)
{
	RawSetShown(enable);

	if (config::grid_hide_in_free_mode)
		SetSnap(enable);
}

void grid::State::ToggleShown()
{
	SetShown(!shown);
}


void grid::State::SetSnap(bool enable)
{
	// The toolbar/menu state is also the next-session default.  The
	// miscellaneous settings writer persists this value on clean shutdown,
	// so users do not need to duplicate the same choice in Preferences.
	if (!initializing)
		config::grid_default_snap = enable;

	if (snap == enable)
		return;

	snap = enable;

	if (config::grid_hide_in_free_mode && snap != shown)
		SetShown(snap);

	listener.gridUpdateSnap();
	listener.gridRedrawMap();
}

void grid::State::ToggleSnap()
{
	SetSnap(! snap);
}


void grid::State::NearestScale(double want_scale)
{
	int best = 0;

	for (int i = 0 ; i < NUM_SCALE_VALUES ; i++)
	{
		best = i;

		if (scale_values[i] < want_scale * 1.1)
			break;
	}

	RawSetScale(best);
}


bool grid::State::parseUser(const std::vector<SString> &tokens)
{
	if (tokens[0] == "map_pos" && tokens.size() >= 4)
	{
		double x = atof(tokens[1]);
		double y = atof(tokens[2]);

		MoveTo({ x, y });

		double new_scale = atof(tokens[3]);

		NearestScale(new_scale);

		listener.gridRedrawMap();
		return true;
	}

	if (tokens[0] == "grid" && tokens.size() >= 4)
	{
		bool t_shown = atoi(tokens[1]) ? true : false;

		configureGrid(atoi(tokens[3]), t_shown);
		// tokens[2] was grid.mode, currently unused
		return true;
	}

	if (tokens[0] == "snap" && tokens.size() >= 2)
	{
		// Legacy map-checksum state used to make snapping vary by map and
		// could lose the latest choice when quitting with discarded edits.
		// Consume the old record without overriding the global setting.
		return true;
	}

	if (tokens[0] == "math_grid" && tokens.size() >= 10)
	{
		MathSettings parsed;
		const int pattern = atoi(tokens[1]);
		const int rounding = atoi(tokens[2]);
		if (pattern < static_cast<int>(Pattern::orthogonal) ||
				pattern > static_cast<int>(Pattern::polar) ||
				rounding < static_cast<int>(Rounding::nearest) ||
				rounding > static_cast<int>(Rounding::awayFromOrigin))
			return true;
		parsed.pattern = static_cast<Pattern>(pattern);
		parsed.rounding = static_cast<Rounding>(rounding);
		parsed.origin.x = atof(tokens[3]);
		parsed.origin.y = atof(tokens[4]);
		parsed.rotation = atof(tokens[5]);
		parsed.secondaryRatio = atof(tokens[6]);
		parsed.axisAngle = atof(tokens[7]);
		parsed.angularDivisions = atoi(tokens[8]);
		parsed.majorEvery = atoi(tokens[9]);
		if (MathSettingsValid(parsed))
			SetMathSettings(parsed);
		return true;
	}

	return false;
}

void grid::State::writeUser(std::ostream &os) const
{
	os << "map_pos " << SString::printf("%1.0f %1.0f %1.6f", getOrig().x, getOrig().y, getScale()) <<
		'\n';
	os << "grid " << (isShown() ? 1 : 0) << ' ' << (config::grid_style ? 0 : 1) << ' ' <<
		getStep() << '\n';
	os << "math_grid " << static_cast<int>(math.pattern) << ' ' <<
			static_cast<int>(math.rounding) << ' ' <<
			SString::printf("%.12g %.12g %.12g %.12g %.12g %d %d",
					math.origin.x, math.origin.y, math.rotation,
					math.secondaryRatio, math.axisAngle,
					math.angularDivisions, math.majorEvery) << '\n';
}


//--- editor settings ---
// vi:ts=4:sw=4:noexpandtab
