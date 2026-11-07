# RC1 cumulative changelog

This lists the upgrade work represented by the 2026-11-06 candidate. It is
cumulative relative to the supplied NifSkope 2.0.dev11 source backup, not a
claim that inherited NifSkope features were newly implemented here. The package
also includes a generated changed-file CSV/Markdown report with baseline and
current SHA-256 values, plus complete source and runtime manifests.

## New geometry workflow

1. Added a toolbar-accessible non-destructive Vanilla+ Geometry dialog using
   existing NifSkope geometry, resource, normals/tangents, bounds and meshoptimizer paths.
2. Added immutable geometry/material snapshots, temporary working geometry,
   resource availability reporting and explicit staged Apply/Cancel.
3. Added diffuse/normal/existing-height experimentation, per-source weighting,
   blend modes, one-button height generation, histogram and remap controls.
4. Added bilateral smoothing and fine/coarse scale separation; higher-resolution
   preview field option; manual material Surface Profiles and diagnostic masks.
5. Added uniform and surface-error/curvature-based adaptive triangle subdivision.
6. Added bounded displacement, positive/negative limits, neutral level and strength.
7. Added stronger pre-bake geometry visualization, smooth shading, wire overlays,
   multiple presentation modes and safety/reduction statistics.
8. Added triangle-quality edge cleanup with minimum-pair improvement, winding,
   protected-edge and surface-deviation checks.
9. Added dense-reference, attribute-aware reduction and verification before Apply.
10. Added conservative compatible-patch retopology connected to preview, normal/
    tangent reconstruction, reduction, verification and final candidate output.
11. Added patch boundary recovery, interior validation, corner/domain handling,
    per-vertex original-surface projection, equivalent internal-edge source
    canonicalization and exact fallback when verification rejects a candidate.
12. Added experimental Dark Feature Bias and attraction preview with bounded,
    source-aware sample relocation; it does not equate every dark pixel with depth.

## Geometry fidelity and safety fixes

- Made original UVs and the shader's scale/offset/addressing authoritative across
  field generation, subdivision and displacement. Added exact original packed-UV
  checks, including finite half-precision and signed-zero coverage.
- Preserved UV/hard-normal seams, authored normal groups, vertex colors/alpha,
  material/section boundaries and supported source attributes through correspondence.
- Pinned geometric seams without welding independent coincident shells or UV islands.
- Added pinch/slope and face-orientation safeguards, with local limiting so a short
  problematic edge no longer flattens legitimate relief across an entire section.
- Addressed broad inflation/DC drift and repeated-pass billowing using section-bias
  handling, conditioned relief/profile response and retained-reference refinement.
- Added conservative UV-island isolation and fallback for unsuitable overlap/tiling
  cases; retained original UV values and transforms rather than repacking islands.
- Replaced the earlier adaptive height-variation trigger with surface representation
  error/curvature criteria while retaining seam, pinch and inversion safeguards.
- Verified compatible patches through downstream output instead of leaving them
  diagnostic-only, preserving the legacy ancestry path for other geometry.
- Added private full-document staging, save/reopen checks, replacement/rollback
  preflight, selected-geometry commit and exact document Undo/Redo.
- Extended accepted input beyond vanilla-named assets and implemented supported
  classic Skyrim NiTriShape write-back; unsupported formats retain useful refusal
  or preview-only behavior rather than silent conversion.
- Replaced blanket rejection of animated lighting shaders with persistent notices
  in Sculpt and Vanilla+. Shader/controller/key data remains byte-preserved;
  baked relief uses the stored base material, not an animation frame.

## New Sculpt workspace

- Added a private ordinary-renderer workspace for existing-vertex artist edits.
- Added Push/Pull, Flatten, Smooth, Restore, connected brush influence, boundary
  locks, face safety, stroke history and revision checkpoints.
- Added textured Original/Sculpt comparison, restrained wire overlay and an
  influence ring with bracket-key radius adjustment.
- Increased maximum brush strength to 3; added optional stroke-falloff smoothing
  and an explicit boundary-lock release control for correcting locked spikes.
- Reused staged geometry verification and exact document Undo/Redo; preserved
  source UVs and triangle connectivity. No dynamic topology was introduced.

## New Material Graph and animated-layer workflow

- Added an editable graph for Skyrim lighting and effect material families,
  while retaining the traditional block editor and existing renderer.
- Exposed supported shader types, complete Shader Flags 1/2 lists, meaningful
  tooltips, active/resource state distinctions and live flag words.
- Exposed all seven requested lighting inputs in standard slot order, working
  choose/clear controls, resource lookup, thumbnails and preserved trailing data.
- Added effect source/greyscale inputs, base RGBA, brightness, lighting and falloff.
- Added static UV pad/numeric transforms and a shared base sampling preview,
  preserving numeric ranges and wrap/clamp behavior.
- Added scalar/vector/RGB animation inspection, timing/interpolation information,
  supported renderer playback and compatible named-state inspection.
- Added the independent Pan instruction: left mouse U, right mouse V, signed
  speeds and loop duration. Valid Bethesda controllers/interpolators/keys are
  created internally; compatible existing origins, clocks and easing are retained.
- Added staged controller/block creation with exact rollback/redo and persistent
  block identity recovery through later normal block-editor edits.
- Made the application graph modeless over a private document snapshot.
- Added Create animated layer, independent static render-branch duplication,
  main-render Original/Animated-layer tabs, local XYZ offsets, rotation offsets,
  scale and framing, followed by one-step Apply or complete Cancel.
- Added optional NifSkope ColorWheel pickers for emission/base color and specular
  tint, HDR multiplier support and alpha precision preservation. Explained that
  environment reflection color comes from the cubemap, not an invented tint field.
- Fixed menus/submenus being drawn behind other graph nodes; restored node stacking
  after the final popup closes.
- Bounded/wrapped graph help, shortened its initial delay and coalesced lookups.
  Preview limitations and unsupported authoring arrangements remain explicit.

## Viewport visibility

- Added an eye gutter to the normal block list in hierarchy and flat modes.
- Added transient show/hide overrides with parent/child behavior, authored-hidden
  reveal, Reset, stable identity through renumbering and clearing on reload.
- Excluded metadata/field rows, preserved selection and normal expand/edit controls,
  and kept all eye state out of NIF flags, saved files, settings and Undo history.
- Suppressed hidden-object selection overlays and associated marker/collision
  display without modifying collision data.

## Performance, resources and engineering

- Fixed detected-game resource initialization and conservative settings migration
  without overwriting explicitly empty/custom resource lists or disabled games.
- Moved toolchain/build/runtime artifacts outside the source workspace; established
  an isolated working UCRT/Qt build and explicit dependency/resource staging.
- Added conditioned-field/presentation caches and validated fast material sampling.
- Avoided repeated unrelated document serialization, duplicate texture decode,
  unchanged-UV animation rerenders and per-frame color stylesheet parsing.
- Deferred numeric typing commits until Enter/focus loss without triggering dialog
  buttons; retained immediate stepping/programmatic updates.
- Cached immutable sculpt safety data, avoided no-op refreshes, reused normal data
  appropriately and moved motion throttling before expensive picking.
- Added extensive geometry, source authority, patch/fallback, classic write-back,
  material, animation, layer, sculpt, viewport visibility and performance probes.
- Added portable RC build/deploy helpers, reproducible source/runtime manifests,
  dependency/license inventories and packaging/review documentation.

## Intentionally unchanged / not included

Normal NifSkope file editing and manual saving remain available. No new game
plugin, collision remesher, batch processing of these workflows, skinning support,
billboard generator, shader-UV rotation instruction, free-form shader compiler,
residual normal-map bake or automatic texture rewrite is included in this RC.
