# Repeat-pass surface feedback investigation

This records the pre-fix investigation. The document-lifetime correction and
updated repeat-probe results are described in
[Surface Profiles and refinement](surface-profiles-and-refinement.md).

The Apply → reopen Vanilla+ → Apply sequence reuses the already displaced
positions and newly reconstructed normals as the next authoritative source.
There is no retained original displacement reference or subtraction of relief
already represented by the mesh. The same height is applied again.

Relevant paths:

- `inspectShape` in `src/ui/vanillaplusdialog.cpp` reads the live vertex positions
  and stored normals, then calls `captureSource` for this new dialog session.
- `VanillaPlus::displace` samples `(H - neutral) * strength` again and adds it
  along each current vertex normal to each current position.
- `stageShape` writes `shadingNormals` into the replacement NIF geometry.
- `shadingNormals` preserves authored shading by adding the difference between
  normalized, area-weighted moved and reference geometric normals.

For a simplified repeated sample, the sequence is therefore
`P1 = P0 + d*N0`, then `P2 = P1 + d*N1`, not refinement toward the same target.
After the first pass, normals tilt with the reconstructed relief. The next
displacement has a new tangential component relative to the original surface.
Outward-facing directions can spread protrusions; inward-facing directions
can crowd troughs. On an already curved object this can produce locally lobed
or pinched growth. A zero average signed displacement does not remove this
feedback, and face-inversion protection does not make repeated relief a
refinement operation.

## Controls separating the causes

`tests/run-vanillaplus-repeat-probe.cmd` runs a synthetic plane with a fixed
sinusoidal height field. It adds no triangles and uses no adaptive weighting,
Dark Feature Bias, cleanup, reduction, or height reconstruction filters.

| Case | Pass 1 lateral RMS | Pass 2 | Pass 3 |
| --- | ---: | ---: | ---: |
| Zero displacement | 0 | 0 | 0 |
| Reapply relief, fixed normals | 0 | 0 | 0 |
| Reapply relief, updated normals | 0 | 0.00646832 | 0.0174229 |

Fixed-normal vertical RMS is 0.0210905 → 0.0421810 → 0.0632716: direct stacking.
Updated normals produce additional sideways geometric deformation. Independently
computed area-weighted geometric normals agree with `shadingNormals` within
5.76e-8 throughout the updated-normal experiment. Thus a normal-averaging error
or adaptive curvature overcorrection is not required to produce this behavior.
This does not claim every imported authored normal field is identical to its
geometric normal field.

`tests/run-vanillaplus-repeat-nif-probe.sh` repeats actual UI Apply, in-memory
save, and reopen on the supplied `rockm01.nif`. First pass uniformly subdivides
once; later passes use depth zero, retaining 504 triangles. Strength is fixed
at 3, Dark Feature Bias/cleanup/reduction/patch retopology are off, and existing
displacement slope/bias protections remain active.

| Rock pass | Original-vertex total movement RMS | Lateral RMS relative to initial normals |
| --- | ---: | ---: |
| 1 | 0.332005 | 0.00000113825 |
| 2 | 0.663916 | 0.0109982 |
| 3 | 0.995658 | 0.0326250 |

The zero-strength control remains exactly unchanged across all three Apply/
reopen cycles. Additional deformation therefore occurs without additional
triangles, adaptive curvature weighting, or feature relocation. The measurements
describe actual vertex positions, not a shading-only illusion. No source NIF
or texture files were changed.

Generate Height also derives its suggested strength from the currently loaded
bounding-box diagonal. A changing envelope can change that suggestion on
later passes; this is a potential further amplification, but was excluded by
the fixed-strength experiment above.

## Consequence for a correction

Repeated refinement needs an immutable macro-surface reference and accounting
for displacement already represented, rather than applying the complete
texture-derived relief to the latest positions again. A local normal smoothing
change alone would obscure the symptom without removing the stacking.
The current NIF write-back does not persist original-source provenance for a
later session; it must not guess or destructively flatten an imported asset to
recover that reference.

This change adds reproducible investigation probes and records their results.
It does not alter application behavior or claim repeated Apply is fixed.
