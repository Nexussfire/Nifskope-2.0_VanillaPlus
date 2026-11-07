# Document reference refinement and Surface Profiles

This records the initial Neutral/Rock milestone. The later six-profile
comparison, shared analysis, and updated mask diagnostics are documented in
[Comparative Surface Profiles](surface-profile-comparison.md).

## Reference authority

Vanilla+ now retains its immutable geometry/material snapshot in a document-owned
`OperationStore`, keyed by selected shape. Apply commits a candidate through the
existing `QUndoStack`; it does not replace the operation's reference. Reopening
the dialog restores the reference and the last committed control settings.

The dialog also captures a separate live snapshot. Before staging or Apply,
current shape/data/material bytes and the NIF version fields must still match
that live snapshot. Refinement staging uses a private copy of the current NIF,
restores the selected reference geometry there, and runs the existing geometry,
UV, normals/tangents, bounds, unrelated-block, and save/reopen validators. The
live document is never rolled back for a preview. Undo commands replace the
actual live geometry and update the operation's matching settings/state.

The store is a QObject child of `NifModel`, not a second history or a NIF block.
Reference/candidate source correspondence stays on the existing temporary Mesh
and PatchAuthority representations. Undo history carries the before/after
settings for each Apply. Exact shape/material byte checks and model signals
invalidate references after incompatible external edits. Root structural edits
are conservatively invalidating; unrelated block value/array edits do not
invalidate an otherwise matching operation. Header, selected material, and
texture-set edits also invalidate it conservatively.

Loading a file resets these operations. No provenance is inferred or persisted
in a NIF or sidecar. The UI explicitly distinguishes retained-reference
refinement from a new operation on the currently imported geometry. A source
that was already displaced before file reopen cannot be undisplaced by this
feature; use the original asset when that original reference is required.
Cancel does not change the NIF or committed settings.

## Neutral first

The reference correction was implemented and tested before adding morphology.
The actual rock Apply/close-tool/reopen-tool sequence, with fixed strength 3,
no adaptive tessellation, no Dark Feature Bias, no cleanup, and no reduction,
now reports:

| Pass | Original-vertex movement RMS | Lateral RMS | Triangles |
| --- | ---: | ---: | ---: |
| 1 | 0.332005 | 0.00000113825 | 504 |
| 2 | 0.332005 | 0.00000113825 | 504 |
| 3 | 0.332005 | 0.00000113825 | 504 |

The NIF bytes are identical across all three Apply cycles. Previously movement
grew 0.332005 → 0.663916 → 0.995658. The same probe verifies settings restoration,
changed-strength Undo/Redo, exact source Undo, candidate Redo, external vertex
edit invalidation, and file loading into the same model ending the operation.
The Rock-profile repeat case is also byte-identical at 0.331569 RMS.

## Optional Rock target

Neutral is the default and bypasses morphology processing exactly. Rock is an
optional texture-space prior, not a material classifier or a geological model.
No Auto mode or additional material classes were added.

The core `surfacePrior` function constructs signed displacement
`D = (conditionedHeight - neutral) * strength`. It uses fixed reference-texture
scales: 8 pixels for the broad band, 2 for local ridge/valley evidence, and 8
for averaging the structure tensor. These radii double in the 256² preview;
they never depend on generated edge spacing or tessellation depth.

The structure tensor is built from gradients of the broad signed field, so
opposing ridge/fissure banks reinforce a shared orientation. Closed-form 2×2
eigenvalues supply tensor coherence and Hessian anisotropy. Only significant
same-sign Hessian curvatures qualify as blob/bowl candidates. Saddles and
nearly flat numerical noise are excluded. Broad support is reduced by existing
fine-band and local ridge/valley evidence.

The bounded mask is a product of isotropy, low orientation coherence, and broad
support. The conservative fixed attenuation coefficient is 0.65:

`candidate = D - 0.65 * mask * D_broad`

`target = clamp(candidate, min(0,D), max(0,D))`

The signed bound is applied both at texels and after bilinear material sampling.
Texel-only bounds would still allow a sign reversal between opposing neighbors.
The target cannot itself amplify, reverse, or invent requested displacement.
Existing independent section-bias and geometric safety processing remains in
place during realization; the profile's target bounds should not be confused
with a claim that those existing operations have been removed.

The UI caches target construction by conditioned height, strength, neutral,
profile, and the snapshotted material addressing. Depth/view/cleanup/reduction
changes do not reconstruct the morphology field. Adaptive tessellation samples
the bounded target in normalized height units; ordinary displacement samples
it in mesh units. Zero-strength Rock targets are flat. Dark Feature Bias still
uses the original conditioned height for feature evidence and only places
samples; it receives no new authority over displacement amplitude.

## Results and limits

Synthetic tests cover Neutral identity, broad positive bumps and negative bowls,
narrow radial pits, directional ridges/fissures, saddles, parallel bands, noise,
positive/negative/zero strength, interpolated signed bounds, rounded reference
preservation, and fixed-target depth 0–4 measurements.

A broad synthetic bump peak falls from 0.399576 to 0.304672. Its broad energy
falls from 0.00571649 to 0.00495297. Narrow pits retain over 95% of the tested
peak; directional ridge/fissure peaks and parallel-band RMS retain over 99%.

For the fixed target, depth 1–4 displacement envelope remains 0.304672. Mean
surface approximation error at depths 1–4 is 0.0447749, 0.0128827, 0.00532769,
0.00124211. Depth 0 error is 0.034486, so improvement is not claimed to be
monotonic at every insufficient resolution. These convergence measurements
isolate target sampling without realization safety constraints.

Real `rockm01.nif` Neutral/Rock comparison uses the same original source and
4,260 vertices / 8,064 uniform triangles, with identical height and safety
settings. Field RMS is 1.271 → 1.268; broad energy 0.3507 → 0.3468; fine-band
energy 0.1752 → 0.1754. Signed range is [-3.512,3.544] → [-3.511,3.543]. Mean
mask is 0.57%. The effect on this fixture is modest; it does not establish a
large visible improvement for every material. The important repeat-pass
correction is independently established under Neutral.

Preview artifacts beside the external build executable are
`surface-rock-neutral.png`, `surface-rock-profile.png`, and
`surface-rock-suppression.png`. Suppression preview displays the raw 0–1 mask.
No final vertex smoothing, new reducer, feature tracing, collision edit,
texture edit, NIF version change, or automatic file save was introduced.

## Final validation and runnable build

319,940 core checks pass. The final linked application objects pass the
Neutral/Rock repeat lifecycle, Rock and Dark Feature UI, exact UV bit-stream,
material graph, farmhouse patch/fallback, sconce, classic sword, staged
write-back, save/reopen, Undo/Redo, and unchanged Cancel regressions.

The established Qt/UCRT build and runtime deployment completed. The installed
executable matches the current-source build and passed an actual launch test:

`C:\Utility Apps\NifSkopeBuildTools\nifskope-ucrt\release\NifSkope.exe`

Build directory: `C:\Utility Apps\NifSkopeBuildTools\nifskope-resource-fix\release`.
SHA256: `C40495E0780B0B185F6F9C068B4CBCCCC84C055458407E04FB4919F7749A649F`.
