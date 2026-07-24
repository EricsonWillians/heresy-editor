//------------------------------------------------------------------------
//  FORMAT-AWARE SURFACE TEXTURE TRANSFORMS
//------------------------------------------------------------------------

#include "m_surface_transform.h"

#include "Document.h"
#include "e_basis.h"
#include "im_color.h"
#include "im_img.h"
#include "LineDef.h"
#include "Sector.h"
#include "SideDef.h"
#include "Vertex.h"
#include "WindowsSanitization.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace
{
bool Different(double first, double second)
{
	return std::abs(first - second) > 1e-12 *
			std::max({1.0, std::abs(first), std::abs(second)});
}
}

SurfaceTransformCapabilities M_SurfaceTransformCapabilities(
		MapFormat format, const ConfigData &config) noexcept
{
	SurfaceTransformCapabilities result;

	// Every supported binary map format has ordinary integer sidedef offsets.
	result.sharedWallOffsets = true;

	if (format != MapFormat::udmf)
		return result;

	if (config.features.udmf_surface_transforms &
			kSurfaceTransformWallUDMF)
	{
		result.wallPartOffsets = true;
		result.wallScale = true;
	}

	if (config.features.udmf_surface_transforms &
			kSurfaceTransformPlaneUDMF)
	{
		result.planeOffsets = true;
		result.planeScale = true;
		result.planeRotation = true;
	}

	return result;
}

SurfaceTransform M_WallSurfaceTransform(
		const SideDef &side, WallSurfacePart part) noexcept
{
	switch (part)
	{
	case WallSurfacePart::upper:
		return {side.offsetx_top, side.offsety_top,
				side.scalex_top, side.scaley_top, 0.0};

	case WallSurfacePart::middle:
		return {side.offsetx_mid, side.offsety_mid,
				side.scalex_mid, side.scaley_mid, 0.0};

	case WallSurfacePart::lower:
		return {side.offsetx_bottom, side.offsety_bottom,
				side.scalex_bottom, side.scaley_bottom, 0.0};
	}

	return {};
}

SurfaceTransform M_PlaneSurfaceTransform(
		const Sector &sector, PlaneSurfacePart part) noexcept
{
	if (part == PlaneSurfacePart::floor)
	{
		return {sector.xpanningfloor, sector.ypanningfloor,
				sector.xscalefloor, sector.yscalefloor,
				sector.rotationfloor};
	}

	return {sector.xpanningceiling, sector.ypanningceiling,
			sector.xscaleceiling, sector.yscaleceiling,
			sector.rotationceiling};
}

SurfaceTransform M_EffectiveWallSurfaceTransform(const SideDef &side,
		WallSurfacePart part, MapFormat format,
		const ConfigData &config) noexcept
{
	const auto capabilities =
			M_SurfaceTransformCapabilities(format, config);
	SurfaceTransform result = M_WallSurfaceTransform(side, part);
	if (side.preview_surface_transform)
	{
		result.rotation = 0.0;
		return M_SurfaceTransformValid(result, false) ?
				result : SurfaceTransform{};
	}
	if (!capabilities.wallPartOffsets)
		result.offsetX = result.offsetY = 0.0;
	if (!capabilities.wallScale)
		result.scaleX = result.scaleY = 1.0;
	result.rotation = 0.0;
	return M_SurfaceTransformValid(result, false) ?
			result : SurfaceTransform{};
}

SurfaceTransform M_EffectivePlaneSurfaceTransform(const Sector &sector,
		PlaneSurfacePart part, MapFormat format,
		const ConfigData &config) noexcept
{
	const auto capabilities =
			M_SurfaceTransformCapabilities(format, config);
	SurfaceTransform result = M_PlaneSurfaceTransform(sector, part);
	const bool preview = part == PlaneSurfacePart::floor ?
			sector.preview_floor_transform :
			sector.preview_ceiling_transform;
	if (preview)
	{
		return M_SurfaceTransformValid(result, true) ?
				result : SurfaceTransform{};
	}
	if (!capabilities.planeOffsets)
		result.offsetX = result.offsetY = 0.0;
	if (!capabilities.planeScale)
		result.scaleX = result.scaleY = 1.0;
	if (!capabilities.planeRotation)
		result.rotation = 0.0;
	return M_SurfaceTransformValid(
			result, capabilities.planeRotation) ?
			result : SurfaceTransform{};
}

bool M_SurfaceTransformValid(const SurfaceTransform &transform,
		bool allowRotation, SString *reason) noexcept
{
	auto fail = [reason](const char *message)
	{
		if (reason)
			*reason = message;
		return false;
	};

	if (!std::isfinite(transform.offsetX) ||
			!std::isfinite(transform.offsetY) ||
			!std::isfinite(transform.scaleX) ||
			!std::isfinite(transform.scaleY) ||
			!std::isfinite(transform.rotation))
	{
		return fail("Transform values must be finite.");
	}

	if (std::abs(transform.offsetX) > kSurfaceTransformMaxOffset ||
			std::abs(transform.offsetY) > kSurfaceTransformMaxOffset)
	{
		return fail("Offsets exceed the supported one-trillion-unit range.");
	}

	const double absScaleX = std::abs(transform.scaleX);
	const double absScaleY = std::abs(transform.scaleY);
	if (absScaleX < kSurfaceTransformMinScale ||
			absScaleY < kSurfaceTransformMinScale ||
			absScaleX > kSurfaceTransformMaxScale ||
			absScaleY > kSurfaceTransformMaxScale)
	{
		return fail("Scale must be between 1/65536 and 65536 in magnitude.");
	}

	if (!allowRotation && transform.rotation != 0.0)
		return fail("This surface does not support rotation.");

	if (reason)
		reason->clear();
	return true;
}

SurfaceTransform M_NormalizeSurfaceTransform(
		SurfaceTransform transform) noexcept
{
	if (std::isfinite(transform.rotation))
	{
		transform.rotation = std::fmod(transform.rotation, 360.0);
		if (transform.rotation < 0.0)
			transform.rotation += 360.0;
		if (transform.rotation == 360.0)
			transform.rotation = 0.0;
	}
	return transform;
}

SurfaceDimensions M_WallSurfaceDimensions(const Document &document,
		int linedef, int sidedef, WallSurfacePart part) noexcept
{
	if (!document.isLinedef(linedef) || !document.isSidedef(sidedef))
		return {};

	const LineDef &line = *document.linedefs[linedef];
	const bool right = line.right == sidedef;
	const bool left = line.left == sidedef;
	if (!right && !left)
		return {};

	const SideDef &side = *document.sidedefs[sidedef];
	if (!document.isSector(side.sector))
		return {};
	const Sector &sector = *document.sectors[side.sector];
	const int otherSide = right ? line.left : line.right;

	double height = sector.HeadRoom();
	if (document.isSidedef(otherSide))
	{
		const SideDef &other = *document.sidedefs[otherSide];
		if (!document.isSector(other.sector))
			return {};
		const Sector &neighbor = *document.sectors[other.sector];
		if (part == WallSurfacePart::upper)
			height = sector.ceilh - neighbor.ceilh;
		else if (part == WallSurfacePart::lower)
			height = neighbor.floorh - sector.floorh;
		else
			height = std::min(sector.ceilh, neighbor.ceilh) -
					std::max(sector.floorh, neighbor.floorh);
	}

	const double width = document.calcLength(line);
	if (!std::isfinite(width) || !std::isfinite(height) ||
			width <= 0.0 || height <= 0.0)
	{
		return {};
	}
	return {width, height};
}

SurfaceDimensions M_PlaneSurfaceDimensions(
		const Document &document, int sector) noexcept
{
	if (!document.isSector(sector))
		return {};

	double minimumX = std::numeric_limits<double>::infinity();
	double minimumY = std::numeric_limits<double>::infinity();
	double maximumX = -std::numeric_limits<double>::infinity();
	double maximumY = -std::numeric_limits<double>::infinity();
	bool found = false;
	for (const std::shared_ptr<LineDef> &line : document.linedefs)
	{
		bool touches = false;
		for (const int sidedef : {line->right, line->left})
		{
			if (document.isSidedef(sidedef) &&
					document.sidedefs[sidedef]->sector == sector)
			{
				touches = true;
				break;
			}
		}
		if (!touches || !document.isVertex(line->start) ||
				!document.isVertex(line->end))
		{
			continue;
		}

		for (const int vertex : {line->start, line->end})
		{
			const Vertex &point = *document.vertices[vertex];
			minimumX = std::min(minimumX, point.x());
			minimumY = std::min(minimumY, point.y());
			maximumX = std::max(maximumX, point.x());
			maximumY = std::max(maximumY, point.y());
			found = true;
		}
	}

	if (!found || maximumX <= minimumX || maximumY <= minimumY)
		return {};
	return {maximumX - minimumX, maximumY - minimumY};
}

SurfaceTransform M_FitSurfaceTransform(const SurfaceTransform &current,
		int imageWidth, int imageHeight, const SurfaceDimensions &surface,
		SurfaceFitMode mode) noexcept
{
	SurfaceTransform result = current;
	const double signX = result.scaleX < 0.0 ? -1.0 : 1.0;
	const double signY = result.scaleY < 0.0 ? -1.0 : 1.0;

	if (mode == SurfaceFitMode::native)
	{
		result.scaleX = signX;
		result.scaleY = signY;
		return result;
	}
	if (imageWidth <= 0 || imageHeight <= 0 || !surface.valid())
		return result;

	if (mode == SurfaceFitMode::fitWidth ||
			mode == SurfaceFitMode::fitSurface)
	{
		result.scaleX = signX * imageWidth / surface.width;
	}
	if (mode == SurfaceFitMode::fitHeight ||
			mode == SurfaceFitMode::fitSurface)
	{
		result.scaleY = signY * imageHeight / surface.height;
	}
	return result;
}

std::vector<uint8_t> M_BakeSurfaceTextureTGA(const Img_c &source,
		const Palette &palette, int width, int height,
		bool mirrorX, bool mirrorY)
{
	constexpr int maximumDimension = 8192;
	if (source.is_null() || width <= 0 || height <= 0 ||
			width > maximumDimension || height > maximumDimension)
	{
		throw std::runtime_error(
				"Fitted texture dimensions must be between 1 and 8192.");
	}

	const size_t pixelBytes = static_cast<size_t>(width) *
			static_cast<size_t>(height) * 4;
	if (pixelBytes + 18 > 256ull * 1024ull * 1024ull)
		throw std::runtime_error(
				"The fitted texture would exceed the 256 MiB resource limit.");
	std::vector<uint8_t> result(18 + pixelBytes, 0);
	result[2] = 2; // uncompressed true-color TGA
	result[12] = static_cast<uint8_t>(width & 255);
	result[13] = static_cast<uint8_t>((width >> 8) & 255);
	result[14] = static_cast<uint8_t>(height & 255);
	result[15] = static_cast<uint8_t>((height >> 8) & 255);
	result[16] = 32;
	result[17] = 8; // eight alpha bits, bottom-left origin

	struct Sample
	{
		double red = 0.0;
		double green = 0.0;
		double blue = 0.0;
		double alpha = 0.0;
	};
	auto sample = [&](int x, int y)
	{
		x = std::clamp(x, 0, source.width() - 1);
		y = std::clamp(y, 0, source.height() - 1);
		const img_pixel_t pixel = source.buf()[y * source.width() + x];
		Sample value;
		if (pixel == TRANS_PIXEL)
			return value;
		byte red, green, blue;
		palette.decodePixelRaw(pixel, red, green, blue);
		value.red = red;
		value.green = green;
		value.blue = blue;
		value.alpha = 1.0;
		return value;
	};

	size_t destination = 18;
	for (int outputY = height - 1; outputY >= 0; --outputY)
	{
		const int mappedY = mirrorY ? height - 1 - outputY : outputY;
		const double sourceY =
				(mappedY + 0.5) * source.height() / height - 0.5;
		const int y0 = static_cast<int>(std::floor(sourceY));
		const int y1 = y0 + 1;
		const double fy = sourceY - y0;
		for (int outputX = 0; outputX < width; ++outputX)
		{
			const int mappedX = mirrorX ? width - 1 - outputX : outputX;
			const double sourceX =
					(mappedX + 0.5) * source.width() / width - 0.5;
			const int x0 = static_cast<int>(std::floor(sourceX));
			const int x1 = x0 + 1;
			const double fx = sourceX - x0;

			const Sample samples[] = {
					sample(x0, y0), sample(x1, y0),
					sample(x0, y1), sample(x1, y1)};
			const double weights[] = {
					(1.0 - fx) * (1.0 - fy), fx * (1.0 - fy),
					(1.0 - fx) * fy, fx * fy};
			double alpha = 0.0;
			double red = 0.0;
			double green = 0.0;
			double blue = 0.0;
			for (int index = 0; index < 4; ++index)
			{
				const double weight = weights[index] *
						samples[index].alpha;
				alpha += weight;
				red += weight * samples[index].red;
				green += weight * samples[index].green;
				blue += weight * samples[index].blue;
			}
			if (alpha > 0.0)
			{
				red /= alpha;
				green /= alpha;
				blue /= alpha;
			}
			result[destination++] = static_cast<uint8_t>(
					std::clamp(std::lround(blue), 0l, 255l));
			result[destination++] = static_cast<uint8_t>(
					std::clamp(std::lround(green), 0l, 255l));
			result[destination++] = static_cast<uint8_t>(
					std::clamp(std::lround(red), 0l, 255l));
			result[destination++] = static_cast<uint8_t>(
					std::clamp(std::lround(alpha * 255.0), 0l, 255l));
		}
	}
	return result;
}

void M_SetWallSurfaceTransform(SideDef &side, WallSurfacePart part,
		const SurfaceTransform &transform) noexcept
{
	switch (part)
	{
	case WallSurfacePart::upper:
		side.offsetx_top = transform.offsetX;
		side.offsety_top = transform.offsetY;
		side.scalex_top = transform.scaleX;
		side.scaley_top = transform.scaleY;
		return;

	case WallSurfacePart::middle:
		side.offsetx_mid = transform.offsetX;
		side.offsety_mid = transform.offsetY;
		side.scalex_mid = transform.scaleX;
		side.scaley_mid = transform.scaleY;
		return;

	case WallSurfacePart::lower:
		side.offsetx_bottom = transform.offsetX;
		side.offsety_bottom = transform.offsetY;
		side.scalex_bottom = transform.scaleX;
		side.scaley_bottom = transform.scaleY;
		return;
	}
}

void M_SetPlaneSurfaceTransform(Sector &sector, PlaneSurfacePart part,
		const SurfaceTransform &transform) noexcept
{
	if (part == PlaneSurfacePart::floor)
	{
		sector.xpanningfloor = transform.offsetX;
		sector.ypanningfloor = transform.offsetY;
		sector.xscalefloor = transform.scaleX;
		sector.yscalefloor = transform.scaleY;
		sector.rotationfloor = transform.rotation;
		return;
	}

	sector.xpanningceiling = transform.offsetX;
	sector.ypanningceiling = transform.offsetY;
	sector.xscaleceiling = transform.scaleX;
	sector.yscaleceiling = transform.scaleY;
	sector.rotationceiling = transform.rotation;
}

SurfaceTransformPreview::SurfaceTransformPreview(
		Document &document) noexcept :
	document_(document)
{
}

SurfaceTransformPreview::~SurfaceTransformPreview()
{
	clear();
}

bool SurfaceTransformPreview::active() const noexcept
{
	return !sidedefSnapshots_.empty() || !sectorSnapshots_.empty();
}

void SurfaceTransformPreview::clear() noexcept
{
	for (auto &[index, snapshot] : sidedefSnapshots_)
	{
		if (document_.isSidedef(index))
			*document_.sidedefs[index] = std::move(snapshot);
	}
	for (auto &[index, snapshot] : sectorSnapshots_)
	{
		if (document_.isSector(index))
			*document_.sectors[index] = std::move(snapshot);
	}
	sidedefSnapshots_.clear();
	sectorSnapshots_.clear();
}

bool SurfaceTransformPreview::apply(
		const std::vector<WallSurfacePreviewValue> &walls,
		const std::vector<PlaneSurfacePreviewValue> &planes)
{
	clear();

	for (const WallSurfacePreviewValue &wall : walls)
	{
		if (!document_.isSidedef(wall.sidedef))
			return false;
	}
	for (const PlaneSurfacePreviewValue &plane : planes)
	{
		if (!document_.isSector(plane.sector))
			return false;
	}

	std::set<int> capturedSides;
	for (const WallSurfacePreviewValue &wall : walls)
	{
		if (capturedSides.insert(wall.sidedef).second)
			sidedefSnapshots_.push_back(
					{wall.sidedef, *document_.sidedefs[wall.sidedef]});
	}
	std::set<int> capturedSectors;
	for (const PlaneSurfacePreviewValue &plane : planes)
	{
		if (capturedSectors.insert(plane.sector).second)
			sectorSnapshots_.push_back(
					{plane.sector, *document_.sectors[plane.sector]});
	}

	for (const WallSurfacePreviewValue &wall : walls)
	{
		SideDef &side = *document_.sidedefs[wall.sidedef];
		if (wall.setSharedOffsets)
		{
			side.x_offset = wall.sharedOffsetX;
			side.y_offset = wall.sharedOffsetY;
		}
		if (wall.setPartTransform)
			M_SetWallSurfaceTransform(side, wall.part, wall.transform);
		if (wall.forceUnsupportedPreview)
			side.preview_surface_transform = true;
	}
	for (const PlaneSurfacePreviewValue &plane : planes)
	{
		M_SetPlaneSurfaceTransform(*document_.sectors[plane.sector],
				plane.part, plane.transform);
		if (plane.forceUnsupportedPreview)
		{
			Sector &sector = *document_.sectors[plane.sector];
			if (plane.part == PlaneSurfacePart::floor)
				sector.preview_floor_transform = true;
			else
				sector.preview_ceiling_transform = true;
		}
	}
	return true;
}

void M_ChangeWallSurfaceTransform(EditOperation &operation, int sidedef,
		WallSurfacePart part, const SurfaceTransform &transform)
{
	const SideDef &current = *operation.doc.sidedefs[sidedef];
	const SurfaceTransform old = M_WallSurfaceTransform(current, part);
	switch (part)
	{
	case WallSurfacePart::upper:
		if (Different(old.offsetX, transform.offsetX))
			operation.changeSidedef(sidedef, &SideDef::offsetx_top, transform.offsetX);
		if (Different(old.offsetY, transform.offsetY))
			operation.changeSidedef(sidedef, &SideDef::offsety_top, transform.offsetY);
		if (Different(old.scaleX, transform.scaleX))
			operation.changeSidedef(sidedef, &SideDef::scalex_top, transform.scaleX);
		if (Different(old.scaleY, transform.scaleY))
			operation.changeSidedef(sidedef, &SideDef::scaley_top, transform.scaleY);
		return;

	case WallSurfacePart::middle:
		if (Different(old.offsetX, transform.offsetX))
			operation.changeSidedef(sidedef, &SideDef::offsetx_mid, transform.offsetX);
		if (Different(old.offsetY, transform.offsetY))
			operation.changeSidedef(sidedef, &SideDef::offsety_mid, transform.offsetY);
		if (Different(old.scaleX, transform.scaleX))
			operation.changeSidedef(sidedef, &SideDef::scalex_mid, transform.scaleX);
		if (Different(old.scaleY, transform.scaleY))
			operation.changeSidedef(sidedef, &SideDef::scaley_mid, transform.scaleY);
		return;

	case WallSurfacePart::lower:
		if (Different(old.offsetX, transform.offsetX))
			operation.changeSidedef(sidedef, &SideDef::offsetx_bottom, transform.offsetX);
		if (Different(old.offsetY, transform.offsetY))
			operation.changeSidedef(sidedef, &SideDef::offsety_bottom, transform.offsetY);
		if (Different(old.scaleX, transform.scaleX))
			operation.changeSidedef(sidedef, &SideDef::scalex_bottom, transform.scaleX);
		if (Different(old.scaleY, transform.scaleY))
			operation.changeSidedef(sidedef, &SideDef::scaley_bottom, transform.scaleY);
		return;
	}
}

void M_ChangePlaneSurfaceTransform(EditOperation &operation, int sector,
		PlaneSurfacePart part, const SurfaceTransform &transform)
{
	const Sector &current = *operation.doc.sectors[sector];
	const SurfaceTransform old = M_PlaneSurfaceTransform(current, part);
	if (part == PlaneSurfacePart::floor)
	{
		if (Different(old.offsetX, transform.offsetX))
			operation.changeSector(sector, &Sector::xpanningfloor, transform.offsetX);
		if (Different(old.offsetY, transform.offsetY))
			operation.changeSector(sector, &Sector::ypanningfloor, transform.offsetY);
		if (Different(old.scaleX, transform.scaleX))
			operation.changeSector(sector, &Sector::xscalefloor, transform.scaleX);
		if (Different(old.scaleY, transform.scaleY))
			operation.changeSector(sector, &Sector::yscalefloor, transform.scaleY);
		if (Different(old.rotation, transform.rotation))
			operation.changeSector(sector, &Sector::rotationfloor, transform.rotation);
		return;
	}

	if (Different(old.offsetX, transform.offsetX))
		operation.changeSector(sector, &Sector::xpanningceiling, transform.offsetX);
	if (Different(old.offsetY, transform.offsetY))
		operation.changeSector(sector, &Sector::ypanningceiling, transform.offsetY);
	if (Different(old.scaleX, transform.scaleX))
		operation.changeSector(sector, &Sector::xscaleceiling, transform.scaleX);
	if (Different(old.scaleY, transform.scaleY))
		operation.changeSector(sector, &Sector::yscaleceiling, transform.scaleY);
	if (Different(old.rotation, transform.rotation))
		operation.changeSector(sector, &Sector::rotationceiling, transform.rotation);
}
