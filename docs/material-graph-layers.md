# Material Graph animated layers

The graph is a modeless editor over a private document snapshot. Existing
material controls and verified Pan authoring are reused. A snapshot is committed
with the existing tail-block/delta undo mechanism; it never reloads the open
document or saves its file. Concurrent document changes cause Apply to refuse.

Create animated layer first stages the current material edits privately, copies
the selected static render branch with NifModel::saveIndex/loadAndMapLinks, then
attaches the copy to the same unambiguous NiNode parent. The copied material,
texture set, geometry data, alpha property and supported controller/data blocks
are independent. A fresh copy of the original document supplies all original
blocks: edits from the first graph carry into the new layer only. Original
blocks are byte-checked except for the parent's appended child link.

The initial supported branch consists of static BSTriShape/NiTriShape/NiTriStrips,
their geometry data, Skyrim lighting/effect shader and texture set, alpha
property, shader float/color controllers and float/point interpolators/data.
Unknown children, skinning, collision, shape animation, external animation
targets and sequence-managed controllers fail safely. No billboard or shader
rotation representation is invented.

The main render area temporarily offers Original and Animated layer preview
tabs. A separate ordinary GLView renders the private document; the original
viewport and its camera remain intact. Dedicated local translation offsets,
rotation offsets and relative scale affect the temporary shape transform, not
its vertices. Material Graph remains modeless. The block editor still refers
to the original document; new-layer placement uses its dedicated controls until
Apply. Closing/Cancel removes the temporary tab and restores the original view.

The selected layer's material preview receives the graph's resolved UV/color
animation values, while other layers use the normal scene renderer. Its stored
controllers remain untouched in the candidate; only the private display model
disconnects that material's controllers to avoid applying animation twice.
Depth, transparency and alpha properties are inherited, not guessed. Zero
translation can still overlap the original; the artist chooses separation.
This is an authored duplicate of the source surface, not an automatically
expanded hull or a generated billboard.

The private document loads with the original filename as its resource context,
retaining the existing NIF-relative resource lookup and configured game sources.
Shader scalar edits, texture paths and resolved animation values update only
the private display model. Updates skip unchanged values and reuse the existing
small sampling preview cache.

Before final Apply, the candidate must reopen and reproduce the original bytes
on rollback, then reproduce the candidate bytes on redo. A successful Apply is
one Undo action, with created-block identity recovery reused from Pan editing.

## Verification — 2026-11-06

`tests/run-materialgraph-layer-probe.sh` exercises the actual modeless graph and
native GLView tabs with rockm01, Townportal01 and Jeweledlongsword. It verifies
that first-graph edits transfer to the new layer only; local XYZ offsets,
rotation and scale; independent material/Pan links; byte-identical copied vertex
data, triangle indices and classic geometry data; unchanged original blocks
other than the parent's child list; candidate save/reopen; exact Undo/Redo;
Cancel; stale-document refusal; existing-surface material editing; and safe
rejection of shape animation. Native preview teardown is synchronous before
the private document is released, covering repeated creation/Apply/Cancel.

120 native layer preview updates took approximately two seconds for each
asset. The existing Pan/named-state, effect material, shader/flags/seven-input,
geometry UI and new-feature performance regressions passed. The unchanged-UV
animation cache and bounded tooltip checks continue to pass.

The Qt/UCRT build and installed runtime match SHA256
`50A367ED55E075BB8151ED91AC8344B17588358DFFCDBBFC5DEE61D7A43205A2`.
Runnable application:
`C:\Utility Apps\NifSkopeBuildTools\nifskope-ucrt\release\NifSkope.exe`.
The installed executable opened Townportal01 in an actual document window and
closed normally during the launch smoke test; its source file hash was unchanged.
