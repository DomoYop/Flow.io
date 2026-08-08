from pathlib import Path
import gzip
import hashlib
import json
import re
import shutil
import os
import subprocess

Import("env")

# Identite du contenu SPIFFS, ecrite dans l'image elle-meme (jamais dans data/).
# Pendant du .bin firmware qui embarque FIRMW + FLOW_BUILD_REF : cf.
# docs/notes/versionnage-firmware-et-spiffs.md
FS_VERSION_FILE = "fsver.j"
FS_VERSION_SCHEMA = "flowio.fsver.v1"
_VERSION_SANITIZE_RE = re.compile(r"[^0-9A-Za-z._-]+")


def _gzip_file(src: Path, dst: Path):
    dst.parent.mkdir(parents=True, exist_ok=True)
    with src.open("rb") as in_file, gzip.GzipFile(filename="", mode="wb", fileobj=dst.open("wb"), mtime=0) as out_file:
        shutil.copyfileobj(in_file, out_file)


def _clean_value(value):
    if value is None:
        return ""
    return str(value).strip().replace("\\", "").strip('"').strip("'")


def _resolve_define(name):
    """Relit un define pose par un pre-script anterieur (generate_build_version.py)."""
    for define in env.get("CPPDEFINES", []):
        if isinstance(define, (tuple, list)) and len(define) >= 2 and str(define[0]) == name:
            return _clean_value(define[1])
    return ""


def _content_digest(root: Path):
    """Empreinte deterministe du contenu reellement flashe (chemins + tailles + octets).

    Les .gz sont produits avec mtime=0, donc deux builds d'un data/ inchange donnent
    la meme empreinte -- c'est ce qui distingue deux images SPIFFS que custom_version
    ne separe pas (un i18n modifie ne change pas la version produit).
    """
    digest = hashlib.sha256()
    files = 0
    total = 0
    paths = sorted(
        (p for p in root.rglob("*") if p.is_file()),
        key=lambda p: p.relative_to(root).as_posix(),
    )
    for path in paths:
        payload = path.read_bytes()
        digest.update(path.relative_to(root).as_posix().encode("utf-8"))
        digest.update(b"\0")
        digest.update(len(payload).to_bytes(8, "little"))
        digest.update(payload)
        files += 1
        total += len(payload)
    return digest.hexdigest()[:8], files, total


def _write_fs_version(root: Path):
    version = _VERSION_SANITIZE_RE.sub("", _clean_value(env.GetProjectOption("custom_version", "")))
    content_hash, files, total = _content_digest(root)
    payload = {
        "schema": FS_VERSION_SCHEMA,
        "version": version or "0.0.0",
        "build_ref": _resolve_define("FLOW_BUILD_REF") or "dev",
        "env": pio_env,
        "content_hash": content_hash,
        "files": files,
        "bytes": total,
    }
    (root / FS_VERSION_FILE).write_text(
        json.dumps(payload, separators=(",", ":"), ensure_ascii=True) + "\n",
        encoding="utf-8",
    )
    print(
        f"[prepare_spiffs_data] {FS_VERSION_FILE}: version={payload['version']} "
        f"build_ref={payload['build_ref']} content={content_hash} files={files} bytes={total}"
    )


project_dir = Path(env.subst("$PROJECT_DIR"))
build_dir = Path(env.subst("$BUILD_DIR"))
src_dir = project_dir / "data"
staging_dir = build_dir / "spiffs_data"
pio_env = str(env.subst("$PIOENV") or "").strip()
cfgdocs_profile = ""
try:
    cfgdocs_profile = str(env.GetProjectOption("custom_cfgdocs_profile") or "").strip().lower()
except Exception:
    cfgdocs_profile = ""


def _run_step(cmd):
    print(f"[prepare_spiffs_data] run: {' '.join(cmd)}")
    step_env = os.environ.copy()
    if pio_env:
        step_env["PIOENV"] = pio_env
    if cfgdocs_profile:
        step_env["FLOW_CFGDOC_PROFILE"] = cfgdocs_profile
    subprocess.run(cmd, cwd=str(project_dir), check=True, env=step_env)

if src_dir.exists():
    transients = (
        project_dir / "data" / "webinterface" / "cfgdocs.json",
        project_dir / "data" / "webinterface" / "cfgmods.json",
        project_dir / "data" / "webinterface" / "cfgdocs.jz",
        project_dir / "data" / "webinterface" / "cfgmods.jz",
    )
    # Ensure a clean state before regeneration.
    for transient in transients:
        if transient.exists():
            transient.unlink()
    _run_step(["python3", "scripts/generate_config_docs.py"])
    _run_step(["python3", "scripts/generate_cfgdoc_chunks.py"])
    # Keep only segmented cfgdoc assets in source data.
    for transient in transients:
        if transient.exists():
            transient.unlink()
            print(f"[prepare_spiffs_data] removed transient {transient}")

    if staging_dir.exists():
        shutil.rmtree(staging_dir)
    staging_dir.mkdir(parents=True, exist_ok=True)

    compressed_sources = {
        Path("webinterface/index.html"): Path("webinterface/index.html.gz"),
        Path("webinterface/sh.html"): Path("webinterface/sh.html.gz"),
        Path("webinterface/app.js"): Path("webinterface/app.js.gz"),
        Path("webinterface/i18n/fr.json"): Path("webinterface/i18n/fr.json.gz"),
        Path("webinterface/i18n/en.json"): Path("webinterface/i18n/en.json.gz"),
        Path("webinterface/app-core.css"): Path("webinterface/app-core.css.gz"),
        Path("webinterface/app-core.js"): Path("webinterface/app-core.js.gz"),
        Path("webinterface/light.html"): Path("webinterface/light.html.gz"),
        Path("webinterface/light.css"): Path("webinterface/light.css.gz"),
        Path("webinterface/light.js"): Path("webinterface/light.js.gz"),
        Path("webinterface/prov.html"): Path("webinterface/prov.html.gz"),
        Path("webinterface/prov.js"): Path("webinterface/prov.js.gz"),
        Path("webinterface/runtimeui.json"): Path("webinterface/runtimeui.json.gz"),
    }
    cfgdoc_dir = src_dir / "wc"
    if cfgdoc_dir.exists():
        for cfgdoc_src in sorted(cfgdoc_dir.glob("*.j")):
            rel = cfgdoc_src.relative_to(src_dir)
            compressed_sources[rel] = rel.with_suffix(".j.gz")
    generated_outputs = set(compressed_sources.values())

    for path in src_dir.rglob("*"):
        if not path.is_file():
            continue
        rel = path.relative_to(src_dir)
        if rel.parts[:2] == ("webinterface", "cfgdoc"):
            continue
        if rel in compressed_sources or rel in generated_outputs:
            continue
        dst = staging_dir / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, dst)

    for src_rel, dst_rel in compressed_sources.items():
        src = src_dir / src_rel
        if src.exists():
            _gzip_file(src, staging_dir / dst_rel)

    # En dernier : l'empreinte porte sur le staging complet, et fsver.j ne s'inclut pas lui-meme.
    _write_fs_version(staging_dir)

    env.Replace(PROJECT_DATA_DIR=str(staging_dir), PROJECTDATA_DIR=str(staging_dir))
    print(f"[prepare_spiffs_data] staging {src_dir} -> {staging_dir}")
