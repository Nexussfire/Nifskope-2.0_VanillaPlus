"""Stage a UCRT/Qt runtime from the active kit, without changing source files."""
from pathlib import Path
import argparse
import os
import re
import shutil
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("build", type=Path)
parser.add_argument("qt_prefix", type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
runtime = args.build.resolve() / "release"
tools = args.qt_prefix.resolve() / "bin"
exe = runtime / "NifSkope.exe"
if not exe.is_file():
    parser.error("Build NifSkope before deploying it.")
env = os.environ.copy()
env["PATH"] = str(tools) + os.pathsep + env.get("PATH", "")
subprocess.run([str(tools / "windeployqt6.exe"), "--release", "--no-translations",
                "--compiler-runtime", "--no-system-d3d-compiler",
                "--no-system-dxc-compiler", str(exe)], env=env, check=True)
plugins = args.qt_prefix / "share" / "qt6" / "plugins"
offscreen = plugins / "platforms" / "qoffscreen.dll"
if offscreen.is_file():
    shutil.copy2(offscreen, runtime / "platforms")
available = {p.name.lower(): p for p in tools.glob("*.dll")}
system = Path(os.environ["SystemRoot"]) / "System32"
queue, seen, missing = [exe] + list(runtime.rglob("*.dll")), set(), set()
while queue:
    binary = queue.pop()
    key = str(binary.resolve()).lower()
    if key in seen:
        continue
    seen.add(key)
    result = subprocess.run([str(tools / "objdump.exe"), "-p", str(binary)],
                            env=env, check=True, capture_output=True, text=True)
    for name in re.findall(r"DLL Name:\s*(\S+)", result.stdout, re.I):
        local = runtime / name
        if local.exists():
            queue.append(local)
        elif name.lower() in available:
            shutil.copy2(available[name.lower()], local)
            queue.append(local)
        elif not name.lower().startswith(("api-ms-win-", "ext-ms-win-")) and not (system / name).exists():
            missing.add(name)
if missing:
    raise SystemExit("Missing runtime DLLs: " + ", ".join(sorted(missing)))
for path in (root / "build/nif.xml", root / "build/docsys/kfmxml/kfm.xml",
             root / "res/style.qss", root / "res/qt.conf"):
    shutil.copy2(path, runtime / path.name)
shutil.copytree(root / "res/shaders", runtime / "shaders", dirs_exist_ok=True)
print(f"Deployed runtime and resources to {runtime}")
