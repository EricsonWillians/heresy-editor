//------------------------------------------------------------------------
//
//  Eureka DOOM Editor
//
//  Copyright (C) 2022 Ioan Chera
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

#ifndef SECTOR_H_
#define SECTOR_H_

#include "m_strings.h"
#include "UdmfProperty.h"

struct ConfigData;
enum class MapFormat;

// Valid range for sector floor/ceiling heights in a given map format.
// The binary Doom/Hexen formats store heights as int16, while UDMF keeps
// full 32-bit ints (engines such as GZDoom allow heights up to their own
// map coordinate limits).
int MinSectorHeight(MapFormat format) noexcept;
int MaxSectorHeight(MapFormat format) noexcept;

struct Sector
{
	int floorh = 0;
	int ceilh = 0;
	StringID floor_tex;
	StringID ceil_tex;
	int light = 0;
	int type = 0;
	int tag = 0;

	// UDMF plane texture transforms.  Scale values use the UDMF convention:
	// 1 is native 1:1 mapping and larger magnitudes make a tile repeat more
	// frequently.  Rotation is clockwise in degrees.
	double xpanningfloor = 0.0;
	double ypanningfloor = 0.0;
	double xscalefloor = 1.0;
	double yscalefloor = 1.0;
	double rotationfloor = 0.0;

	double xpanningceiling = 0.0;
	double ypanningceiling = 0.0;
	double xscaleceiling = 1.0;
	double yscaleceiling = 1.0;
	double rotationceiling = 0.0;

	// Transient editor-only projections used for always-live transform review.
	// They are deliberately absent from every map serializer.
	bool preview_floor_transform = false;
	bool preview_ceiling_transform = false;

	// Unrecognized UDMF extension fields, retained verbatim across saves.
	UdmfProperties udmf_properties;

	enum IntAddress
	{
		F_FLOORH,
		F_CEILH,
		F_LIGHT = 4,
		F_TYPE,
		F_TAG,
	};

	enum StringIDAddress
	{
		F_FLOOR_TEX = 2,
		F_CEIL_TEX = 3,
	};

	SString FloorTex() const noexcept;
	SString CeilTex() const noexcept;

	int HeadRoom() const
	{
		return ceilh - floorh;
	}

	void SetDefaults(const ConfigData &config);
};

#endif
