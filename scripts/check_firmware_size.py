#!/usr/bin/env python3
"""Echoue quand le binaire applicatif depasse son budget de partition.

La taille de partition n'est pas ecrite en dur : elle est lue dans le CSV
declare par `board_build.partitions` de l'environnement, lui-meme resolu depuis
platformio.ini en suivant `extends`. Un chiffre en dur derive des que la table
de partitions change, ce qui est deja arrive sur ce projet (la doc annoncait
~93 % d'occupation pour une partition depuis passee de 2 a 4 Mo).

Usage :
    python scripts/check_firmware_size.py --max-percent 85
    python scripts/check_firmware_size.py --env FlowIO --max-percent 90
"""

from __future__ import annotations

import argparse
import configparser
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PLATFORMIO_INI = ROOT / "platformio.ini"


def _read_platformio_ini() -> configparser.ConfigParser:
    # interpolation=None : les valeurs contiennent des ${common.xxx} que
    # configparser refuserait d'interpoler lui-meme.
    parser = configparser.ConfigParser(interpolation=None, strict=False)
    with PLATFORMIO_INI.open(encoding="utf-8") as handle:
        parser.read_file(handle)
    return parser


def _lookup_inherited(parser: configparser.ConfigParser, section: str, option: str) -> str | None:
    """Lit une option en suivant la chaine `extends`, sans boucler."""
    seen: set[str] = set()
    while section and section not in seen:
        seen.add(section)
        if not parser.has_section(section):
            return None
        if parser.has_option(section, option):
            return parser.get(section, option).strip()
        parent = parser.get(section, "extends", fallback="").strip()
        section = parent.splitlines()[0].strip() if parent else ""
    return None


def _parse_size(raw: str) -> int:
    """Convertit une taille de table de partitions (0x140000, 1M, 512K, 4096)."""
    text = raw.strip()
    if not text:
        raise ValueError("taille de partition vide")
    multiplier = 1
    if text[-1] in "kKmM":
        multiplier = 1024 if text[-1] in "kK" else 1024 * 1024
        text = text[:-1]
    return int(text, 0) * multiplier


def app_partition_size(csv_path: Path) -> int:
    """Plus petite partition de type `app` : celle qui borne une image OTA.

    Avec deux partitions app0/app1, l'image doit tenir dans la plus petite des
    deux, puisque l'OTA ecrit dans celle qui n'est pas active.
    """
    sizes: list[int] = []
    for line in csv_path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        fields = [field.strip() for field in stripped.split(",")]
        if len(fields) < 5 or fields[1] != "app":
            continue
        sizes.append(_parse_size(fields[4]))
    if not sizes:
        raise ValueError(f"aucune partition de type 'app' dans {csv_path.name}")
    return min(sizes)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--env", default="Waveshare-ESP32-S3", help="environnement PlatformIO")
    parser.add_argument("--max-percent", type=float, default=85.0)
    args = parser.parse_args()

    if not 0.0 < args.max_percent <= 100.0:
        parser.error("--max-percent doit etre dans ]0, 100]")

    firmware = ROOT / ".pio" / "build" / args.env / "firmware.bin"
    if not firmware.is_file():
        parser.error(f"binaire absent : {firmware.relative_to(ROOT)} (lancer d'abord pio run -e {args.env})")

    ini = _read_platformio_ini()
    section = f"env:{args.env}"
    if not ini.has_section(section):
        parser.error(f"environnement inconnu dans platformio.ini : {args.env}")

    csv_name = _lookup_inherited(ini, section, "board_build.partitions")
    if not csv_name:
        parser.error(f"board_build.partitions introuvable pour l'env {args.env}")

    csv_path = ROOT / csv_name
    if not csv_path.is_file():
        parser.error(f"table de partitions absente : {csv_name}")

    try:
        budget = app_partition_size(csv_path)
    except ValueError as exc:
        parser.error(str(exc))

    size = firmware.stat().st_size
    percent = size * 100.0 / budget
    print(
        f"{args.env}: firmware {size} / {budget} octets ({percent:.2f}%) "
        f"- partition app de {csv_name}, plafond {args.max_percent:.2f}%"
    )
    if percent > args.max_percent:
        parser.error(f"budget depasse : {percent:.2f}% > {args.max_percent:.2f}%")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
