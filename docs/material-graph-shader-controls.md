# Material Graph shader controls

Skyrim / Skyrim SE lighting materials now offer Default, Glow, Environment and
Parallax/height selection. The current shader type is preserved on opening,
including specialized types outside those four choices. Selecting a different
type configures its matching Environment/Parallax/Glow flags; returning to the
original type restores its original managed feature bits. Other bits are retained.

The Shader flags dialog exposes both complete Skyrim words: 32 entries in Shader
Flags 1 and 32 in Shader Flags 2, in separate scrollable lists ordered by bit.
Names come from the loaded NifSkope schema. Each entry has a hover tooltip with
its purpose, prerequisites and limits; undocumented or uncertain behavior is
identified explicitly. Reserved/possibly unused bits are identified as such.
Live hexadecimal words reflect checkbox edits. Bits missing from a future loaded
schema are shown read-only and preserved rather than assigned invented meanings.
It stages changes until the graph's Apply action; Cancel in either dialog does
not change the loaded material. Full flag words remain visible for inspection.
Texture-node statuses and wires use the edited type/flags and resource availability.

Emission strength, emission RGB and Environment strength are editable alongside
the existing surface values. Glow needs a slot-2 texture and non-black emission
color; it does not create a light illuminating nearby geometry. Environment uses
a slot-4 cubemap. Parallax uses slot 3 and does not move mesh vertices. Tooltips
explain these requirements, opacity, highlights, glossiness, UV components and
texture-slot meanings. Existing texture-slot order and UV pad/numeric controls
remain in place. The preview remains explicitly a base-UV texture preview, not a
rendering of emission, reflections, parallax or animated controller output.

The implementation reuses NifSkope's shader flags, renderer conditions, schema
fields and ChangeValueCommand Undo infrastructure. Environment strength has a
type-dependent condition; its dormant field is obtained using the actual NIF
version, excluding the same-named inherited older-game field. Shader-setting
commands stay separate inside one Undo macro so condition caches are invalidated
on Undo/Redo. Ordinary UV-only edits keep the established merged-command path.
No new NIF property types, textures, controllers or runtime features are invented.

Verification: `tests/run-materialgraph-shader-probe.sh` exercises all four types
on Skyrim SE rock and classic Skyrim sword shaders: flag presets and edits,
unchanged unexposed bits, refreshed feature labels, tooltips, emission RGB,
conditional Environment strength, Apply/save/reopen, exact Undo/Redo, graph
Cancel, rejection of invalid RGB input without partial writes and unchanged
unrelated blocks/source files. It also checks undoing an existing Environment
material back from Glow retains its authored reflection strength. The existing
UI/UV/material/animation regression suite remains applicable.

Verified 2026-11-04: all four shader choices pass the new probe on BS 83 and BS
100 assets. The existing UI/UV/material/animation suite passed, and performance
probe geometry/UV preview and candidate hashes match the prior build exactly.
The application was built, runtime-staged, installed and launch-tested at:

`C:\Utility Apps\NifSkopeBuildTools\nifskope-ucrt\release\NifSkope.exe`

Full-list update verified 2026-11-05: all 64 controls have unique bit mappings and
tooltips; both high bits, live hexadecimal words, inner-dialog Cancel, one-step
Undo/Redo, save/reopen and unrelated-block preservation passed on BS 83 and 100.
The existing UI/UV/material/animation suite also passed. The updated application
was installed at the same path and launch-tested.

Tooltip detail update: incorporated the user's extended descriptions for fading,
decals, palette mapping, LOD, refraction, premultiplied alpha and specialized
lighting. Requirements and uncertain interpretations remain explicit. Slot
numbers remain zero-based. Alternative names are explained without changing the
schema's names or bit mappings: Facegen Detail Map / Facegen, Hair Soft Lighting /
Hair Tint, FaceGen RGB Tint / Skin Tint, and Packed Tangent / Character Lighting.
The Parallax tooltip distinguishes this build's verified renderer condition
(matching shader + flag + slot 3) from uncertain descriptions in published docs.

Cross-check references:
- https://www.niftools.org/nifxml/SkyrimShaderPropertyFlags1.html
- https://www.niftools.org/nifxml/SkyrimShaderPropertyFlags2.html
- Local `build/nif.xml` and `src/gl/glproperty.cpp`.

Expanded tooltip build: the full shader probe passed on both supported formats,
and the installed executable was hash-matched and launch-tested at the path above.
Tooltip-build SHA256: `75847C3ACC083DC2C6D89DDFE52C476AC5D7B6D95DA18160F149AC2E3F23D830`.

## Seven texture inputs

The graph exposes slots 0–6 in their stored order, including inactive or empty
inputs: diffuse/alpha, normal/specular, glow/lighting mask, height/parallax,
environment cubemap, environment mask, and multilayer inner color/depth. Slots 5
and 6 have the same editable path, resource-browser, clear, availability and
thumbnail controls as other inputs. Slot 1 is labeled Normal/specular to describe
the ordinary normal-map alpha channel accurately. Single-channel height/mask
meaning is kept separate from BC4 compression.

Assigning a path does not switch the shader or enable flags. Mask contribution
follows the existing environment conditions; inner-layer contribution follows the
renderer's multilayer flag. The base-UV preview still shows diffuse only; this
change does not implement a new multilayer/depth preview.

Shorter texture sets also show seven editable inputs. Opening, clearing an absent
input, or cancelling never resizes the live set. Applying a nonempty path beyond
its stored length adds only the needed entries. That exceptional path stages the
combined material edits in a private full-document copy, verifies save/reopen and
unrelated blocks, preflights block replacement/rollback and commits the shader
and texture-set blocks as one atomic Undo command. Existing longer sets retain
all trailing slots. Ordinary edits to already stored slots use the existing
ChangeValueCommand path.

The shader probe verifies all seven controls, mask clearing, inner-layer browser
Cancel, unchanged dialog editing, malformed-path rejection, and combined shader
plus texture edits on sets with 9, 6 and 0 stored entries. Both supported Skyrim
formats pass exact count/path save/reopen, trailing/unrelated-block preservation
and byte-identical document Undo/Redo. No fixture files are saved.

Verified 2026-11-05: the expanded shader probe and existing UI/material/UV suite
passed on BS 83/100; the performance probe passed. The staged application was
launch-tested, then the user's saved running instance was closed normally with
permission and the matching executable was installed and launch-tested at the
usual `nifskope-ucrt\release\NifSkope.exe` location.

Seven-input build SHA256:
`C896AA42CD06AFF7027BD6B1AC0DC171A27FB701A38AADB6DB298F090F6F9912`.

## Graph menus and color controls — 2026-11-06

Embedded menus belong to their node's graphics stacking context. The graph now
raises that node while its menus or submenus are visible, and restores its
previous stacking level when the last popup closes. This also handles embedded
combo popups; ordinary application menus and the original viewport are unchanged.

Emission RGB (effect base RGBA) has an optional picker using NifSkope's existing
ColorWheel widget. Numeric entry remains available. A separate RGB multiplier
supports existing HDR values up to the graph's 1000 limit; alpha remains separate.
Cancel and unchanged acceptance preserve the exact original text. Alpha-only
edits preserve RGB text and precision. Hex input synchronizes in one direction
without the legacy color-line editor's recursive wheel feedback.

Lighting materials expose their actual Specular Color field with numeric RGB
entry and the same picker. Specular strength/glossiness and the Specular flag
retain their existing roles. The animated-layer viewport receives the resolved
specular color, including supported existing color controllers, independently
of emission. The small sampling preview remains a UV/emission diagnostic.

Skyrim BS 83/100 lighting shaders have no separate environment RGB tint field.
Reflection color is authored by the slot-4 cubemap; Environment Map Scale and
the slot-5 mask control its contribution. The graph explains this beside the
specular controls rather than introducing a value the NIF cannot represent.

`tests/run-materialgraph-color-probe.sh` covers native menu/submenu stacking and
restoration, picker ownership, HDR/no-op/Cancel behavior, effect alpha precision,
specular persistence, candidate reopen, and exact Undo/Redo on rockm01,
Jeweledlongsword and Winterdawn. It writes no source assets.

Final color/menu, animated-layer, shader/flags/seven-input, effect, Pan/named-state,
new-feature performance, and geometry/material UI regressions passed. The final
Qt/UCRT build was staged and installed at
`C:\Utility Apps\NifSkopeBuildTools\nifskope-ucrt\release\NifSkope.exe`.
The installed application opened rockm01 in an actual document window and
closed normally; the source file hash was unchanged. Built and installed SHA256:
`54B67F8E94F3F99D1301A24436AF96DC22D9F623B792AF3745E31F7D36F8D2F1`.
