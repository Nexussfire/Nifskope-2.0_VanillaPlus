# Local displacement limiting

The section-bias correction exposed an interaction in `VanillaPlus::displace`:
centering the signed offsets can reintroduce a slope violation at a pinned seam.
The final uniform contraction then used that one edge to attenuate every vertex
in the shape. The global face-orientation scale did not report this contraction,
so a preview could report 100% while its relief was nearly flat.

The existing mesh, height sampling, source correspondence, seam pins, and
orientation checks remain in use. For bias-corrected displacement, the final
slope constraint now uses shortest-path Lipschitz envelopes on the existing
triangle edges. Distance-to-pin cones fix boundary values to zero. Signed-limit
clipping and a convex blend of the envelopes preserve slope bounds; a separate
area-weighted blend in each connected section preserves zero mean. No vertex
position, UV, or source attribute is averaged with a new geometric neighborhood.
The legacy path without section-bias correction is unchanged.

The regression fixture contains a short interior edge next to a pinned seam,
with legitimate relief farther away in the same surface. Before this change its
distant relief was 0.00003535 units; afterward it is 0.05955 units. Tests also
check every edge's slope, pinned positions, original UVs, zero section bias,
determinism, and independence of disconnected sections.

Preview statistics report vertex RMS requested displacement after initial bias
removal and signed limits, and actual position changes after safety processing,
before connectivity cleanup and reduction. This measures attenuation; it does
not establish that an experimental normal-derived scalar reconstructs true
height or that a finite preview field can represent every texture feature.
