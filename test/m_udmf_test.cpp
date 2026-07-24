//------------------------------------------------------------------------
//  UDMF LOAD/SAVE TESTS
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

#include "testUtils/TempDirContext.hpp"

#include "gtest/gtest.h"

#include "Instance.h"
#include "m_game.h"
#include "m_loadsave.h"
#include "w_rawdef.h"
#include "w_texture.h"
#include "w_wad.h"

#include <cstring>

class UdmfTest : public TempDirContext
{
protected:
	Instance instance;

	void SetUp() override
	{
		TempDirContext::SetUp();
		instance.Editor_Init();
	}

	std::shared_ptr<Wad_file> wadWithTextMap(const fs::path &path,
			const char *text)
	{
		auto wad = Wad_file::Open(path, WadOpenMode::write);
		EXPECT_TRUE(wad);
		if (!wad)
			return nullptr;

		wad->AddLevel("MAP01");
		Lump_c &textmap = wad->AddLump("TEXTMAP");
		textmap.Write(text, static_cast<int>(strlen(text)));
		wad->AddLump("ENDMAP");
		return wad;
	}

	static const SString *findProperty(const UdmfProperties &properties,
			const char *name)
	{
		for (const UdmfProperty &property : properties)
			if (property.name.noCaseEqual(name))
				return &property.value;
		return nullptr;
	}

	static std::string lumpText(const Lump_c &lump)
	{
		const std::vector<byte> &data = lump.getData();
		return std::string(reinterpret_cast<const char *>(data.data()), data.size());
	}
};

TEST_F(UdmfTest, LineSplitsCopyPreservedPropertiesWithTheirObjects)
{
	Document document(instance);
	for (double x : {0.0, 128.0, 64.0})
	{
		auto vertex = std::make_unique<Vertex>();
		vertex->SetRawXY(MapFormat::udmf, {x, 0.0});
		document.vertices.push_back(std::move(vertex));
	}

	auto sector = std::make_shared<Sector>();
	sector->udmf_properties.push_back({"lightcolor", "16755200"});
	document.sectors.push_back(std::move(sector));

	auto side = std::make_shared<SideDef>();
	side->sector = 0;
	side->upper_tex = BA_InternaliseString("-");
	side->mid_tex = BA_InternaliseString("STARTAN3");
	side->lower_tex = BA_InternaliseString("-");
	side->udmf_properties.push_back({"scalex_mid", "1.25"});
	document.sidedefs.push_back(std::move(side));

	auto line = std::make_shared<LineDef>();
	line->start = 0;
	line->end = 1;
	line->right = 0;
	line->udmf_properties.push_back({"locknumber", "3"});
	document.linedefs.push_back(std::move(line));

	int newLine;
	{
		EditOperation operation(document.basis);
		newLine = document.linemod.splitLinedefAtVertex(operation, 0, 2);
	}

	ASSERT_EQ(document.numLinedefs(), 2);
	ASSERT_EQ(document.numSidedefs(), 2);
	ASSERT_GE(newLine, 0);
	for (const auto &splitLine : document.linedefs)
	{
		const SString *value = findProperty(splitLine->udmf_properties,
				"locknumber");
		ASSERT_NE(value, nullptr);
		EXPECT_EQ(*value, "3");
	}
	for (const auto &splitSide : document.sidedefs)
	{
		const SString *value = findProperty(splitSide->udmf_properties,
				"scalex_mid");
		ASSERT_NE(value, nullptr);
		EXPECT_EQ(*value, "1.25");
	}
	EXPECT_NE(findProperty(document.sectors[0]->udmf_properties,
			"lightcolor"), nullptr);
}

TEST_F(UdmfTest, PreservesBiasedDoomGeneratorPropertiesAcrossEditsAndReload)
{
	static const char source[] = R"UDMF(namespace = "zdoom";

vertex { x = 0.0; y = 0.0; }
vertex { x = 128.0; y = 0.0; }

sector
{
	heightfloor = 0;
	heightceiling = 128;
	texturefloor = "FLOOR0_1";
	textureceiling = "CEIL1_1";
	lightlevel = 160;
	xpanningfloor = 4096.5;
	ypanningfloor = -2048.25;
	xscalefloor = 32.0;
	yscalefloor = 0.125;
	rotationfloor = 37.5;
	lightcolor = 16755200;
	fadecolor = 1122867;
	damageamount = 7;
	damageinterval = 16;
	damagetype = "Fire";
	leakiness = 256;
	damageterraineffect = true;
}

sidedef
{
	sector = 0;
	texturemiddle = "STARTAN3";
	scaley_top = 0.750000;
	scalex_mid = 1.250000;
	scaley_mid = 0.500000;
}

linedef
{
	v1 = 0;
	v2 = 1;
	sidefront = 0;
	blocking = true;
	playeruse = true;
	locknumber = 3;
	blockplayers = false;
}
)UDMF";

	auto input = wadWithTextMap(getSubPath("input.wad"), source);
	ASSERT_TRUE(input);
	Document document(instance);
	LoadingData loading;
	loading.levelFormat = MapFormat::udmf;
	BadCount bad{};
	instance.UDMF_LoadLevel(0, input.get(), document, loading, bad);

	ASSERT_EQ(document.numLinedefs(), 1);
	ASSERT_EQ(document.numSidedefs(), 1);
	ASSERT_EQ(document.numSectors(), 1);
	EXPECT_EQ(loading.udmfNamespace, "zdoom");

	const SString *lockNumber = findProperty(
			document.linedefs[0]->udmf_properties, "locknumber");
	ASSERT_NE(lockNumber, nullptr);
	EXPECT_EQ(*lockNumber, "3");
	EXPECT_EQ(document.linedefs[0]->udmf_properties.size(), 2u);
	EXPECT_TRUE(document.sidedefs[0]->udmf_properties.empty());
	EXPECT_EQ(document.sectors[0]->udmf_properties.size(), 7u);
	EXPECT_DOUBLE_EQ(document.sidedefs[0]->scaley_top, 0.75);
	EXPECT_DOUBLE_EQ(document.sidedefs[0]->scalex_mid, 1.25);
	EXPECT_DOUBLE_EQ(document.sidedefs[0]->scaley_mid, 0.5);
	EXPECT_DOUBLE_EQ(document.sectors[0]->xpanningfloor, 4096.5);
	EXPECT_DOUBLE_EQ(document.sectors[0]->ypanningfloor, -2048.25);
	EXPECT_DOUBLE_EQ(document.sectors[0]->xscalefloor, 32.0);
	EXPECT_DOUBLE_EQ(document.sectors[0]->yscalefloor, 0.125);
	EXPECT_DOUBLE_EQ(document.sectors[0]->rotationfloor, 37.5);

	// Model ordinary editor changes to recognized fields before saving.
	document.linedefs[0]->type = 12;
	document.sidedefs[0]->x_offset = 8;
	document.sectors[0]->floorh = 16;

	auto output = Wad_file::Open(getSubPath("output.wad"), WadOpenMode::write);
	ASSERT_TRUE(output);
	output->AddLevel("MAP01");
	instance.UDMF_SaveLevel(loading, *output, document);
	const Lump_c *savedTextmap = output->FindLump("TEXTMAP");
	ASSERT_NE(savedTextmap, nullptr);
	const std::string saved = lumpText(*savedTextmap);

	for (const char *expected : {
			"locknumber = 3;", "blockplayers = false;",
			"scaley_top = 0.75;",
			"scalex_mid = 1.25;", "scaley_mid = 0.5;",
			"xpanningfloor = 4096.5;", "ypanningfloor = -2048.25;",
			"xscalefloor = 32;", "yscalefloor = 0.125;",
			"rotationfloor = 37.5;",
			"lightcolor = 16755200;", "fadecolor = 1122867;",
			"damageamount = 7;", "damageinterval = 16;",
			"damagetype = \"Fire\";", "leakiness = 256;",
			"damageterraineffect = true;"})
	{
		EXPECT_NE(saved.find(expected), std::string::npos) << expected;
	}
	EXPECT_NE(saved.find("special = 12;"), std::string::npos);
	EXPECT_NE(saved.find("offsetx = 8;"), std::string::npos);
	EXPECT_NE(saved.find("heightfloor = 16;"), std::string::npos);

	Document reloaded(instance);
	LoadingData reloadedLoading;
	reloadedLoading.levelFormat = MapFormat::udmf;
	BadCount reloadedBad{};
	instance.UDMF_LoadLevel(0, output.get(), reloaded, reloadedLoading,
			reloadedBad);
	ASSERT_EQ(reloaded.numLinedefs(), 1);
	ASSERT_EQ(reloaded.numSidedefs(), 1);
	ASSERT_EQ(reloaded.numSectors(), 1);
	EXPECT_EQ(reloaded.linedefs[0]->udmf_properties.size(), 2u);
	EXPECT_TRUE(reloaded.sidedefs[0]->udmf_properties.empty());
	EXPECT_EQ(reloaded.sectors[0]->udmf_properties.size(), 7u);
	EXPECT_DOUBLE_EQ(reloaded.sidedefs[0]->scaley_top, 0.75);
	EXPECT_DOUBLE_EQ(reloaded.sidedefs[0]->scalex_mid, 1.25);
	EXPECT_DOUBLE_EQ(reloaded.sidedefs[0]->scaley_mid, 0.5);
	EXPECT_DOUBLE_EQ(reloaded.sectors[0]->xpanningfloor, 4096.5);
	EXPECT_DOUBLE_EQ(reloaded.sectors[0]->ypanningfloor, -2048.25);
	EXPECT_DOUBLE_EQ(reloaded.sectors[0]->xscalefloor, 32.0);
	EXPECT_DOUBLE_EQ(reloaded.sectors[0]->yscalefloor, 0.125);
	EXPECT_DOUBLE_EQ(reloaded.sectors[0]->rotationfloor, 37.5);

	auto expectProperty = [](const UdmfProperties &properties,
			const char *name, const char *value)
	{
		const SString *actual = findProperty(properties, name);
		ASSERT_NE(actual, nullptr) << name;
		EXPECT_EQ(*actual, value) << name;
	};
	expectProperty(reloaded.linedefs[0]->udmf_properties, "locknumber", "3");
	expectProperty(reloaded.linedefs[0]->udmf_properties, "blockplayers", "false");
	for (const auto &[name, value] : {
			std::pair{"lightcolor", "16755200"},
			std::pair{"fadecolor", "1122867"},
			std::pair{"damageamount", "7"},
			std::pair{"damageinterval", "16"},
			std::pair{"damagetype", "\"Fire\""},
			std::pair{"leakiness", "256"},
			std::pair{"damageterraineffect", "true"}})
		expectProperty(reloaded.sectors[0]->udmf_properties, name, value);
}

TEST_F(UdmfTest, ClassicSerializersIgnoreUdmfProperties)
{
	auto populate = [](Document &document, bool withProperties)
	{
		auto sector = std::make_shared<Sector>();
		sector->floorh = 0;
		sector->ceilh = 128;
		sector->floor_tex = BA_InternaliseString("FLOOR0_1");
		sector->ceil_tex = BA_InternaliseString("CEIL1_1");
		sector->light = 160;
		if (withProperties)
		{
			sector->udmf_properties.push_back({"lightcolor", "16755200"});
			sector->xpanningfloor = 4096.5;
			sector->xscalefloor = 32.0;
			sector->rotationfloor = 37.5;
		}
		document.sectors.push_back(std::move(sector));

		auto side = std::make_shared<SideDef>();
		side->sector = 0;
		side->upper_tex = BA_InternaliseString("-");
		side->mid_tex = BA_InternaliseString("STARTAN3");
		side->lower_tex = BA_InternaliseString("-");
		if (withProperties)
		{
			side->udmf_properties.push_back({"scalex_mid", "1.25"});
			side->offsetx_mid = 12.5;
			side->scalex_mid = 64.0;
		}
		document.sidedefs.push_back(std::move(side));

		auto line = std::make_shared<LineDef>();
		line->start = 0;
		line->end = 1;
		line->right = 0;
		line->flags = MLF_Blocking;
		if (withProperties)
			line->udmf_properties.push_back({"locknumber", "3"});
		document.linedefs.push_back(std::move(line));
	};

	auto serialize = [this](Document &document, const fs::path &path,
			bool hexen)
	{
		auto wad = Wad_file::Open(path, WadOpenMode::write);
		EXPECT_TRUE(wad);
		if (!wad)
			return std::vector<std::vector<byte>>{};
		if (hexen)
			document.SaveLineDefs_Hexen(*wad);
		else
			document.SaveLineDefs(*wad);
		document.SaveSideDefs(*wad);
		document.SaveSectors(*wad);
		std::vector<std::vector<byte>> result;
		for (const char *name : {"LINEDEFS", "SIDEDEFS", "SECTORS"})
		{
			const Lump_c *lump = wad->FindLump(name);
			EXPECT_NE(lump, nullptr);
			if (lump)
				result.push_back(lump->getData());
		}
		return result;
	};

	Document plain(instance);
	Document extended(instance);
	populate(plain, false);
	populate(extended, true);
	EXPECT_EQ(serialize(plain, getSubPath("plain-doom.wad"), false),
			serialize(extended, getSubPath("extended-doom.wad"), false));
	EXPECT_EQ(serialize(plain, getSubPath("plain-hexen.wad"), true),
			serialize(extended, getSubPath("extended-hexen.wad"), true));
}
