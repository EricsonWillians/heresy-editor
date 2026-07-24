//------------------------------------------------------------------------
//  TRANSIENT DESIGN-ASSIST PREVIEW DATA
//------------------------------------------------------------------------

#ifndef __EUREKA_E_DESIGN_H__
#define __EUREKA_E_DESIGN_H__

#include "m_strings.h"
#include "m_vector.h"
#include "objid.h"

#include <vector>

enum class DesignPreviewRole
{
	proposed,
	retained,
	opening,
	door,
	track,
	stair,
	lift,
	architecture,
	cut,
	anchor,
	warning,
	conflict
};

struct DesignPreviewPath
{
	std::vector<v2double_t> points;
	DesignPreviewRole role = DesignPreviewRole::proposed;
	bool closed = true;
	bool filled = false;
};

struct DesignPreviewPoint
{
	v2double_t position;
	DesignPreviewRole role = DesignPreviewRole::anchor;
};

struct DesignPreviewLabel
{
	v2double_t position;
	SString text;
	DesignPreviewRole role = DesignPreviewRole::proposed;
};

#endif
