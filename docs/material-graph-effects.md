# Material Graph: effects and existing animation

## Pan and sequence extension architecture

Pan is an independent artist instruction: left drag changes sampled U velocity,
right drag changes sampled V velocity. Static UV controls stay in place. The
instruction writes the existing lighting/effect float-controller U/V channels,
NiFloatInterpolator and two linear NiFloatData keys for new axes. It is not a
static offset editor. New-axis duration defines a resetting loop; seamless
repetition needs wrapping and whole-number UV travel per loop. Existing direct
two-key linear/quadratic pans expose average UV travel per scene second. Editing
speed scales travel and active tangents about the authored first value, retaining
key times, interpolation, controller frequency/phase and independent axis periods.
Unchanged axes are not rewritten; zero-speed missing axes create no blocks.
Shared, sequence-controlled and unsupported arrangements remain preserved/read-only.

Named sequence preview resolves the selected material's controller links through
NiControllerSequence Controlled Blocks, matching ControllerManager's explicit
controller-link path. It reuses renderer controllers/interpolation with private
targets. Sequence selection never edits sequence, blend, visibility, geometry or
particle data. No state transitions or shader UV rotation are fabricated.

Pan Apply stages a full private copy, validates save/reopen and unrelated blocks,
and commits appended animation blocks plus changed existing blocks in one Undo
action. Existing model indices survive; the live document is never reset merely
to implement Undo. Rollback and redo are preflighted on the private copy.
The creating command also registers explicit identities for its appended blocks.
Ordinary value/flag Undo commands can recover those identities when Pan creation
is undone/redone, without rebinding unrelated or ordinary deleted blocks.

The graph extends the Skyrim BS83/BS100 adapter without changing the block
editor or renderer. Lighting keeps its seven texture-set inputs. Effect
materials use their actual Source Texture and Greyscale Texture fields,
Base Color/Scale, falloff and UV fields; they are not converted to lighting.

Reuse `EffectFloatController`, `EffectColorController`, `LightingFloatController`
and `LightingColorController` with private property targets for playback.
Their existing `Controller::ctrlTime` and interpolation remain authority.
Playback never updates the NIF or creates controllers. Static UV controls
remain separate from existing animation, which can override their values.

The 2D preview is a sampling diagnostic, not a complete scene renderer.
Palette, view/depth falloff, refraction, full lighting, vertex color/alpha,
billboards, sequences and external behavior require the normal viewport.
Unsupported mappings are identified instead of approximated silently.
Animation inspection includes RGB/vector keys, constant/changing curves,
frequency/phase and effective cycle duration. Shared controller targets are
reported and never repaired or retargeted.

Apply retains the existing stale-material checks and one Undo action;
Cancel discards staged changes. Texture files and source NIFs are not saved.

## Using the additions

The **PAN** node is below Animation; the static UV pad stays where it was.
Enable **Edit / add Pan**, then left-drag horizontally for U and right-drag
vertically for V. Each mouse button controls only its own axis. Numeric fields
give signed UV units/second, and Loop duration defines the reset interval.
Pan affects the material's shared UV transform, not an individual texture slot.
It does not modify vertex UV data. Compatible existing two-key pans can be
edited, including the direct quadratic pans in Townportal01 and maginvturnundead.
Shared, managed or unsupported arrangements display a preservation reason.
New-axis loop duration is disabled when both axes already have authored timing.

Activation correction: the initial authoring gate rejected all quadratic pans,
non-default clocks and animated origins differing from static UV Offset. The
Townportal01/maginvturnundead fixtures demonstrate valid direct pans with those
features. All nine Townportal pan materials and both Turn Undead pans are now
editable. Tests compare preview samples and scaled renderer curves at multiple
times, preserve controller bytes and interpolation, and verify no-op activation,
save/reopen and exact Undo/Redo. Existing sequence/shared protections remain.

The Animation selector lists direct material animation and relevant named states.
Selecting a state pauses at its start for inspection; Resume runs the preview
clock. This selects material key bindings without simulating game state changes.

Select an effect shape or its BSEffectShaderProperty and open Material Graph.
The source/palette inputs, base RGBA, brightness, lighting-influence byte and
view/depth falloff values are editable. The seven lighting inputs and existing
UV pad/numeric controls remain unchanged for lighting materials.

Enable **Use existing animation** in the sampling preview. **Pause** freezes
the clock for numeric time scrubbing; Resume continues from that time.
The image shows animated UV sampling, with resolved RGB, brightness and opacity
and a separate emission swatch. It does not claim to composite the full material.
Inspect Animation shows float/RGB keys and a selected channel's renderer curve.
Pan can now author supported linear U/V offset animation. Other behavior is
inspected/played without rewriting its existing arrangement. The state selector
resolves explicit material bindings from managed sequences; it does not simulate
state transitions, geometry animation, particles or external behavior. The time
control remains the material controller's scene clock, as in the existing
viewport binding path; selecting a state changes keys rather than inventing a
new composite timing system.

## Verification

The effect probe exercises all lighting/effect properties in Winterdawn,
1stpersonWinterdawn and Townportal01. It checks actual RGB playback, U/V pan
images against renderer timing/interpolation, constant channels, clock
pause/resume, private staging, malformed input/stale-source rejection, and effect
Apply/save/reopen with byte-exact Undo/Redo and unchanged unrelated/controller
blocks. The fixture files remain unchanged. Referenced custom textures are not
in the project texture root, so those cases use the explicit diagnostic checker;
they do not establish an in-game visual match.

The existing shader probe passes BS83/100 shader changes, all 64 flags and seven
inputs including short-set expansion. The existing material/UV UI regression
suite passes, including all 63,488 finite half-UV bit patterns.

Before the final build, the performance probe measured rock graph opening at
180–184 ms and 50 staged UV edits at 135 ms total. Asset playback updates took
approximately 1.75–1.9 ms each (120 synchronous updates per property, offscreen
Qt). These measure UI update work, not GPU frame rate or in-game rendering.
DDS resources are decoded on path refresh, not on animation frames.

Pan/state extension verification: lighting BS83/100 and effect fixtures pass real
left/right mouse isolation, renderer-evaluated U/V motion, combined short texture
set expansion, stage/save/reopen, invalid/stale-data rejection and exact Undo/Redo.
Tests undo and redo creation through subsequent Pan edits and ordinary block-editor
value/flag edits, and verify older Undo commands remain valid. Four managed assets
provide 16 states; their resolved color/opacity matches explicit sequence-bound
renderer controllers. Existing material, shader, UV and native sculpt UI probes
pass. Latest performance before release: graph opening 177 ms, 50 UV edits 134 ms
total, and animation updates approximately 1.8 ms. No automatic source-file save.

Final build installed and native-window smoke-tested with Winterdawn at:
`C:\Utility Apps\NifSkopeBuildTools\nifskope-ucrt\release\NifSkope.exe`.
The staged and installed executables have the same SHA256:
`735ABB989703CBDDCF128602A278BC3F135F7535B7CAAE39AE86A3B8BDD3B1EE`.

The subsequent Pan/state release is installed at the same runnable path, with
staged/installed SHA256
`7C59B9DDD1D05F00AD8ECD1EE1635BC261AEB7B24825D7550231CC86DE02C900`.
Its native startup smoke test loaded lightspellactorfx.nif and closed only the
test instance; the asset hash was unchanged. The final Pan probe also captures
`release/materialgraph-pan-instruction.png` for visual layout review.

The activation-correction release is installed and native-smoke-tested with
Townportal01 at the same path. Staged and installed SHA256:
`BFCFC90477EFAE6B3AFB60024F9A7D443B96B69753E16A5AD5426375266F0CA8`.
Pan, effect, shader and UV/UI regressions pass; the performance probe measured
145 ms graph opening and 134 ms for 50 UV edits before the final build.
