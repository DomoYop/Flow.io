#!/usr/bin/env python3
"""Construit le paquet web OTA a partir du staging SPIFFS.

Pourquoi un paquet plutot que l'image SPIFFS : mkspiffs pade l'image jusqu'a la
taille de la partition (8,3 Mo pour ~320 Ko de contenu). Transferer l'image, meme
compressee, oblige a reecrire toute la partition -- donc a detruire le systeme de
fichiers en cas de coupure. Le paquet ne contient que les fichiers, qui sont
ensuite remplaces un par un dans le systeme de fichiers monte.

Le contenu n'est pas compresse : 169 des 172 fichiers sont deja gzippes
individuellement par prepare_spiffs_data.py (91 % des octets). Compresser le
paquet ne gagnerait que ~7 %, pour un decodeur de plus dans le chemin critique.

Format (little-endian), en-tete de 24 octets :

    "FLOWPKG1"          8 o   magie
    u16 format          = 1
    u16 fileCount
    u32 indexBytes
    u32 payloadBytes
    u32 crc32                 sur index + payload

    index, pour chaque fichier :
        u16 pathLen
        u32 size
        u32 crc32             sur le contenu du fichier
        char path[pathLen]    chemin absolu SPIFFS, ex. "/webinterface/app.js.gz"

    payload : fichiers concatenes, dans l'ordre de l'index
"""

from __future__ import annotations

import struct
import zlib
from pathlib import Path

MAGIC = b"FLOWPKG1"
FORMAT_VERSION = 1
HEADER_BYTES = 24


def build_package(staging_dir: Path) -> bytes:
    files = sorted(
        (p for p in staging_dir.rglob("*") if p.is_file()),
        key=lambda p: p.relative_to(staging_dir).as_posix(),
    )

    index = bytearray()
    payload = bytearray()
    for path in files:
        data = path.read_bytes()
        # Chemin tel que SPIFFS l'expose : absolu, separateurs POSIX.
        spiffs_path = "/" + path.relative_to(staging_dir).as_posix()
        encoded = spiffs_path.encode("utf-8")
        if len(encoded) > 0xFFFF:
            raise RuntimeError(f"chemin trop long pour le format : {spiffs_path}")
        index += struct.pack("<HII", len(encoded), len(data), zlib.crc32(data) & 0xFFFFFFFF)
        index += encoded
        payload += data

    body = bytes(index) + bytes(payload)
    header = struct.pack(
        "<8sHHIII",
        MAGIC,
        FORMAT_VERSION,
        len(files),
        len(index),
        len(payload),
        zlib.crc32(body) & 0xFFFFFFFF,
    )
    assert len(header) == HEADER_BYTES, "en-tete de taille inattendue"
    return header + body


def summarize(package: bytes, staging_dir: Path) -> str:
    count = struct.unpack_from("<H", package, 10)[0]
    payload_bytes = struct.unpack_from("<I", package, 16)[0]
    return (f"{count} fichiers, {payload_bytes} o de contenu, "
            f"{len(package)} o au total")


if __name__ == "__main__":
    import sys

    if len(sys.argv) != 3:
        print("usage: build_web_package.py <staging_dir> <sortie.pkg>")
        raise SystemExit(2)
    staging = Path(sys.argv[1])
    out = Path(sys.argv[2])
    pkg = build_package(staging)
    out.write_bytes(pkg)
    print(f"[build_web_package] {out.name}: {summarize(pkg, staging)}")
