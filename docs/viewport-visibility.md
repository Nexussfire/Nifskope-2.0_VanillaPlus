# Block-list viewport visibility

The normal block list has a 22-DIP eye gutter to the left of its existing
columns. It works in hierarchy and flat-list modes, without changing the NIF
model's columns, delegate, editing, selection, or expand/collapse controls.
Only actual blocks represented by scene nodes receive eyes; field/property
rows and metadata blocks do not. Right-clicking the gutter offers Reset
viewport visibility.

Eyes set transient Scene visibility overrides, never NiAVObject flags or
visibility-controller data. A node override can reveal an authored-hidden
object, subject to its parent's visibility. Hiding a parent hides its children;
showing it retains their independent hidden choices. Render Show Hidden does
not bypass explicit eye hiding. Selection overlays and associated node marker /
collision rendering respect eye hiding too; collision geometry is not edited.

Overrides are keyed by stable item pointers with persistent-index lifetime
checks, so block renumbering cannot transfer them to another object. Scene
recompiles retain valid overrides. Switching/reloading the renderer document
clears them. They are per viewport, not stored in files/settings or Undo history.
Existing authored visibility resumes after Reset.

`tests/run-block-visibility-probe.sh` exercises the actual NifSkope window and
renderer with rockm01 and Townportal01: hierarchy/flat eye clicks, parent/child
behavior, Show Hidden, authored-hidden reveal/reset, unchanged selection, gutter
alignment after resizing, metadata exclusion, block renumbering, reload/reset,
and unchanged NIF bytes, Undo history and modified state. Native screenshots
are staged beside the external test executable.

Verified 2026-11-06: native eye tests passed on rockm01 and Townportal01 in both
list modes, including actual document reload. Sculpt UI, material-layer UI,
geometry/material UI and new-feature performance regressions passed. The normal
renderer keeps its original fast visibility path when no eye overrides exist.

The established Qt/UCRT build was staged and installed at
`C:\Utility Apps\NifSkopeBuildTools\nifskope-ucrt\release\NifSkope.exe`.
Built and installed SHA256:
`7961EB4E4904270FAE271F67C1AF5BD8BE5F115D4BBA83786E753718BF358A6C`.
The installed application opened Townportal01 and closed normally in the launch
smoke test; the source file hash was unchanged.
