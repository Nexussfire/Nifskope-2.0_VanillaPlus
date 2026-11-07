# Vanilla+ Geometry implementation status

## Implemented and verified

`src/geometry/vanillaplus.{h,cpp}` provides an owned, triangle-only working mesh
and height field. It has no Qt, NIF model, filesystem, or renderer dependencies.
The qmake project includes the new module. The render toolbar now has a
Vanilla+ Geometry button for a read-only preview of selected static SSE shapes.

* Diffuse grayscale and a separately named experimental normal-direction cue.
* Source weights/multipliers and Add, Multiply, Screen, Overlay, Min, Max blends.
* Black/white point, midpoint, contrast, inversion, output range, 256-bin histogram.
* Bilinear sampling with independent U/V wrap or clamp and material UV transforms.
* Bilateral height smoothing and coarse/medium/fine frequency controls, using
  the material's U/V wrapping or clamping.
* Deterministic uniform and height-adaptive triangle subdivision, shared-edge
  conformity, UV/color interpolation, normalized interpolated normals,
  preserved distinct seam indices, and preflight size budgets. Adaptive mode
  compares edge and interior height samples to the corner-interpolated height,
  weighted modestly by height variation and the fixed preview silhouette.
  Significant faces use the balanced uniform four-triangle split. Sampling
  is finite and can miss detail between probes or at the subdivision limit.
  RGBA values, including alpha, are interpolated from each original triangle's
  barycentric weights; original color bytes survive the staged round trip.
* Normal-directed displacement with neutral height, strength, and positive/negative limits.
* Optional displacement-slope limiting and local triangle-orientation protection reduce
  pinching while retaining pinned boundaries and unchanged UVs. Smooth preview shading
  is independently selectable. These safeguards do not detect all self-intersections.
* Preview and staged write-back retain authored vertex normals at zero displacement.
  Generated vertices interpolate source normals; hard-normal seams remain split.
  Displacement adds an approximate local geometric-normal correction per vertex;
  separate UV seam vertices are not welded, so corrections can differ across seams.
* Optional triangle-quality cleanup flips qualifying interior edges within one original
  source face, without moving vertices. Each flip improves the weaker triangle and
  limits surface deviation to 1% of the local edge scale; seams and original edges
  are preserved. This is a conservative local test, not a global intersection test.
* Optional attribute-aware meshoptimizer reduction operates within each original
  triangle and locks its boundary, sharp edges, and edges on the fixed preview
  silhouette. Candidate reductions must pass
  bidirectional sampled comparisons against the dense reconstructed surface in
  original-triangle coordinates. The tolerance is a percentage of the original
  bounding-box diagonal. Finite samples do not guarantee a global maximum error;
  regions exceeding the work budget remain dense.
* A staging check clones the NIF in memory, writes preview geometry into the copied
  shape, updates normals, tangents, data sizes and bounds, and checks the reopened
  copy and unrelated blocks. Staging rejects changed source/material blocks and
  shapes without tangent data, with particle data, or using tree animation or
  multi-index snow. Apply preflights replacement
  and rollback on the copy, then replaces only the selected shape in the open
  model using an undo command; it never saves files automatically.
* Validation of finite attributes/heights, indices, dimensions, normals, degenerate
  triangles, and vertex/triangle budgets. Failure leaves the source buffers intact.

The MSVC C++20 build passes `/W4 /WX`; **7,983 checks pass**. A separately optimized
demo builds and generates `C:\Utility Apps\NifSkopeBuildTools\vanillaplus-tests\preview.svg`, showing a synthetic
height field and subdivision from 2 to 512 displaced triangles (289 vertices).
The SVG is valid XML with 514 triangle polygons across the original/final views.
This is a mathematical demonstration, not an in-application screenshot.

Run from an x64 Visual Studio Native Tools command prompt:

```bat
tests\run-vanillaplus.cmd
```

Build artifacts remain under `C:\Utility Apps\NifSkopeBuildTools\vanillaplus-tests`.

## Current preview and remaining limitations

The separate **Material Graph** toolbar action displays supported Skyrim and
Skyrim SE `BSLightingShaderProperty` materials as a fixed graph. It can edit
texture paths, alpha, specular strength, glossiness and UV scale/offset; Apply
groups edits into one undo action and does not save the NIF. Shader type,
flags, clamp modes and graph connections are read-only. Slots whose effect
depends on a disabled shader feature do not activate it when edited.
Texture nodes are ordered by slot number; their plus buttons use NifSkope's
resource browser, and minus clears the staged path.
The UV transform node includes a relative offset pad (Shift constrains an axis,
Ctrl adjusts finely) and an explicitly labeled base-UV preview. Its downsampled
diffuse/checker preview applies scale then offset and per-axis wrap or clamp;
it does not simulate animation or view-dependent parallax.
The Animation node inspects directly linked controllers, timing, float keys and
interpolation; it does not create, edit, play or resolve sequence-driven or
external animation. Skyrim lighting controllers have no UV rotation channel.

The tool does not distinguish vanilla files from modded files. Supported packed
`BSTriShape` geometry can be previewed regardless of the version gate; static
`NiTriShape` and `NiTriStrips` geometry can also be previewed using the
existing triangle-strip converter. These preview paths still require a supported
`BSLightingShaderProperty` and texture set. Write-back supports the verified
Skyrim SE `BSTriShape` layout and static Skyrim BS version 83 `NiTriShape`
with one UV set and tangent arrays. Triangle strips and other versions remain
preview-only. The supplied `Jeweledlongsword.nif` has been tested in memory:
its two eligible shapes pass staged write-back, save/reopen and undo/redo, with
the original file untouched. Tests use substituted in-memory rock texture paths
because the sword's own DDS resources are not included in the project.
Shared geometry, match groups and additional vertex-data blocks are rejected.

The toolbar opens a dialog that reads supported geometry into temporary buffers,
resolves DDS resources through `NifModel`, builds a 128x128 height preview and
histogram, and renders a shaded isometric original/displaced comparison with
optional triangle edges. **Generate Height from Textures** combines available
diffuse and normal cues, conditions their range, chooses a safe subdivision depth,
and sets a mesh-scale preview displacement. The controls remain adjustable.
The preset also enables bilateral smoothing, suppresses fine-scale height noise,
and uses height-adaptive subdivision. The UI compares its triangle count with
uniform subdivision at the same maximum depth.
Before preview, the selected shape's source triangles and original UV values,
shader/texture links and paths, flags, UV transform and addressing are captured.
Each generated triangle corner carries barycentric provenance to its original
triangle. A UV mapping view overlays original and subdivided edges against the
same transformed height sampling used by displacement. Open boundaries and
duplicated UV/hard seams are pinned; they are never welded by position.
If only one source is available, the preset uses it alone; if neither is found,
the button is disabled. The normal cue is experimental and is not a physical
normal-to-height solver. The generated field is temporary and not saved as a
texture. Apply changes the open document only, supports undo/redo, and does not
save a file. Optional reduction can be previewed and included in Apply. In-game
appearance and unsupported geometry/material variants still require validation.

The preview samples each source onto a common 128x128 field. Full-resolution
sampling, normal-map channel conventions, geometric seam displacement policy,
remain limitations. Normals and tangents are rebuilt for staged write-back.
The direction cue is not an integration-based normal-to-height solver and should
be off by default in the future UI. The current mesh validator rejects zero-area
triangles but does not detect self-intersections or flipped surfaces.
Adaptive subdivision samples edges and triangle interiors; small
features between those samples can be missed. It is a preview heuristic, not an
globally error-bounded remesher.

The newly supplied assets were inventoried read-only; no NIF or DDS was modified.
The local diagnostic inventory, including SHA-256 fingerprints, is at
`C:\Utility Apps\NifSkopeBuildTools\vanillaplus-tests\asset-inventory.json`. Texture references in that report
were discovered as embedded strings, not through a completed material adapter.

The offscreen UI smoke test constructs the main window and checks the toolbar
action, opens the supplied `rockm01.nif`, loads the supplied diffuse and normal
DDS files through the resource system, exercises the generation button with two,
one, and zero sources, displacement and safety limits, checks invalid selection,
and compares serialized model bytes before and after Cancel. Run it from the
UCRT64 shell with `bash tests/run-vanillaplus-ui-smoke.sh`
after building the application. A visual on-screen viewport test remains.
