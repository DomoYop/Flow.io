#!/usr/bin/env python3
"""Generate cfgdocs/cfgmods payloads from module text manifests."""

from __future__ import annotations

import json
import os
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent))
import io_port_labels  # noqa: E402

Import = type("Import", (), {})

try:
    Import("env")  # type: ignore
except Exception:
    env = None


def _get_project_dir() -> Path:
    if env is not None:
        try:
            return Path(env.get("PROJECT_DIR"))
        except Exception:
            pass
    return Path(os.getcwd())


def _merge_meta_dict(base: dict, overlay: dict) -> dict:
    out = dict(base or {})
    if not isinstance(overlay, dict):
        return out
    for key, value in overlay.items():
        if isinstance(value, dict) and isinstance(out.get(key), dict):
            out[key] = _merge_meta_dict(out[key], value)
            continue
        if isinstance(value, list) and isinstance(out.get(key), list):
            merged: List[Any] = []
            seen = set()
            for source in (out.get(key, []), value):
                for item in source:
                    token = json.dumps(item, ensure_ascii=False, sort_keys=True)
                    if token in seen:
                        continue
                    seen.add(token)
                    merged.append(item)
            out[key] = merged
            continue
        out[key] = value
    return out


def _load_text_payload(path: Path) -> Optional[dict]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except Exception as exc:
        print(f"[generate_config_docs] warning: invalid JSON in {path}: {exc}")
        return None
    if not isinstance(data, dict):
        print(f"[generate_config_docs] warning: ignored non-object payload in {path}")
        return None
    return data


def _text_manifest_files(src_root: Path, stem: str, locale: str = "fr") -> List[Path]:
    modules_root = src_root / "Modules"
    if not modules_root.exists():
        return []
    candidates = sorted(modules_root.rglob(f"text/{stem}*.json"))
    by_dir: Dict[Path, List[Path]] = {}
    for path in candidates:
        by_dir.setdefault(path.parent, []).append(path)

    selected: List[Path] = []
    target_name = f"{stem}.{locale}.json"
    for _, paths in sorted(by_dir.items(), key=lambda item: str(item[0])):
        picks = {path.name: path for path in paths}
        chosen = (
            picks.get(target_name)
            or picks.get(f"{stem}.json")
            or picks.get(f"{stem}.fr.json")
            or (sorted(paths)[0] if paths else None)
        )
        if chosen:
            selected.append(chosen)
    return selected


def _load_text_docs(src_root: Path, stem: str, locale: str = "fr") -> Tuple[Dict[str, dict], dict, List[Path]]:
    docs: Dict[str, dict] = {}
    meta: dict = {}
    loaded_files: List[Path] = []
    for path in _text_manifest_files(src_root, stem=stem, locale=locale):
        payload = _load_text_payload(path)
        if payload is None:
            continue
        loaded_files.append(path)
        payload_docs = payload.get("docs")
        if isinstance(payload_docs, dict):
            for raw_key, raw_val in payload_docs.items():
                if not isinstance(raw_key, str) or not isinstance(raw_val, dict):
                    continue
                docs[raw_key.strip("/")] = dict(raw_val)
        payload_meta = payload.get("meta")
        if not isinstance(payload_meta, dict):
            payload_meta = payload.get("_meta")
        if isinstance(payload_meta, dict):
            meta = _merge_meta_dict(meta, payload_meta)
    return docs, meta, loaded_files


def _load_text_translations(src_root: Path, locale: str = "fr") -> Tuple[Dict[str, str], List[Path]]:
    modules_root = src_root / "Modules"
    if not modules_root.exists():
        return {}, []
    out: Dict[str, str] = {}
    loaded_files: List[Path] = []
    for path in sorted(modules_root.rglob(f"text/i18n.{locale}.json")):
        payload = _load_text_payload(path)
        if payload is None:
            continue
        loaded_files.append(path)
        translations = payload.get("translations")
        source = translations if isinstance(translations, dict) else payload
        for raw_key, raw_val in source.items():
            if not isinstance(raw_key, str) or not isinstance(raw_val, str):
                continue
            key = raw_key.strip()
            if key:
                out[key] = raw_val
    return out, loaded_files


def _resolve_doc_i18n_fields(raw_doc: dict, translations: Dict[str, str]) -> dict:
    doc = dict(raw_doc or {})
    label_token = doc.get("label_t")
    help_token = doc.get("help_t")
    if isinstance(label_token, str) and label_token.strip():
        token = label_token.strip()
        doc["label"] = translations.get(token, token)
        doc["label_i18n"] = token
    if isinstance(help_token, str) and help_token.strip():
        token = help_token.strip()
        doc["help"] = translations.get(token, token)
        doc["help_i18n"] = token
    return doc


def _resolve_meta_i18n(node: Any, translations: Dict[str, str]) -> Any:
    if isinstance(node, list):
        return [_resolve_meta_i18n(item, translations) for item in node]
    if not isinstance(node, dict):
        return node
    out = {k: _resolve_meta_i18n(v, translations) for k, v in node.items()}
    label_token = out.get("label_t")
    help_token = out.get("help_t")
    if isinstance(label_token, str) and label_token.strip():
        token = label_token.strip()
        out["label"] = translations.get(token, token)
        out["label_i18n"] = token
    if isinstance(help_token, str) and help_token.strip():
        token = help_token.strip()
        out["help"] = translations.get(token, token)
        out["help_i18n"] = token
    return out


def _to_int(value: Any) -> Optional[int]:
    try:
        return int(value)
    except Exception:
        return None


def _apply_profile_specific_io_enum_sets(meta: dict,
                                         profile: str,
                                         translations: Dict[str, str],
                                         locale: str) -> dict:
    """Reecrit les enum_sets de binding IO selon le profil compile.

    Les libellés proviennent de io_port_labels (gabarits bilingues) et portent un
    token synthetique : contrairement a la version precedente, cette fonction
    n'efface plus label_t/label_i18n, donc l'interface web peut re-traduire.
    """
    if not isinstance(meta, dict):
        return meta
    enum_sets = meta.get("enum_sets")
    if not isinstance(enum_sets, dict):
        return meta

    def tokenized_entry(entry: dict, enum_set: str, value: int, label: io_port_labels.PortLabel) -> dict:
        out = dict(entry or {})
        token = io_port_labels.token_for(enum_set, value)
        out["label_t"] = token
        out["label_i18n"] = token
        out["label"] = translations.get(token) or io_port_labels.render(label, value, locale)
        return out

    def relabel(enum_set: str, entries: List[dict], keep_unknown: bool) -> List[dict]:
        """Applique les libellés du profil ; garde ou filtre les valeurs inconnues."""
        known = io_port_labels.labels_for(profile, enum_set)
        out: List[dict] = []
        for entry in entries:
            value = _to_int(entry.get("value"))
            if value is not None and value in known:
                out.append(tokenized_entry(entry, enum_set, value, known[value]))
            elif keep_unknown:
                out.append(dict(entry))
        return out

    def non_connected_entry() -> dict:
        token = io_port_labels.NONE_TOKEN
        return {
            "value": 0,
            "label_t": token,
            "label_i18n": token,
            "label": translations.get(token) or io_port_labels.render_none(locale),
        }

    def binding_entries_with_non_connected(entries: List[dict]) -> List[dict]:
        filtered = []
        for entry in entries:
            value = _to_int(entry.get("value"))
            if value is None or value == 0 or value == 65535:
                continue
            filtered.append(dict(entry))
        return [non_connected_entry()] + filtered

    # Analog bindings: Micronova's local DS18B20 GPIO binding is not valid on
    # flow.io boards, which expose DS18B20 probes through profile ports 120-123.
    analog_key = "flowio_binding_port_analog"
    analog_entries = enum_sets.get(analog_key)
    if profile in ("flowio", "waveshare") and isinstance(analog_entries, list):
        analog_filtered = [
            dict(entry)
            for entry in analog_entries
            if isinstance(entry, dict) and _to_int(entry.get("value")) != 2
        ]
        if profile == "waveshare":
            # Seuls les ports decrits par le profil sont exposes.
            analog_filtered = relabel(analog_key, analog_filtered, keep_unknown=False)
        enum_sets[analog_key] = binding_entries_with_non_connected(analog_filtered)

    # Digital input bindings: pin labels differ across flow.io and Waveshare.
    din_key = "flowio_binding_port_digital_input"
    din_entries = enum_sets.get(din_key)
    if isinstance(din_entries, list) and profile in ("flowio", "waveshare"):
        current = [item for item in din_entries if isinstance(item, dict)]
        enum_sets[din_key] = binding_entries_with_non_connected(
            relabel(din_key, current, keep_unknown=False)
        )

    # Digital output bindings: flow.io uses PCF8574 ports, Waveshare uses TCA9554/MCP23017.
    dout_key = "flowio_binding_port_digital_output"
    dout_entries = enum_sets.get(dout_key)
    if isinstance(dout_entries, list):
        current = [item for item in dout_entries if isinstance(item, dict)]
        filtered: List[dict] = []
        for entry in current:
            value = _to_int(entry.get("value"))
            if value is None:
                continue
            keep = True
            # Micronova aux_output must not be exposed on flow.io profiles.
            if profile in ("flowio", "waveshare") and value == 1:
                keep = False
            if profile == "flowio":
                keep = keep and not (300 <= value <= 399)
            if keep:
                filtered.append(dict(entry))

        if profile == "waveshare":
            # keep_unknown=False : ne proposer que les ports que le profil sait
            # resoudre. Sinon l'UI offre un binding que le firmware refuse au
            # boot ("unresolved binding_port"), sans erreur visible cote web.
            filtered = relabel(dout_key, filtered, keep_unknown=False)

        # Ensure flow.io exposes all 8 PCF bits (400..407) in UI bindings.
        if profile == "flowio":
            present_values = {_to_int(item.get("value")) for item in filtered}
            known = io_port_labels.labels_for(profile, dout_key)
            for value, label in sorted(known.items()):
                if value not in present_values:
                    filtered.append(tokenized_entry({"value": value}, dout_key, value, label))
        enum_sets[dout_key] = binding_entries_with_non_connected(filtered)

    # PoolLogic device slots: keep generic labels by default, but expose
    # profile wiring-specific mapping in UI for faster setup.
    slot_key = "poollogic_device_slot"
    slot_entries = enum_sets.get(slot_key)
    if profile == "waveshare" and isinstance(slot_entries, list):
        current = [item for item in slot_entries if isinstance(item, dict)]
        known = io_port_labels.labels_for(profile, slot_key)
        relabeled: List[dict] = []
        # Entrees hors plage (ex. 255 = "aucun PDM") : conservees telles quelles,
        # en tete, sinon la reconstruction les ferait disparaitre.
        for entry in current:
            value = _to_int(entry.get("value"))
            if value is None or value in known:
                continue
            relabeled.append(entry)
        by_value = {}
        for entry in current:
            value = _to_int(entry.get("value"))
            if value is not None:
                by_value[value] = entry
        # Le domaine decide combien d'appareils existent ; MaxPoolDevices n'est
        # qu'un plafond de capacite.
        for value in sorted(v for v in by_value if v in known):
            relabeled.append(tokenized_entry(by_value[value], slot_key, value, known[value]))
        enum_sets[slot_key] = relabeled

    return meta


# Branches de configuration decrivant un composant que le profil ne porte pas :
# masquees de l'arbre web plutot que d'exposer un reglage sans materiel derriere.
# Le pendant firmware est le defaut du champ `enabled` correspondant.
PROFILE_HIDDEN_BRANCHES: Dict[str, Tuple[str, ...]] = {
    # Waveshare : pas de PCF8574 (un TCA9554 occupe deja 0x20) ni de MCP23017
    # (aucun port 400-415 dans WaveshareIoLayout.h).
    "waveshare": ("io/drivers/pcf857x", "io/drivers/mcp23017"),
}


def _apply_profile_hidden_branches(docs: Dict[str, dict], profile: str) -> Dict[str, dict]:
    branches = PROFILE_HIDDEN_BRANCHES.get(profile)
    if not branches:
        return docs
    out = dict(docs)
    for branch in branches:
        entry = dict(out.get(branch) or {})
        entry["hidden"] = True
        out[branch] = entry
    return out


def _resolved_docs(docs: Dict[str, dict], translations: Dict[str, str]) -> Dict[str, dict]:
    return {
        key: _resolve_doc_i18n_fields(value, translations)
        for key, value in docs.items()
        if isinstance(value, dict)
    }


def _write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def main() -> None:
    project_dir = _get_project_dir()
    src_root = project_dir / "src"
    locale = os.getenv("FLOW_CFGDOC_LOCALE", "fr").strip().lower() or "fr"

    out_path = project_dir / "data" / "webinterface" / "cfgdocs.json"
    cfgmods_out_path = project_dir / "data" / "webinterface" / "cfgmods.json"

    cfgdocs_docs, cfgdocs_meta, cfgdocs_files = _load_text_docs(src_root, stem="cfgdocs", locale=locale)
    cfgmods_docs, cfgmods_meta, cfgmods_files = _load_text_docs(src_root, stem="cfgmods", locale=locale)
    i18n, i18n_files = _load_text_translations(src_root, locale=locale)

    profile = io_port_labels.detect_profile(env)

    combined_meta = _resolve_meta_i18n(_merge_meta_dict(cfgdocs_meta, cfgmods_meta), i18n)
    combined_meta = _apply_profile_specific_io_enum_sets(combined_meta, profile, i18n, locale)

    merged_docs = _resolved_docs(dict(cfgdocs_docs), i18n)

    cfgdocs_payload = {
        "_meta": {
            "generated": True,
            "locale": locale,
            "source": "text",
            "total": len(merged_docs),
        },
        "meta": combined_meta if isinstance(combined_meta, dict) else {},
        "docs": dict(sorted(merged_docs.items(), key=lambda kv: kv[0])),
    }
    _write_json(out_path, cfgdocs_payload)

    cfgmods_payload = {
        "_meta": {
            "generated": True,
            "locale": locale,
            "version": 1,
            "source": "text",
        },
        "meta": combined_meta if isinstance(combined_meta, dict) else {},
        "docs": dict(sorted(
            _resolved_docs(_apply_profile_hidden_branches(cfgmods_docs, profile), i18n).items(),
            key=lambda kv: kv[0],
        )),
    }
    _write_json(cfgmods_out_path, cfgmods_payload)

    print(
        f"[generate_config_docs] wrote {out_path} "
        f"(docs={len(cfgdocs_payload['docs'])} cfgmods={len(cfgmods_payload['docs'])} "
        f"text_files={len(cfgdocs_files) + len(cfgmods_files)} i18n_files={len(i18n_files)} "
        f"locale={locale} profile={profile})"
    )


if __name__ == "__main__":
    main()
