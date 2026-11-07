# Sculpt workspace

The workspace edits a private copy of the currently selected static shape, before
or after Vanilla+ Geometry. It does not change tessellation, source UVs or topology.

Reuse `GLView` for textured rendering/navigation; add a read-only local-space ray
query using its actual projection and the selected node's transform. Brush events
are intercepted only in Sculpt mode. Navigation mode passes ordinary events through.
Reuse Vanilla+'s shape inspection, `stageShape` save/reopen validation and exact
serialized replacement/rollback machinery for Apply. No automatic disk saves.

New components: a triangle-only brush session with connected-surface distance,
locked open/nonmanifold/hard-normal/coincident-vertex boundaries, transactional
face-orientation/area safeguards, bounded stroke history and named checkpoints;
a Sculpt/Original tabbed dialog controlling that session and a private NifModel.
Normals follow the existing authored-normal correction; tangent space and bounds
are rebuilt by the existing staging path on Apply.

Push/Pull, Flatten, Smooth and Restore operate only on existing positions. Flatten
uses a plane captured at stroke start. Restore targets this session's starting
shape. History and checkpoints are temporary and disappear when the dialog closes.
Boundary locks are conservative: separate coincident vertices are locked, never
welded. Small brushes on sparse meshes may touch no editable vertices. There is
no remeshing, dynamic topology, collision editing or collision/self-intersection
solver. Face checks prevent local inversions; they do not prove global clearance.

## Animated shader notice — 2026-11-06

An existing lighting-shader controller no longer rejects Sculpt or Vanilla+
Geometry. Both workspaces show a persistent notice instead of a blocking dialog.
Sculpt preserves stored UVs, seam boundaries and shader animation. Vanilla+
reconstructs from stored base texture paths and UV transforms, not a sampled
animation frame: animated material detail can move away from static baked relief.
The notice makes that limitation explicit without disabling supported Apply.

Shader/controller/interpolator/key data is retained by the existing unrelated-
block verification. UV authority, exact original UV checks, seam locks, source
staleness, skinning/unsupported attributes, material-mapping restrictions, format
limits, staged reopen and Undo/Redo checks remain in place.

`tests/run-sculpt-ui-probe.sh` now tests ordinary and animated UV/alpha fixtures
through actual native brush strokes before and after tessellation, including
staging, Apply/reopen, exact unrelated-block preservation, Cancel, stale rejection
and document/stroke Undo/Redo. `tests/run-animated-shader-geometry-probe.sh` checks
both SE BSTriShape and classic NiTriShape notices, Sculpt staging/Cancel,
tessellation Apply/reopen and byte-identical shader/animation blocks. The classic
fixture uses the supplied mountain textures in memory, matching the established
classic test because the original sword textures are not supplied. No asset
files are changed. Existing unsupported projected-UV rejection is also tested.

Native static/animated Sculpt, animated geometry, classic sword write-back and
geometry/material UI suites passed. The Qt/UCRT application was built, staged
and installed at
`C:\Utility Apps\NifSkopeBuildTools\nifskope-ucrt\release\NifSkope.exe`.
Built/installed SHA256:
`22F975DB81586807F54DFC7EEB718440AD0D783C8BE83F2760F2E4266691C26A`.
The installed application opened Townportal01 and closed normally in the smoke
test; its source file hash was unchanged.

## Usage

Select a supported static shape and choose the brush icon, **Sculpt...**, beside
Vanilla+ Geometry on the render toolbar. Use Sculpt/Original to compare. Left drag
paints, Shift reverses Push/Pull, Alt-drag navigates, and the wheel zooms.
Use `[` to shrink and `]` to enlarge the brush (1.15× scale steps; holding repeats).
A contrasting ring follows the selected surface, showing the local mesh-space
radius in its tangent plane with the viewport's actual perspective and transform.
It is a radius guide: locked vertices and connected-surface distance still limit
which vertices can move. The ring hides off the target, while navigating, and in
Original view. Hover picking is coalesced to avoid a scan for every mouse event.
Disable Brush mode for normal mouse navigation. Frame selected mesh recenters the camera.
The sculpt viewport uses muted green-gray wireframe lines at 16% opacity so the
textured surface remains readable; the main viewport's wireframe settings are unchanged.
Radius is measured in local mesh units. Strength ranges from 0 to 3; Push/Pull's
strength-1 dab distance is one tenth of the radius, multiplied by strength and
falloff. Other brushes approach their targets faster above strength 1 without
overshooting them. **Smooth stroke falloff** is optional and off by default. It
softens accumulated Push/Pull offsets within the connected brush footprint, using
a simultaneous neighbor pass weighted by falloff. It preserves the starting
surface, UVs, topology and locked boundaries and shares the dab's face-safety
transaction and Undo action. It does not change Flatten/Restore targets or add a
second smoothing pass to the explicit Smooth brush. Smooth explicitly changes
the surface; it does not alter connectivity. Restore blends toward session start.

**Release crease locks** is off by default. Enable it to edit vertices protected
only by the automatic 60-degree sharp-edge detector, then use Smooth or Flatten
to repair existing spikes. UV seams, separate coincident vertices and open
boundaries remain locked. The toggle never moves vertices or disables face-safety
checks. Turning it off pins those feature vertices at their current edited
positions. Stroke Undo/Redo and checkpoints can cross a protection change without
snapping the geometry or leaving later brush operations stuck against old anchors.

Undo/Redo tracks strokes (up to 32, within a 64 MiB position-history budget).
Store up to eight named checkpoints; restoring one is itself undoable. Checkpoints
and uncommitted edits are temporary, not saved as NIF blocks or external projects.
The staging check is optional; Apply always repeats it against the live source.
Apply is one document Undo action. Cancel leaves the document and its Undo history
unchanged. A subsequent Vanilla+ session starts from the sculpted result; an older
retained reconstruction reference is invalidated so it cannot erase sculpt edits.

The initial write path retains the existing static Skyrim SE BSTriShape / Skyrim
NiTriShape format restrictions and conservative material eligibility checks. It
does not silently convert another format. On coarse geometry, a small brush or a
shape made entirely of locked boundary vertices can make no edit; prepare more
vertices with the separate tessellator when needed.

## Verification

`tests/run-sculpt-core.sh` checks brush operations, exact position history,
checkpoints, bitwise UV preservation, disconnected/overlapping-shell isolation,
locked boundaries, invalid inputs and aggressive face-inversion rejection.
`tests/run-sculpt-ui-probe.sh` uses native OpenGL and actual mouse events on the
supplied rock, before and after tessellation. It checks tabs, named checkpoints,
stroke and document Undo/Redo, staging/reopen, unchanged indices and UV bits,
unrelated blocks, stale-source rejection, retained-reference invalidation and
unchanged files after Cancel. No fixture is saved automatically.
Native rendering checks also verify the visible ring grows with the radius,
bracket controls respect numeric limits and remain usable at minimum precision,
and Original/navigation views hide the guide. Radius edits leave geometry unchanged.

Verified 2026-11-03: 914 sculpt core checks; native OpenGL tests before/after
tessellation; 1,499,721 existing geometry checks; UI/material graph, repeated
refinement, farmhouse patches, classic sword, sconce displacement, UV fallback
and six-profile asset regressions. The performance probe retained identical
preview/statistics/candidate hashes and the prior performance improvements.

Built and dependency-staged with the established Qt/UCRT configuration. The
installed executable was hash-matched to the tested build, launched successfully
and checked for responsiveness:

`C:\Utility Apps\NifSkopeBuildTools\nifskope-ucrt\release\NifSkope.exe`

SHA256: `837D691578D6F0F18EB7AFA3CF8F3EEE79503797F1330C9F07DA20D2463237F5`.
