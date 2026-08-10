from datetime import datetime
import gzip
import json
from pathlib import Path
import re
import shutil

Import("env")


_ARTIFACT_RE = re.compile(
    r"^(?P<software>[A-Za-z0-9][A-Za-z0-9._-]*)-(?P<version>[0-9][0-9A-Za-z._-]*)\.(?P<ext>bin|tft)$"
)
_VERSION_SANITIZE_RE = re.compile(r"[^0-9A-Za-z._-]+")


def _project_dir():
    return Path(env.subst("$PROJECT_DIR"))


def _binary_dir():
    out_dir = _project_dir() / "binary"
    out_dir.mkdir(exist_ok=True)
    return out_dir


def _clean_value(value):
    if value is None:
        return ""
    return str(value).strip().replace("\\", "").strip('"').strip("'")


def _sanitize_version(value):
    cleaned = _VERSION_SANITIZE_RE.sub("", _clean_value(value))
    return cleaned if cleaned else "0.0.0"


def _resolve_firmware_version():
    version = ""
    try:
        version = _sanitize_version(env.GetProjectOption("custom_version"))
    except Exception:
        version = ""

    if version:
        return version

    for define in env.get("CPPDEFINES", []):
        if isinstance(define, (tuple, list)) and len(define) >= 2 and define[0] == "FIRMW":
            version = _sanitize_version(define[1])
            if version:
                return version

    return "0.0.0"


def _classify_artifact(software, ext):
    software_name = str(software or "").strip()
    norm = software_name.lower()

    if ext == "tft":
        return {
            "category": "nextion",
            "target": "nextion",
            "kind": "nextion-tft",
            "route": "/fwupdate/nextion",
        }
    if norm in ("flowios3-spiffs", "esp32s3-spiffs", "waveshare-spiffs"):
        return {
            "category": "spiffs",
            "target": "spiffs",
            "kind": "esp32-spiffs",
            "route": "/fwupdate/spiffs",
        }
    if norm in ("flowios3", "esp32s3", "waveshare"):
        return {
            "category": "flowios3",
            "target": "flowios3",
            "kind": "esp32-firmware",
            "route": "/fwupdate/waveshare",
        }

    return None


def _update_manifest():
    out_dir = _binary_dir()
    now_iso = datetime.now().astimezone().isoformat(timespec="seconds")
    artifacts = {}
    newest_ts = None

    for path in sorted(out_dir.iterdir()):
        if not path.is_file():
            continue
        if path.name == "manifest.json":
            continue

        match = _ARTIFACT_RE.match(path.name)
        if not match:
            print(f"[export_binaries] skip manifest entry for '{path.name}' (format attendu: <software>-<version>.bin/tft)")
            continue

        stat = path.stat()
        mtime = datetime.fromtimestamp(stat.st_mtime).astimezone()
        mtime_iso = mtime.isoformat(timespec="seconds")
        if newest_ts is None or stat.st_mtime > newest_ts:
            newest_ts = stat.st_mtime

        software = match.group("software")
        version = match.group("version")
        ext = match.group("ext")
        spec = _classify_artifact(software, ext)
        if spec is None:
            print(f"[export_binaries] skip manifest entry for '{path.name}'")
            continue

        entry = {
            "title": software,
            "label": software,
            "version": version,
            "build_date": mtime_iso,
            "target": spec["target"],
            "path": path.name,
            "kind": spec["kind"],
            "route": spec["route"],
            "size": stat.st_size,
        }
        artifacts.setdefault(spec["category"], []).append(entry)

    manifest = {
        "schema": "flowio.firmware-manifest.v1",
        "generated_at": now_iso,
        "release": datetime.fromtimestamp(newest_ts).strftime("%Y.%m.%d") if newest_ts is not None else datetime.now().strftime("%Y.%m.%d"),
        "artifacts": artifacts,
    }

    manifest_path = out_dir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    rel_manifest = manifest_path.relative_to(_project_dir())
    print(f"[export_binaries] manifest updated -> {rel_manifest}")


def _copy_if_exists(src_path, dst_name):
    src = Path(str(src_path))
    if not src.exists():
        return
    dst = _binary_dir() / dst_name
    shutil.copy2(src, dst)
    print(f"[export_binaries] copied {src.name} -> {dst.relative_to(_project_dir())}")
    _update_manifest()


def _export_program_bin(source, target, env):
    build_dir = Path(env.subst("$BUILD_DIR"))
    env_name = env.subst("$PIOENV")
    fw_version = _resolve_firmware_version()

    if env_name == "Waveshare-ESP32-S3" or env_name == "WaveshareWokwi":
        _copy_if_exists(build_dir / "firmware.bin", f"flowios3-{fw_version}.bin")
    elif env_name == "FlowConnectDisplay":
        _copy_if_exists(build_dir / "firmware.bin", f"flow-connect-display-{fw_version}.bin")


def _gzip_spiffs_image(bin_name):
    """Variante compressee de l'image SPIFFS, servie a cote du .bin.

    mkspiffs pade l'image jusqu'a la taille de la partition (7,9 Mo pour ~320 Ko de
    contenu) : le remplissage 0xFF se compresse a presque rien, ce qui divise le
    transfert OTA d'un facteur ~25. Le firmware demande d'abord ce .gz et retombe
    sur le .bin s'il est absent, donc rien a declarer dans le manifeste.

    mtime=0 et filename vide : l'en-tete gzip fait alors exactement 10 octets avec
    FLG=0, ce dont depend le decodeur embarque (il saute une taille fixe).
    """
    src = _binary_dir() / bin_name
    if not src.exists():
        return
    dst = src.with_suffix(src.suffix + ".gz")
    payload = src.read_bytes()
    with dst.open("wb") as out_file:
        with gzip.GzipFile(filename="", mode="wb", fileobj=out_file, mtime=0) as gz:
            gz.write(payload)
    ratio = (dst.stat().st_size / len(payload) * 100.0) if payload else 0.0
    print(f"[export_binaries] gzip {bin_name} -> {dst.name} "
          f"({dst.stat().st_size} o, {ratio:.1f} % de l'original)")


def _export_spiffs_bin(source, target, env):
    build_dir = Path(env.subst("$BUILD_DIR"))
    env_name = env.subst("$PIOENV")
    fw_version = _resolve_firmware_version()
    if env_name == "Waveshare-ESP32-S3" or env_name == "WaveshareWokwi":
        name = f"flowios3-spiffs-{fw_version}.bin"
        _copy_if_exists(build_dir / "spiffs.bin", name)
        _gzip_spiffs_image(name)
        return


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", _export_program_bin)
env.AddPostAction("$BUILD_DIR/spiffs.bin", _export_spiffs_bin)
