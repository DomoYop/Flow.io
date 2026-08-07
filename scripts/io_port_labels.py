#!/usr/bin/env python3
"""Libelles bilingues des ports de binding IO, par gabarits.

Ces libelles etaient auparavant codes en dur en francais dans
generate_config_docs.py, et le code effacait au passage les tokens i18n
(`pop("label_t")`, `pop("label_i18n")`) : les menus de binding IO etaient donc
intraduisibles par construction.

Ici les donnees (quel port, quel alias, quel indice) sont separees des seuls
fragments de langue (DEVICES, PATTERNS). Chaque port recoit un token synthetique
`cfgmods_meta.iomodule.port.*`, injecte dans data/wc/i18n.<locale>.j par
generate_cfgdoc_chunks.py, ce qui permet a l'interface web de re-traduire cote
client (app.js cfgDocTr) sans rebuild.

Attention : les libellés sont resolus PAR PROFIL. La valeur 407 par exemple
designe `MCPOut8 - MCP23017 bit 7` sur Waveshare et `PortPCF0Bit7 - Sortie
PCF8574 - Bit 7` sur flow.io. C'est coherent avec data/wc/ qui est deja un
artefact dependant du profil compile.
"""

from __future__ import annotations

import os
from typing import Dict, Optional

SUPPORTED_LOCALES = ("fr", "en")
DEFAULT_LOCALE = "fr"

TOKEN_ROOT = "cfgmods_meta.iomodule.port"
NONE_TOKEN = TOKEN_ROOT + ".none.label"

# Cles d'enum_set -> segment court du token, pour eviter les collisions de
# valeurs entre familles (le slot 0 et "non connecte" valent tous deux 0).
ENUM_SET_SLUGS = {
    "flowio_binding_port_analog": "analog",
    "flowio_binding_port_digital_input": "din",
    "flowio_binding_port_digital_output": "dout",
}

# --- Fragments de langue : les SEULES chaines traduisibles de ce fichier -----

NONE_LABEL = {"fr": "Non connecté", "en": "Not connected"}

DEVICES: Dict[str, Dict[str, str]] = {
    "ads_int": {"fr": "ADS1115 interne", "en": "internal ADS1115"},
    "ads_ext": {"fr": "ADS1115 externe", "en": "external ADS1115"},
    "ds18b20": {"fr": "Sonde DS18B20", "en": "DS18B20 probe"},
    "powermon": {"fr": "Moniteur puissance", "en": "Power monitor"},
    "pcf_out": {"fr": "Sortie PCF8574", "en": "PCF8574 output"},
    # Noms de composants : identiques dans les deux langues.
    "sht40": {"fr": "SHT40", "en": "SHT40"},
    "bmp280": {"fr": "BMP280", "en": "BMP280"},
    "bme680": {"fr": "BME680", "en": "BME680"},
    "tca9554": {"fr": "TCA9554", "en": "TCA9554"},
    "mcp23017": {"fr": "MCP23017", "en": "MCP23017"},
}

PATTERNS: Dict[str, Dict[str, str]] = {
    "channel": {
        "fr": "{alias} - {device} canal {index}{note} [{value}]",
        "en": "{alias} - {device} channel {index}{note} [{value}]",
    },
    "diff_pair": {
        "fr": "{alias} - {device} paire diff {index} [{value}]",
        "en": "{alias} - {device} diff pair {index} [{value}]",
    },
    "probe": {
        "fr": "{alias} - {device} n°{index} [{value}]",
        "en": "{alias} - {device} #{index} [{value}]",
    },
    # "bit" est identique dans les deux langues.
    "bit": {
        "fr": "{alias} - {device} bit {index} [{value}]",
        "en": "{alias} - {device} bit {index} [{value}]",
    },
    "pcf_bit": {
        "fr": "{alias} - {device} - Bit {index} [{value}]",
        "en": "{alias} - {device} - Bit {index} [{value}]",
    },
    # Segments purement symboliques : aucune langue naturelle.
    "neutral": {
        "fr": "{alias} - {detail} [{value}]",
        "en": "{alias} - {detail} [{value}]",
    },
    "slot": {
        "fr": "pd{index} -> d{index:02d} [{value}]",
        "en": "pd{index} -> d{index:02d} [{value}]",
    },
}


class PortLabel(object):
    __slots__ = ("alias", "pattern", "device", "index", "note", "detail")

    def __init__(self, alias, pattern, device="", index=None, note="", detail=""):
        self.alias = alias
        self.pattern = pattern
        self.device = device
        self.index = index
        self.note = note
        self.detail = detail


def _channel(alias, device, index, note=""):
    return PortLabel(alias, "channel", device=device, index=index, note=note)


def _powermon(alias, index, ina228=False):
    return _channel(alias, "powermon", index, note=" (INA228)" if ina228 else "")


_ANALOG_WAVESHARE = {
    100: _channel("ADSInt0", "ads_int", 0),
    101: _channel("ADSInt1", "ads_int", 1),
    102: _channel("ADSInt2", "ads_int", 2),
    103: _channel("ADSInt3", "ads_int", 3),
    110: PortLabel("ADSExt0", "diff_pair", device="ads_ext", index=0),
    111: PortLabel("ADSExt1", "diff_pair", device="ads_ext", index=1),
    120: PortLabel("OneWire1", "probe", device="ds18b20", index=1),
    121: PortLabel("OneWire2", "probe", device="ds18b20", index=2),
    122: PortLabel("OneWire3", "probe", device="ds18b20", index=3),
    123: PortLabel("OneWire4", "probe", device="ds18b20", index=4),
    130: _channel("SHT40Temp", "sht40", 0),
    131: _channel("SHT40Humidity", "sht40", 1),
    132: _channel("BMP280Temp", "bmp280", 0),
    133: _channel("BMP280Pressure", "bmp280", 1),
    134: _channel("BME680Temp", "bme680", 0),
    135: _channel("BME680Humidity", "bme680", 1),
    136: _channel("BME680Pressure", "bme680", 2),
    137: _channel("BME680Gas", "bme680", 3),
    143: _powermon("PowermonShuntMv", 0),
    144: _powermon("PowermonBusV", 1),
    145: _powermon("PowermonCurrentMa", 2),
    146: _powermon("PowermonPowerMw", 3),
    147: _powermon("PowermonLoadV", 4),
    148: _powermon("PowermonTemp", 5, ina228=True),
    149: _powermon("PowermonEnergy", 6, ina228=True),
    150: _powermon("PowermonCharge", 7, ina228=True),
}

_DIN_FLOWIO = {
    200: PortLabel("DIN0", "neutral", detail="GPIO34"),
    201: PortLabel("DIN1", "neutral", detail="GPIO36"),
    202: PortLabel("DIN2", "neutral", detail="GPIO39"),
    203: PortLabel("DIN3", "neutral", detail="GPIO35"),
}

_DIN_WAVESHARE = {
    200 + i: PortLabel("DIN%d" % i, "neutral", detail="GPIO%d" % (4 + i))
    for i in range(8)
}

# Waveshare ne cable que les 8 bits du TCA9554. Les 16 ports MCP23017 (400-415)
# etaient decrits ici alors que WaveshareIoLayout.h ne les declare pas : l'UI les
# proposait au binding et le firmware repondait "unresolved binding_port" en
# silence au boot. Les reintroduire le jour ou kBindingPorts[] les porte.
_DOUT_WAVESHARE = {}
for _i in range(8):
    _DOUT_WAVESHARE[300 + _i] = PortLabel("EXIO%d" % (_i + 1), "bit", device="tca9554", index=_i)

_DOUT_FLOWIO = {
    407: PortLabel("PortPCF0Bit7", "pcf_bit", device="pcf_out", index=7),
}

PROFILE_PORTS: Dict[str, Dict[str, Dict[int, PortLabel]]] = {
    "waveshare": {
        "flowio_binding_port_analog": _ANALOG_WAVESHARE,
        "flowio_binding_port_digital_input": _DIN_WAVESHARE,
        "flowio_binding_port_digital_output": _DOUT_WAVESHARE,
    },
    "flowio": {
        "flowio_binding_port_digital_input": _DIN_FLOWIO,
        "flowio_binding_port_digital_output": _DOUT_FLOWIO,
    },
    "generic": {},
}


# --- Resolution du profil (partagee par les deux generateurs) ---------------

def detect_profile(env=None) -> str:
    """Profil de build : override explicite, sinon deduit de $PIOENV."""
    override = str(os.getenv("FLOW_CFGDOC_PROFILE", "") or "").strip().lower()
    if override in PROFILE_PORTS:
        return override

    if env is not None:
        try:
            value = str(env.GetProjectOption("custom_cfgdocs_profile") or "").strip().lower()
            if value in PROFILE_PORTS:
                return value
        except Exception:
            pass

    pio_env = ""
    if env is not None:
        try:
            pio_env = str(env.subst("$PIOENV") or "").strip()
        except Exception:
            pio_env = ""
    if not pio_env:
        pio_env = str(os.getenv("PIOENV", "") or "").strip()

    name = pio_env.lower()
    if "waveshare" in name:
        return "waveshare"
    if "flowio" in name:
        return "flowio"
    return "generic"


# --- Rendu -----------------------------------------------------------------

def token_for(enum_set: str, value: int) -> str:
    slug = ENUM_SET_SLUGS.get(enum_set, enum_set)
    return "%s.%s.%d.label" % (TOKEN_ROOT, slug, value)


def render(label: PortLabel, value: int, locale: str) -> str:
    loc = locale if locale in SUPPORTED_LOCALES else DEFAULT_LOCALE
    device = DEVICES.get(label.device, {}).get(loc, label.device)
    return PATTERNS[label.pattern][loc].format(
        alias=label.alias,
        device=device,
        index=label.index,
        note=label.note,
        detail=label.detail,
        value=value,
    )


def render_none(locale: str) -> str:
    return NONE_LABEL.get(locale if locale in SUPPORTED_LOCALES else DEFAULT_LOCALE,
                          NONE_LABEL[DEFAULT_LOCALE])


def labels_for(profile: str, enum_set: str) -> Dict[int, PortLabel]:
    return PROFILE_PORTS.get(profile, {}).get(enum_set, {})


def synthetic_translations(profile: str) -> Dict[str, Dict[str, str]]:
    """{locale: {token: texte}} pour tous les ports connus de ce profil."""
    out = {loc: {} for loc in SUPPORTED_LOCALES}
    for loc in SUPPORTED_LOCALES:
        out[loc][NONE_TOKEN] = render_none(loc)
    for enum_set, ports in PROFILE_PORTS.get(profile, {}).items():
        for value, label in ports.items():
            token = token_for(enum_set, value)
            for loc in SUPPORTED_LOCALES:
                out[loc][token] = render(label, value, loc)
    return out


if __name__ == "__main__":
    import sys
    prof = sys.argv[1] if len(sys.argv) > 1 else "waveshare"
    for loc, entries in sorted(synthetic_translations(prof).items()):
        print("--- %s (%d tokens) ---" % (loc, len(entries)))
        for token in sorted(entries):
            print("  %-48s %s" % (token, entries[token]))
