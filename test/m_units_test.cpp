//------------------------------------------------------------------------
//
//  Eureka DOOM Editor
//
//  Copyright (C) 2026 Ioan Chera
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

#include "m_units.h"
#include "gtest/gtest.h"

using units::System;

TEST(Units, DisabledWhenOffOrBadScale)
{
	ASSERT_TRUE(units::FormatLength(512.0, 32, System::off).empty());
	ASSERT_TRUE(units::FormatLength(512.0, 0, System::metric).empty());
	ASSERT_TRUE(units::FormatLength(512.0, -5, System::imperial).empty());
}

TEST(Units, MetricAutoScaling)
{
	// 32 map units = 1 meter
	ASSERT_STREQ(units::FormatLength(27.2, 32, System::metric).c_str(), "85 cm");
	ASSERT_STREQ(units::FormatLength(32.0, 32, System::metric).c_str(), "1.0 m");
	ASSERT_STREQ(units::FormatLength(512.0, 32, System::metric).c_str(), "16.0 m");
	ASSERT_STREQ(units::FormatLength(16384.0, 32, System::metric).c_str(), "512.0 m");
	ASSERT_STREQ(units::FormatLength(32000.0, 32, System::metric).c_str(), "1.00 km");
	ASSERT_STREQ(units::FormatLength(320000.0, 32, System::metric).c_str(), "10.00 km");
}

TEST(Units, MetricThresholds)
{
	// just below 1 meter shows cm, exactly 1 meter shows m
	ASSERT_STREQ(units::FormatLength(31.9, 32, System::metric).c_str(), "100 cm");
	ASSERT_STREQ(units::FormatLength(31999.9, 32, System::metric).c_str(), "1000.0 m");
	ASSERT_STREQ(units::FormatLength(32000.0, 32, System::metric).c_str(), "1.00 km");
}

TEST(Units, ImperialAutoScaling)
{
	// 8 units at 32 units/m = 0.25 m = 0.82 ft = 9.84 in
	ASSERT_STREQ(units::FormatLength(8.0, 32, System::imperial).c_str(), "10 in");
	// 512 units = 16 m = 52.49 ft
	ASSERT_STREQ(units::FormatLength(512.0, 32, System::imperial).c_str(), "52.5 ft");
	// 50000 units = 1562.5 m = 5126.3 ft (just under a mile)
	ASSERT_STREQ(units::FormatLength(50000.0, 32, System::imperial).c_str(), "5126.3 ft");
	// 51600 units = 1612.5 m = 5290.4 ft = 1.002 miles
	ASSERT_STREQ(units::FormatLength(51600.0, 32, System::imperial).c_str(), "1.00 mi");
}

TEST(Units, CustomScale)
{
	// 16 units per meter: 512 units = 32 m
	ASSERT_STREQ(units::FormatLength(512.0, 16, System::metric).c_str(), "32.0 m");
	// 64 units per meter: 512 units = 8 m
	ASSERT_STREQ(units::FormatLength(512.0, 64, System::metric).c_str(), "8.0 m");
}

TEST(Units, ZeroLength)
{
	ASSERT_STREQ(units::FormatLength(0.0, 32, System::metric).c_str(), "0 cm");
	ASSERT_STREQ(units::FormatLength(0.0, 32, System::imperial).c_str(), "0 in");
}

TEST(Units, NegativeLengthKeepsSign)
{
	ASSERT_STREQ(units::FormatLength(-512.0, 32, System::metric).c_str(), "-16.0 m");
	ASSERT_STREQ(units::FormatLength(-32000.0, 32, System::metric).c_str(), "-1.00 km");
}
