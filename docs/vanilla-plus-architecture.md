# Vanilla+ Geometry architecture

## Scope and inspected paths

The first target is an exact, static `BSTriShape` in Skyrim SE (NIF
20.2.0.7, user version 12, BS version 100). Derived shapes, skin links or
skinning flags, eye attributes, and unknown vertex layouts must be rejected
until explicitly supported. No collision, texture, shader-link, transform, or
unrelated block edits belong in this operation.

| Responsibility | Existing code to reuse |
| --- | --- |
| Geometry access | `SimplifyMeshDialog::findBlock/loadGeometryData` in `src/spells/simplify.cpp`; `BSShape::updateData` in `src/gl/BSShape.cpp` |
| Storage | `BSTriShape` and `BSVertexDataSSE` in `build/nif.xml`; `Vertex Data` rows contain Vertex, UV, Normal, Tangent, split Bitangent components, optional Vertex Colors |
| Indices | `NifModel::getArray<Triangle>` / `setArray<Triangle>`; each `Triangle` has three 16-bit indices |
| Shader/texture links | `BSShaderLightingProperty::updateImpl` and `BSLightingShaderProperty::updateParams` in `src/gl/glproperty.cpp`; shape Shader Property -> Texture Set -> Textures |
| Resource resolution | `NifModel::findResourceFile/getResourceFile`, delegating to the existing game resource system (loose files and archives) |
| CPU DDS decode | `DDSTexture16` in `lib/libfo76utils/src/ddstxt16.hpp`; `DDSTexturePreview` in `src/ui/widgets/ddspreview.cpp` demonstrates decoding and display without GPU readback |
| Viewport | `BSShape::drawShapes`, `Shape::bindShape/drawWireframe`, `Scene`, and `GLView`; render an owned preview without replacing the live shape's buffers |
| Operation registration | `Spell` / `REGISTER_SPELL`, Mesh page; account for `SpellBook::cast`'s processing-state and confirmation behavior |
| Reduction | `SimplifyMeshDialog::updateIndices` already calls `meshopt_simplifyWithAttributes` with normal, UV, and color weights and optional border locking |
| Size maintenance | `spRemoveWasteVertices::updateBSTriShape`; also handles optional SSE particle arrays |
| Unused vertices | `spRemoveWasteVertices::cast`; compact temporary buffers before commit instead of mutating the live model during preview |
| Bounds | `spUpdateBounds::cast`, using existing bounding-sphere facilities |
| Normals | `spFaceNormals::cast` in `src/spells/normals.cpp`; area-weighted normals by existing index connectivity |
| Tangents | `spTangentSpace::cast` in `src/spells/tangentspace.cpp` |
| Undo | `NifModel::undoStack`, `ChangeValueCommand`, `ArrayUpdateCommand` in `src/model/undocommands.*` |

## Constraints discovered

* SSE stores **both** Num Vertices and Num Triangles as `ushort`: maximum
  65,535 of either in the saved shape. Enforce this before narrowing counts.
  A bounded temporary representation can eventually exceed these counts,
  but must reduce successfully before Apply is enabled.
* The existing simplification dialog writes to the model on changes. Reuse
  its meshoptimizer call and attribute convention, not the dialog as a preview.
* `ArrayUpdateCommand::undo` is explicitly unfinished. It cannot provide
  transactional geometry undo. A new command must own complete before/after
  shape geometry snapshots, including derived arrays, sizes, and bounds.
* `SpellBook::cast` treats ordinary Mesh spells as mutating batch operations.
  Opening/canceling a preview must avoid marking the document changed. Add a
  narrow transactional-spell hook when integrating; do not mislabel a mutating
  Apply operation as a constant spell.
* Normal maps encode direction, not height. The initial scalar experiment
  must be named and isolated, default off, and must not be advertised as a
  physical normal-to-height reconstruction.
* Distinct vertex indices at UV/hard seams must remain distinct. Shared-edge
  subdivision keys use indices, never position welding. Different UV samples
  can still open displacement cracks at seams; index preservation alone does
  not solve this. Boundary/seam displacement policy requires asset validation.

## New components

1. A small, Qt-independent working-mesh/height module. Own positions, normals,
   UVs, colors and triangle indices; generate deterministic, bounded subdivision
   and displacement without any NifModel pointer. Validate every stage.
2. A NIF adapter to gate supported shapes, preserve all active attributes,
   resolve and decode textures using the facilities above, and convert buffers.
   Respect material UV scale/offset, addressing, and texture slot semantics.
3. A preview dialog/controller with source weights/multipliers, six blend modes,
   histogram conditioning, mesh statistics, and original/height/subdivided/
   displaced/reduced views. Recompute from immutable source buffers.
4. A pure-buffer reduction adapter using the existing meshoptimizer dependency.
   Preserve seams/borders, report measured error, and treat target count as
   subordinate to error. Never use aggressive permissive/prune options by default.
5. A shape-scoped commit/undo command. Validate output first, write only selected
   geometry, run existing normals/tangents/size/bounds facilities, and restore
   exactly on failure/undo. Saving remains the user's normal Save action.

## Build and validation status

The supplied snapshot initially had empty `lib/gli`, `lib/qhull`,
`lib/meshoptimizer`, `build/docsys/nifxml`, and `build/docsys/kfmxml` submodule
folders and no Git metadata. The exact upstream snapshot and pinned dependency
revisions have since been identified and restored. A local Qt/MSYS2 kit now
builds NifSkope successfully; see `docs/local-build.md` for details and limits.

The user subsequently supplied nine SSE NIFs and six 4096x4096 DDS textures.
Header/block-table inspection confirms version 20.2.0.7, user version 12,
BS version 100, and exact BSTriShape blocks. `rockm01.nif` is the first candidate:
one BSTriShape with MountainSlab02 diffuse/normal references, both supplied,
plus collision blocks that must remain unchanged. `rockl01`, `rockl04`, and
`rockm04` also each have one shape and the same texture pair. Mountain trim,
rock cliff, and rock pile assets reference additional snow/dirt/rock textures
not included in the supplied texture directory. Mask files must not be assumed
to be height maps merely because of their filenames.

A wider tool search found Houdini's Qt 5.12.6 qmake, which is below the project's
supported version. The compatible Qt kits were subsequently built or installed
locally in the project; they are separate from that Houdini installation.

Synthetic tests establish mathematical and safety invariants only; they cannot
establish NIF round-trip correctness, texture alignment, viewport behavior,
Cancel, or Undo. Source assets were inspected read-only and not processed.
