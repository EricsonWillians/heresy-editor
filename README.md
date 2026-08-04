<p align="center">
  <img src="misc/about_logo.png" width="440" alt="Heresy Editor">
</p>

<h1 align="center">Heresy Editor</h1>

<p align="center">
  A fast, preservation-minded map editor for Doom-engine games, built for
  modern project authoring and first-class BiasedDoom workflows.
</p>

<p align="center">
  <a href="https://github.com/EricsonWillians/heresy-editor/actions/workflows/build-artifacts.yml"><img alt="Build, test, and publish artifacts" src="https://github.com/EricsonWillians/heresy-editor/actions/workflows/build-artifacts.yml/badge.svg?branch=master"></a>
  <a href="https://github.com/EricsonWillians/heresy-editor/actions/workflows/codeql-analysis.yml"><img alt="CodeQL" src="https://github.com/EricsonWillians/heresy-editor/actions/workflows/codeql-analysis.yml/badge.svg?branch=master"></a>
  <a href="https://github.com/EricsonWillians/heresy-editor/actions/workflows/biaseddoom-compatibility.yml"><img alt="BiasedDoom compatibility" src="https://github.com/EricsonWillians/heresy-editor/actions/workflows/biaseddoom-compatibility.yml/badge.svg?branch=master"></a>
  <a href="https://github.com/EricsonWillians/heresy-editor/releases/latest"><img alt="Latest release" src="https://img.shields.io/github/v/release/EricsonWillians/heresy-editor?display_name=tag&sort=semver"></a>
  <a href="GPL.txt"><img alt="License: GPL-2.0-or-later" src="https://img.shields.io/badge/license-GPL--2.0--or--later-blue.svg"></a>
</p>

Heresy Editor continues the Eureka Editor codebase with an emphasis on making
whole maps and campaigns safer and dramatically faster to design. It supports
Linux, Windows, and macOS; Doom, Hexen, and UDMF maps; and the Doom, Doom II,
Final Doom, Freedoom, HacX, Heretic, Hexen, and Strife game families.

## Why Heresy Editor?

- **Smart Sector Designer** — draw rectangles, detailed polygons, concave
  rooms, extrusions, rings, routed corridors, stairs, lifts, and architectural
  layouts through one live, format-quantized workflow.
- **Smart Doors** — turn existing sectors or extrusion seams into verified
  local doors with inferred face/track textures and format-correct activation.
- **58 architectural structures** — build supports and towers, stages and
  amphitheaters, stairs and catwalks, canals and fountain courts, gatehouses,
  domes, vaults, and beam lattices inside ordinary, concave, or holed sectors
  with visible validation before committing.
- **Campaign-scale editing** — manage WAD and PK3 projects, map order, titles,
  episodes, normal/secret routes, entry points, and generated runtime ZMAPINFO.
- **Surface texture import** — add multiple wall, floor/ceiling, or dual-use
  PNG/JPEG/TGA/LMP resources with decoded previews, exact package destinations,
  safe conflict policies, and immediate browser/Smart Tool availability.
- **Live texture fit and alignment** — Hammer-style face fitting, holdable
  offset/size controls, and an always-live reversible canvas preview; UDMF
  stores native transforms while modern-port classic projects can create safe
  fitted texture copies.
- **Preservation first** — protect existing specials, doors, lifts, extended
  UDMF properties, unknown PK3 content, undo history, and unsaved multi-map
  work.
- **Fast test loop** — automatically discover BiasedDoom or GZDoom on each
  platform and preserve Undo/Redo across Save and Test in Game.
- **Mathematical construction grids** — use rotated, rectangular, oblique,
  triangular/hexagonal, and polar snapping, 75 grouped size presets, custom
  origins and rounding, and direct render/whole-linedef 3D inspection keys.

Every Smart authoring gesture is planned without modifying the document,
previewed with proposed and conflicting geometry, revalidated on commit, and
applied as one atomic Undo operation.

## Download

Tested packages and matching SHA-256 checksums are published on the
[latest release page](https://github.com/EricsonWillians/heresy-editor/releases/latest).

| Platform | Architectures | Package |
| --- | --- | --- |
| Linux | x86_64, arm64 | Portable `.tar.gz` |
| Windows | x86_64, x86 | Portable `.zip` |
| macOS | Apple silicon, Intel | `.dmg` |

macOS packages are ad-hoc signed for integrity but are not Apple-notarized.

## What's new in 2.5.0

Version 2.5.0 makes the construction grid legible at a glance with semantic
High-Contrast Dark, Vintage Phosphor, Blueprint Light, and Custom visibility
themes, adds optional metric/imperial measurement readouts across the canvas,
linedef overlay, and status bar, and removes the 16-bit ceiling on sector
heights for UDMF maps — with an explicit warning when a binary-format save
would truncate out-of-range heights.

Read the complete [2.5.0 changelog](changelogs/2.5.0.md).

## Quick start

1. Download and verify the package for your platform.
2. Launch `heresy` and select an IWAD when prompted.
3. Create or open a WAD/PK3 project.
4. Use **File → Import Surface Textures** to add project images, or in Sector
   mode press <kbd>Ctrl</kbd>/<kbd>Cmd</kbd> +
   <kbd>Shift</kbd> + <kbd>S</kbd> to open Smart Sector Designer.
5. Select **BiasedDoom** or another supported source port, then use
   **Tools → Test in Game**.

Heresy Editor searches environment overrides, `PATH`, portable/build
locations, the user home directory, and platform installation roots for
compatible test engines. Linux executables do not need an `.exe` suffix.

## Smart authoring

### Sectors and architecture

The nonmodal Smart Sector Designer stays open across repeated commits and
offers:

- rooms, 3–288 vertex polygon profiles, and concave freeform outlines;
- inward/outward extrusion and inset/ring operations;
- ranked straight, L, Z, and waypoint corridors;
- generated or existing-sector stairs;
- tagged lift platforms and wall alcoves;
- Open, Wall, or Smart Door endpoint semantics;
- an ordered catalog of 58 generators across structural supports, floors and
  terraces, circulation, waterworks, walls and screens, and ceilings and
  vaults.

Architecture is organized as a two-level catalog: choose Structural supports,
Floors and terraces, Circulation, Waterworks, Walls and screens, or Ceilings
and vaults, then choose a focused Structure. The catalog includes the original
support layouts and real sector generators for cross cores, accessible tower
shells, buttressed towers, monuments, platforms, stages, podiums,
amphitheaters, switchback/bifurcated/spiral stairs, catwalks, bridge
crossings, moats, canals, cascades, fountain courts, partitions, crenellated
and buttressed walls, privacy screens, gatehouses, tray/barrel/cross vaults,
domes, and downstand beams.

Architecture locks the sector under the first unsnapped press. That makes
layouts reliable inside deeply concave rooms and around holes even when the
footprint center is void. Valid structures remain visible in their dedicated
semantic preview color: cyan supports, gold floors, green circulation, blue
water, red wall mass, and violet ceilings. The review states the exact
floor/ceiling effect before commit; blocked intent remains red with exact
point/line diagnostics.

Architecture drags consume the canvas event coordinates directly, making
press-drag-release reliable across X11, Wayland/XWayland, and software/OpenGL
canvas paths. Generated walkable cells have open two-sided boundaries to their
host. Set Margin to `0` and touch a host boundary to split/reuse it as a portal
to an existing neighboring sector without replacing that neighbor's
properties.

Structure-specific Bays, Size, Elevation, and Margin controls drive real
walkable or overhead sectors rather than disguising every option as a pillar.
Their labels and safe ranges come from the selected catalog entry. Anchor
order controls directional rises and cascades; <kbd>F</kbd> mirrors supported
layouts. A central platform can optionally become a format-correct Smart Lift
with its fresh tag and local triggers in the same Undo operation.

Walls and screens span the drag's long axis, with thickness on the short axis.
Cross vaults use four rising quadrants and a distinct center crossing; beam
lattices are connected two-axis ceiling sectors whose openings retain the
host room instead of becoming void.

Read the complete [Smart Sector Designer guide](docs/SmartSectors.txt).

### Mathematical grid and fast 3D inspection

Press <kbd>Alt</kbd>+<kbd>G</kbd> for the live Mathematical Grid & Snapping
tool. It offers 30 geometry presets and editable square/rectangular, rotated,
oblique, triangular/hexagonal, and 3–360-division polar grids. Set independent
primary and secondary spacing, rotation, inter-axis angle, origin/pivot, major
interval, and nearest/lower/upper/toward/away rounding. The info-bar Grid menu
also groups 75 useful spacings—Doom architecture, large-map, decimal,
Fibonacci, and powers of three—while accepting any whole spacing from 1 to
65,536.

Snapping uses the same intersections for ordinary drawing, object insertion,
paste/move/quantize, and Smart Sector gestures. Map-local mathematical state
is restored with the project; Smart Room follows rotated/oblique construction
axes, and UDMF large positive coordinates retain correct bounds and status-bar
precision.

Grid visibility is now semantic rather than a set of similarly dark lines.
High-Contrast Dark, Vintage Phosphor, and Blueprint Light themes give fine,
reference, major, axis, origin, snap-target, halo, and guide roles distinct
weights and colors across square, Dotty, and mathematical grids. Each theme
also coordinates the map itself: the grid always stays far weaker than
geometry — dim on the dark canvases, pale on the light one — while walls,
linedefs, things, vertices, selection, and snapping each own a separate
high-contrast hue band, so grid, snapping, things, linedefs, and sector
diagnostics can never be mistaken for one another. Because a full-screen
grid aggregates into a veil, grid lines also fade toward the canvas as they
pack together, dissolving to a faint texture instead of drowning the map.
A grid opacity slider (20-100%) fades grid lines toward the canvas on any
theme for an even quieter grid. The snap reticle renders above map geometry
and points back to the raw cursor position, while Preferences → Grid
retains complete Custom color control with the classic map object colors.
Camera, error, tagged-feedback, sound-blocking, and sound-propagation
colors follow the active theme as well, design-assist preview hues adapt
to the canvas, and the Preferences guidance box reports the measured
worst-case contrast (grid and ink) for the selected theme.

Use <kbd>Shift</kbd>+<kbd>Tab</kbd> to inspect every linedef immediately in 3D.
The editor temporarily selects every wall, then <kbd>Tab</kbd> or
<kbd>Shift</kbd>+<kbd>Tab</kbd> returns to the exact prior mode, selection,
and 2D rendering overlay. <kbd>Shift</kbd>+<kbd>F8</kbd> cycles forward through
all seven 2D render overlays and 3D; <kbd>Ctrl</kbd>/<kbd>Cmd</kbd>+
<kbd>F8</kbd> cycles backward. F8 remains the direct rendering menu.

Read the [Mathematical grid and inspection guide](docs/MathematicalGrid.txt).

### Doors

Smart Door reviews all selected sectors as a batch, resolves compatible
configuration-declared presets, infers loaded textures deterministically, and
shows portal/track geometry before changing the map. Face and Track Auto
controls clear explicit overrides and immediately show the re-inferred
textures. The same semantics are available directly from the Extrude pipeline.

Read the [Smart Door guide](docs/SmartDoors.txt).

### Surface textures

**File → Import Surface Textures** accepts one or many PNG, JPEG, TGA, Doom
patch, or raw-flat files. Modern images default to All Surfaces; review can
target Walls or Floors/Ceilings instead. Portable names, loaded IWAD/resource
ownership, exact WAD lumps or PK3 paths, active-map usage, and
Rename/Override/Replace/Skip policy are resolved before any write.

The complete batch is decoded, revalidated, backed up, and committed through
one atomic package replacement. Imported dimensions remain intrinsic, so
rendering, tiling, offsets, pegging, and alignment behave like loaded IWAD
images. Surface catalogs refresh without changing map selection, dirty
resident documents, canvas position, or Undo/Redo history.

Read the [Surface texture import guide](docs/SurfaceTextures.txt).

Select a wall, floor, or ceiling and use **Edit → Surface Texture Transform**,
<kbd>Alt</kbd>+<kbd>T</kbd>, or the Transform/Xform panel buttons. The dialog
is independently movable/resizable and always previews live. Hammer-style
Both/Width/Height fitting uses actual surface geometry; every numeric field
has holdable coarse/fine step buttons alongside Native/64/128/256 sizing,
exact/relative edits, panning, mirroring, and plane rotation. Supported UDMF
ports store part-specific transforms. Writable modern-port classic projects
instead create safe fitted resources while retaining native integer offsets.
Both renderers, map save/load, and one-step map Undo/Redo use the reviewed
result.

Read the [Surface transform guide](docs/SurfaceTransforms.txt).

## Projects, campaigns, and recovery

- Atomic **Save Project** and **Save All** for WAD and PK3 projects.
- Campaign Navigator with bounded resident-map caching and independent history.
- Validated project sessions with portable IWAD hints.
- Rotating, per-map recovery snapshots that never overwrite the project.
- Read-only campaign graph, PK3 metadata, and resource-collision diagnostics.
- Previewed ZMAPINFO generation that refuses to overwrite user-authored
  MAPINFO-family declarations.

See [Project and campaign workflows](docs/Projects.txt) and
[BiasedDoom integration](docs/BiasedDoom.txt).

## Build from source

The supported entry point is:

```bash
./build.sh --clean --type Release --test
```

On Debian/Ubuntu, install:

```bash
sudo apt-get install build-essential clang cmake ninja-build \
  libgl-dev libglu1-mesa-dev libjpeg-dev libpng-dev libxpm-dev \
  libz-dev python3
```

CMake 3.28+ and a C++20 compiler are required. FLTK and GoogleTest use
project-pinned revisions unless system dependency options are requested.
See [INSTALL.txt](INSTALL.txt) for compiler, installation, packaging, and
platform-specific details.

## Pipeline and release integrity

The live badges above report the `master` branch status. The release workflow
builds and tests:

- Linux x86_64 with GCC and Clang;
- Linux arm64 with GCC;
- Windows x86 and x86_64;
- macOS Intel and Apple silicon.

Every version tag must match `CMakeLists.txt` and a versioned changelog. The
release job verifies every generated SHA-256 checksum before it publishes or
updates the GitHub Release. CodeQL analyzes C++ and Python, while the scheduled
BiasedDoom compatibility contract detects upstream release or metadata drift.

## Documentation

- [Complete bundled manual](README.txt)
- [Installation and distribution](INSTALL.txt)
- [Smart Sector Designer](docs/SmartSectors.txt)
- [Smart Doors](docs/SmartDoors.txt)
- [Surface texture import](docs/SurfaceTextures.txt)
- [Surface texture transforms](docs/SurfaceTransforms.txt)
- [Mathematical grids and fast 3D inspection](docs/MathematicalGrid.txt)
- [Heresy Editor 2.4.0 changelog](changelogs/2.4.0.md)
- [Projects and roadmap](docs/Projects.txt)
- [BiasedDoom compatibility](docs/BiasedDoom.txt)
- [Authors and contributors](AUTHORS.md)

## License

Heresy Editor is distributed under the
[GNU General Public License, version 2](GPL.txt). Vendored dependencies retain
their own license notices.
