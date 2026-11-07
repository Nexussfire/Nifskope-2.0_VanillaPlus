"""Stage or finalize review-only source/runtime archives; never publish or delete files."""
from pathlib import Path
import argparse
import csv
import hashlib
import json
import os
import re
import shutil
import zipfile

ROOT = Path(__file__).resolve().parent.parent
PREFIX = "NifSkope-VanillaPlus-RC1"
EXPECTED_EXE = "22F975DB81586807F54DFC7EEB718440AD0D783C8BE83F2760F2E4266691C26A"
SOURCE_DIRS = ("src", "lib", "res", "build", "install", "docs", "scripts", "tests", ".github")
ROOT_FILES = (".editorconfig", ".gitattributes", ".gitignore", ".gitmodules", ".travis.yml",
              "appveyor.yml", "CHANGELOG.md", "CONTRIBUTORS.md", "DOXYGEN.md", "LICENSE.md",
              "NifSkope.pro", "NifSkope_functions.pri", "NifSkope_targets.pri", "README.md",
              "README_GLTF.md", "TROUBLESHOOTING.md")
EXCLUDED_DIRS = {".git", ".vscode", ".vs", "__pycache__", "GeneratedFiles", ".moc", ".obj", ".qrc", ".ui"}
EXCLUDED_SUFFIXES = {".exe", ".dll", ".pdb", ".o", ".obj", ".pyc", ".bak", ".log", ".tmp", ".a", ".lib"}


def sha(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for data in iter(lambda: f.read(1024 * 1024), b""):
            h.update(data)
    return h.hexdigest()


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def dump(path, data):
    write(path, json.dumps(data, indent=2, ensure_ascii=False) + "\n")


def files(root):
    return sorted((p for p in root.rglob("*") if p.is_file()), key=lambda p: p.relative_to(root).as_posix().lower())


def selected(root):
    result = []
    for name in ROOT_FILES:
        if (root / name).is_file():
            result.append(root / name)
    for name in SOURCE_DIRS:
        if not (root / name).exists():
            continue
        for parent, directories, names in os.walk(root / name):
            directories[:] = sorted(d for d in directories if d not in EXCLUDED_DIRS)
            for item in sorted(names):
                p = Path(parent) / item
                if item == ".git" or p.suffix.lower() in EXCLUDED_SUFFIXES or item in {".qmake.stash", "CMakeCache.txt"}:
                    continue
                if p.is_symlink():
                    raise RuntimeError(f"Review source symlink before packaging: {p}")
                result.append(p)
    return sorted(set(result), key=lambda p: p.relative_to(root).as_posix().lower())


def pacman_records(kit):
    result, ownership = {}, {}
    for folder in (kit / "var/lib/pacman/local").iterdir():
        if not (folder / "desc").is_file():
            continue
        data = {}
        for block in re.split(r"\r?\n\r?\n", (folder / "desc").read_text(encoding="utf-8")):
            lines = block.splitlines()
            if lines and lines[0].startswith("%"):
                data[lines[0].strip("%")] = lines[1:]
        name = data["NAME"][0]
        result[name] = {key.lower(): data.get(key, []) for key in ("NAME", "BASE", "VERSION", "URL", "LICENSE", "ARCH")}
        base = data.get("BASE", [name])[0]
        result[name]["packaging_recipe"] = f"https://github.com/msys2/MINGW-packages/tree/master/{base}"
        if (folder / "files").is_file():
            for line in (folder / "files").read_text(encoding="utf-8").splitlines():
                if line.lower().endswith(".dll"):
                    ownership.setdefault(Path(line).name.lower(), set()).add(name)
    return result, ownership


def revision(folder):
    git = folder / ".git"
    if not (git / "HEAD").is_file():
        return None
    head = (git / "HEAD").read_text().strip()
    if not head.startswith("ref: "):
        return head
    ref = head[5:]
    if (git / ref).is_file():
        return (git / ref).read_text().strip()
    if (git / "packed-refs").is_file():
        for line in (git / "packed-refs").read_text().splitlines():
            if line.endswith(" " + ref):
                return line.split()[0]
    return None


def stage(destination, runtime, kit):
    if destination.exists():
        raise RuntimeError("Destination exists; use --finalize for the staged candidate or choose a new directory.")
    if sha(runtime / "NifSkope.exe").upper() != EXPECTED_EXE:
        raise RuntimeError("Runtime differs from the latest completed/tested executable.")
    source = destination / "Source" / (PREFIX + "-source")
    binary = destination / "Windows" / (PREFIX + "-windows-x64")
    docs = destination / "Documentation"
    source.mkdir(parents=True)
    binary.mkdir(parents=True)
    docs.mkdir(parents=True)
    inventory = {}
    for p in selected(ROOT):
        relative = p.relative_to(ROOT).as_posix()
        inventory[relative] = {"sha256": sha(p), "bytes": p.stat().st_size}
        target = source / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(p, target)
    # Public landing files identify this fork; preserve original upstream texts.
    upstream = source / "docs/upstream"
    upstream.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source / "README.md", upstream / "README.upstream.md")
    shutil.copy2(source / "CHANGELOG.md", upstream / "CHANGELOG.upstream.md")
    shutil.copy2(source / ".gitmodules", upstream / "gitmodules.upstream.txt")
    # Vendored dependencies are regular files in this snapshot, not gitlinks.
    (source / ".gitmodules").unlink()
    for p in (source / "docs").rglob("*.md"):
        text = p.read_text(encoding="utf-8")
        text = text.replace("C:\\Users\\Nexus\\Desktop\\NifSkope_Upgrade", "<PROJECT_ROOT>")
        text = text.replace("/c/Users/Nexus/Desktop/NifSkope_Upgrade", "$PROJECT_ROOT")
        p.write_text(text, encoding="utf-8", newline="\n")
    write(source / "docs/README.md", "# Documentation index\n\nCurrent public documentation starts at [release/README.md](release/README.md).\n\nThe other implementation notes are historical development records; earlier\nlimitations, local paths and executable hashes describe their milestones, not\nthe final candidate. See the RC known-limitations and validation documents for\ncurrent behavior. Original upstream README/changelog/gitmodule references are\nunder upstream/.\n")
    release_docs = ROOT / "docs/release"
    for p in files(release_docs):
        shutil.copy2(p, docs / p.name)
        target = binary / "docs/release" / p.relative_to(release_docs)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(p, target)
    landing = (release_docs / "README.md").read_text(encoding="utf-8")
    landing = re.sub(r"\]\(([A-Z][A-Z0-9-]*\.md)\)", r"](docs/release/\1)", landing)
    write(source / "README.md", landing)
    write(binary / "README.md", landing)
    shutil.copy2(release_docs / "CHANGELOG-RC1.md", source / "CHANGELOG.md")
    shutil.copy2(release_docs / "CHANGELOG-RC1.md", binary / "CHANGELOG.md")
    for name in ("LICENSE.md", "CONTRIBUTORS.md"):
        shutil.copy2(ROOT / name, binary / name)
    allowed_dirs = ("platforms", "imageformats", "styles", "tls", "networkinformation", "generic", "shaders")
    allowed_files = {"NifSkope.exe", "nif.xml", "kfm.xml", "qt.conf", "style.qss"}
    for p in runtime.iterdir():
        if p.is_file() and (p.name in allowed_files or p.suffix.lower() == ".dll"):
            shutil.copy2(p, binary / p.name)
        elif p.is_dir() and p.name in allowed_dirs:
            shutil.copytree(p, binary / p.name)
    if [p.name for p in binary.rglob("*.exe")] != ["NifSkope.exe"]:
        raise RuntimeError("Unexpected executable in runtime.")
    shutil.copytree(kit / "ucrt64/share/licenses", binary / "licenses/runtime-kit")
    embedded = binary / "licenses/embedded"
    for name in ("lib/qhull/COPYING.txt", "lib/meshoptimizer/LICENSE.md", "lib/libfo76utils/LICENSE",
                 "lib/libfo76utils/README.detex", "lib/libfo76utils/README.mman-win32", "lib/gli/manual.md",
                 "lib/gli/readme.md", "lib/miniball/LICENSE-2.0.txt", "lib/tiny_gltf.h", "lib/json.hpp",
                 "lib/stb_image.h", "lib/stb_image_write.h", "lib/xxhash.h", "lib/meshlet.cpp", "lib/meshlet.h"):
        p = ROOT / name
        target = embedded / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(p, target)
    shutil.copy2(ROOT / "lib/qhull/COPYING.txt", binary / "Qhull_COPYING.txt")
    records, ownership = pacman_records(kit)
    runtime_components = []
    used = set()
    for p in binary.rglob("*.dll"):
        kit_files = (kit / "ucrt64/bin" / p.name, kit / "ucrt64/share/qt6/plugins" / p.relative_to(binary))
        if not any(candidate.is_file() and sha(candidate) == sha(p) for candidate in kit_files):
            raise RuntimeError(f"Runtime DLL differs from installed kit: {p.name}")
        owners = sorted(ownership.get(p.name.lower(), set()))
        if not owners:
            raise RuntimeError(f"Unidentified DLL owner: {p.name}")
        used.update(owners)
        runtime_components.append({"file": p.relative_to(binary).as_posix(), "sha256": sha(p), "packages": owners})
    components = {"dlls": sorted(runtime_components, key=lambda x: x["file"]), "packages": {name: records[name] for name in sorted(used)}}
    for folder in (binary / "docs/release", source / "docs/release", docs):
        dump(folder / "RUNTIME-COMPONENTS.json", components)
    dependencies = [{"directory": name, "revision": revision(ROOT / name)} for name in
                    ("lib/qhull", "lib/gli", "lib/meshoptimizer", "lib/libfo76utils", "lib/miniball", "build/docsys/nifxml", "build/docsys/kfmxml")]
    for folder in (source / "docs/release", docs):
        dump(folder / "DEPENDENCIES.json", dependencies)
    baselines = list((ROOT / "source_backup").glob("*/nifskope-2.0.dev11-20251230"))
    if len(baselines) != 1:
        raise RuntimeError("Original source backup not uniquely identified.")
    baseline = baselines[0]
    original = {p.relative_to(baseline).as_posix(): sha(p) for p in selected(baseline)}
    changes = []
    for name in sorted(set(original) | set(inventory)):
        old, new = original.get(name), inventory.get(name, {}).get("sha256")
        if old != new:
            category = "vendored/dependency" if name.startswith("lib/") or name.startswith(("build/docsys/nifxml/", "build/docsys/kfmxml/")) else "application/resource" if name.startswith(("src/", "res/", "build/")) or name.endswith((".pro", ".pri")) else "documentation/test/tooling"
            changes.append({"status": "added" if old is None else "removed" if new is None else "modified", "path": name, "category": category, "baseline_sha256": old or "", "current_sha256": new or ""})
    for folder in (source / "docs/release", docs):
        with (folder / "SOURCE-CHANGES.csv").open("w", encoding="utf-8", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=list(changes[0])); writer.writeheader(); writer.writerows(changes)
        summary = "# Changed source files\n\nComparison against the supplied nifskope-2.0.dev11-20251230 source backup.\nThis is a file-content comparison, not a Git-history reconstruction. Added vendored\nfiles restore dependencies absent from the backup; they are not all newly authored\nupgrade code. Packaging-only README/changelog/path adjustments are described in\nPACKAGING.md. Full paths and hashes are in SOURCE-CHANGES.csv.\n\n"
        for category in ("application/resource", "documentation/test/tooling", "vendored/dependency"):
            rows = [x for x in changes if x["category"] == category]
            summary += f"## {category} ({len(rows)} files)\n\n" + "".join(f"- {x['status']}: `{x['path']}`\n" for x in rows) + "\n"
        write(folder / "SOURCE-CHANGES.md", summary)
    dump(destination / "STAGING-INVENTORY.json", {"label": PREFIX, "release_date": "2026-11-06", "expected_executable_sha256": EXPECTED_EXE.lower(), "working_tree_files": inventory,
         "source": source.relative_to(destination).as_posix(), "windows": binary.relative_to(destination).as_posix(), "upstream_reference": "8e30c9fb758d7bed6d21d2eab57b3e9654707e3a", "published": False})
    write(destination / "START-HERE.md", f"# {PREFIX} review packages\n\n- `Source/{PREFIX}-source/`: source repository contents, with vendored dependencies.\n- `Windows/{PREFIX}-windows-x64/`: runnable portable Windows application.\n- `Documentation/`: changelog, features, build, limitations and review documents.\n- `Archives/`: source and Windows upload archives after finalization.\n- `SHA256SUMS.txt`: archive checksums after finalization.\n\nRead [Documentation/README.md](Documentation/README.md) first. No publishing has been performed.\n")
    print(json.dumps({"destination": str(destination), "source_files": len(inventory), "runtime_dlls": len(runtime_components), "changed_files": len(changes)}, indent=2))


def manifest(folder):
    entries = [p for p in files(folder) if p.name != "FILE-MANIFEST.sha256"]
    write(folder / "FILE-MANIFEST.sha256", "".join(f"{sha(p)}  {p.relative_to(folder).as_posix()}\n" for p in entries))
    return len(entries)


def finalize(destination):
    inventory = json.loads((destination / "STAGING-INVENTORY.json").read_text(encoding="utf-8"))
    source, binary = destination / inventory["source"], destination / inventory["windows"]
    for relative, record in inventory["working_tree_files"].items():
        if relative.startswith(("src/", "lib/", "res/")) or relative.endswith((".pro", ".pri")):
            if sha(ROOT / relative) != record["sha256"] or sha(source / relative) != record["sha256"]:
                raise RuntimeError(f"Implementation changed after staging: {relative}")
    if sha(binary / "NifSkope.exe") != inventory["expected_executable_sha256"]:
        raise RuntimeError("Staged executable changed.")
    archives = destination / "Archives"
    archives.mkdir(exist_ok=True)
    artifact_data = []
    for folder in (source, binary):
        count = manifest(folder)
        archive = archives / (folder.name + ".zip")
        if archive.exists():
            raise RuntimeError(f"Archive already exists: {archive}")
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
            for p in files(folder):
                z.write(p, arcname=f"{folder.name}/{p.relative_to(folder).as_posix()}")
        with zipfile.ZipFile(archive) as z:
            if z.testzip() is not None:
                raise RuntimeError(f"ZIP integrity check failed: {archive}")
        artifact_data.append({"file": archive.relative_to(destination).as_posix(), "bytes": archive.stat().st_size, "sha256": sha(archive), "payload_files": count + 1})
    write(destination / "SHA256SUMS.txt", "".join(f"{x['sha256']}  {x['file']}\n" for x in artifact_data))
    dump(destination / "PACKAGE-MANIFEST.json", {"label": PREFIX, "release_date": "2026-11-06", "executable_sha256": inventory["expected_executable_sha256"], "archives": artifact_data, "published": False})
    print(json.dumps(artifact_data, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--runtime", type=Path)
    parser.add_argument("--kit", type=Path)
    parser.add_argument("--finalize", action="store_true")
    args = parser.parse_args()
    if args.finalize:
        finalize(args.destination.resolve())
    else:
        if not args.runtime or not args.kit:
            parser.error("--runtime and --kit are required for staging")
        stage(args.destination.resolve(), args.runtime.resolve(), args.kit.resolve())
