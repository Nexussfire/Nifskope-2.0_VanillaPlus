# Performance maintenance

This pass keeps the tessellation, material, and Surface Profile feature set
fixed. It removes repeated work without changing the generated geometry or
material sampling rules.

## Measured changes

The paired probe uses the supplied `rockm01.nif` and MountainSlab02 resources,
the existing Generate Height preset, and the release Qt/UCRT build. Timings are
local measurements, not universal latency guarantees.

| Operation | Before | After |
| --- | ---: | ---: |
| Switch to displaced view | 101.4 ms | 43.6 ms |
| Enable wireframe | 94.2 ms | 36.0 ms |
| Switch to flat shading | 73.8 ms | 16.3 ms |
| Enable dark-feature relocation | 213.1 ms | 97.3 ms |
| Return to Neutral profile | 102.3 ms | 81.8 ms |
| 200 unrelated edits after dense Apply | 530.3 ms | 0.060 ms |
| Open Material Graph | 299.6 ms | 160.7–197.8 ms |
| 50 UV edits in Material Graph | 175.5 ms | 134.8 ms |

The final run's Material Graph opening time was 197.8 ms; the earlier post-change
run was 160.7 ms. Vanilla+ opening remains approximately 1.1 seconds for these
4K texture inputs. Explicit staged validation remains about 62–68 ms. Those
operations have not been made artificially faster by omitting validation.

## Changes and correctness boundaries

### Presentation reuse

View, wireframe, shading, attraction-preview, and profile-mask changes now render
the last valid working/dense/final meshes. They do not rerun height filtering,
tessellation, displacement, triangle cleanup, or reduction.

Any geometry, height, safety, profile, or resource-resolution control still
updates synchronously through the full applicable pipeline. Failure invalidates
the presentation and disables Apply. Staging/Apply always perform their existing
live-source checks and save/reopen validation; validated NIF output is not cached.

The dialog retains only the current working mesh and, when reduction requires
it, a separate dense mesh. Dense presentation aliases the existing final
candidate when reduction is off or has fallen back to the dense surface.
Caches move/replace their buffers, respect the existing vertex/triangle limits,
and die with the dialog. No unbounded preview history is retained.

### Conditioned height reuse

The complete recipe includes source weights/multipliers, blend mode, histogram
controls, inversion, all smoothing/band controls, relief mode, precision, and
neutral height when shallow conditioning depends on it. Source buffers and
addressing are already immutable dialog snapshots. Profile, tessellation,
displacement-strength, cleanup, reduction, and presentation changes can reuse
the same conditioned field. Failed conditioning does not publish a new cache key.

### Sample validation

Dark-feature relocation previously called the public checked material sampler
for every search candidate. That rescanned the entire attraction buffer each
time. It now validates the field and mapping once per operation, retains the
per-query finite-UV check, and uses the existing validated sampler internally.
Addressing, interpolation, candidate ordering, and acceptance rules are unchanged.

### Document editing

The document-operation observer previously serialized every active operation's
geometry/material snapshots on every model dataChanged notification, including
unrelated block edits. It now filters known unrelated block ranges first.
Relevant shape/data/shader/texture-set edits still receive exact checks; header
and ambiguous ranges remain conservative. Existing external-edit invalidation,
Undo/Redo, and file-reopen behavior is retained.

### Material Graph

Resource refresh now uses its already fetched DDS bytes to produce both the
48-pixel thumbnail and diffuse UV-preview input in one decoder lifetime. It
avoids duplicate reads and a second diffuse decode. Explicit refresh still reads
current resource bytes: no persistent path-only resource cache was added.
Only the small output images survive; the native decoded texture is released.

UV preview precomputes independent U/V addressing once per axis rather than per
pixel. RGB/ARGB formats use direct scanline reads/writes; other image formats
retain the generic pixel path. Transform order, bilinear arithmetic, wrap/clamp,
raw premultiplied channel behavior, and finite/overflow rejection are preserved.

## Verification

`tests/run-vanillaplus-performance-probe.sh` records operation times, pixel hashes,
statistics hashes, and an applied NIF candidate hash. Every recorded preview,
statistic, Material Graph thumbnail/UV image, and complete candidate NIF matches
the before-change baseline exactly. The probe also checks staged validation,
in-memory Apply/Undo, and unchanged Cancel.

The CSV artifacts are beside the external build executable:

- `performance-before.csv`: geometry/document baseline;
- `performance-graph-before.csv`: Material Graph baseline;
- `performance-final.csv`: final maintenance implementation.

The UI regression suite also compares the fast and generic UV preview behavior
across RGB32, ARGB32, premultiplied ARGB32, RGB888, and grayscale inputs, with
non-default transforms and all clamp/wrap combinations. Existing geometry,
source authority, six-profile comparisons, stale-source, budget/failure,
save/reopen, Undo/Redo, and asset regressions remain required.

Final validation: 1,499,721 core checks and the complete UI/asset/comparison
matrix passed. The established Qt/UCRT application build linked, its runtime
resources were staged, and the installed executable passed an actual launch
test:

`C:\Utility Apps\NifSkopeBuildTools\nifskope-ucrt\release\NifSkope.exe`

It matches the current-source executable in `nifskope-resource-fix\release`.
SHA256: `9A0D6E9F824119B67CBB974E4859BAF80C5F8F4BA7ED872A2C88BFAD5AB012B1`.
No source NIF or texture files were overwritten.

## Material Graph, geometry preview and sculpt audit — 2026-11-05

Material Graph help now uses a dialog-local presenter with a 200 ms initial
hover delay and a 100 ms delay between visible help targets. Text wraps within
360 device-independent pixels, including long paths, and stays inside the graph
viewport or the owning graph/inspection dialog. It does not take keyboard or
mouse focus. Hit tests are coalesced at 16 ms; unrelated application events do
not enter help lookup. Native help popups are suppressed only for this graph.
The native Windows probe measured approximately 226 ms from hover to display.
The previous Qt style requested 700 ms; hidden-window automation did not reliably
deliver its native hover event, so that is a configured delay, not a measured
before/after latency comparison.

Animated sampling retains one small image keyed by source-image identity and
effective UV transform. Unchanged UVs reuse that image; animated panning still
resamples each changed transform. Emission swatches update their palette only
when the color changes, avoiding per-frame stylesheet parsing.

Vanilla+ retains up to four presentation images (about 3 MiB at the current
preview size). Every processing change invalidates them. Only display toggles
reuse them; staged verification and Apply are never cached. Numeric typing
commits on Enter/focus loss instead of regenerating for every digit. Enter in
a numeric control commits that field without activating a dialog button.

Sculpt caches immutable reference-face normals and lengths for safety checks,
reuses the existing pre-dab positions during backtracking, and calculates brush
normals only for tools that use them. No-op dabs avoid renderer refreshes.
Stroke-motion throttling now precedes expensive picking. Face safety, boundary
locks, undo history and stroke-end tangent reconstruction remain in place.

Representative measurements from the same machine/build configuration:

| Operation | Before | After |
| --- | ---: | ---: |
| 240 unchanged-UV animation frames | 460 ms | 37 ms |
| 240 color-only animation frames | 491 ms | 39 ms |
| 240 animated-pan frames | 475 ms | 455 ms |
| Return to cached smooth preview | 42 ms | 0.43 ms |
| 20 Smooth dabs, 18,432-triangle grid | 30 ms | 7.1 ms |
| 20 Flatten dabs, same grid | 29 ms | 7.1 ms |

Initial 4K texture/source preparation remains approximately 1.1 seconds and
full geometry regeneration approximately 132 ms in this test. This pass does
not bypass those operations or retain large decoded texture buffers.

Artifacts alongside the external executable:
`newfeatures-performance-before.csv`, `newfeatures-performance-after-final.csv`,
`performance-ui-audit-before.csv`, `performance-ui-audit-after.csv`, and
`materialgraph-bounded-help.png`. Geometry preview/statistics/candidate hashes
match before and after. All four sculpt tool position fingerprints match.
The tooltip probe also covers embedded UV controls, rich help text, owned
shader-flag dialogs and returning to the graph after those dialogs close.

Final validation passed: 1,499,721 geometry core checks, 914 sculpt core checks,
geometry UI, native sculpt UI, material Pan/named-state, effect-material and
BS 83/100 shader/flag/seven-input regressions. The established Qt/UCRT build
linked and staged successfully. The installed executable opened `rockm01.nif`
in a real document window and closed normally; the source file hash did not
change. Built and installed executables match SHA256
`1D47E50B0FF8AF1CC53E6ADE1A4EA92362DE22ADC8829867FC4B74920CE10228`.

Runnable application:
`C:\Utility Apps\NifSkopeBuildTools\nifskope-ucrt\release\NifSkope.exe`.
