# Material Graph: artist instructions

The existing static UV controls, addressing, staged Apply/Cancel and Undo remain
in place. Animated instructions are separate controls. Their visual connections
mean only that they contribute to the preview; they do not depict controller
links or assert an order of operations.

## Pan investigation

For the Skyrim lighting shader, the NIF schema provides
`BSLightingShaderPropertyFloatController` targets 20 (U Offset) and 22 (V
Offset). Each uses a float interpolator and float keys. NifSkope's
`LightingFloatController` applies these values to the shader UV offset and its
controller timing implements cyclic, reverse and constant extrapolation. The
animation replaces the corresponding base offset while active. The offset is a
material-level transform, not an independently animated texture slot.

The proposed Pan instruction uses a direction/speed arrow: left mouse controls
X and right mouse controls Y. Its units, sign convention and loop behavior must
be explicit and agree with the preview and written controllers. Repeating a
linear ramp can jump at the cycle boundary, especially with clamped addressing;
it must not be presented as universally seamless. Existing animation on these
channels, shared shader properties, controller sequences and external animation
need safe detection and must not be overwritten implicitly.

The current graph's Undo transaction handles value changes, but authoring Pan
also requires creating/removing and linking multiple NIF blocks. Before enabling
authoring, implement and verify atomic Undo/Redo for those changes, preserving
existing controller chains and unrelated blocks. Cancel must leave the model
untouched, and the application must not automatically save a NIF. Preview must
use the same timing and interpolation as the authored representation, clearly
state its material/rendering limitations, and never claim unsupported effects.

The UI smoke test includes an isolated two-axis linear-controller save/reopen
and NifSkope interpolation check. This is evidence for NifSkope's representation,
not an in-game verification or a complete authoring safety test.

## Other instructions

Do not expose Rotate, dissolve, or any other operation until its representation,
read/write safety and preview semantics are established for the relevant shader
and format. The lighting float-controller targets do not provide UV rotation.
Other shader classes or older texture controllers must not be treated as proof
that rotation is available on this graph's Skyrim lighting shader.
