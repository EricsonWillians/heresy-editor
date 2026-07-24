Heresy Editor README
====================


INTRODUCTION

Heresy Editor is a map editor for the classic DOOM games and related games
such as Heretic, Hexen and Strife.  This fork is being developed as a mapping
companion for BiasedDoom while retaining the established upstream editing
workflow.  The supported operating systems are Linux (and other Unix-likes),
Windows and macOS.


PROJECT SITE

https://github.com/EricsonWillians/heresy-editor


DOWNLOADS

Tested release builds for Linux, Windows and macOS are published at:

https://github.com/EricsonWillians/heresy-editor/releases/latest

Every package has a matching SHA-256 checksum file.


FEATURES

-  Undo/Redo (multiple levels)
-  3D view with good lighting emulation
-  Editable panels for things, linedefs, sectors (etc)
-  Review-first Smart Door authoring for existing sector geometry
-  Nonmodal Smart Sector Designer for rooms, polygons, freeform sectors,
   extrusions, rings, corridors, stairs, lifts, architecture, and door-aware
   connections
-  Browser for textures, flats, things (etc)
-  Multi-file wall, floor/ceiling, and all-surface texture import for WAD/PK3
-  Format-aware wall, floor, and ceiling panning/scaling/rotation
-  Key binding system
-  Built-in nodes builder
-  Test-map workflow compatible with BiasedDoom
-  Campaign Navigator with safe multi-map working state
-  Full-IWAD, single-map, and custom ordered campaign layouts
-  Per-map titles, episode groups, entry points, and normal/secret routes
-  Previewed, conflict-safe runtime ZMAPINFO generation
-  Read-only campaign reachability, cycle, and missing-target diagnostics
-  Atomic Save Project / Save All for WAD and PK3 projects
-  Recursive PK3 flat, sprite, and texture resource discovery
-  Read-only PK3 declaration, runtime-source, and resource inventory
-  Per-map autosave with validated, rotating startup recovery
-  Portable project sessions with recent-project and IWAD restoration
-  Mathematical rotated, oblique, triangular/hexagonal, and polar grids
-  Temporary all-linedef 3D inspection with exact selection restoration


RELEASE 2.4.0

Heresy Editor 2.4.0 adds professional surface-resource authoring and
large-map construction.  Import one or many wall, floor/ceiling, or dual-use
textures into WAD/PK3 projects with decoded review, explicit conflict policy,
backed-up atomic writes, and immediate availability throughout the editor.
Then pan, fit, scale, mirror, rotate, and align selected surfaces through an
always-live transform review shared by OpenGL and software rendering.

The release also provides 30 mathematical construction grids, 75 grouped
spacings, custom pivots and directional rounding, fast whole-linedef 3D
inspection, large-coordinate UDMF safety, and an expanded catalog of 58 Smart
Sector architectural structures.  Complete release notes are in
changelogs/2.4.0.md.


MATHEMATICAL GRID AND FAST 3D INSPECTION

Press Alt+G or choose View / Mathematical Grid & Snapping.  The live tool
provides 30 geometry presets: square and proportional rectangles, rotated
diamonds, triangular and hexagonal axial lattices, isometric/dimetric/oblique
projections, and polar grids from 8 through 360 directions.  Primary and
secondary spacing, rotation, the angle between axes, radial divisions, major
line interval, and the construction origin remain editable.

Snap direction can use nearest, lower, upper, toward-origin, or away-from-
origin intersections.  Set the origin to world 0,0, the pointer, selection
center, or the 3D camera.  The information-bar Grid menu has 75 grouped size
presets covering ordinary detail, Doom architecture, 2048-65536-unit large-map
planning, decimal engineering, Fibonacci, and powers of three.  A custom
whole-number primary spacing from 1 through 65536 is also valid.

The same mathematical intersections drive ordinary drawing, insert, move,
paste, Quantize, and Smart Sector gestures.  Doom and Hexen candidates are
quantized to integer map coordinates; UDMF retains 16.16 coordinates.  The
mathematical grid is stored with map editor state.  See
docs/MathematicalGrid.txt.

Shift+Tab is the inspect-every-wall shortcut.  From 2D it saves the current
mode, selection, and sector-rendering overlay, selects every linedef, and
enters 3D.  Tab or Shift+Tab returns and restores that exact state.
Shift+F8 cycles forward through Plain, Floor, Ceiling, Lighting, Floor Bright,
Ceiling Bright, Sound, and 3D; Ctrl/Cmd+F8 cycles backward.  F8 still opens the
rendering menu, which also contains 3D and whole-linedef inspection entries.


SURFACE TEXTURE IMPORT

Choose File / Import Surface Textures, use Import in the Texture or Flat
Browser, or open Import from a searchable wall/floor/ceiling chooser.  Select
one or many PNG, JPEG, TGA, or LMP files.  Modern images default to All
Surfaces, valid Doom patches to Walls, and traditional 4096-byte raw flats to
Floors / Ceilings.

Review decodes every source and shows its intrinsic dimensions, alpha, exact
WAD namespace lumps or PK3 paths, complete loaded owner and precedence, and
active-map references.  Portable names use uppercase eight-character
identifiers.  Rename Imported safely generates _2, _3, and later suffixes;
Override Loaded adds a later project definition without changing the IWAD or
external resource; Replace Project requires one unambiguous existing entry;
Skip excludes that item.  Override and Replace receive a final exact impact
confirmation.

The valid batch is re-read, backed up, and written through one validated atomic
package replacement.  Wall and plane catalogs and rendering caches refresh
immediately without changing map selection, dirty resident maps, canvas
position, or Undo/Redo history.  Imported resources retain their real pixel
dimensions, so tiling, texture height, offsets, pegging, split correction, and
alignment behave exactly like loaded IWAD images.  See
docs/SurfaceTextures.txt.


SURFACE TEXTURE TRANSFORMS

Select or highlight walls, floors, or ceilings and choose Edit / Surface
Texture Transform, press Alt+T, or use the Xform/Transform panel button.  The
independently movable and resizable tool previews every valid field, holdable
plus/minus step, preset, Fit command, scope, and mirror change immediately.
Live display is always active.  Invalid input, Cancel, Escape, and window
close restore the exact original map without affecting dirty state or
Undo/Redo.

The dialog reports intrinsic image dimensions and rendered tile dimensions.
Set Exact produces consistent batch values; Adjust applies relative pan, size,
mirror, and rotation changes without flattening different existing values.
Fit Both fits one tile to the selected surface's actual width and height;
Fit Width and Fit Height constrain one axis.  Native and proportional
64/128/256 quick sizes remain available.  Hold a minus or plus button for
continuous live adjustment, with Shift for coarse and Ctrl for fine steps.

Doom and Hexen maps keep native integer wall offsets.  In writable modern-port
projects, size/fit/mirror creates a safely renamed, bilinearly resized project
texture or flat and assigns it without overwriting the source.  ZDoom/
BiasedDoom and Eternity UDMF maps store per-part wall pan/scale plus independent
floor/ceiling pan, scale, mirror, and clockwise rotation directly.  Both 3D
renderers, save/load, scaled wall alignment, and one-step map Undo/Redo use the
same reviewed result.  See docs/SurfaceTransforms.txt.


SMART DOORS

In 2D Sector mode, select one or more already-drawn door sectors and choose
Edit / Make Smart Door or Make smart door from the Sector Operations menu.
When there is no selection, one highlighted sector is accepted.

The review dialog chooses behavior from presets declared by the active game
and map format.  Its TARGET box names the exact sector numbers that will become
doors.  Those sectors use a heavy orange outline and orange hatch on the map;
activating portal lines are orange and one-sided track walls are green.

Door face texture means the surface visible on the moving door slab.  Track
wall texture means the narrow side walls inside the doorway.  Use Browse or
click a preview tile to search and choose from loaded wall textures, use Auto
to return to deterministic inference, or type a custom resource name.  The
resolved choice and its purpose are shown beneath each control.  Unknown custom
texture names warn but remain usable.

Each sector needs at least two two-sided portal boundaries.  Selected doors
cannot touch each other.  Smart Door reports malformed geometry and invalid
heights as blocking errors, while unusual shapes, clearance, floor mismatches,
missing tracks, shared sidedefs, and replaced specials appear as warnings.
Every sector in a batch is revalidated before the map changes.  A successful
batch is one Undo operation; Cancel and window close change neither the map nor
the selection.

Smart Door finishes existing geometry.  It does not split or resize walls,
create a door sector, assign remote tags or switches, or build animated,
sliding, or polyobject doors.


SMART SECTOR DESIGNER

In 2D Sector mode, press Ctrl/Cmd+Shift+S, choose Edit / Smart Sector
Designer, or use its Sector Operations entry.  Press X in Sector mode to jump
straight into Extrude.  The right-side panel stays open while you create
rectangles, ordered polygon profiles, concave freeform rooms, wall extrusions,
insets/rings, routed corridors, stair runs, lifts, and architectural layouts.
Polygon profiles progress from triangle and square through circles, stars,
crosses, gears, sawblades, foils, rosettes, and 288-vertex grand cathedral
tracery.  Architecture offers an ordered library of 58 real structures across
Structural supports, Floors and terraces, Circulation, Waterworks, Walls and
screens, and Ceilings and vaults.  Beyond the original support layouts and
floor/water/ceiling tools, it includes cross cores, tower shells, buttressed
towers, monuments, platforms, stages, podiums, amphitheaters,
switchback/bifurcated/spiral stairs, catwalks, crossing bridges, moats, canals,
cascades, fountain courts, partitions, crenellated and buttressed walls,
privacy screens, gatehouses, tray/barrel/cross vaults, domes, and downstand
beams.  These create real walkable, closed, depressed, or overhead sectors
rather than pillar substitutes.  Structure shows only the focused family
instead of one long mixed list.

A footprint inside a room preserves that host; one in clear void creates its
walkable hall and structures atomically.  It locks an existing host from the
first press, so holed and concave rooms work even when the footprint center is
void.  Cyan, gold, green, blue, warm-red, and violet fills distinguish
supports, floor forms, circulation, waterworks, wall mass, and ceiling work
while dragging; Review states the exact height effect.  Invalid layouts remain
visible in red with exact point/line markers instead of disappearing.  It can
also finish selected sector paths as stairs or platforms.

Architecture reads the press, drag, and release coordinates from the canvas
event itself, including Wayland/XWayland, and accepts a displaced release when
an intermediate drag event is coalesced.  Generated walkable cells share open
two-sided boundaries with their retained host.  Set Margin to 0 and touch a
host boundary to split/reuse it as a portal to an existing neighboring sector;
that neighbor's heights, flats, light, special, and tag remain unchanged.
Walls and screens span the drag's long axis while Thickness controls the short
axis.  Repeated panels, merlons, and buttresses run along the wall, and a
gatehouse reserves its exact centered passage.  Ribbed cross vaults use four
half-relief quadrants and a full-relief crossing.  A downstand beam lattice is
one connected two-axis ceiling sector whose openings retain the host room's
properties instead of becoming void.

View / Snap to Grid controls both ordinary sector drawing and every Smart
Sector anchor or drag.  With snapping off, geometry is still quantized for
the active Doom, Hexen, or UDMF format before it is previewed.  The last
explicit Snap to Grid choice is restored on the next launch.  Blue outlines
show proposed geometry; distinct opening, door, track, stair, lift, semantic
architecture, cut, warning, and conflict roles make the result readable
before applying.  Auto properties inherit context, while the panel offers
absolute/relative heights and light, explicit textures, and optional sector
Special and Tag values.
Floor, ceiling, and wall resources have searchable loaded-resource selectors,
live preview tiles, and contextual Auto controls; Inset preserves separate
Ring and Inner choices.  Special can be entered manually or chosen from the
active game's searchable, described list.  Drag the panel's left edge to
resize the designer, or double-click the grip to restore its default width.

Left-click places anchors; Room, Polygon, Architecture, Corridor, Stairs, and
wall-alcove Lift also support direct press-drag-release.  Inset and Lift accept
canvas sector targets, while Shift/Ctrl-click builds a batch and selected
Stairs use plain clicks for explicit path endpoints.  Right-click removes the
last anchor, Enter or Space commits, Tab cycles corridor routes, f or F flips
an extrusion/corridor or mirrors a supported structure, Shift+G toggles Snap
to Grid, and double Escape clears then exits.  The canvas mouse wheel changes
Corridor width or Inset / Ring thickness by the current grid step; Inset
preserves its explicit inward or outward direction.  In Architecture it
changes the relevant Structure size by a quarter grid step and respects the
selected generator's safe minimum.  Bays controls repeated
cells/openings/steps, Elevation controls vertical relief or depth, and Margin
protects circulation.  These fields are relabelled for the selected structure
as tiers, rows, rings, buttresses, panels, tread, passage, well radius, recess,
or drop.  Anchor order controls rises and cascades; F mirrors supported
structures.  A central platform can optionally become a Smart Lift with its
fresh tag and local triggers in the same Undo operation.  Other modes retain
normal zoom.  Returning U routes preserve their empty middle.

Extrude supports a real press-drag-release gesture without preselection: grab
a wall directly, or begin inside a sector and drag through the wall that
should be extruded.  The filled preview, normal-depth ruler, numeric depth, and
visible Opposite side control show exactly where it will go.  The tool remains
active after each commit, selects the resulting sectors, and creates exactly
one Undo record per gesture.  Existing actions, recognized doors and lifts,
ambiguous overlaps, slivers, and invalid topology block a commit unless
protected replacement is explicitly requested.

Open, Wall, and Door connections are semantic choices.  Door connections use
the Smart Door presets and texture inference in the same atomic edit.
Extrusion follows the pointer side of the seam where the drag began, even for
reversed linedefs and bent chains, or can use an exact signed depth.  Pressing
F or selecting Opposite side deliberately inverts that result.  A door can use
the longest boundary automatically or use one
or more chosen chain segments, with independent width, offset, and depth
controls; choose Smart Door for the Extrude Base seam, and use the green Face
and Track Auto toggles to clear overrides and re-infer both textures.  Auto
uses source-wall textures when available, shows the resolved names and tiles
in the live review, and applies exactly that previewed result.  Auto width
reserves safe track walls at chain bends.  Lift mode explains both workflows
in-panel: click an existing platform (Shift/Ctrl-click for a batch), or drag
outward from a wall to build an alcove.  Its live plan shows the inferred
lowest adjacent-floor stop, travel, and trigger count.  The chosen
configuration-declared behavior and selected usable portal boundaries receive
one fresh tag per connected platform in the same atomic operation.
Make Sectors revalidates the gesture and selects the result even when there was
no previous selection; blocked previews report their error count and exact
reason without partially editing the map.  Copy Review copies every diagnostic
without truncation, while Expand Review opens a resizable, wrapped, selectable
view; a blocked Enter attempt opens that view automatically.  Actions on the
chosen extrusion seam are preserved for Open/Wall connections instead of being
misclassified as consumed geometry.  Successful repeat-mode commits close stale
diagnostics and return to a neutral WAITING FOR GESTURE state; an extra Enter
while idle is guidance, not an error or edit.

Saving and Test in Game preserve the active map's Undo and Redo history.  When
the engine is launched, keyboard focus returns to the map canvas so Ctrl/Cmd+Z
continues to undo the tested edit.  An accidental Undo can be recovered through
Edit / Redo, Ctrl/Cmd+Shift+Z, or Ctrl/Cmd+Y until a different edit replaces
the redo future.


SUPPORTED GAMES

-  DOOM
-  DOOM 2
-  Final Doom
-  FreeDoom
-  HacX
-  Heretic
-  Hexen
-  Strife


BIASEDDOOM SUPPORT

BiasedDoom is the primary target engine for this fork.  It is derived from
GZDoom and keeps the standard IWAD, WAD, PK3, ZDoom map-format and command-line
workflows that Heresy Editor relies on.  Heresy Editor creates direct WAD
projects and BiasedDoom/GZDoom-style PK3 projects using `maps/<slot>.wad`.

Select "biaseddoom" as the source port.  Its profile derives from the ZDoom
definitions and supports Doom, Hexen and UDMF map formats, all five compatible
games, ZDoom action specials, and dynamic lights without duplicating the shared
definitions.

Tools / Test in Game automatically finds BiasedDoom or GZDoom when a remembered
path is unavailable.  The search checks `BIASEDDOOM_EXE` and `GZDOOM_EXE`, the
process `PATH`, portable editor locations, common CMake build layouts, and then
recursively scans the user's home and platform installation roots.  A typical
local build is:

   ~/workspace/BiasedDoom/build/biaseddoom

Windows `.exe` builds, macOS application bundles, Linux extensionless binaries,
and AppImages are recognized.  Deep searches run behind a responsive progress
dialog that reports the current location and can be cancelled.  The engine
picker does not require a filename extension.  Test Settings also provides an
editable path, Browse and Auto Detect controls, and inline validation for
missing, directory, and non-executable selections.  Cancelling or completing a
rediscovery without a match keeps the current selection.

For supported explicit projects, Test in Game also checks the freshness of an
existing marker-owned runtime ZMAPINFO.  Current output launches silently.  If
campaign metadata has changed, preflight can cancel, launch with the existing
declaration, or open the exact preview and regenerate before launch.  A missing
declaration remains optional, and user-authored MAPINFO is never compared or
rewritten.

Heresy Editor will pass the selected IWAD, resource WADs or PK3s, edited project
package, and map name to BiasedDoom.  PK3 resources under `flats/`, `sprites/`,
and `textures/` are projected recursively into the corresponding editor
browser namespaces while their original archive paths and records remain
untouched.  File/PK3 Metadata inventories MAPINFO-family and common resource
declarations, lists runtime sources without interpreting them, and summarizes
conventional asset groups with bounded, read-only text previews.  It also
reports uppercase eight-character projected-name collisions and overrides in
the loaded IWAD/resource/project order without renaming or reordering anything.
The pinned BiasedDoom release adds no map-facing actors, specials, or namespace
beyond its audited GZDoom base.


REQUIREMENTS

-  128 MB of computer memory
-  1024x720 or higher screen resolution
-  3D accelerated graphics card
-  a keyboard and a two-button mouse
-  the data (iwad) file from a supported game


COMPILATION

See the INSTALL.txt document (in source code)


RUNNING


Command line:

You can run Heresy Editor from the command line, or it can be run from
the desktop menu (on Linux: if your OS handles .desktop files as per the
XDG specs).  Heresy Editor will need to be able to find an IWAD to run,
if it cannot find any then the "Manage Wads" dialog will open up,
allowing you to "Find" one (which is remembered for next time).

The executable is named `heresy`.  The inherited `eureka` configuration-
directory names remain compatibility identifiers so existing settings continue
to work.

You can open a WAD or PK3 project using File/Open Map, or create one with
File/New Project.  Its three pages collect project settings, campaign/resources,
and a final validated destination summary before anything is written.  New
Project and Manage Project support a full IWAD campaign, a single-map campaign,
or a validated custom map order.  Campaign Navigator can add display titles,
episode groups, normal-route overrides, explicit endings, and secret routes to
configured slots.  The first slot is always a campaign entry, and Map Details
can mark additional episode or hub starts.  File/Create Next Map follows the
resulting normal route.

Explicit projects also use an adjacent `<package>.heresy` session file.  It
restores the last active map and Campaign Navigator selection and records only
portable IWAD hints: the game, IWAD filename, and a path relative to the
project package.  It does not contain an absolute IWAD path or gameplay data.
Keep it beside the WAD or PK3 when moving a project; Heresy Editor validates it,
ignores damaged or incompatible sessions, and rewrites it atomically.

File/Recent Projects is separate from File/Recent Files, so projects with the
same filename in different folders remain distinct.  With no project specified
on the command line, startup automatically reopens the latest explicit project
and restores its last map when available.  The preference named "open the most
recent loose WAD when no project exists" controls only the legacy loose-PWAD
fallback; it does not disable explicit-project restoration.

File/Campaign Navigator shows configured and additional maps together with
their title, episode, effective routes, and current, dirty, or missing state.
It can edit campaign details or open, create, duplicate, rename, and delete
maps.  It analyzes those same effective routes from every campaign entry,
flags unreachable maps, potential cycles, and routes whose configured targets
are still missing from the package, and gives selected-map remediation details.
The analysis is read-only.  These campaign fields are editor metadata and do
not affect gameplay until File/Generate Runtime MAPINFO is explicitly run.
For BiasedDoom, GZDoom, and ZDoom profiles, that command previews a root
ZMAPINFO translating entries, titles, routes, and endings, then writes only a
marker-owned declaration.  It shows map-slot title fallbacks and refuses to
overwrite any user-authored MAPINFO-family declaration.  Campaign Navigator
labels managed output as current or out of date.  Navigation retains up
to eight resident map documents (one active and seven cached), never evicting
unsaved work.

For PK3 or ZIP projects, File/PK3 Metadata shows recognized campaign and
resource declarations, runtime-source entries, conventional resource groups,
managed map/project entries, and other preserved files.  It never edits the
archive; previews are verbatim and limited to 512 KiB.
The complete dialog also enforces a 4 MiB aggregate preview budget and reports
duplicate projected flat, sprite, and texture names plus loaded-source
overrides, including nominal winners or ambiguous sprite combinations.

File/Save Map saves only the active map.  File/Save Project saves every dirty
resident map and the project settings in one validated, atomic package update.
File/Save All currently performs the same application-wide operation because
Heresy Editor has one project window.

While a project has unsaved maps or settings, Heresy Editor writes a separate
recovery snapshot at the interval configured under Preferences / General (two
minutes by default; zero disables it).  Each dirty map is stored separately,
three validated generations are retained, and node building is skipped so the
editor remains responsive.  Recovery data never modifies the project package.
When that package is opened again, the editor offers to recover, keep for
later, or discard the snapshot, and warns if the package changed in the
meantime.  Use Save Project after recovery to commit the restored work.

You can also specify the WAD or PK3 package to edit on the command line, either
on its own or with the -file option:

   heresy -file masterpiece.wad

If that package contains multiple maps, you may need to specify which
one to edit using the -warp option:

   heresy -file masterpiece.wad -warp 14

For a summary of useful command line options, type:

   heresy --help



KEYBOARD AND MOUSE CONTROLS

All Modes
---------

LMB
* select an object, drag to move the object(s)
* click in empty area to clear the selection
* click + drag in empty area to select a group of objects

RMB
* begin/continue line drawing (in vertex mode)
* merge sectors (in sectors mode)
* with CTRL pressed: bring up operation menu

MMB
* scroll the map around (by dragging)

wheel : zoom in or out

cursor keys : scroll the map

F1 : operation menu

TAB : toggle the 3D preview on or off
SHIFT-TAB : inspect every linedef in 3D; press again to restore prior state
ESC : cancel the current operation

t : enter Thing mode
l : enter Linedef mode
s : enter Sector mode
v : enter Vertex mode

b : toggle the Browser on or off

1..9 : select the grid size (smallest to largest)
ALT-G : configure mathematical grid geometry, origin, and snapping

CTRL-Z : undo (can be used multiple times)
CTRL-Y : redo (i.e. undo the previous undo)

CTRL-A : select all
CTRL-I : invert the selection
CTRL-U : unselect all
` (backquote) : unselect all

HOME : zoom 2D viewport to show the whole map
END  : move 2D viewport to camera location
' (quote) : move 3D camera to position of mouse pointer

g : toggle grid on / off
G : toggle Snap to Grid (free vs snapped drawing)

N : open next map in the current wad
P : open previous map in the current wad

j : jump to object (by its numeric id)
J : toggle object number display

o : copy and paste the selected objects
c : copy properties from selected --> highlighted object
C : copy properties from highlighted --> selected objects

H : mirror objects horizontally
V : mirror objects vertically
R : rotate objects 90 degrees clockwise
W : rotate objects 90 degrees anti-clockwise

a : scroll map with the mouse
r : scale selected objects with the mouse
R : scale selected objects, allow stretching
CTRL-R : rotate the selected objects (with the mouse)
K : skew (shear) the selected objects

\ : toggle the RECENT category in the Browser

u  : popup menu to set ratio lock
z  : popup menu to set current scale
B  : popup menu to set browser mode
F8 : popup menu to set sector rendering mode
SHIFT-F8 : cycle to the next 2D/3D rendering mode
CTRL/CMD-F8 : cycle to the previous 2D/3D rendering mode

; : make the next key pressed META

META-N : load next file in given list
META-P : load previous file in given list

META-F : apply a fresh tag to the current objects
META-L : apply the last (highest) tag to the current objects


Things Mode
-----------

SPACE : add a new thing

d : disconnect things at the same location
m : move selected things to occupy the same location

w : rotate things 45 degrees anti-clockwise
x : rotate things 45 degrees clockwise


Vertex Mode
-----------

SPACE
* begin/continue line drawing
* with SHIFT key: always continue line drawing
* with CTRL key: inhibit creation of sectors

d : disconnect all linedefs at the selected vertices
m : merge selected vertices into a single one
u : unlock any current ratio lock

I : reshape selected vertices into a line
O : reshape selected vertices into a circle
D : reshape selected vertices into a half-circle
C : reshape selected vertices into a 120-degree arc
Q : reshape selected vertices into a 240-degree arc


Linedef Mode
------------

e : select a chain of linedefs
E : select a chain of linedefs with same textures

w : flip linedefs
k : split linedefs in two
A : auto-align offsets on all selected linedefs

d : disconnect selected linedefs from the rest
m : merge two one-sided linedefs into a two-sided linedef


Sector Mode
-----------

SPACE
* add a new sector to area around the mouse pointer
* if a sector is selected, copy that sector instead of using defaults

d : disconnect sector(s) from their neighbors
m : merge all selected sectors into a single one

w : swap floor and ceiling textures
i : increase light level
I : decrease light level

e : select sectors with same floor height
E : select sectors with same floor texture
D : select sectors with same ceiling texture

, and < : lower floor heights
. and > : raise floor heights
[ and { : lower ceiling heights
] and } : raise ceiling heights


3D View
-------

(cursor keys will move forward and back, turn left and right)
(the WASD keys can also be used to move the camera)

LMB : select walls, floors or ceilings
MMB : turn or move the camera (by dragging the mouse)

wheel : move camera forwards or backwards

PGUP and PGDN : move camera up and down

g : toggle gravity (i.e. as if the player was on the ground)
e : popup menu to set edit mode
o : toggle objects on or off

META-v : drop to the ground
META-l : toggle lighting on or off
META-t : toggle texturing on or off

F11 : increase brightness (gamma)

r : adjust offsets on highlighted wall (with the mouse)
c : clear offsets on highlighted wall

x : align X offset with wall to the left
y : align Y offset with wall to the left
z : align both X + Y offsets

X : align X offset with wall to the right
Y : align Y offset with wall to the right
Z : align both X + Y offsets



COPYRIGHT and LICENSE

  Heresy Editor

  Copyright © 2014-2026 Ioan Chera
  Copyright © 2001-2020 Andrew Apted, et al
  Copyright © 1997-2003 Andre Majorel et al

  Heresy Editor is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 2 of the License,
  or (at your option) any later version.

  Heresy Editor is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.
