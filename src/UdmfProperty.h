//------------------------------------------------------------------------
//  UNRECOGNIZED UDMF OBJECT PROPERTIES
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

#ifndef UDMF_PROPERTY_H_
#define UDMF_PROPERTY_H_

#include "m_strings.h"

#include <vector>

// The parser has already validated both entries as single UDMF tokens.  Keep
// the original spelling so extension fields survive without the editor having
// to understand or normalize their engine-specific semantics.
struct UdmfProperty
{
	SString name;
	SString value;
};

using UdmfProperties = std::vector<UdmfProperty>;

#endif
