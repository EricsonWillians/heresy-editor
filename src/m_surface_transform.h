//------------------------------------------------------------------------
//  FORMAT-AWARE SURFACE TEXTURE TRANSFORMS
//------------------------------------------------------------------------
//
//  Heresy Editor
//
//  Copyright (C) 2026 Ericson Willians
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 2
//  of the License, or (at your option) any later version.
//
//------------------------------------------------------------------------

#ifndef M_SURFACE_TRANSFORM_H_
#define M_SURFACE_TRANSFORM_H_

#include "m_game.h"
#include "Sector.h"
#include "SideDef.h"

#include <cstdint>
#include <utility>
#include <vector>

class EditOperation;
class Document;
class Img_c;
class Palette;
enum class MapFormat;
struct ConfigData;

enum class WallSurfacePart
{
	upper,
	middle,
	lower
};

enum class PlaneSurfacePart
{
	floor,
	ceiling
};

enum class SurfaceFitMode
{
	none,
	native,
	fitWidth,
	fitHeight,
	fitSurface
};

struct SurfaceDimensions
{
	double width = 0.0;
	double height = 0.0;

	bool valid() const noexcept
	{
		return width > 0.0 && height > 0.0;
	}
};

struct SurfaceTransform
{
	double offsetX = 0.0;
	double offsetY = 0.0;
	double scaleX = 1.0;
	double scaleY = 1.0;
	double rotation = 0.0;
};

struct SurfaceTransformCapabilities
{
	bool sharedWallOffsets = false;
	bool wallPartOffsets = false;
	bool wallScale = false;
	bool planeOffsets = false;
	bool planeScale = false;
	bool planeRotation = false;

	bool anyWallTransform() const noexcept
	{
		return sharedWallOffsets || wallPartOffsets || wallScale;
	}

	bool anyPlaneTransform() const noexcept
	{
		return planeOffsets || planeScale || planeRotation;
	}
};

struct WallSurfacePreviewValue
{
	int sidedef = -1;
	WallSurfacePart part = WallSurfacePart::middle;
	SurfaceTransform transform;
	bool setPartTransform = false;
	bool setSharedOffsets = false;
	bool forceUnsupportedPreview = false;
	int sharedOffsetX = 0;
	int sharedOffsetY = 0;
};

struct PlaneSurfacePreviewValue
{
	int sector = -1;
	PlaneSurfacePart part = PlaneSurfacePart::floor;
	SurfaceTransform transform;
	bool forceUnsupportedPreview = false;
};

// Temporarily projects reviewed values into a document so every existing 2D
// and 3D renderer can display them. It never touches dirty state or Undo/Redo,
// and clear()/destruction restores complete object properties.
class SurfaceTransformPreview
{
public:
	explicit SurfaceTransformPreview(Document &document) noexcept;
	~SurfaceTransformPreview();

	SurfaceTransformPreview(const SurfaceTransformPreview &) = delete;
	SurfaceTransformPreview &operator=(
			const SurfaceTransformPreview &) = delete;

	bool apply(const std::vector<WallSurfacePreviewValue> &walls,
			const std::vector<PlaneSurfacePreviewValue> &planes);
	void clear() noexcept;
	bool active() const noexcept;

private:
	Document &document_;
	std::vector<std::pair<int, SideDef>> sidedefSnapshots_;
	std::vector<std::pair<int, Sector>> sectorSnapshots_;
};

constexpr int kSurfaceTransformWallUDMF = 1;
constexpr int kSurfaceTransformPlaneUDMF = 2;

// These bounds deliberately exceed traditional texture sizes by orders of
// magnitude.  They reject malformed/infinite values without imposing
// 64/128/256-era assumptions on modern standalone images.
constexpr double kSurfaceTransformMinScale = 1.0 / 65536.0;
constexpr double kSurfaceTransformMaxScale = 65536.0;
constexpr double kSurfaceTransformMaxOffset = 1000000000000.0;

SurfaceTransformCapabilities M_SurfaceTransformCapabilities(
		MapFormat format, const ConfigData &config) noexcept;

SurfaceTransform M_WallSurfaceTransform(
		const SideDef &side, WallSurfacePart part) noexcept;
SurfaceTransform M_PlaneSurfaceTransform(
		const Sector &sector, PlaneSurfacePart part) noexcept;
SurfaceTransform M_EffectiveWallSurfaceTransform(const SideDef &side,
		WallSurfacePart part, MapFormat format,
		const ConfigData &config) noexcept;
SurfaceTransform M_EffectivePlaneSurfaceTransform(const Sector &sector,
		PlaneSurfacePart part, MapFormat format,
		const ConfigData &config) noexcept;

bool M_SurfaceTransformValid(const SurfaceTransform &transform,
		bool allowRotation, SString *reason = nullptr) noexcept;
SurfaceTransform M_NormalizeSurfaceTransform(
		SurfaceTransform transform) noexcept;

SurfaceDimensions M_WallSurfaceDimensions(const Document &document,
		int linedef, int sidedef, WallSurfacePart part) noexcept;
SurfaceDimensions M_PlaneSurfaceDimensions(const Document &document,
		int sector) noexcept;
SurfaceTransform M_FitSurfaceTransform(const SurfaceTransform &current,
		int imageWidth, int imageHeight, const SurfaceDimensions &surface,
		SurfaceFitMode mode) noexcept;

// Produce an uncompressed 32-bit TGA for a safe project-local fitted copy.
// Resampling is bilinear and preserves binary transparency and mirroring.
std::vector<uint8_t> M_BakeSurfaceTextureTGA(const Img_c &source,
		const Palette &palette, int width, int height,
		bool mirrorX, bool mirrorY);

void M_SetWallSurfaceTransform(SideDef &side, WallSurfacePart part,
		const SurfaceTransform &transform) noexcept;
void M_SetPlaneSurfaceTransform(Sector &sector, PlaneSurfacePart part,
		const SurfaceTransform &transform) noexcept;

void M_ChangeWallSurfaceTransform(EditOperation &operation, int sidedef,
		WallSurfacePart part, const SurfaceTransform &transform);
void M_ChangePlaneSurfaceTransform(EditOperation &operation, int sector,
		PlaneSurfacePart part, const SurfaceTransform &transform);

#endif
