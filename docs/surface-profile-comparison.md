# Comparative Surface Profiles

The profile set is Neutral, Rock, Wood, Masonry, Metal, Soil. Selection is manual.
No subtype inference, Auto mode, snow/deformable profile, new geometry algorithm,
renderer change, or new dependency was introduced.

## Shared operators and response

`analyzeSurfaceMorphology` measures the signed field once, independently of the
selected semantics. `applySurfaceProfile` interprets those immutable measurements
using a small const `SurfaceProfileParams` record. `surfacePrior` remains the
convenience wrapper; Neutral bypasses analysis/filtering exactly.

The shared measurements are the existing 8-reference-pixel broad band,
complementary fine band, 2-pixel ridge/valley residual, Hessian anisotropy and
same-sign bump/bowl classification, and gradient structure tensors. The same
tensor operator is evaluated at the appropriate scales: broad coherence uses
the broad field and an 8-pixel neighborhood; fine coherence uses the original
signed field and a 2-pixel neighborhood. This avoids misinterpreting fine grain
or noise using a direction measured only from the broad envelope. All radii
scale with texture preview resolution and never with tessellation depth.

The fixed coefficients below are experimental priors, not physical constants:

| Profile | Broad base | Incoherent broad | Blob | Ridge guard | Valley guard | Fine guard | Incoherent fine |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Rock | 0 | 0 | 0.65 | 4 | 4 | 4 | 0 |
| Wood | 0 | 0.55 | 0.25 | 4 | 5 | 4 | 0.10 |
| Masonry | 0.40 | 0.45 | 0.10 | 2 | 10 | 4 | 0.12 |
| Metal | 0.70 | 0.20 | 0.05 | 2 | 2 | 2 | 0.65 |
| Soil | 0 | 0 | 0 | 0 | 0 | 0 | 0 |

Broad support is reduced by fine, ridge, and valley evidence according to the
guard coefficients. The broad attenuation combines base attenuation, locally
incoherent response, and the same-sign isotropic blob term. Fine attenuation
uses the fine-scale coherence. Both components feed one candidate:

`candidate = D - broadMask * D_broad - fineMask * D_fine`

Every processed profile then clamps the candidate into `[min(0,D), max(0,D)]`.
The existing sample-time signed clamp also remains, preventing bilinear
interpolation from introducing sign reversal between opposing texels.

Rock retains its established broad response and fine preservation; factoring
the measurement pass can introduce only ordinary floating-point roundoff.
Wood uses local coherence without a global grain direction. Masonry gives
additional protection to localized valley/joint evidence. Metal is deliberately
the most conservative interpretation of broad and incoherent fine requests.

Soil explicitly permits isotropic and irregular evidence: its target equals
the original signed field. It is deliberately not Rock with a small blob
penalty. Morphology descriptors remain available, but Soil assigns no semantic
penalty to them. Consequently Soil and Neutral agree on target displacement;
Neutral differs architecturally by skipping morphology analysis entirely.
They are not forced to look different by adding arbitrary filtering.

The UI reuses the measurement cache when switching non-Neutral profiles. The
target cache still excludes depth, cleanup, reduction, and view changes. The
suppression preview now reports actual pointwise target-amplitude attenuation,
0–1, comparably across profiles. This changes the meaning of the old Rock-only
blob-mask diagnostic, not its displacement response. The legacy checkbox object
name remains for test compatibility; its visible label is profile-neutral.

## Shared synthetic fixture

`tests/run-surface-profile-comparison.sh` generates a common 256² height field
with ten smoothly bounded regions, in two rows of five:

1. broad radial bump;
2. broad radial bowl;
3. narrow radial pit;
4. elongated ridge;
5. elongated fissure;
6. saddle;
7. parallel bands;
8. deterministic incoherent roughness;
9. planar unit with narrow recessed boundaries;
10. curved directional grain with one localized radial knot-like feature.

All profiles receive identical pixels, signed strength, neutral, scales, and
addressing. The fixture does not secretly change sampling density or masks for
different materials. CSV regional measurements record original/target RMS,
retained squared-displacement energy, and mean amplitude attenuation.

Selected retained-energy results, as percentages:

| Profile | Radial bump | Fissure | Bands | Incoherent roughness | Knot-like grain region |
| --- | ---: | ---: | ---: | ---: | ---: |
| Neutral | 100 | 100 | 100 | 100 | 100 |
| Rock | 98.41 | 99.94 | 100.00 | 100.00 | 100.00 |
| Wood | 89.17 | 99.38 | 99.94 | 82.94 | 99.15 |
| Masonry | 85.93 | 98.86 | 99.93 | 79.67 | 98.88 |
| Metal | 46.60 | 95.49 | 99.63 | 18.09 | 94.68 |
| Soil | 100 | 100 | 100 | 100 | 100 |

These are measured outcomes, not hard-coded ranking tests. The atlas bump is
broad relative to its narrow pit but smaller than the standalone broad-bump
control used by the existing Rock tests. The established larger-scale control
still shows peak 0.399576 → 0.304672. Likewise strongly coherent fine bands can
remain admissible even for Metal: the semantic table is not a mandate to reduce
every named feature by a predetermined percentage.

## Real meshes and artifacts

The comparison script also tests every profile on:

- rock: `rockm01.nif`, first BSTriShape, MountainSlab02 material;
- wood: `farmhouse02.nif`, block 22, WoodWall01 material;
- masonry: `farmhouse02.nif`, block 15, StoneWall01 material;
- metal: `dwewallsconce03.nif`, block 5, DweDoor01 material.

Each comparison holds source, material reconstruction, generated strength,
uniform depth 2, and geometric safety fixed. Dark Feature Bias, cleanup,
reduction, and patch retopology are disabled for the comparison. All six
profiles retain identical vertex/triangle counts for each fixture. Each passes
the existing staged save/reopen validator. Returning to Neutral restores its
exact preview; Cancel leaves the complete source-model bytes unchanged.

For example, the metal fixture retains 5,861 vertices / 9,600 triangles.
Its field RMS is 0.8411 for Neutral and 0.6180 for Metal; broad energy falls
from 0.2789 to 0.1261. Fine-band energy changes from 0.03472 to 0.02680.
This is a comparative response, not proof of an objectively correct material
reconstruction. Sensible strength/detail settings are still required.

No real dirt/ground mesh with its referenced texture pair was available. This
is reported rather than substituting unrelated textures. Soil is covered by
the synthetic fixture and comparisons on all four available real meshes.

The exported interactive comparison and data live outside the source folder:

`C:\Utility Apps\NifSkopeBuildTools\nifskope-resource-fix\release\profile-comparison\comparison.html`

The page switches fixture/profile images beside Neutral and the attenuation
mask. Images are grayscale geometry previews, not textured viewport renders.
`synthetic.csv` contains all 60 regional results; `real-meshes.txt` records the
complete geometry/field statistics. PNGs include all profile outputs and masks.

The immutable document/shape reference, explicit file-reopen boundary,
Undo/Redo, UV authority, source attributes, and existing independent realization
safety remain unchanged. No profile gains authority to redefine the source.

## Validation and runnable build

1,499,721 core checks pass, plus the complete geometry/UI/asset regressions.
All six profiles pass actual repeated Apply/reopen with byte-identical targets,
changed-setting Undo/Redo, external-edit invalidation, and explicit file-reopen
fallback. Rock/wood/masonry/metal comparisons all pass staging and unchanged
Cancel. All 60 comparison images/masks are present.

The current-source Qt/UCRT application builds and links, its runtime resources
are deployed, and the following executable passed an actual launch test:

`C:\Utility Apps\NifSkopeBuildTools\nifskope-resource-fix\release\NifSkope.exe`

SHA256: `958681F69DA13FA56187CDC492A06DC808E1EF7D196A8F0A0427ECA35F1B4DBA`.

The user's existing window was running from `nifskope-ucrt\release`; it was
left open and that executable was not replaced. Use the new build path above
for this comparison milestone. No source NIFs or texture files were overwritten.
