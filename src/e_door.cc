//------------------------------------------------------------------------
//  SMART DOOR AUTHORING
//------------------------------------------------------------------------

#include "e_door.h"

#include "Document.h"
#include "WadData.h"
#include "WindowsSanitization.h"
#include "w_rawdef.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace
{

constexpr unsigned UDMF_ACTIVATIONS =
		MLF_UDMF_playercross | MLF_UDMF_playeruse |
		MLF_UDMF_monstercross | MLF_UDMF_monsteruse |
		MLF_UDMF_impact | MLF_UDMF_playerpush |
		MLF_UDMF_monsterpush | MLF_UDMF_missilecross |
		MLF_UDMF_repeatspecial;

void AddIssue(DoorPlan &plan, DoorIssueSeverity severity, const SString &message,
			  int sector = -1, int line = -1)
{
	plan.issues.push_back({severity, sector, line, message});
}

bool ValidTexture(const SString &texture)
{
	return !texture.empty() && !is_null_tex(texture);
}

struct TextureCandidate
{
	SString texture;
	double weight = 0;
	int firstLine = -1;
};

class TextureWeights
{
public:
	void add(const SString &texture, double weight, int line)
	{
		if (!ValidTexture(texture))
			return;

		SString key = texture.asUpper();
		TextureCandidate &candidate = candidates[key];
		if (candidate.firstLine < 0)
		{
			candidate.texture = texture;
			candidate.firstLine = line;
		}
		candidate.weight += std::max(0.0, weight);
		candidate.firstLine = std::min(candidate.firstLine, line);
	}

	bool empty() const
	{
		return candidates.empty();
	}

	SString best() const
	{
		const TextureCandidate *winner = nullptr;
		for (const auto &[key, candidate] : candidates)
		{
			if (!winner ||
				candidate.weight > winner->weight ||
				(std::abs(candidate.weight - winner->weight) < 0.0001 &&
				 candidate.firstLine < winner->firstLine))
			{
				winner = &candidate;
			}
		}
		return winner ? winner->texture : SString();
	}

private:
	std::map<SString, TextureCandidate> candidates;
};

bool PresetSupported(const ConfigData &config, const DoorPreset &preset,
					 MapFormat format)
{
	if (config.line_types.find(preset.special) == config.line_types.end())
		return false;

	if (preset.activation == DoorActivation::encoded)
		return format == MapFormat::doom;

	if (format != MapFormat::hexen && format != MapFormat::udmf)
		return false;

	if (format == MapFormat::hexen)
	{
		if (preset.special < 0 || preset.special > 255)
			return false;
		for (int argument : preset.args)
			if (argument < 0 || argument > 255)
				return false;
	}

	return true;
}

const DoorPreset *FindPreset(const std::vector<DoorPreset> &presets,
							 const SString &id)
{
	auto found = std::find_if(presets.begin(), presets.end(),
			[&](const DoorPreset &preset)
			{
				return preset.id.noCaseEqual(id);
			});
	return found == presets.end() ? nullptr : &*found;
}

bool SideReferenceValid(const Document &doc, int side)
{
	return side < 0 || doc.isSidedef(side);
}

int SectorForSide(const Document &doc, int side)
{
	if (!doc.isSidedef(side))
		return -1;
	int sector = doc.sidedefs[side]->sector;
	return doc.isSector(sector) ? sector : -1;
}

int SidedefReferenceCount(const Document &doc, int side)
{
	int references = 0;
	for (const auto &linedef : doc.linedefs)
	{
		if (linedef->right == side)
			references++;
		if (linedef->left == side)
			references++;
	}
	return references;
}

void AddUnique(std::vector<int> &values, int value)
{
	if (std::find(values.begin(), values.end(), value) == values.end())
		values.push_back(value);
}

int IsolateSidedef(EditOperation &op, int line, Side side)
{
	LineDef &target = *op.doc.linedefs[line];
	int sideNum = target.WhatSideDef(side);
	if (!op.doc.isSidedef(sideNum))
		return -1;

	int references = 0;
	for (const auto &candidate : op.doc.linedefs)
	{
		if (candidate->right == sideNum)
			references++;
		if (candidate->left == sideNum)
			references++;
	}

	if (references <= 1)
		return sideNum;

	int copy = op.addNew(ObjType::sidedefs);
	*op.doc.sidedefs[copy] = *op.doc.sidedefs[sideNum];
	if (side == Side::right)
		op.changeLinedef(line, &LineDef::right, copy);
	else
		op.changeLinedef(line, &LineDef::left, copy);
	return copy;
}

} // namespace

void M_SetLineArguments(EditOperation &op, int line,
						const std::array<int, 5> &arguments)
{
	op.changeLinedef(line, &LineDef::arg1, arguments[0]);
	op.changeLinedef(line, &LineDef::arg2, arguments[1]);
	op.changeLinedef(line, &LineDef::arg3, arguments[2]);
	op.changeLinedef(line, &LineDef::arg4, arguments[3]);
	op.changeLinedef(line, &LineDef::arg5, arguments[4]);
}

void M_SetLineActivation(EditOperation &op, int line, MapFormat format,
						 ActivationPolicy activation)
{
	LineDef &linedef = *op.doc.linedefs[line];
	int flags = linedef.flags;
	unsigned udmfFlags = linedef.udmfFlags;

	if (format == MapFormat::hexen)
	{
		flags &= ~(MLF_Activation | MLF_Repeatable);
		udmfFlags &= ~UDMF_ACTIVATIONS;
		flags |= SPAC_Use << 10;
		if (activation == ActivationPolicy::useRepeat)
			flags |= MLF_Repeatable;
	}
	else if (format == MapFormat::udmf)
	{
		flags &= ~(MLF_Activation | MLF_Repeatable);
		udmfFlags &= ~UDMF_ACTIVATIONS;
		udmfFlags |= MLF_UDMF_playeruse;
		if (activation == ActivationPolicy::useRepeat)
			udmfFlags |= MLF_UDMF_repeatspecial;
	}

	op.changeLinedef(line, &LineDef::flags, flags);
	op.changeLinedef(line, &LineDef::udmfFlags, udmfFlags);
}

void M_ClearLineActivation(EditOperation &op, int line, MapFormat format)
{
	LineDef &linedef = *op.doc.linedefs[line];
	int flags = linedef.flags;
	if (format != MapFormat::doom)
		flags &= ~(MLF_Activation | MLF_Repeatable);
	op.changeLinedef(line, &LineDef::flags, flags);
	op.changeLinedef(line, &LineDef::udmfFlags,
					 linedef.udmfFlags & ~UDMF_ACTIVATIONS);
}

bool DoorPlan::valid() const
{
	return std::none_of(issues.begin(), issues.end(),
			[](const DoorIssue &issue)
			{
				return issue.severity == DoorIssueSeverity::error;
			});
}

std::vector<DoorPreset> M_AvailableDoorPresets(const ConfigData &config,
											   MapFormat format)
{
	std::vector<DoorPreset> result;
	for (const DoorPreset &preset : config.door_presets)
		if (PresetSupported(config, preset, format))
			result.push_back(preset);
	return result;
}

SString M_DoorPresetLabel(const ConfigData &config, const DoorPreset &preset)
{
	if (!preset.label.noCaseEqual("@special"))
		return preset.label;

	auto found = config.line_types.find(preset.special);
	return found == config.line_types.end() ? preset.id : found->second.desc;
}

DoorPlan M_PlanSmartDoors(const Document &doc, const ConfigData &config,
						  const ImageSet *images, MapFormat format,
						  const selection_c &selection,
						  const DoorOptions &options)
{
	DoorPlan plan;
	if (selection.what_type() != ObjType::sectors)
	{
		AddIssue(plan, DoorIssueSeverity::error,
				 "Smart Door requires a sector selection.");
		return plan;
	}

	for (int sector : selection.asArray())
	{
		if (!doc.isSector(sector))
			AddIssue(plan, DoorIssueSeverity::error,
					 SString::printf("Sector #%d does not exist.", sector), sector);
		else
			plan.sectors.push_back(sector);
	}

	if (plan.sectors.empty())
	{
		AddIssue(plan, DoorIssueSeverity::error,
				 "Select at least one sector to make a smart door.");
		return plan;
	}

	std::vector<DoorPreset> available = M_AvailableDoorPresets(config, format);
	if (available.empty())
	{
		AddIssue(plan, DoorIssueSeverity::error,
				 "Smart Door is unavailable for this game and map format.");
		return plan;
	}

	const DoorPreset *preset = FindPreset(available, options.presetId);
	if (!preset)
	{
		AddIssue(plan, DoorIssueSeverity::error,
				 options.presetId.empty() ?
				 "Choose a door preset." :
				 SString::printf("Door preset '%s' is unavailable.",
								 options.presetId.c_str()));
		return plan;
	}
	plan.preset = *preset;
	plan.presetLabel = M_DoorPresetLabel(config, *preset);

	std::set<int> selected(plan.sectors.begin(), plan.sectors.end());
	std::map<int, std::vector<DoorPortalChange>> sectorPortals;
	std::map<int, std::vector<DoorTrackChange>> sectorTracks;
	std::map<int, std::vector<int>> sectorBoundaries;
	std::map<int, std::set<int>> neighbors;
	std::set<int> reportedAdjacentLines;
	std::set<int> reportedMalformedLines;
	std::set<int> reportedSharedLines;

	for (int sector : plan.sectors)
	{
		const Sector &door = *doc.sectors[sector];
		if (door.ceilh < door.floorh)
			AddIssue(plan, DoorIssueSeverity::error,
					 "The sector ceiling is below its floor.", sector);
	}

	for (int line = 0; line < doc.numLinedefs(); line++)
	{
		const LineDef &linedef = *doc.linedefs[line];
		int rightSector = SectorForSide(doc, linedef.right);
		int leftSector = SectorForSide(doc, linedef.left);
		bool rightSelected = selected.count(rightSector) != 0;
		bool leftSelected = selected.count(leftSector) != 0;

		if (!rightSelected && !leftSelected)
			continue;

		if (!SideReferenceValid(doc, linedef.right) ||
			!SideReferenceValid(doc, linedef.left) ||
			!doc.isVertex(linedef.start) || !doc.isVertex(linedef.end) ||
			linedef.start == linedef.end ||
			(linedef.right >= 0 && rightSector < 0) ||
			(linedef.left >= 0 && leftSector < 0) ||
			(linedef.left >= 0 && linedef.right < 0))
		{
			if (reportedMalformedLines.insert(line).second)
				AddIssue(plan, DoorIssueSeverity::error,
						 "A boundary has invalid geometry, sidedef, or sector references.",
						 rightSelected ? rightSector : leftSector, line);
			continue;
		}

		if (rightSelected && leftSelected)
		{
			if (rightSector == leftSector)
				AddIssue(plan, DoorIssueSeverity::error,
						 "The sector has a self-referencing boundary.",
						 rightSector, line);
			else if (reportedAdjacentLines.insert(line).second)
				AddIssue(plan, DoorIssueSeverity::error,
						 "Selected door sectors may not be adjacent.",
						 rightSector, line);
			continue;
		}

		int doorSector = rightSelected ? rightSector : leftSector;
		Side doorSide = rightSelected ? Side::right : Side::left;

		if (linedef.TwoSided())
		{
			int outsideSector = rightSelected ? leftSector : rightSector;
			if (outsideSector == doorSector || doc.isSelfRef(linedef))
			{
				AddIssue(plan, DoorIssueSeverity::error,
						 "The sector has a self-referencing boundary.",
						 doorSector, line);
				continue;
			}

			DoorPortalChange portal;
			portal.sector = doorSector;
			portal.line = line;
			portal.doorSide = doorSide;
			portal.outsideSide = rightSelected ? Side::left : Side::right;
			portal.flip = rightSelected;
			sectorPortals[doorSector].push_back(portal);
			sectorBoundaries[doorSector].push_back(line);
			neighbors[doorSector].insert(outsideSector);
		}
		else
		{
			DoorTrackChange track;
			track.sector = doorSector;
			track.line = line;
			track.doorSide = doorSide;
			sectorTracks[doorSector].push_back(track);
			sectorBoundaries[doorSector].push_back(line);
		}
	}

	TextureWeights portalFaceWeights;
	TextureWeights trackWeights;

	for (int sector : plan.sectors)
	{
		const auto &portals = sectorPortals[sector];
		const auto &tracks = sectorTracks[sector];
		const auto &boundaries = sectorBoundaries[sector];

		std::map<int, int> vertexDegree;
		std::map<int, std::vector<int>> connectedVertices;
		for (int line : boundaries)
		{
			const LineDef &linedef = *doc.linedefs[line];
			vertexDegree[linedef.start]++;
			vertexDegree[linedef.end]++;
			connectedVertices[linedef.start].push_back(linedef.end);
			connectedVertices[linedef.end].push_back(linedef.start);
		}
		bool closedLoop = !vertexDegree.empty() &&
				std::all_of(vertexDegree.begin(), vertexDegree.end(),
						[](const auto &entry)
						{
							return entry.second == 2;
						});
		if (closedLoop)
		{
			std::set<int> visited;
			std::vector<int> pending = {vertexDegree.begin()->first};
			while (!pending.empty())
			{
				int vertex = pending.back();
				pending.pop_back();
				if (!visited.insert(vertex).second)
					continue;
				for (int next : connectedVertices[vertex])
					pending.push_back(next);
			}
			closedLoop = visited.size() == vertexDegree.size();
		}
		if (!closedLoop)
			AddIssue(plan, DoorIssueSeverity::error,
					 "The sector boundary does not form one closed loop.",
					 sector);

		if (portals.size() < 2)
			AddIssue(plan, DoorIssueSeverity::error,
					 "A door sector needs at least two two-sided portal boundaries.",
					 sector);
		if (portals.size() > 2)
			AddIssue(plan, DoorIssueSeverity::warning,
					 "This sector has an unusual split or multi-portal shape.",
					 sector);
		else if (!tracks.empty() && tracks.size() != 2)
			AddIssue(plan, DoorIssueSeverity::warning,
					 "This sector has an unusual boundary shape.", sector);
		if (neighbors[sector].size() > 2)
			AddIssue(plan, DoorIssueSeverity::warning,
					 "This sector connects to more than two neighboring sectors.",
					 sector);
		if (tracks.empty())
			AddIssue(plan, DoorIssueSeverity::warning,
					 "No one-sided track walls were found.", sector);

		const Sector &door = *doc.sectors[sector];
		int openCeiling = INT_MAX;
		for (const DoorPortalChange &portal : portals)
		{
			const LineDef &linedef = *doc.linedefs[portal.line];
			int outside = doc.getSectorID(linedef, portal.outsideSide);
			if (doc.isSector(outside))
			{
				const Sector &neighbor = *doc.sectors[outside];
				openCeiling = std::min(openCeiling, neighbor.ceilh - 4);
				if (neighbor.floorh != door.floorh)
					AddIssue(plan, DoorIssueSeverity::warning,
							 "The neighboring sector has a different floor height.",
							 sector, portal.line);
			}
			if (linedef.type != 0)
				AddIssue(plan, DoorIssueSeverity::warning,
						 "An existing line special will be replaced.",
						 sector, portal.line);
			if ((SidedefReferenceCount(doc, linedef.right) > 1 ||
				 SidedefReferenceCount(doc, linedef.left) > 1) &&
				reportedSharedLines.insert(portal.line).second)
				AddIssue(plan, DoorIssueSeverity::warning,
						 "Shared sidedefs on this line will be isolated.",
						 sector, portal.line);

			const SideDef *outsideSide = doc.getSide(linedef, portal.outsideSide);
			if (outsideSide)
			{
				SString candidate = outsideSide->UpperTex();
				if (!ValidTexture(candidate))
					candidate = outsideSide->MidTex();
				portalFaceWeights.add(candidate, doc.calcLength(linedef),
									  portal.line);
			}
		}

		if (openCeiling != INT_MAX &&
			openCeiling - door.floorh < config.miscInfo.player_h)
			AddIssue(plan, DoorIssueSeverity::warning,
					 "The inferred open door has inadequate player clearance.",
					 sector);

		for (const DoorTrackChange &track : tracks)
		{
			const LineDef &linedef = *doc.linedefs[track.line];
			const SideDef *side = doc.getSide(linedef, track.doorSide);
			if (side)
				trackWeights.add(side->MidTex(), doc.calcLength(linedef),
								 track.line);
			if (linedef.type != 0)
				AddIssue(plan, DoorIssueSeverity::warning,
						 "An existing line special will be replaced.",
						 sector, track.line);
			if (SidedefReferenceCount(doc,
						linedef.WhatSideDef(track.doorSide)) > 1 &&
				reportedSharedLines.insert(track.line).second)
				AddIssue(plan, DoorIssueSeverity::warning,
						 "A shared sidedef on this line will be isolated.",
						 sector, track.line);
		}
	}

	if (!portalFaceWeights.empty())
		plan.inferredFaceTexture = portalFaceWeights.best();
	else if (!trackWeights.empty())
		plan.inferredFaceTexture = trackWeights.best();
	else
		plan.inferredFaceTexture = config.default_wall_tex;

	plan.inferredTrackTexture = !trackWeights.empty() ?
			trackWeights.best() : plan.inferredFaceTexture;
	if (plan.inferredTrackTexture.empty())
		plan.inferredTrackTexture = config.default_wall_tex;

	plan.faceTexture = options.faceTexture.empty() ?
			plan.inferredFaceTexture : options.faceTexture;
	plan.trackTexture = options.trackTexture.empty() ?
			plan.inferredTrackTexture : options.trackTexture;

	if (images)
	{
		if (!images->W_TextureIsKnown(config, plan.faceTexture))
			AddIssue(plan, DoorIssueSeverity::warning,
					 SString::printf("Face texture '%s' is not currently loaded.",
									 plan.faceTexture.c_str()));
		if (!images->W_TextureIsKnown(config, plan.trackTexture))
			AddIssue(plan, DoorIssueSeverity::warning,
					 SString::printf("Track texture '%s' is not currently loaded.",
									 plan.trackTexture.c_str()));
	}

	for (int sector : plan.sectors)
	{
		for (const DoorPortalChange &portal : sectorPortals[sector])
		{
			plan.portals.push_back(portal);
			AddUnique(plan.portalLines, portal.line);
			if (portal.flip)
				AddUnique(plan.requiredFlips, portal.line);
		}
		for (const DoorTrackChange &track : sectorTracks[sector])
		{
			plan.tracks.push_back(track);
			AddUnique(plan.trackLines, track.line);
		}
	}

	return plan;
}

bool M_ApplySmartDoors(Document &doc, const ConfigData &config,
					   const ImageSet *images, MapFormat format,
					   const selection_c &selection,
					   const DoorOptions &options,
					   DoorPlan *appliedPlan)
{
	DoorPlan plan = M_PlanSmartDoors(doc, config, images, format,
									selection, options);
	if (appliedPlan)
		*appliedPlan = plan;
	if (!plan.valid())
		return false;

	EditOperation op(doc.basis);
	try
	{
		M_AppendSmartDoors(op, plan, format);

		if (plan.sectors.size() == 1)
			op.setMessage("made 1 smart door");
		else
			op.setMessage("made %d smart doors",
						  static_cast<int>(plan.sectors.size()));
	}
	catch (...)
	{
		op.setAbort(false);
		throw;
	}

	return true;
}

bool M_AppendSmartDoors(EditOperation &op, const DoorPlan &plan,
						MapFormat format)
{
	if (!plan.valid())
		return false;

	Document &doc = op.doc;
	StringID nullTexture = BA_InternaliseString("-");
	StringID faceTexture = BA_InternaliseString(plan.faceTexture);
	StringID trackTexture = BA_InternaliseString(plan.trackTexture);

	for (const DoorPortalChange &portal : plan.portals)
	{
		IsolateSidedef(op, portal.line, portal.doorSide);
		IsolateSidedef(op, portal.line, portal.outsideSide);
		if (portal.flip)
			doc.linemod.flipLinedef(op, portal.line);

		op.changeLinedef(portal.line, &LineDef::type, plan.preset.special);
		M_SetLineArguments(op, portal.line, plan.preset.args);
		M_SetLineActivation(op, portal.line, format, plan.preset.activation);

		LineDef &linedef = *doc.linedefs[portal.line];
		int flags = linedef.flags;
		flags |= MLF_TwoSided;
		flags &= ~(MLF_Blocking | MLF_UpperUnpegged |
				   MLF_LowerUnpegged);
		op.changeLinedef(portal.line, &LineDef::flags, flags);

		int doorSide = linedef.left;
		int outsideSide = linedef.right;
		op.changeSidedef(doorSide, SideDef::F_MID_TEX, nullTexture);
		op.changeSidedef(doorSide, SideDef::F_UPPER_TEX, nullTexture);
		op.changeSidedef(outsideSide, SideDef::F_MID_TEX, nullTexture);
		op.changeSidedef(outsideSide, SideDef::F_UPPER_TEX, faceTexture);
	}

	const std::array<int, 5> noArguments = {};
	for (const DoorTrackChange &track : plan.tracks)
	{
		int side = IsolateSidedef(op, track.line, track.doorSide);
		op.changeLinedef(track.line, &LineDef::type, 0);
		M_SetLineArguments(op, track.line, noArguments);
		M_ClearLineActivation(op, track.line, format);

		LineDef &linedef = *doc.linedefs[track.line];
		int flags = linedef.flags;
		flags |= MLF_Blocking | MLF_LowerUnpegged;
		flags &= ~(MLF_TwoSided | MLF_UpperUnpegged);
		op.changeLinedef(track.line, &LineDef::flags, flags);

		op.changeSidedef(side, SideDef::F_MID_TEX, trackTexture);
		op.changeSidedef(side, SideDef::F_UPPER_TEX, nullTexture);
		op.changeSidedef(side, SideDef::F_LOWER_TEX, nullTexture);
	}

	for (int sector : plan.sectors)
		op.changeSector(sector, Sector::F_CEILH,
						doc.sectors[sector]->floorh);
	return true;
}
