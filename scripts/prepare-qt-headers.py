"""Prepare syncqt output directories for restricted Windows environments.

Some sandboxed Windows accounts get ACCESS_DENIED for weakly_canonical() on a
nonexistent output directory. Qt's syncqt normalizes these paths before it
creates them. Only create paths named by Qt's generated argument files and
only under this project's Qt build directory.
"""
from pathlib import Path
import os

root = Path(os.environ.get("NIFSKOPE_QT_BUILD", "C:/Utility Apps/NifSkopeBuildTools/qt6-build-fresh")).resolve()
directory_options = {
    "-includeDir", "-privateIncludeDir", "-qpaIncludeDir", "-rhiIncludeDir",
    "-ssgIncludeDir", "-spiIncludeDir", "-stagingDir",
}
count = 0
for args_file in root.rglob("*syncqt_args"):
    args = args_file.read_text(encoding="utf-8").splitlines()
    for index, option in enumerate(args[:-1]):
        if option in directory_options:
            path = Path(args[index + 1]).resolve()
            if not path.is_relative_to(root):
                raise ValueError(f"Qt output path outside build directory: {path}")
            path.mkdir(parents=True, exist_ok=True)
            count += 1
print(f"Prepared {count} Qt header output paths")
