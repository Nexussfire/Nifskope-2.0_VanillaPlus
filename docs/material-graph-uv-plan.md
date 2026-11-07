# Material Graph: constrained UV controls

* Scope: Skyrim and Skyrim SE BSLightingShaderProperty only; graph connections
  remain fixed. Rotation and animation authoring are not added.
* Edit existing UV Scale and UV Offset as dialog-local staged values. A bounded
  relative drag changes offsets without clamping existing or numeric values.
  Shift constrains the dominant axis; Ctrl provides fine movement.
* Preview the staged base UV transform in NifSkope's order: UV * scale + offset,
  then independent U/V wrap or clamp. Use the selected diffuse texture when
  available and a clearly labeled diagnostic texture otherwise. This 2D preview
  does not simulate parallax, full material shading, or animation controllers.
* Derive feature labels from Skyrim renderer conditions and distinguish
  disabled features, missing paths and unavailable resources.
* Preserve shader type, flags, existing staged Apply/Cancel and grouped Undo.
  No NIF is automatically saved.
* Verify wrapping, clamping, negative/zero scales, staged edits, drag behavior,
  material state, Cancel, Apply/Undo and the existing geometry regressions.
