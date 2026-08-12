#!/usr/bin/env python3
"""Verifie que io_port_labels.PROFILE_PORTS colle a kBindingPorts[] du firmware.

Les listes de ports vivent en double : en C++ dans `src/Profiles/<Profil>/
*IoLayout.h` (ce que le firmware sait resoudre) et en Python dans
`scripts/io_port_labels.py` (ce que l'interface web propose). Une divergence ne
casse aucun build : l'UI offre simplement un binding que le firmware refuse au
boot, sans erreur visible cote web. C'est deja arrive avec les ports MCP23017
400-415 (cf. commentaire en tete de io_port_labels.py).

Un port present cote C++ mais absent cote Python est benin (l'UI ne le propose
pas) ; l'inverse est le vrai defaut, et le seul qui fait echouer ce script.

Usage: python scripts/check_io_port_sync.py [--profile waveshare]
"""
import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import io_port_labels  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent

# Profil -> en-tete de brochage. Les profils absents d'ici ne sont pas verifies.
PROFILE_LAYOUTS = {
    "waveshare": ROOT / "src/Profiles/Waveshare/WaveshareIoLayout.h",
    "flowio": ROOT / "src/Profiles/FlowIO/FlowIOIoLayout.h",
}

# Direction de port -> enum_sets qui doivent contenir le port. Un port d'entree
# peut alimenter l'analogique comme le digital : le firmware tranche par backend,
# donc on se contente d'exiger la presence dans au moins un enum_set d'entree.
IN_SETS = ("flowio_binding_port_analog", "flowio_binding_port_digital_input")
OUT_SETS = ("flowio_binding_port_digital_output",)


def parse_binding_ports(header: Path):
    """Extrait {portId: (nom, direction)} de kBindingPorts[] et de l'enum de ports."""
    text = header.read_text(encoding="utf-8")

    # 1. Nom symbolique -> valeur numerique (enum : PhysicalPortId { PortX = 100, ... }).
    values = {}
    enum_match = re.search(r"enum\s*:\s*PhysicalPortId\s*\{(.*?)\n\};", text, re.S)
    if enum_match:
        for name, value in re.findall(r"(\w+)\s*=\s*(\d+)", enum_match.group(1)):
            values[name] = int(value)

    # 2. Lignes de kBindingPorts[] : {PortX, IO_BACKEND_Y, ch, flags, "nom"}.
    table_match = re.search(r"kBindingPorts\[\]\s*=\s*\{(.*?)\n\};", text, re.S)
    if not table_match:
        raise SystemExit(f"kBindingPorts[] introuvable dans {header}")

    ports = {}
    # Le canal n'est pas toujours un litteral : FlowIO y met une expression
    # (BoardProfiles::kFlowIODINv1IoPoints[8].pin), d'ou [^,]+ plutot que \d+.
    row = re.compile(
        r"\{\s*(?P<port>\w+)\s*,\s*IO_BACKEND_\w+\s*,\s*[^,]+,"
        r"\s*(?P<flags>[^,]+),\s*\"(?P<name>[^\"]*)\"\s*\}"
    )
    for match in row.finditer(table_match.group(1)):
        token = match.group("port")
        port_id = values.get(token)
        if port_id is None and token.isdigit():
            port_id = int(token)
        if port_id is None:
            continue
        direction = "out" if "IO_PORT_DIR_OUT" in match.group("flags") else "in"
        ports[port_id] = (match.group("name"), direction)
    return ports


def check_profile(profile: str) -> int:
    header = PROFILE_LAYOUTS.get(profile)
    if header is None or not header.exists():
        print(f"[check_io_port_sync] profil '{profile}' sans en-tete connu, ignore")
        return 0

    firmware = parse_binding_ports(header)
    declared = io_port_labels.PROFILE_PORTS.get(profile, {})

    errors = []
    for enum_set, ports in declared.items():
        if enum_set not in IN_SETS + OUT_SETS:
            continue
        expected_dir = "out" if enum_set in OUT_SETS else "in"
        for value in sorted(ports):
            spec = firmware.get(value)
            if spec is None:
                errors.append(
                    f"{enum_set}: port {value} propose par l'UI mais absent de "
                    f"kBindingPorts[] ({header.name}) — le firmware refusera ce binding"
                )
            elif spec[1] != expected_dir:
                errors.append(
                    f"{enum_set}: port {value} ({spec[0]}) est une {spec[1]} cote "
                    f"firmware, propose comme {expected_dir} par l'UI"
                )

    missing = sorted(set(firmware) - {v for s in declared.values() for v in s})
    print(f"[check_io_port_sync] profil={profile} ports firmware={len(firmware)} "
          f"proposes={sum(len(s) for s in declared.values())}")
    if missing:
        print(f"  info: {len(missing)} ports du firmware non proposes par l'UI: {missing}")
    for message in errors:
        print(f"  ERREUR {message}")
    return 1 if errors else 0


def main():
    parser = argparse.ArgumentParser(prog="check_io_port_sync.py", description=__doc__)
    parser.add_argument("--profile", default=None, help="profil a verifier (defaut: tous)")
    args = parser.parse_args()

    profiles = [args.profile] if args.profile else sorted(PROFILE_LAYOUTS)
    return max(check_profile(p) for p in profiles)


if __name__ == "__main__":
    raise SystemExit(main())
