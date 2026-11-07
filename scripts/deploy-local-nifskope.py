"""Deploy the locally built UCRT application without modifying source files."""
from pathlib import Path
import os
import re
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parent.parent
toolchain = Path(os.environ.get("NIFSKOPE_TOOLS_DIR", "C:/Utility Apps/NifSkopeBuildTools"))
build_dir = Path(os.environ.get("NIFSKOPE_BUILD_DIR_WIN", str(toolchain / "nifskope-ucrt")))
runtime = build_dir / "release"
tools = toolchain / "msys64" / "ucrt64" / "bin"
exe = runtime / "NifSkope.exe"
if not exe.is_file():
    sys.exit("Build NifSkope before deploying it.")
env = os.environ.copy()
env["PATH"] = str(tools) + os.pathsep + env.get("PATH", "")
subprocess.run([str(tools / "windeployqt6.exe"), "--release", "--no-translations",
                "--compiler-runtime", "--no-system-d3d-compiler",
                "--no-system-dxc-compiler", str(exe)], env=env, check=True)
# Useful for automated startup checks without showing a window.
shutil.copy2(toolchain / "msys64" / "ucrt64" / "share" / "qt6" /
             "plugins" / "platforms" / "qoffscreen.dll", runtime / "platforms")

# windeployqt does not reliably deploy MinGW's transitive non-Qt DLLs. Inspect
# imports recursively and copy only dependencies supplied by this UCRT kit.
available = {p.name.lower(): p for p in tools.glob("*.dll")}
system = Path(os.environ["SystemRoot"]) / "System32"
queue = list(runtime.rglob("*.dll")) + [exe]
seen = set()
missing = set()
while queue:
    binary = queue.pop()
    key = str(binary.resolve()).lower()
    if key in seen:
        continue
    seen.add(key)
    result = subprocess.run([str(tools / "objdump.exe"), "-p", str(binary)],
                            env=env, check=True, capture_output=True, text=True)
    for name in re.findall(r"DLL Name:\s*(\S+)", result.stdout, re.IGNORECASE):
        lower = name.lower()
        local = runtime / name
        if local.exists():
            queue.append(local)
        elif lower in available:
            shutil.copy2(available[lower], local)
            queue.append(local)
        elif not lower.startswith(("api-ms-win-", "ext-ms-win-")) and not (system / name).exists():
            missing.add(name)
if missing:
    sys.exit("Missing runtime DLLs: " + ", ".join(sorted(missing)))

for path in (root / "build" / "nif.xml", root / "build" / "docsys" / "kfmxml" / "kfm.xml",
             root / "res" / "style.qss", root / "res" / "qt.conf"):
    shutil.copy2(path, runtime / path.name)
shutil.copytree(root / "res" / "shaders", runtime / "shaders", dirs_exist_ok=True)
print(f"Deployed NifSkope runtime and resources to {runtime}")
