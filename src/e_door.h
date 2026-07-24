//------------------------------------------------------------------------
//  SMART DOOR AUTHORING
//------------------------------------------------------------------------

#ifndef __EUREKA_E_DOOR_H__
#define __EUREKA_E_DOOR_H__

#include "Side.h"
#include "Thing.h"
#include "m_game.h"
#include "m_select.h"

#include <vector>

struct Document;
class ImageSet;
class EditOperation;

enum class DoorIssueSeverity
{
	warning,
	error
};

struct DoorOptions
{
	SString presetId;
	SString faceTexture;
	SString trackTexture;

	void useAutoFaceTexture()
	{
		faceTexture.clear();
	}

	void useAutoTrackTexture()
	{
		trackTexture.clear();
	}
};

struct DoorIssue
{
	DoorIssueSeverity severity = DoorIssueSeverity::warning;
	int sector = -1;
	int line = -1;
	SString message;
};

struct DoorPortalChange
{
	int sector = -1;
	int line = -1;
	Side doorSide = Side::neither;
	Side outsideSide = Side::neither;
	bool flip = false;
};

struct DoorTrackChange
{
	int sector = -1;
	int line = -1;
	Side doorSide = Side::neither;
};

struct DoorPlan
{
	std::vector<int> sectors;
	std::vector<DoorPortalChange> portals;
	std::vector<DoorTrackChange> tracks;
	std::vector<int> portalLines;
	std::vector<int> trackLines;
	std::vector<int> requiredFlips;
	std::vector<DoorIssue> issues;

	DoorPreset preset;
	SString presetLabel;
	SString inferredFaceTexture;
	SString inferredTrackTexture;
	SString faceTexture;
	SString trackTexture;

	bool valid() const;
};

std::vector<DoorPreset> M_AvailableDoorPresets(const ConfigData &config,
											   MapFormat format);
SString M_DoorPresetLabel(const ConfigData &config, const DoorPreset &preset);

DoorPlan M_PlanSmartDoors(const Document &doc, const ConfigData &config,
						  const ImageSet *images, MapFormat format,
						  const selection_c &selection,
						  const DoorOptions &options);

bool M_ApplySmartDoors(Document &doc, const ConfigData &config,
					   const ImageSet *images, MapFormat format,
					   const selection_c &selection,
					   const DoorOptions &options,
					   DoorPlan *appliedPlan = nullptr);

void M_SetLineArguments(EditOperation &op, int line,
						const std::array<int, 5> &arguments);
void M_SetLineActivation(EditOperation &op, int line, MapFormat format,
						 ActivationPolicy activation);
void M_ClearLineActivation(EditOperation &op, int line, MapFormat format);
bool M_AppendSmartDoors(EditOperation &op, const DoorPlan &plan,
						MapFormat format);

#endif
