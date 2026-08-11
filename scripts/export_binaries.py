from datetime import datetime
import gzip
import hashlib
import json
from pathlib import Path
import re
import shutil
import sys

Import("env")

sys.path.insert(0, str(Path(env.subst("$PROJECT_DIR")) / "scripts"))
from build_web_package import build_package as build_web_package, summarize  # noqa: E402


_ARTIFACT_RE = re.compile(
    r"^(?P<software>[A-Za-z0-9][A-Za-z0-9._-]*)-(?P<version>[0-9][0-9A-Za-z._-]*)\.(?P<ext>bin|tft)$"
)
_VERSION_SANITIZE_RE = re.compile(r"[^0-9A-Za-z._-]+")

# Deposes a cote des artefacts sans en etre : variante compressee, paquet web,
# side-cars. Les ignorer silencieusement evite un avertissement par build.
_NON_ARTIFACT_SUFFIXES = (".gz", ".pkg", ".json")

_SIDECAR_SCHEMA = "flowio.artifact.v1"


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


def _resolve_define(name):
    """Relit un define pose par un pre-script anterieur (generate_build_version.py)."""
    for define in env.get("CPPDEFINES", []):
        if isinstance(define, (tuple, list)) and len(define) >= 2 and str(define[0]) == name:
            return _clean_value(define[1])
    return ""


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


def _build_ref_to_iso(build_ref):
    """`20260811.112703` -> `2026-08-11T11:27:03+02:00`.

    Le manifeste date donc l'artefact au meme instant que l'estampille qu'il
    embarque (FLOW_BUILD_REF cote firmware, /fsver.j cote SPIFFS), et non a l'heure
    d'ecriture du fichier : les deux colonnes de la page Mise a jour deviennent
    comparables au caractere pres.
    """
    try:
        stamp = datetime.strptime(build_ref, "%Y%m%d.%H%M%S")
    except ValueError:
        return ""
    return stamp.astimezone().isoformat(timespec="seconds")


def _file_mtime_iso(path):
    return datetime.fromtimestamp(path.stat().st_mtime).astimezone().isoformat(timespec="seconds")


def _sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


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


def _sidecar_path(artifact_path):
    return artifact_path.with_suffix(".json")


def _write_sidecar(artifact_path, entry):
    """Identite de l'artefact, ecrite par le build qui l'a produit.

    Le manifeste n'est plus qu'une agregation de ces fichiers : il ne deduit plus
    rien du nom ni du mtime, et reste reconstructible a l'identique.
    """
    payload = {"schema": _SIDECAR_SCHEMA}
    payload.update(entry)
    path = _sidecar_path(artifact_path)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return path


def _read_sidecar(artifact_path):
    path = _sidecar_path(artifact_path)
    if not path.is_file():
        return None
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        print(f"[export_binaries] side-car illisible ({path.name}) : {exc}")
        return None
    if not isinstance(data, dict) or not data.get("version"):
        print(f"[export_binaries] side-car inexploitable ({path.name})")
        return None
    entry = dict(data)
    entry.pop("schema", None)
    entry["path"] = artifact_path.name
    return entry


def _build_entry(artifact_path, software, version, spec, extra=None):
    """Entree construite depuis ce que le build sait, jamais depuis le systeme de fichiers."""
    build_ref = _resolve_define("FLOW_BUILD_REF")
    build_date = _build_ref_to_iso(build_ref)
    if not build_date:
        print("[export_binaries] FLOW_BUILD_REF absent ou illisible, date de fichier utilisee")
        build_date = _file_mtime_iso(artifact_path)

    entry = {
        "title": software,
        "label": software,
        "version": version,
        "build_ref": build_ref,
        "build_date": build_date,
        "target": spec["target"],
        "path": artifact_path.name,
        "kind": spec["kind"],
        "route": spec["route"],
        "size": artifact_path.stat().st_size,
        "sha256": _sha256(artifact_path),
        "env": env.subst("$PIOENV"),
        "source": "build",
    }
    if extra:
        entry.update(extra)
    return entry


def _legacy_entry(artifact_path, software, version, spec):
    """Dernier recours : artefact sans side-car ni entree deja publiee.

    Version deduite du nom, date deduite du mtime -- ni l'une ni l'autre n'est
    reproductible, d'ou le marquage explicite. Concerne les binaires anterieurs a ce
    dispositif et les .tft deposes a la main.
    """
    stat = artifact_path.stat()
    return {
        "title": software,
        "label": software,
        "version": version,
        "build_date": _file_mtime_iso(artifact_path),
        "target": spec["target"],
        "path": artifact_path.name,
        "kind": spec["kind"],
        "route": spec["route"],
        "size": stat.st_size,
        "source": "filesystem",
    }


def _published_entries(manifest_path):
    """Entrees deja publiees, indexees par nom de fichier.

    Reprendre l'existant est ce qui rend la mise a jour additive : un build ne
    reecrit plus que sa propre entree, la ou l'ancienne version recalculait les 76
    entrees depuis les mtime courants du dossier (donc les faussait des qu'un
    fichier etait recopie).
    """
    if not manifest_path.is_file():
        return {}
    try:
        data = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        print(f"[export_binaries] manifeste precedent illisible, reconstruction complete : {exc}")
        return {}

    published = {}
    artifacts = data.get("artifacts") if isinstance(data, dict) else None
    if isinstance(artifacts, dict):
        for entries in artifacts.values():
            if not isinstance(entries, list):
                continue
            for entry in entries:
                if isinstance(entry, dict) and entry.get("path"):
                    published[str(entry["path"])] = entry
    return published


def _entry_datetime(entry):
    try:
        stamp = datetime.fromisoformat(str(entry.get("build_date", "")))
    except ValueError:
        return None
    return stamp if stamp.tzinfo else stamp.astimezone()


def _update_manifest():
    out_dir = _binary_dir()
    manifest_path = out_dir / "manifest.json"
    published = _published_entries(manifest_path)
    artifacts = {}
    newest = None

    for path in sorted(out_dir.iterdir()):
        if not path.is_file() or path.name == "manifest.json":
            continue
        if path.suffix in _NON_ARTIFACT_SUFFIXES:
            continue

        match = _ARTIFACT_RE.match(path.name)
        if not match:
            print(f"[export_binaries] skip manifest entry for '{path.name}' (format attendu: <software>-<version>.bin/tft)")
            continue

        software = match.group("software")
        spec = _classify_artifact(software, match.group("ext"))
        if spec is None:
            print(f"[export_binaries] skip manifest entry for '{path.name}'")
            continue

        # Le side-car prime : il vient du build qui a produit ce fichier precis.
        entry = _read_sidecar(path) or published.get(path.name)
        if entry is None:
            entry = _legacy_entry(path, software, match.group("version"), spec)

        stamp = _entry_datetime(entry)
        if stamp is not None and (newest is None or stamp > newest):
            newest = stamp

        artifacts.setdefault(spec["category"], []).append(entry)

    manifest = {
        "schema": "flowio.firmware-manifest.v1",
        "generated_at": datetime.now().astimezone().isoformat(timespec="seconds"),
        "release": (newest or datetime.now().astimezone()).strftime("%Y.%m.%d"),
        "artifacts": artifacts,
    }

    manifest_path.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    rel_manifest = manifest_path.relative_to(_project_dir())
    print(f"[export_binaries] manifest updated -> {rel_manifest}")


def _publish_artifact(src_path, software, version, extra=None, ext="bin"):
    """Copie l'artefact sous son nom publie, ecrit son side-car, met a jour le manifeste.

    Le nom de fichier est construit depuis `software` et `version` au lieu d'etre
    reanalyse ensuite : la version publiee vient de custom_version, comme FIRMW et
    comme /fsver.j.
    """
    src = Path(str(src_path))
    if not src.exists():
        return None

    dst = _binary_dir() / f"{software}-{version}.{ext}"
    shutil.copy2(src, dst)

    spec = _classify_artifact(software, ext)
    if spec is None:
        print(f"[export_binaries] '{dst.name}' hors nomenclature, side-car non ecrit")
        _update_manifest()
        return dst

    entry = _build_entry(dst, software, version, spec, extra)
    _write_sidecar(dst, entry)
    print(f"[export_binaries] copied {src.name} -> {dst.relative_to(_project_dir())} "
          f"(version {entry['version']}+{entry['build_ref'] or '?'})")
    _update_manifest()
    return dst


def _export_program_bin(source, target, env):
    build_dir = Path(env.subst("$BUILD_DIR"))
    env_name = env.subst("$PIOENV")
    fw_version = _resolve_firmware_version()

    if env_name == "Waveshare-ESP32-S3" or env_name == "WaveshareWokwi":
        _publish_artifact(build_dir / "firmware.bin", "flowios3", fw_version)
    elif env_name == "FlowConnectDisplay":
        _publish_artifact(build_dir / "firmware.bin", "flow-connect-display", fw_version)


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


def _export_web_package(build_dir, bin_name):
    """Paquet de fichiers, construit depuis le meme staging que l'image SPIFFS.

    C'est la variante d'OTA non destructive : le firmware remplace les fichiers un
    par un au lieu de reecrire toute la partition. Meme nom de base que l'image,
    extension .pkg, ce qui permet au firmware de le deviner sans entree dediee dans
    le manifeste.
    """
    staging = Path(str(build_dir)) / "spiffs_data"
    if not staging.is_dir():
        print("[export_binaries] staging SPIFFS absent, paquet web non genere")
        return
    package = build_web_package(staging)
    dst = _binary_dir() / (bin_name[: -len(".bin")] + ".pkg")
    dst.write_bytes(package)
    print(f"[export_binaries] paquet web -> {dst.name} ({summarize(package, staging)})")


def _spiffs_content_meta(build_dir, version):
    """Reprend l'identite que l'image embarque (/fsver.j) au lieu d'en fabriquer une autre.

    content_hash est la seule grandeur qui distingue deux images d'une meme
    custom_version (un i18n modifie ne change pas la version produit) : sans elle
    dans le manifeste, deux SPIFFS differents y sont indiscernables.
    """
    path = Path(str(build_dir)) / "spiffs_data" / "fsver.j"
    if not path.is_file():
        print("[export_binaries] fsver.j absent du staging, content_hash non publie")
        return {}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        print(f"[export_binaries] fsver.j illisible ({exc}), content_hash non publie")
        return {}

    embedded = str(data.get("version", ""))
    if embedded and embedded != version:
        print(f"[export_binaries] fsver.j annonce la version {embedded} pour une image publiee en {version}")

    return {key: data[key] for key in ("content_hash", "files", "bytes") if key in data}


def _export_spiffs_bin(source, target, env):
    build_dir = Path(env.subst("$BUILD_DIR"))
    env_name = env.subst("$PIOENV")
    fw_version = _resolve_firmware_version()
    if env_name == "Waveshare-ESP32-S3" or env_name == "WaveshareWokwi":
        dst = _publish_artifact(
            build_dir / "spiffs.bin",
            "flowios3-spiffs",
            fw_version,
            extra=_spiffs_content_meta(build_dir, fw_version),
        )
        if dst is None:
            return
        _gzip_spiffs_image(dst.name)
        _export_web_package(build_dir, dst.name)
        return


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", _export_program_bin)
env.AddPostAction("$BUILD_DIR/spiffs.bin", _export_spiffs_bin)
