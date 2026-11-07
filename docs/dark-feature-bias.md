# Dark Feature Bias prototype

Implemented as an optional, disabled-by-default sample-placement experiment.
The existing height reconstruction, displacement, cleanup, reduction, and NIF
commit paths are retained. No texture files or source NIF files are written
automatically.

## Controls and conservative limits

- Enable Dark Feature Bias: off initially.
- Attraction Preview: shows the depression residual, with display brightness
  multiplied by four. This display toggle does not enable relocation.
- Threshold: 0.01 conditioned-height units initially, before display amplification.
- Attraction Strength: 0.5 initially, range 0–1.
- Maximum Shift: 0.10 initially, range 0–0.25 times the shortest incident edge
  in the unbiased generated mesh.

Attraction is `max(0, Gaussian(H, sigma) - H)` from the final conditioned height.
Sigma is two reference pixels at 128², four at 256². Filtering and sampling use
the snapshotted addressing and `UV * Scale + Offset` transform. No diffuse-only
darkness weighting or extra displacement multiplier was added.

Search is derived automatically: two rings at 0.25 and 0.5 local edge lengths,
with 16 directions each in the source triangle's tangent plane. A bounded step
may approach a stronger nearby target without reaching its attraction peak;
it may not reduce the attraction already captured at the sample. There is one
deterministic relocation pass, not repeated snapping or global optimization.

The minor domain restriction is deliberate: only generated vertices strictly
inside one working-reference triangle can move. Source vertices, seed vertices,
reference edges, UV/hard seams, and open/protected boundaries stay fixed.
For a verified patch, its reference triangles may span former original
diagonals. Original source correspondence still resolves through the existing
patch authority, including compatible shared-edge equivalence. This pass does
not relocate across working-reference edges.

## Safety and data flow

`relocateDarkFeatures` runs between tessellation and ordinary displacement.
Position, UV, normal, and vertex color are recovered together from source
barycentrics. Every incident face's working-source barycentrics update in the
same transaction. The immutable source snapshot is retained.

Candidates must remain inside the permitted reference face and inside the
positively oriented incident fan. Each incident face must retain at least 25%
of its current source-domain signed area. Triangle quality must stay above
`min(0.10, 0.8 * current quality)`. Stored float geometry also receives an
orientation/area check. A positive incident-fan partition on the same source
triangle preserves coverage without crossing edges or introducing a new
surface intersection; other source triangles and all shared edges stay fixed.
Ambiguous patch lookup or incompatible source attributes reject the proposal.
Rejected proposals update neither vertices nor provenance.

Diagnostics report eligible samples, nearby above-threshold evidence, proposal
and accept/reject counts, topology and provenance rejection counts, ambiguous
samples held fixed, and mean/maximum movement in mesh units. Disabled, zero
strength, and zero maximum shift return the original working geometry exactly.

## Recorded experiment

The synthetic trough fixture uses the same 512 triangles for both outputs,
with a narrow known depression between baseline sample columns. With strength
1 and maximum shift 0.25, 28 generated samples move. Fixed independent probes
of actual displaced triangle interpolation measure:

| Region | Baseline mean absolute error | Biased error |
| --- | ---: | ---: |
| Trough core | 0.0388966 | 0.0186422 |
| Wider neighborhood | 0.0103476 | 0.0118849 |

The core improves by about 52%; the wider neighborhood worsens by about 15%.
This is a mixed experimental result, not proof of global improvement. Moving
samples can capture a missed trough while spreading its linear relief into
neighboring triangles. Other trough alignments also produced worse wider-area
error. No acceptance claim is made for every groove or tessellation budget.
Curve tracing and feature-aligned connectivity remain unimplemented.

The real-rock A/B fixture is `rockm01.nif`, using the supplied MountainSlab02
diffuse and normal DDS resources. Both runs use the existing Generate Height
preset and retain 3,807 vertices / 7,211 triangles. At the default experimental
settings, 2,319 samples are eligible; 1,701 proposals are accepted, none rejected.
Mean movement is 0.1755 mesh units, maximum 0.3479. Ordinary safety processing
remains active. Requested/applied displacement RMS is 1.244/1.175 units for
baseline and 1.245/1.176 for biased. These numbers establish relocation at equal
density, not a measured reconstruction-error improvement on the real rock.

The rock's attraction preview, shaded comparisons, and wireframes are written
beside the test executable in the existing external build directory:

- `dark-rock-attraction.png`
- `dark-rock-baseline.png` and `dark-rock-baseline-wire.png`
- `dark-rock-biased.png` and `dark-rock-biased-wire.png`

`tests/run-vanillaplus.cmd` exercises uniform dark/bright fields, a broad gradient,
the fixed-budget trough comparison, zero/disabled identity, UV recovery,
mirrored/non-default transforms and all four addressing combinations, patch
source resolution, ambiguous provenance fallback, pinned boundaries, local
movement limits, determinism, and a forced topology rejection.

`tests/run-vanillaplus-ui-smoke.sh` records the real-rock A/B, verifies the
independent attraction preview, stages biased displaced geometry through the
existing save/reopen validator, and exercises biased undisplaced Apply with
reduction, source attributes, stale-source rejection, exact Undo/Redo, and
Cancel. Existing farmhouse, sword, sconce, material graph, and UV fallback
regressions remain part of validation.

Final validation: 24,598 core checks passed, plus the rock/UI/material graph,
farmhouse patch/write-back, sconce, classic sword, and farmhouse UV fallback
suites. The established Qt/UCRT application build linked successfully with its
existing staged runtime dependencies. The final installed executable launched
and remained responsive:

`C:\Utility Apps\NifSkopeBuildTools\nifskope-ucrt\release\NifSkope.exe`

It matches the executable built from this source tree in
`C:\Utility Apps\NifSkopeBuildTools\nifskope-resource-fix\release`.
SHA256: `A5B1D721C92D8204843C08FFC74DFDAE933552F7B965D98D0D3A433BA55EFB4A`.
