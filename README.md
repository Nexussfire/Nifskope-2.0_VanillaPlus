# NifSkope Vanilla+ RC1

Release candidate review snapshot: **2026-11-06**.

This is a customized NifSkope 2.0.dev11 build with Vanilla+ Geometry, Sculpt,
Material Graph and viewport visibility tools. It is a candidate for manual
review and publication, not an official upstream NifSkope release. The binary
still reports the inherited `2.0.dev11` application version; RC1 identifies this
package, not a rewritten version resource.

## Start here

Extract the complete Windows archive into a writable folder and run
`NifSkope.exe`. Keep the DLLs, plugins, XML schemas, style and shaders beside it.
Qt and a compiler do not need to be installed to use the staged runtime.
Configure your game/resource folders in NifSkope before using texture-based
features. Your existing NifSkope preferences may be reused; no preferences,
game archives, meshes or textures are supplied by this package.

The Windows build is x64, targets AVX and F16C (`noavx2=1`), and requests an
OpenGL 4.2 core context. AVX2 is not required by the application build. Other
operating systems, older CPUs and fresh machines have not been validated for
this candidate. The tested kit is MSYS2 UCRT64, GCC 16.2.0 and Qt 6.12.0.

## Documentation

- [Features and workflows](docs/release/FEATURES.md)
- [Full candidate changelog](docs/release/CHANGELOG-RC1.md)
- [Known limitations and compatibility](docs/release/KNOWN-LIMITATIONS.md)
- [Source build and tests](docs/release/BUILDING.md)
- [Validation and release review](docs/release/VALIDATION.md)
- [Package contents and manual Git upload](docs/release/PACKAGING.md)
- [Third-party components](docs/release/THIRD-PARTY.md)

The source package contains a complete vendored source snapshot. It does not
need submodule downloads for the included dependencies. The top-level source
README and changelog identify this candidate; their originals remain under
`docs/upstream/`. Original application licensing and credits are retained.

No upload, Git push, tag, GitHub release or publication has been performed.
